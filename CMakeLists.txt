cmake_minimum_required(VERSION 3.15)
project(GramsReadout)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_BUILD_TYPE Debug)

# Enable pedantic compile mode, we want to be as strict as possible
# at compile time rather than find problems at run time which could be too late.
add_compile_options(-Wall -Wextra -pedantic)

# For Grafana metrics
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)
find_package(Boost REQUIRED COMPONENTS python3 regex)

if(NOT DEFINED ENV{GLIB})
    message(FATAL_ERROR "Environment variable GLIB is not set! Please set it to the TOF library install directory, e.g.:\n"
                        "  export GLIB=\$HOME/work/source/grams-tof-library/00build/00install")
endif()

set(TOFLIB_INSTALL $ENV{GLIB})
message(STATUS "Using TOF library install dir: ${TOFLIB_INSTALL}")

find_package(fmt REQUIRED PATHS "${TOFLIB_INSTALL}/lib/cmake/fmt" NO_DEFAULT_PATH)
set(CMAKE_PREFIX_PATH "${TOFLIB_INSTALL}" ${CMAKE_PREFIX_PATH})
find_package(GramsTofLibrary REQUIRED)

if(DEFINED ENV{WD_BASEDIR})
    set(WD_INCLUDE_DIR "$ENV{WD_BASEDIR}/include")
    set(WD_LIB_DIR "$ENV{WD_BASEDIR}/lib")
    message(STATUS "Using Windriver include dir: ${WD_INCLUDE_DIR}")
    message(STATUS "Using Windriver lib dir: ${WD_LIB_DIR}")
else()
    message(FATAL_ERROR "WD_BASEDIR environment variable not set!")
endif()

# The Communication code definitions, use the shared one but fall back to local copy if shared is not found
set(PRIMARY_COMM_CODES "$ENV{BALLOON_BASE_DIR}/include")
set(FALLBACK_COMM_CODES "$ENV{TPC_DAQ_BASEDIR}/PGramsCommCodec/comm_codes")

# Check if the primary file exists on the file system
if(DEFINED PRIMARY_COMM_CODES)
    message(STATUS "Found primary Communication Code definitions: ${PRIMARY_COMM_CODES}")
    include_directories("${PRIMARY_COMM_CODES}")
else()
    message(STATUS "Primary Communication Code definitions not found, falling back to local copy: ${FALLBACK_COMM_CODES}")
    include_directories("${FALLBACK_COMM_CODES}")
endif()

file(GLOB ASIO_SEARCH_CUSTOM_PATH "$ENV{HOME}/asio*/include")
find_path(ASIO_INCLUDE_DIR asio.hpp
        HINTS ${ASIO_SEARCH_CUSTOM_PATH} NO_DEFAULT_PATH)

