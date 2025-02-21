/* 
 * This program is an EtherCAT master implementation that initializes and configures EtherCAT slaves,
 * manages their states, and handles real-time data exchange. It includes functions for setting up 
 * PDO mappings, synchronizing time with the distributed clock, and controlling servomotors in 
 * various operational modes. The program also features multi-threading for real-time processing 
 * and monitoring of the EtherCAT network.
 */
//#include <QCoreApplication>


#include <stdio.h>
#include <string.h>
#include "ethercat.h"
#include <iostream>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include <sys/time.h>
#include <pthread.h>
#include <math.h>

#include <chrono>
#include <ctime>

#include <iostream>
#include <cstdint>

#include <sched.h>
#include <climits>  // 添加这行来获取 LONG_MAX 定义

// Global variables for EtherCAT communication
char IOmap[4096]; // I/O mapping for EtherCAT
int expectedWKC; // Expected Work Counter
boolean needlf; // Flag to indicate if a line feed is needed
volatile int wkc; // Work Counter (volatile to ensure it is updated correctly in multi-threaded context)
boolean inOP; // Flag to indicate if the system is in operational state
uint8 currentgroup = 0; // Current group for EtherCAT communication
int dorun = 0; // Flag to indicate if the thread should run
bool start_ecatthread_thread; // Flag to start the EtherCAT thread
int ctime_thread; // Cycle time for the EtherCAT thread

int64 toff, gl_delta; // Time offset and global delta for synchronization

// Function prototypes for EtherCAT thread functions
OSAL_THREAD_FUNC ecatcheck(void *ptr); // Function to check the state of EtherCAT slaves
OSAL_THREAD_FUNC_RT ecatthread(void *ptr); // Real-time EtherCAT thread function

// Thread handles for the EtherCAT threads
OSAL_THREAD_HANDLE thread1; // Handle for the EtherCAT check thread
OSAL_THREAD_HANDLE thread2; // Handle for the real-time EtherCAT thread

// Function to synchronize time with the EtherCAT distributed clock
void ec_sync(int64 reftime, int64 cycletime, int64 *offsettime);
// Function to add nanoseconds to a timespec structure
void add_timespec(struct timespec *ts, int64 addtime);

// Define constants for stack size and timing
#define stack64k (64 * 1024) // Stack size for threads
#define NSEC_PER_SEC 1000000000   // Number of nanoseconds in one second
#define EC_TIMEOUTMON 5000        // Timeout for monitoring in microseconds
#define MAX_VELOCITY 30000        // Maximum velocity
#define MAX_ACCELERATION 50000    // Maximum acceleration

// Conversion units for the servomotor
float Cnt_to_deg = 0.000686645; // Conversion factor from counts to degrees
int8_t SLAVE_ID; // Slave ID for EtherCAT communication

// Structure for RXPDO (Control data sent to slave)
typedef struct {
    uint16_t controlword;      // 0x6040:0, 16 bits
    int32_t target_velocity;   // 0x60FF:0, 32 bits
    uint8_t mode_of_operation; // 0x6060:0, 8 bits
    uint8_t padding;          // 8 bits padding for alignment
} __attribute__((__packed__)) rxpdo_t;

// Structure for TXPDO (Status data received from slave)
typedef struct {
    uint16_t statusword;      // 0x6041:0, 16 bits
    int32_t actual_position;  // 0x6064:0, 32 bits
    int32_t actual_velocity;  // 0x606C:0, 32 bits
    int16_t actual_torque;    // 0x6077:0, 16 bits
} __attribute__((__packed__)) txpdo_t;

// Global variables
volatile int target_position = 0;
pthread_mutex_t target_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t target_position_cond = PTHREAD_COND_INITIALIZER;
bool target_updated = false;
int32_t received_target = 0;

rxpdo_t rxpdo;  // Global variable, used for sending data to slaves
txpdo_t txpdo;  // Global variable, used for receiving data from slaves

struct MotorStatus {
    bool is_operational;
    uint16_t status_word;
    int32_t actual_position;
    int32_t actual_velocity;
    int16_t actual_torque;
} motor_status;

// Function to update motor status information
void update_motor_status(int slave_id) {
    // Update status information from TXPDO
    motor_status.status_word = txpdo.statusword;
    motor_status.actual_position = txpdo.actual_position;
    motor_status.actual_velocity = txpdo.actual_velocity;
    motor_status.actual_torque = txpdo.actual_torque;
    
    // Check status word to determine if motor is operational
    // Bits 0-3 should be 0111 for enabled and ready state
    motor_status.is_operational = (txpdo.statusword & 0x0F) == 0x07;
}

//##################################################################################################
// Function: Set the CPU affinity for a thread
void set_thread_affinity(pthread_t thread, int cpu_core) {
    cpu_set_t cpuset; // CPU set to specify which CPUs the thread can run on
    CPU_ZERO(&cpuset); // Clear the CPU set
    CPU_SET(cpu_core, &cpuset); // Add the specified CPU core to the set

    // Set the thread's CPU affinity
    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        printf("Unable to set CPU affinity for thread %d\n", cpu_core); // Error message if setting fails
    } else {
        printf("Thread successfully bound to CPU %d\n", cpu_core); // Confirmation message if successful
    }
}

//##################################################################################################
// Function prototype for the EtherCAT test function
int erob_test();

