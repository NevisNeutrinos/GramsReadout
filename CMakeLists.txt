cmake_minimum_required(VERSION 3.15)
project(GramsReadout)

set(CMAKE_CXX_STANDARD 17)

# Set the expected WinDriver installation and make sure it exists
# The WinDriver _must_ exist to communicate with the hardware, hence
# an error if not found.
set(WINDRIVER_ROOT $ENV{HOME}/WinDriver)

if (EXISTS ${WINDRIVER_ROOT})
    message(STATUS "WinDriver installation directory is: ${WINDRIVER_ROOT}")
else ()
    message(FATAL_ERROR "WinDriver installation directory does not exist: ${WINDRIVER_ROOT}")
endif ()

add_subdirectory(quill)
add_subdirectory(lib/pcie_driver)
add_subdirectory(networking)
#add_subdirectory(tests)
include_directories(lib/pcie_driver)

add_executable(GramsReadout main.cpp
        src/controller.cpp
        src/controller.h
        src/pcie_control.cpp
        src/pcie_control.h
        src/configure_hardware.cpp
        src/configure_hardware.h
        networking/tcp_connection.cpp
        networking/tcp_connection.h
        networking/tcp_protocol.cpp
        networking/tcp_protocol.h)

target_link_libraries(GramsReadout PRIVATE
        quill::quill
        gramsreadout_lib)

add_executable(ReadoutTest test.cpp
        src/pcie_control.cpp
        src/pcie_control.h
        src/configure_hardware.cpp
        src/configure_hardware.h)

target_link_libraries(ReadoutTest PRIVATE
        quill::quill
        gramsreadout_lib)


#######################
# Test

add_executable(PcieReadWrite tests/pcie_read_write.cpp)
target_link_libraries(PcieReadWrite PRIVATE
        gramsreadout_lib)
