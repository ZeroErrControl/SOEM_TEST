# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.22

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/erobman/eRob_SOEM_linux

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/erobman/eRob_SOEM_linux/build

# Include any dependencies generated for this target.
include demo/CMakeFiles/eRob_CST.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include demo/CMakeFiles/eRob_CST.dir/compiler_depend.make

# Include the progress variables for this target.
include demo/CMakeFiles/eRob_CST.dir/progress.make

# Include the compile flags for this target's objects.
include demo/CMakeFiles/eRob_CST.dir/flags.make

demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o: demo/CMakeFiles/eRob_CST.dir/flags.make
demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o: ../demo/eRob_CST.cpp
demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o: demo/CMakeFiles/eRob_CST.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/erobman/eRob_SOEM_linux/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o"
	cd /home/erobman/eRob_SOEM_linux/build/demo && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o -MF CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o.d -o CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o -c /home/erobman/eRob_SOEM_linux/demo/eRob_CST.cpp

demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/eRob_CST.dir/eRob_CST.cpp.i"
	cd /home/erobman/eRob_SOEM_linux/build/demo && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/erobman/eRob_SOEM_linux/demo/eRob_CST.cpp > CMakeFiles/eRob_CST.dir/eRob_CST.cpp.i

demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/eRob_CST.dir/eRob_CST.cpp.s"
	cd /home/erobman/eRob_SOEM_linux/build/demo && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/erobman/eRob_SOEM_linux/demo/eRob_CST.cpp -o CMakeFiles/eRob_CST.dir/eRob_CST.cpp.s

# Object files for target eRob_CST
eRob_CST_OBJECTS = \
"CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o"

# External object files for target eRob_CST
eRob_CST_EXTERNAL_OBJECTS =

demo/eRob_CST: demo/CMakeFiles/eRob_CST.dir/eRob_CST.cpp.o
demo/eRob_CST: demo/CMakeFiles/eRob_CST.dir/build.make
demo/eRob_CST: libsoem.so
demo/eRob_CST: demo/CMakeFiles/eRob_CST.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/erobman/eRob_SOEM_linux/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable eRob_CST"
	cd /home/erobman/eRob_SOEM_linux/build/demo && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/eRob_CST.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
demo/CMakeFiles/eRob_CST.dir/build: demo/eRob_CST
.PHONY : demo/CMakeFiles/eRob_CST.dir/build

demo/CMakeFiles/eRob_CST.dir/clean:
	cd /home/erobman/eRob_SOEM_linux/build/demo && $(CMAKE_COMMAND) -P CMakeFiles/eRob_CST.dir/cmake_clean.cmake
.PHONY : demo/CMakeFiles/eRob_CST.dir/clean

demo/CMakeFiles/eRob_CST.dir/depend:
	cd /home/erobman/eRob_SOEM_linux/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/erobman/eRob_SOEM_linux /home/erobman/eRob_SOEM_linux/demo /home/erobman/eRob_SOEM_linux/build /home/erobman/eRob_SOEM_linux/build/demo /home/erobman/eRob_SOEM_linux/build/demo/CMakeFiles/eRob_CST.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : demo/CMakeFiles/eRob_CST.dir/depend

