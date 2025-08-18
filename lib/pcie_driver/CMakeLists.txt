# Jungo Connectivity Confidential. Copyright (c) 2025 Jungo Connectivity Ltd.
# https://www.jungo.com

cmake_minimum_required(VERSION 3.10)
project(pcie_lib LANGUAGES C)

# Check if WD_BASEDIR is valid
if (NOT DEFINED ENV{WD_BASEDIR})
    message(FATAL_ERROR "You must set WD_BASEDIR to point to a Windriver installation!")
elseif (NOT EXISTS "$ENV{WD_BASEDIR}/include/windrvr.h")
    message(FATAL_ERROR "Please set the WD_BASEDIR variable to point to the location of your WinDriver installation directory.")
else ()
    message("WD_BASEDIR set to: $ENV{WD_BASEDIR}")
endif()

# Check to make sure the Windriver kernel module name is defined
if (DEFINED ENV{WD_KERNEL_MODULE_NAME})
    set(SET_WD_KERNEL_MODULE_NAME $ENV{WD_KERNEL_MODULE_NAME})
    message(STATUS "Set Windriver kernel module name to: ${SET_WD_KERNEL_MODULE_NAME}")
else ()
    message(FATAL_ERROR "Windriver kernel module name not set! Please set WD_KERNEL_MODULE_NAME")
endif ()

# Debugging settings
option(DEBUG "Enable debugging code" OFF)

if(DEBUG)
    set(DEBFLAGS "-g" "-O" "-DDEBUG")
else()
    set(DEBFLAGS "-O2")
endif()

# Detect CPU architecture
if(NOT DEFINED TARGET_CPU)
    execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE TARGET_CPU
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

message("Compiling for CPU ${TARGET_CPU}")

# Set CFLAGS based on architecture
include_directories($ENV{WD_BASEDIR})
include_directories($ENV{WD_BASEDIR}/include)
include_directories($ENV{WD_BASEDIR}/samples/c/shared)

# The "-Wno-unused-result" flag suppresses compiler warnings for unused values
# returned from functions. Suppressing warnings is not ideal but since this warning
# originates deep within the WinDriver library, the choice was made to ignore it
# rather than modify the WinDriver libraries.
set(CFLAGS "-fno-pie" "-Wno-unused-result" "-DLINUX" "-Wall" "-DWD_DRIVER_NAME_CHANGE")
set(LFLAGS "-no-pie" "-lwdapi1630" "-lpthread")

if(TARGET_CPU STREQUAL "x86_64")
    list(APPEND CFLAGS "-Dx86_64" "-m64")
    list(APPEND LFLAGS "-m64")
elseif(TARGET_CPU STREQUAL "i386")
    list(APPEND CFLAGS "-Dx86")
elseif(TARGET_CPU STREQUAL "ARM")
    list(APPEND CFLAGS "-DARM")
elseif(TARGET_CPU STREQUAL "ARM64")
    list(APPEND CFLAGS "-DARM64")
endif()

list(APPEND CFLAGS ${DEBFLAGS})

# Source files
set(SRCS
        gramsreadout_lib.c
        $ENV{WD_BASEDIR}/samples/c/shared/diag_lib.c
        $ENV{WD_BASEDIR}/samples/c/shared/wdc_diag_lib.c
        $ENV{WD_BASEDIR}/samples/c/shared/wds_diag_lib.c
        $ENV{WD_BASEDIR}/samples/c/shared/pci_menus_common.c
        pcie_interface.cpp
        pcie_interface.h
)

# Create a library target
add_library(pcie_lib STATIC ${SRCS})

# Set the Windriver kernel module name
target_compile_definitions(pcie_lib
        PUBLIC
        KP_GRAMSREADOUT_DRIVER_NAME="${SET_WD_KERNEL_MODULE_NAME}")

# Set compilation flags for the library
target_compile_options(pcie_lib PRIVATE ${CFLAGS})

# Set linking flags for the library
target_link_libraries(pcie_lib PRIVATE ${LFLAGS})

# Set include directories for the library
target_include_directories(pcie_lib PRIVATE
        $ENV{WD_BASEDIR}/include
        $ENV{WD_BASEDIR}
        $ENV{WD_BASEDIR}/samples/c/shared
)
