cmake_minimum_required(VERSION 3.30)
project(GramsReadout)

set(CMAKE_CXX_STANDARD 20)

# Set the expected WinDriver installation and make sure it exists
# The WinDriver _must_ exist to communicate with the hardware, hence
# an error if not found.
set(WINDRIVER_ROOT $ENV{HOME}/WinDriver)

if (EXISTS ${WINDRIVER_ROOT})
    message(STATUS "WinDriver installation directory is: ${WINDRIVER_ROOT}")
else ()
    message(FATAL_ERROR "WinDriver installation directory does not exist: ${WINDRIVER_ROOT}")
endif ()

## Add a custom target to build the library using the existing Makefile
#add_custom_target(
#        PCIeLib ALL
#        COMMAND make
#        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/pcie_driver/linux
#)

add_subdirectory(lib/pcie_driver)
include_directories(lib/pcie_driver)

add_executable(GramsReadout main.cpp
        src/controller.cpp
        src/controller.h
        src/pcie_control.cpp
        src/pcie_control.h
        src/configure_hardware.cpp
        src/configure_hardware.h)

# Add an include directory for GramsReadout
target_include_directories(GramsReadout PRIVATE ${CMAKE_SOURCE_DIR}/lib/pcie_driver)


#add_dependencies(GramsReadout PCIeLib)

# Collect all .o files in the directory
#file(GLOB EXTERNAL_OBJECTS "${CMAKE_SOURCE_DIR}/lib/pcie_driver/linux/*.o")
#target_link_libraries(GramsReadout PUBLIC ${EXTERNAL_OBJECTS})

target_link_libraries(GramsReadout PRIVATE gramsreadout_lib)
