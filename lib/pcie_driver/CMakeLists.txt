# Jungo Connectivity Confidential. Copyright (c) 2025 Jungo Connectivity Ltd.
# https://www.jungo.com

cmake_minimum_required(VERSION 3.10)
project(pcie_lib LANGUAGES C)

# TODO: Set WD_BASEDIR to the location of your WinDriver installation directory.
set(WD_BASEDIR "/home/sabertooth/WinDriver" CACHE PATH "WinDriver installation directory")

# Check if WD_BASEDIR is valid
if(NOT EXISTS "${WD_BASEDIR}/include/windrvr.h")
    message(FATAL_ERROR "Please set the WD_BASEDIR variable to point to the location of your WinDriver installation directory.")
endif()

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
include_directories(${WD_BASEDIR})
include_directories(${WD_BASEDIR}/include)
include_directories(${WD_BASEDIR}/samples/c/shared)

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
        ${WD_BASEDIR}/samples/c/shared/diag_lib.c
        ${WD_BASEDIR}/samples/c/shared/wdc_diag_lib.c
        ${WD_BASEDIR}/samples/c/shared/wds_diag_lib.c
        ${WD_BASEDIR}/samples/c/shared/pci_menus_common.c
        pcie_interface.cpp
        pcie_interface.h
)

# Create a library target
add_library(pcie_lib STATIC ${SRCS})

# Set compilation flags for the library
target_compile_options(pcie_lib PRIVATE ${CFLAGS})

# Set linking flags for the library
target_link_libraries(pcie_lib PRIVATE ${LFLAGS})

# Set include directories for the library
target_include_directories(pcie_lib PRIVATE
        ${WD_BASEDIR}/include
        ${WD_BASEDIR}
        ${WD_BASEDIR}/samples/c/shared
)