if (ASIO_INCLUDE_DIR)
    message(STATUS "[GramsReadout] Found ASIO: ${ASIO_INCLUDE_DIR}")
    include_directories("${ASIO_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "ASIO not found")
endif()

include_directories(/usr/include)
include_directories(networking
                    src/control
                    src/hardware
                    src/status
                    lib/pcie_driver
                    lib/folly
                    lib/nlohmann_json
                    PGramsCommCodec/include
                    ReadoutDataMonitor/src/common
                    ReadoutDataMonitor/src/monitor_algs
                    ReadoutDataMonitor/readout_decoder/src
                    ReadoutDataMonitor/readout_decoder/src
                    ${WD_INCLUDE_DIR}
)
link_directories(
    ${WD_LIB_DIR}
)

add_subdirectory(quill)
add_subdirectory(lib/pcie_driver)
add_subdirectory(networking)
add_subdirectory(PGramsCommCodec)
add_subdirectory(ReadoutDataMonitor)

file(GLOB_RECURSE CONTROL_SRC src/control/*.cpp)
file(GLOB_RECURSE DATA_HANDLER_SRC src/data/*.cpp)
file(GLOB_RECURSE HARDWARE_SRC src/hardware/*.cpp)
file(GLOB_RECURSE STATUS_SRC src/status/*.cpp)
file(GLOB NETWORK_SRC networking/tcp*.cpp)
file(GLOB METRIC_SRC PGramsCommCodec/src/*.cpp)
file(GLOB DATAMONITOR_CM_SRC ReadoutDataMonitor/src/common/*.cpp)
file(GLOB DATAMONITOR_MA_SRC ReadoutDataMonitor/src/monitor_algs/*.cpp)
file(GLOB DATAMONITOR_DEC_SRC ReadoutDataMonitor/readout_decoder/src/*.cpp)

# Create a static library containing DataMonitor
#add_library(data_monitor STATIC lib/monitoring/data_monitor.cpp)
#target_include_directories(data_monitor PRIVATE ${ZeroMQ_INCLUDE_DIRS})
#target_link_libraries(data_monitor PRIVATE ${ZeroMQ_LIBRARIES})

# Compile the main program into an executable and library
add_executable(GramsReadout main.cpp
        ${CONTROL_SRC}
        ${DATA_HANDLER_SRC}
        ${HARDWARE_SRC}
        ${STATUS_SRC}
        ${NETWORK_SRC}
        ${METRIC_SRC})

target_compile_definitions(GramsReadout PUBLIC
        -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO)

target_link_libraries(GramsReadout PRIVATE
        quill::quill
        pcie_lib
        wdapi1630
)

add_library(gramsreadout STATIC src/control/controller.cpp
                                    ${CONTROL_SRC}
                                    ${DATA_HANDLER_SRC}
                                    ${HARDWARE_SRC}
                                    ${STATUS_SRC}
                                    ${NETWORK_SRC}
                                    ${METRIC_SRC})

target_compile_definitions(gramsreadout PUBLIC
        -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO)

target_link_libraries(gramsreadout PRIVATE
        quill::quill
        pcie_lib)

#######################
# TPC Data Monitor
add_library(Datamonitor STATIC ReadoutDataMonitor/src/common/data_monitor.cpp
        ${DATAMONITOR_CM_SRC}
        ${DATAMONITOR_MA_SRC}
        ${DATAMONITOR_DEC_SRC}
        ${NETWORK_SRC}
        ${METRIC_SRC})

target_link_libraries(Datamonitor PRIVATE datamon_core)
target_link_libraries(Datamonitor PRIVATE pthread)
target_link_libraries(DataMonitor PRIVATE raw_decoder)

#######################
# Daemon

add_executable(daq_orchestrator daemon/daq_orchestrator.cpp)

target_include_directories(daq_orchestrator PRIVATE
        ${TOFLIB_INSTALL}/include
        ${TOFLIB_INSTALL}/include/FlightOps
        ${TOFLIB_INSTALL}/include/daqd
        ${TOFLIB_INSTALL}/include/base
        ${TOFLIB_INSTALL}/include/rawdata
        ${TOFLIB_INSTALL}/include/scripts_c
        ${TOFLIB_INSTALL}/include/shota
        ${Boost_INCLUDE_DIRS}
)
target_link_directories(daq_orchestrator PRIVATE
    ${TOFLIB_INSTALL}/lib
)

target_compile_definitions(daq_orchestrator PUBLIC
        -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO)

target_link_libraries(daq_orchestrator PRIVATE
        gramsreadout
        Datamonitor
        quill::quill
        pcie_lib
        statgrab
        GramsTofFlightOpsLib
        ${Boost_LIBRARIES}
)

# Define the installation location for the daemon
message("Installing DAQ Orchestrator")
install(TARGETS daq_orchestrator DESTINATION /usr/local/bin)

#######################
# Test

add_executable(PcieReadWrite tests/pcie_read_write.cpp)
target_link_libraries(PcieReadWrite PRIVATE
        pcie_lib)
