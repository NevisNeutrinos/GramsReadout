cmake_minimum_required(VERSION 3.30)
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
include_directories(lib/pcie_driver)

add_executable(GramsReadout main.cpp
        src/controller.cpp
        src/controller.h
        src/pcie_control.cpp
        src/pcie_control.h
        src/configure_hardware.cpp
        src/configure_hardware.h)

target_link_libraries(GramsReadout PRIVATE
        quill::quill
        gramsreadout_lib)

####################
# TCP/IP Server
####################

# ASIO should be in the home directory and named asio-xx.yy.zz with xyz the version
set(ASIO_INCLUDE_DIR "$ENV{HOME}/asio*/include")

add_executable(GramsReadoutServer server.cpp
        src/networking/tcp_server.cpp
        src/networking/tcp_server.h)

target_compile_definitions(GramsReadoutServer PRIVATE ASIO_STANDALONE)
target_include_directories(GramsReadoutServer PRIVATE ${ASIO_INCLUDE_DIR})
target_link_libraries(GramsReadoutServer PRIVATE pthread)

add_executable(GramsReadoutClient tests/tcp_client.cpp)
target_compile_definitions(GramsReadoutClient PRIVATE ASIO_STANDALONE)
target_include_directories(GramsReadoutClient PRIVATE ${ASIO_INCLUDE_DIR})
target_link_libraries(GramsReadoutClient PRIVATE pthread)