uint16_t data_R;

int erob_test() {
    int rdl; // Variable to hold read data length
    SLAVE_ID = 1; // Set the slave ID to 1
    int i, j, oloop, iloop, chk; // Loop control variables

    // 1. Call ec_config_init() to move from INIT to PRE-OP state.
    printf("__________STEP 1___________________\n");
    // Initialize EtherCAT master on the specified network interface
    if (ec_init("enp6s0") <= 0) {
        printf("Error: Could not initialize EtherCAT master!\n");
        printf("No socket connection on Ethernet port. Execute as root.\n");
        printf("___________________________________________\n");
        return -1; // Return error if initialization fails
    }
    printf("EtherCAT master initialized successfully.\n");
    printf("___________________________________________\n");

    // Search for EtherCAT slaves on the network
    if (ec_config_init(FALSE) <= 0) {
        printf("Error: Cannot find EtherCAT slaves!\n");
        printf("___________________________________________\n");
        ec_close(); // Close the EtherCAT connection
        return -1; // Return error if no slaves are found
    }
    printf("%d slaves found and configured.\n", ec_slavecount); // Print the number of slaves found
    printf("___________________________________________\n");

    // 2. Change to pre-operational state to configure the PDO registers
    printf("__________STEP 2___________________\n");

    // Check if the slave is ready to map
    ec_readstate(); // Read the state of the slaves

    for(int i = 1; i <= ec_slavecount; i++) { // Loop through each slave
        if(ec_slave[i].state != EC_STATE_PRE_OP) { // If the slave is not in PRE-OP state
            // Print the current state and status code of the slave
            printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                   i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
            printf("\nRequest init state for slave %d\n", i); // Request to change the state to INIT
            ec_slave[i].state = EC_STATE_INIT; // Set the slave state to INIT
            printf("___________________________________________\n");
        } else { // If the slave is in PRE-OP state
            ec_slave[0].state = EC_STATE_PRE_OP; // Set the first slave to PRE-OP state
            /* Request EC_STATE_PRE_OP state for all slaves */
            ec_writestate(0); // Write the state change to the slave
            /* Wait for all slaves to reach the PRE-OP state */
            if ((ec_statecheck(0, EC_STATE_PRE_OP,  3 * EC_TIMEOUTSTATE)) == EC_STATE_PRE_OP) {
                printf("State changed to EC_STATE_PRE_OP: %d \n", EC_STATE_PRE_OP);
                printf("___________________________________________\n");
            } else {
                printf("State EC_STATE_PRE_OP cannot be changed in step 2\n");
                return -1; // Return error if state change fails
            }
        }
    }

//##################################################################################################
    //3.- Map RXPOD
    printf("__________STEP 3___________________\n");

    // Modify PDO mapping configuration
    int retval = 0;
    uint16 map_1c12;
    uint8 zero_map = 0;
    uint32 map_object;
    uint16 clear_val = 0x0000;

    for(int i = 1; i <= ec_slavecount; i++) {
        // Clear RXPDO mapping
        retval += ec_SDOwrite(i, 0x1600, 0x00, FALSE, sizeof(zero_map), &zero_map, EC_TIMEOUTSAFE);
        
        // Control word (0x6040:0, 16 bits)
        map_object = 0x60400010;
        retval += ec_SDOwrite(i, 0x1600, 0x01, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);
        
        // Target velocity (0x60FF:0, 32 bits)
        map_object = 0x60FF0020;
        retval += ec_SDOwrite(i, 0x1600, 0x02, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);
        
        // Operation mode (0x6060:0, 8 bits)
        map_object = 0x60600008;
        retval += ec_SDOwrite(i, 0x1600, 0x03, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);
        
        // Padding (8 bits padding)
        map_object = 0x00000008;
        retval += ec_SDOwrite(i, 0x1600, 0x04, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);
        
        uint8 map_count = 4;  // Now there are 4 objects, including padding
        retval += ec_SDOwrite(i, 0x1600, 0x00, FALSE, sizeof(map_count), &map_count, EC_TIMEOUTSAFE);
        
        // Configure RXPDO allocation
        clear_val = 0x0000;
        retval += ec_SDOwrite(i, 0x1c12, 0x00, FALSE, sizeof(clear_val), &clear_val, EC_TIMEOUTSAFE);
        map_1c12 = 0x1600;
        retval += ec_SDOwrite(i, 0x1c12, 0x01, FALSE, sizeof(map_1c12), &map_1c12, EC_TIMEOUTSAFE);
        map_1c12 = 0x0001;
        retval += ec_SDOwrite(i, 0x1c12, 0x00, FALSE, sizeof(map_1c12), &map_1c12, EC_TIMEOUTSAFE);
    }

    printf("RXPDO mapping configuration result: %d\n", retval);
    if (retval < 0) {
        printf("RXPDO mapping failed\n");
        return -1;
    }

    printf("RXPOD Mapping set correctly.\n");
    printf("___________________________________________\n");

    //........................................................................................
    // Map TXPOD
    retval = 0;
    uint16 map_1c13;
    for(int i = 1; i <= ec_slavecount; i++) {
        // Clear TXPDO mapping
        clear_val = 0x0000;
        retval += ec_SDOwrite(i, 0x1A00, 0x00, FALSE, sizeof(clear_val), &clear_val, EC_TIMEOUTSAFE);

        // Status Word (0x6041:0, 16 bits)
        map_object = 0x60410010;
        retval += ec_SDOwrite(i, 0x1A00, 0x01, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);

        // Actual Position (0x6064:0, 32 bits)
        map_object = 0x60640020;
        retval += ec_SDOwrite(i, 0x1A00, 0x02, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);

        // Actual Velocity (0x606C:0, 32 bits)
        map_object = 0x606C0020;
        retval += ec_SDOwrite(i, 0x1A00, 0x03, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);

        // Actual Torque (0x6077:0, 16 bits)
        map_object = 0x60770010;
        retval += ec_SDOwrite(i, 0x1A00, 0x04, FALSE, sizeof(map_object), &map_object, EC_TIMEOUTSAFE);

        uint8 map_count = 4;  // Ensure mapping 4 objects
        retval += ec_SDOwrite(i, 0x1A00, 0x00, FALSE, sizeof(map_count), &map_count, EC_TIMEOUTSAFE);

        // Correctly configure TXPDO allocation
        clear_val = 0x0000;
        retval += ec_SDOwrite(i, 0x1C13, 0x00, FALSE, sizeof(clear_val), &clear_val, EC_TIMEOUTSAFE);
        map_1c13 = 0x1A00;
        retval += ec_SDOwrite(i, 0x1C13, 0x01, FALSE, sizeof(map_1c13), &map_1c13, EC_TIMEOUTSAFE);
        map_1c13 = 0x0001;
        retval += ec_SDOwrite(i, 0x1C13, 0x00, FALSE, sizeof(map_1c13), &map_1c13, EC_TIMEOUTSAFE);


    }

    printf("Slave %d TXPDO mapping configuration result: %d\n", SLAVE_ID, retval);

    if (retval < 0) {
        printf("TXPDO Mapping failed\n");
        printf("___________________________________________\n");
        return -1;
    }

    printf("TXPDO Mapping set successfully\n");
    printf("___________________________________________\n");

   //##################################################################################################

    //4.- Set ecx_context.manualstatechange = 1. Map PDOs for all slaves by calling ec_config_map().
   printf("__________STEP 4___________________\n");

   ecx_context.manualstatechange = 1; //Disable automatic state change
   osal_usleep(1e6); //Sleep for 1 second

    uint8 WA = 0; //Variable for write access
    uint8 my_RA = 0; //Variable for read access
    uint32 TIME_RA; //Variable for time read access

    // Print the information of the slaves found
    for (int i = 1; i <= ec_slavecount; i++) {
       // (void)ecx_FPWR(ecx_context.port, i, ECT_REG_DCSYNCACT, sizeof(WA), &WA, 5 * EC_TIMEOUTRET);
        printf("Name: %s\n", ec_slave[i].name); //Print the name of the slave
        printf("Slave %d: Type %d, Address 0x%02x, State Machine actual %d, required %d\n", 
               i, ec_slave[i].eep_id, ec_slave[i].configadr, ec_slave[i].state, EC_STATE_INIT);
        printf("___________________________________________\n");
        ecx_dcsync0(&ecx_context, i, TRUE, 1000000, 0);  //Synchronize the distributed clock for the slave
    }

    // Map the configured PDOs to the IOmap
    ec_config_map(&IOmap);

    printf("__________STEP 5___________________\n");

    // Ensure all slaves are in PRE-OP state
    ec_readstate();
    for(int i = 1; i <= ec_slavecount; i++) {
        if(ec_slave[i].state != EC_STATE_PRE_OP) {
            printf("Slave %d not in PRE-OP state. Current state: %d, StatusCode=0x%4.4x : %s\n", 
                   i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
            return -1;
        }
    }

    // Configure distributed clock
    printf("Configuring DC...\n");
    ec_configdc();
    osal_usleep(200000);  // Wait for DC configuration to take effect

    // Request to switch to SAFE-OP state before confirming DC configuration
    for(int i = 1; i <= ec_slavecount; i++) {
        printf("Slave %d DC status: 0x%4.4x\n", i, ec_slave[i].DCactive);
        if(ec_slave[i].hasdc && !ec_slave[i].DCactive) {
            printf("DC not active for slave %d\n", i);
        }
    }

    // Request to switch to SAFE-OP state
    printf("Requesting SAFE_OP state...\n");
    ec_slave[0].state = EC_STATE_SAFE_OP;
    ec_writestate(0);
    osal_usleep(200000);  // Give enough time for state transition

    // Check the result of the state transition
    chk = 40;
    do {
        ec_readstate();
        for(int i = 1; i <= ec_slavecount; i++) {
            if(ec_slave[i].state != EC_STATE_SAFE_OP) {
                printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                       i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
            }
        }
        osal_usleep(100000);
    } while (chk-- && (ec_slave[0].state != EC_STATE_SAFE_OP));

    if (ec_slave[0].state != EC_STATE_SAFE_OP) {
        printf("Failed to reach SAFE_OP state\n");
        return -1;
    }

    printf("Successfully reached SAFE_OP state\n");

    // Calculate the expected Work Counter (WKC)
    expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC; // Calculate expected WKC based on outputs and inputs
    printf("Calculated workcounter %d\n", expectedWKC);

    // Read and display basic status information of the slaves
    ec_readstate(); // Read the state of all slaves
    for(int i = 1; i <= ec_slavecount; i++) {
        printf("Slave %d\n", i);
        printf("  State: %02x\n", ec_slave[i].state); // Print the state of the slave
        printf("  ALStatusCode: %04x\n", ec_slave[i].ALstatuscode); // Print the AL status code
        printf("  Delay: %d\n", ec_slave[i].pdelay); // Print the delay of the slave
        printf("  Has DC: %d\n", ec_slave[i].hasdc); // Check if the slave supports Distributed Clock
        printf("  DC Active: %d\n", ec_slave[i].DCactive); // Check if DC is active for the slave
        printf("  DC supported: %d\n", ec_slave[i].hasdc); // Print if DC is supported
    }

    // Read DC synchronization configuration using the correct parameters
    for(int i = 1; i <= ec_slavecount; i++) {
        uint16_t dcControl = 0; // Variable to hold DC control configuration
        int32_t cycleTime = 0; // Variable to hold cycle time
        int32_t shiftTime = 0; // Variable to hold shift time
        int size; // Variable to hold size for reading

        // Read DC synchronization configuration, adding the correct size parameter
        size = sizeof(dcControl);
        if (ec_SDOread(i, 0x1C32, 0x01, FALSE, &size, &dcControl, EC_TIMEOUTSAFE) > 0) {
            printf("Slave %d DC Configuration:\n", i);
            printf("  DC Control: 0x%04x\n", dcControl); // Print the DC control configuration
            
            size = sizeof(cycleTime);
            if (ec_SDOread(i, 0x1C32, 0x02, FALSE, &size, &cycleTime, EC_TIMEOUTSAFE) > 0) {
                printf("  Cycle Time: %d ns\n", cycleTime); // Print the cycle time
            }

        }
    }

    // 在 STEP 8 完成后创建线程
    printf("Starting EtherCAT threads...\n");
    start_ecatthread_thread = TRUE;
    osal_thread_create_rt(&thread1, stack64k * 2, (void *)&ecatthread, (void *)&ctime_thread);
    osal_thread_create(&thread2, stack64k * 2, (void *)&ecatcheck, NULL);
    printf("EtherCAT threads started\n");

    // 继续执行 STEP 9
    printf("__________STEP 9___________________\n");

    // 8. Transition to OP state
    printf("__________STEP 8___________________\n");

    // 请求切换到 OPERATIONAL 状态
    ec_slave[0].state = EC_STATE_OPERATIONAL;
    ec_writestate(0);

    // 等待所有从站进入 OPERATIONAL 状态
    int retries = 40;
    int all_op = 0;
    do {
        osal_usleep(50000); // 50ms
        ec_statecheck(0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);
        
        all_op = 1;
        for(int i = 1; i <= ec_slavecount; i++) {
            ec_readstate();
            if(ec_slave[i].state != EC_STATE_OPERATIONAL) {
                all_op = 0;
                printf("Slave %d State: 0x%02x, StatusCode: 0x%04x - %s\n",
                       i, ec_slave[i].state, ec_slave[i].ALstatuscode,
                       ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
                
                // 如果从站报错，尝试清除错误

            }
        }
        retries--;
    } while (!all_op && retries > 0);

    if (!all_op) {
        printf("Failed to reach operational state for all slaves\n");
        return -1;
    }

    printf("All slaves reached operational state successfully\n");

    // Calculate the expected Work Counter (WKC)
    expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC; // Calculate expected WKC based on outputs and inputs
    printf("Calculated workcounter %d\n", expectedWKC);

    // Read and display basic status information of the slaves
    ec_readstate(); // Read the state of all slaves
    for(int i = 1; i <= ec_slavecount; i++) {
        printf("Slave %d\n", i);
        printf("  State: %02x\n", ec_slave[i].state); // Print the state of the slave
        printf("  ALStatusCode: %04x\n", ec_slave[i].ALstatuscode); // Print the AL status code
        printf("  Delay: %d\n", ec_slave[i].pdelay); // Print the delay of the slave
        printf("  Has DC: %d\n", ec_slave[i].hasdc); // Check if the slave supports Distributed Clock
        printf("  DC Active: %d\n", ec_slave[i].DCactive); // Check if DC is active for the slave
        printf("  DC supported: %d\n", ec_slave[i].hasdc); // Print if DC is supported
    }

    // Read DC synchronization configuration using the correct parameters
    for(int i = 1; i <= ec_slavecount; i++) {
        uint16_t dcControl = 0; // Variable to hold DC control configuration
        int32_t cycleTime = 0; // Variable to hold cycle time
        int32_t shiftTime = 0; // Variable to hold shift time
        int size; // Variable to hold size for reading

        // Read DC synchronization configuration, adding the correct size parameter
        size = sizeof(dcControl);
        if (ec_SDOread(i, 0x1C32, 0x01, FALSE, &size, &dcControl, EC_TIMEOUTSAFE) > 0) {
            printf("Slave %d DC Configuration:\n", i);
            printf("  DC Control: 0x%04x\n", dcControl); // Print the DC control configuration
            
            size = sizeof(cycleTime);
            if (ec_SDOread(i, 0x1C32, 0x02, FALSE, &size, &cycleTime, EC_TIMEOUTSAFE) > 0) {
                printf("  Cycle Time: %d ns\n", cycleTime); // Print the cycle time
            }

        }
    }

    // 9. Configure servomotor and mode operation
    printf("__________STEP 9___________________\n");

    if (ec_slave[0].state == EC_STATE_OPERATIONAL) {
        printf("Operational state reached for all slaves.\n");
        
        uint8 operation_mode = 9;  // CSV mode
        uint16_t Control_Word = 0;
        int32_t Max_Velocity = 5000;  // 最大速度限制
        int32_t Max_Acceleration = 5000;  // 最大加速度限制
        int32_t Quick_Stop_Decel = 10000;  // 快速停止减速度
        int32_t Profile_Decel = 5000;  // 减速度
        
        for (int i = 1; i <= ec_slavecount; i++) {
            // 先禁用电机
            Control_Word = 0x0000;
            ec_SDOwrite(i, 0x6040, 0x00, FALSE, sizeof(Control_Word), &Control_Word, EC_TIMEOUTSAFE);
            osal_usleep(100000);

            // 设置操作模式为CSV
            ec_SDOwrite(i, 0x6060, 0x00, FALSE, sizeof(operation_mode), &operation_mode, EC_TIMEOUTSAFE);
            osal_usleep(100000);

            // 设置速度相关参数
            ec_SDOwrite(i, 0x6080, 0x00, FALSE, sizeof(Max_Velocity), &Max_Velocity, EC_TIMEOUTSAFE);
            ec_SDOwrite(i, 0x60C5, 0x00, FALSE, sizeof(Max_Acceleration), &Max_Acceleration, EC_TIMEOUTSAFE);
            ec_SDOwrite(i, 0x6085, 0x00, FALSE, sizeof(Quick_Stop_Decel), &Quick_Stop_Decel, EC_TIMEOUTSAFE);
            ec_SDOwrite(i, 0x6084, 0x00, FALSE, sizeof(Profile_Decel), &Profile_Decel, EC_TIMEOUTSAFE);
            
            osal_usleep(100000);
            
            // 验证模式是否设置成功
            uint8 actual_mode;
            int size = sizeof(actual_mode);
            if (ec_SDOread(i, 0x6061, 0x00, FALSE, &size, &actual_mode, EC_TIMEOUTSAFE) > 0) {
                printf("Actual operation mode: %d\n", actual_mode);
            }
        }

        while(1) {
            osal_usleep(100000);
        }
    }

    osal_usleep(1e6);

    ec_close();

     printf("\nRequest init state for all slaves\n");
     ec_slave[0].state = EC_STATE_INIT;
     /* request INIT state for all slaves */
     ec_writestate(0);

    printf("EtherCAT master closed.\n");

    return 0;
}

/* 
 * PI calculation to synchronize Linux time with the Distributed Clock (DC) time.
 */
void ec_sync(int64 reftime, int64 cycletime, int64 *offsettime) {
    static int64 offset_history[8] = {0};  // 保存最近8次的偏移值
    static int history_index = 0;
    static bool history_filled = false;
    
    // 计算当前偏移
    int64 delta = (reftime) % cycletime;
    if (delta > (cycletime / 2)) {
        delta = delta - cycletime;
    }
    
    // 添加死区以减少小偏差的补偿
    if (abs(delta) < 500) {  // 500ns的死区
        delta = 0;
    }
    
    // 更新历史数据
    offset_history[history_index] = -delta;
    history_index = (history_index + 1) % 8;
    if (history_index == 0) {
        history_filled = true;
    }
    
    // 计算移动平均
    int64 sum = 0;
    int count = history_filled ? 8 : history_index;
    for(int i = 0; i < count; i++) {
        sum += offset_history[i];
    }
    *offsettime = count > 0 ? sum / count : 0;
    
    // 限幅，使用更保守的限制
    if (*offsettime > cycletime/30) {
        *offsettime = cycletime/30;
    }
    if (*offsettime < -cycletime/30) {
        *offsettime = -cycletime/30;
    }
}


void ec_sync_pi(int64 reftime, int64 cycletime, int64 *offsettime) {
    static int64 integral = 0;
    static int64 prev_error = 0;
    static int64 prev_output = 0;
    
    // 使用整数计算（将系数乘以1000以避免浮点运算）
    const int64 KP_FACTOR = 150;    // 0.15 * 1000
    const int64 KI_FACTOR = 5;      // 0.005 * 1000
    const int64 ALPHA_FACTOR = 700; // 0.7 * 1000
    const int64 SCALE = 1000;       // 缩放因子
    
    // 计算误差
    int64 error = (reftime) % cycletime;
    if (error > (cycletime / 2)) {
        error = error - cycletime;
    }
    
    // 死区控制
    const int64 DEADBAND = 200;
    if (abs(error) < DEADBAND) {
        error = 0;
    }
    
    // 积分项抗饱和
    const int64 MAX_INTEGRAL = 20000;
    if (error * integral > 0 && abs(integral) > MAX_INTEGRAL) {
        integral = (integral > 0) ? MAX_INTEGRAL : -MAX_INTEGRAL;
    } else {
        integral += error;
    }
    
    // 使用定点数计算PI输出
    int64 raw_output = (KP_FACTOR * error + KI_FACTOR * integral) / SCALE;
    
    // 输出限幅
    const int64 MAX_ADJUSTMENT = cycletime / 30;
    if (raw_output > MAX_ADJUSTMENT) {
        raw_output = MAX_ADJUSTMENT;
    } else if (raw_output < -MAX_ADJUSTMENT) {
        raw_output = -MAX_ADJUSTMENT;
    }
    
    // 使用定点数进行输出滤波
    int64 filtered_output = (ALPHA_FACTOR * raw_output + 
                           (SCALE - ALPHA_FACTOR) * prev_output) / SCALE;
    prev_output = filtered_output;
    
    *offsettime = -filtered_output;
    prev_error = error;
    
    // 调试输出
    static int debug_counter = 0;
    if (++debug_counter >= 10000) {
        printf("Sync: err=%ld, int=%ld, out=%ld\n", error, integral, filtered_output);
        debug_counter = 0;
    }
}

/* 
 * Add nanoseconds to a timespec structure.
 * This function updates the timespec structure by adding a specified amount of time.
 */
void add_timespec(struct timespec *ts, int64 addtime) {
    int64 sec, nsec; // Variables to hold seconds and nanoseconds

    nsec = addtime % NSEC_PER_SEC; // Calculate nanoseconds to add
    sec = (addtime - nsec) / NSEC_PER_SEC; // Calculate seconds to add
    ts->tv_sec += sec; // Update seconds in timespec
    ts->tv_nsec += nsec; // Update nanoseconds in timespec
    if (ts->tv_nsec >= NSEC_PER_SEC) { // If nanoseconds exceed 1 second
        nsec = ts->tv_nsec % NSEC_PER_SEC; // Adjust nanoseconds
        ts->tv_sec += (ts->tv_nsec - nsec) / NSEC_PER_SEC; // Increment seconds
        ts->tv_nsec = nsec; // Set adjusted nanoseconds
    }
}

// CiA402 状态字位定义
#define STATUSWORD_READY_TO_SWITCH_ON_BIT    (1<<0)  // Bit 0
#define STATUSWORD_SWITCHED_ON_BIT           (1<<1)  // Bit 1
#define STATUSWORD_OPERATION_ENABLED_BIT     (1<<2)  // Bit 2
#define STATUSWORD_FAULT_BIT                 (1<<3)  // Bit 3
#define STATUSWORD_VOLTAGE_ENABLED_BIT       (1<<4)  // Bit 4
#define STATUSWORD_QUICK_STOP_BIT           (1<<5)  // Bit 5
#define STATUSWORD_SWITCH_ON_DISABLED_BIT    (1<<6)  // Bit 6

// CiA402 控制字位定义
#define CONTROLWORD_SWITCH_ON_BIT            (1<<0)  // Bit 0
#define CONTROLWORD_ENABLE_VOLTAGE_BIT       (1<<1)  // Bit 1
#define CONTROLWORD_QUICK_STOP_BIT          (1<<2)  // Bit 2
#define CONTROLWORD_ENABLE_OPERATION_BIT     (1<<3)  // Bit 3
#define CONTROLWORD_FAULT_RESET_BIT          (1<<7)  // Bit 7

// 在文件开头添加PDO统计结构体和函数定义
struct PDOStats {
    long min_receive_time;
    long max_receive_time;
    long min_send_time;
    long max_send_time;
    long total_receive_time;
    long total_send_time;
    int count;
};

// 更新PDO统计的函数
void update_pdo_stats(PDOStats& stats, long receive_time, long send_time) {
    stats.min_receive_time = std::min(receive_time, stats.min_receive_time);
    stats.max_receive_time = std::max(receive_time, stats.max_receive_time);
    stats.min_send_time = std::min(send_time, stats.min_send_time);
    stats.max_send_time = std::max(send_time, stats.max_send_time);
    stats.total_receive_time += receive_time;
    stats.total_send_time += send_time;
    stats.count++;
}

/* 
 * RT EtherCAT thread function
 */
OSAL_THREAD_FUNC_RT ecatthread(void *ptr) {
    struct timespec ts, tleft;
    int64 cycletime;
    struct timespec cycle_start, cycle_end;
    struct timespec pdo_start, pdo_end;  // PDO时间测量用
    long cycle_time_ns;
    long receive_time = 0;  // 接收时间
    long send_time = 0;     // 发送时间
    int64 dc_time_offset = 0;
    int dc_sync_counter = 0;
    const int DC_SYNC_INTERVAL = 10;
    
    // 状态机相关变量
    uint16_t slave_last_status[EC_MAXSLAVE] = {0};  // 从站状态记录
    uint32_t state_timer[EC_MAXSLAVE] = {0};        // 状态计时器
    const uint32_t STATE_STABLE_TIME = 500;         // 状态稳定时间

    // PDO统计变量
    PDOStats pdo_stats = {LONG_MAX, 0, LONG_MAX, 0, 0, 0, 0};

    // 设置线程优先级和CPU亲和性
    struct sched_param param;
    param.sched_priority = 99;
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("sched_setscheduler failed");
    }

    // 锁定内存
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed");
    }

    // 设置CPU亲和性
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == -1) {
        perror("pthread_setaffinity_np failed");
    }

    // 初始化PDO数据
    rxpdo.mode_of_operation = 9;
    rxpdo.target_velocity = 0;

    // 初始化时间
    clock_gettime(CLOCK_MONOTONIC, &ts);
    cycletime = *(int *)ptr * 1000;
    ts.tv_nsec = ((ts.tv_nsec + cycletime - 1) / cycletime) * cycletime;
    if (ts.tv_nsec >= NSEC_PER_SEC) {
        ts.tv_sec++;
        ts.tv_nsec -= NSEC_PER_SEC;
    }

    while (1) {
        // 开始测量周期时间
        clock_gettime(CLOCK_MONOTONIC, &cycle_start);

        // 测量接收时间
        clock_gettime(CLOCK_MONOTONIC, &pdo_start);
        wkc = ec_receive_processdata(EC_TIMEOUTRET);
        clock_gettime(CLOCK_MONOTONIC, &pdo_end);
        receive_time = (pdo_end.tv_sec - pdo_start.tv_sec) * NSEC_PER_SEC + 
                      (pdo_end.tv_nsec - pdo_start.tv_nsec);

        if (wkc >= expectedWKC) {
            bool all_slaves_stable = true;
            
            // 处理每个从站的状态
            for (int slave = 1; slave <= ec_slavecount; slave++) {
                memcpy(&txpdo, ec_slave[slave].inputs, sizeof(txpdo_t));
                uint16_t status = txpdo.statusword;
                
                // 检查状态位
                bool is_ready = (status & STATUSWORD_READY_TO_SWITCH_ON_BIT);
                bool is_switched_on = (status & STATUSWORD_SWITCHED_ON_BIT);
                bool is_enabled = (status & STATUSWORD_OPERATION_ENABLED_BIT);
                bool has_fault = (status & STATUSWORD_FAULT_BIT);
                bool is_disabled = (status & STATUSWORD_SWITCH_ON_DISABLED_BIT);

                // 如果状态改变，重置计时器
                if (status != slave_last_status[slave]) {
                    state_timer[slave] = 0;
                    printf("Slave %d: Status changed to 0x%04x\n", slave, status);
                    slave_last_status[slave] = status;
                    osal_usleep(1000);  // 状态变化时短暂延时
                } else {
                    state_timer[slave]++;
                }

                // 只有当状态稳定一段时间后才进行状态转换
                if (state_timer[slave] < STATE_STABLE_TIME) {
                    all_slaves_stable = false;
                    continue;
                }

                // 根据当前状态设置控制字
                if (has_fault) {
                    rxpdo.controlword = CONTROLWORD_FAULT_RESET_BIT;
                } else if (is_disabled) {
                    rxpdo.controlword = CONTROLWORD_ENABLE_VOLTAGE_BIT | CONTROLWORD_QUICK_STOP_BIT;
                } else if (!is_ready) {
                    rxpdo.controlword = CONTROLWORD_ENABLE_VOLTAGE_BIT | CONTROLWORD_QUICK_STOP_BIT;
                } else if (!is_switched_on) {
                    rxpdo.controlword = CONTROLWORD_SWITCH_ON_BIT | CONTROLWORD_ENABLE_VOLTAGE_BIT | 
                                      CONTROLWORD_QUICK_STOP_BIT;
                } else if (!is_enabled) {
                    rxpdo.controlword = CONTROLWORD_SWITCH_ON_BIT | CONTROLWORD_ENABLE_VOLTAGE_BIT | 
                                      CONTROLWORD_QUICK_STOP_BIT | CONTROLWORD_ENABLE_OPERATION_BIT;
                } else {
                    // 正常运行状态
                    rxpdo.controlword = CONTROLWORD_SWITCH_ON_BIT | CONTROLWORD_ENABLE_VOLTAGE_BIT | 
                                      CONTROLWORD_QUICK_STOP_BIT | CONTROLWORD_ENABLE_OPERATION_BIT;
                    if (all_slaves_stable) {
                        rxpdo.target_velocity = 1000;
                    }
                }

                rxpdo.mode_of_operation = 9;  // CSV模式
                memcpy(ec_slave[slave].outputs, &rxpdo, sizeof(rxpdo_t));
            }
        } else {
            // 添加错误处理
            static int error_count = 0;
            error_count++;
            if (error_count > 100) {  // 连续100次错误
                printf("Communication error, WKC: %d/%d\n", wkc, expectedWKC);
                error_count = 0;
            }
            // 出错时保持安全状态
            for (int slave = 1; slave <= ec_slavecount; slave++) {
                rxpdo.controlword = 0;
                rxpdo.target_velocity = 0;
                memcpy(ec_slave[slave].outputs, &rxpdo, sizeof(rxpdo_t));
            }
        }

        // 测量发送时间
        clock_gettime(CLOCK_MONOTONIC, &pdo_start);
        ec_send_processdata();
        clock_gettime(CLOCK_MONOTONIC, &pdo_end);
        send_time = (pdo_end.tv_sec - pdo_start.tv_sec) * NSEC_PER_SEC + 
                   (pdo_end.tv_nsec - pdo_start.tv_nsec);

        // 更新统计数据
        update_pdo_stats(pdo_stats, receive_time, send_time);

        // 每1000个周期打印一次统计信息
        if (pdo_stats.count >= 5000) {
            printf("PDO Statistics:\n");
            printf("  Receive time: %ld ns (min: %ld, max: %ld)\n", 
                   receive_time, pdo_stats.min_receive_time, pdo_stats.max_receive_time);
            printf("  Send time: %ld ns (min: %ld, max: %ld)\n", 
                   send_time, pdo_stats.min_send_time, pdo_stats.max_send_time);
            printf("  Jitter: %ld ns\n", pdo_stats.max_receive_time - pdo_stats.min_receive_time);
            printf("  CPU core: %d\n", sched_getcpu());
            printf("-------------------\n");
            
            // 重置统计数据
            pdo_stats = {LONG_MAX, 0, LONG_MAX, 0, 0, 0, 0};
        }

        // 2. 发送数据
        ec_send_processdata();
        
        // 3. 等待下一个周期
        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0) {
            if (errno == EINTR) continue;
        }

        // 4. 更新下一个周期的时间点
        ts.tv_nsec += cycletime;
        if (ts.tv_nsec >= NSEC_PER_SEC) {
            ts.tv_sec++;
            ts.tv_nsec -= NSEC_PER_SEC;
        }

        // DC同步处理
        if (ec_slave[0].hasdc) {
            // ... DC同步代码保持不变 ...
        }
    }
}

/* 
 * EtherCAT check thread function
 */
OSAL_THREAD_FUNC ecatcheck(void *ptr) {
    // Get and print current CPU core
    int cpu = sched_getcpu();
    printf("EtherCAT check thread running on CPU %d\n", cpu);
    
    int slave;
    (void)ptr; // Not used
    int consecutive_errors = 0;
    const int MAX_CONSECUTIVE_ERRORS = 5;

    while (1) {
        if (inOP && ((wkc < expectedWKC) || ec_group[currentgroup].docheckstate)) {
            if (needlf) {
                needlf = FALSE;
                printf("\n");
            }     
            // Increase the consecutive error count
            if (wkc < expectedWKC) {
                consecutive_errors++;
                printf("WARNING: Working counter error (%d/%d), consecutive errors: %d\n", 
                       wkc, expectedWKC, consecutive_errors);
            } else {
                consecutive_errors = 0;
            }
            // If the consecutive errors exceed the threshold, attempt reinitialization
            if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
                printf("ERROR: Too many consecutive errors, attempting recovery...\n");
                ec_group[currentgroup].docheckstate = TRUE;
                // Reset the error count
                consecutive_errors = 0;
            }
            ec_group[currentgroup].docheckstate = FALSE;
            ec_readstate();
            for (slave = 1; slave <= ec_slavecount; slave++) {
                if ((ec_slave[slave].group == currentgroup) && (ec_slave[slave].state != EC_STATE_OPERATIONAL)) {
                    ec_group[currentgroup].docheckstate = TRUE;
                    if (ec_slave[slave].state == (EC_STATE_SAFE_OP + EC_STATE_ERROR)) {
                        printf("ERROR: Slave %d is in SAFE_OP + ERROR, attempting ack.\n", slave);
                        ec_slave[slave].state = (EC_STATE_SAFE_OP + EC_STATE_ACK);
                        ec_writestate(slave);
                    } else if (ec_slave[slave].state == EC_STATE_SAFE_OP) {
                        printf("WARNING: Slave %d is in SAFE_OP, changing to OPERATIONAL.\n", slave);
                        ec_slave[slave].state = EC_STATE_OPERATIONAL;
                        ec_writestate(slave);
                    } else if (ec_slave[slave].state > EC_STATE_NONE) {
                        if (ec_reconfig_slave(slave, EC_TIMEOUTMON)) {
                            ec_slave[slave].islost = FALSE;
                            printf("MESSAGE: Slave %d reconfigured\n", slave);
                        }
                    } else if (!ec_slave[slave].islost) {
                        ec_statecheck(slave, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
                        if (!ec_slave[slave].state) {
                            ec_slave[slave].islost = TRUE;
                            printf("ERROR: Slave %d lost\n", slave);
                        }
                    }
                }
                if (ec_slave[slave].islost) {
                    if (!ec_slave[slave].state) {
                        if (ec_recover_slave(slave, EC_TIMEOUTMON)) {
                            ec_slave[slave].islost = FALSE;
                            printf("MESSAGE: Slave %d recovered\n", slave);
                        }
                    } else {
                        ec_slave[slave].islost = FALSE;
                        printf("MESSAGE: Slave %d found\n", slave);
                    }
                }
            }
            if (!ec_group[currentgroup].docheckstate) {
                printf("OK: All slaves resumed OPERATIONAL.\n");
            }
        }
        osal_usleep(10000); 
    }
}

int correct_count = 0;
int incorrect_count = 0;
int test_count_sum = 100;
int test_count = 0;
float correct_rate = 0;

// Main function
int main(int argc, char **argv) {
    needlf = FALSE;
    inOP = FALSE;
    
    // Get and print current CPU core for main thread
    int cpu = sched_getcpu();
    printf("Main thread running on CPU %d\n", cpu);
    
    start_ecatthread_thread = FALSE;
    ctime_thread = 1000;  // Communication period

    // 只设置主线程的CPU亲和性
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);  // 主线程使用 CPU core 2

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
        perror("sched_setaffinity");
        return EXIT_FAILURE;
    }

    printf("Main thread running on CPU core 2\n");

    // 调用erob_test()，线程的CPU亲和性将在其中设置
    erob_test();
    printf("End program\n");

    return EXIT_SUCCESS;
}