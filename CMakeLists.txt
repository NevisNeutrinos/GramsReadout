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

add_subdirectory(quill)
add_subdirectory(lib/pcie_driver)
add_subdirectory(networking)
add_subdirectory(PGramsCommCodec)
add_subdirectory(ReadoutDataMonitor)

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
                    ReadoutDataMonitor/readout_decoder/src)

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
        pcie_lib)


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

target_compile_definitions(daq_orchestrator PUBLIC
        -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO)

target_link_libraries(daq_orchestrator PRIVATE
        gramsreadout
        Datamonitor
        quill::quill
        pcie_lib
        statgrab)

# Define the installation location for the daemon
message("Installing DAQ Orchestrator")
install(TARGETS daq_orchestrator DESTINATION /usr/local/bin)

#######################
# Test

add_executable(PcieReadWrite tests/pcie_read_write.cpp)
target_link_libraries(PcieReadWrite PRIVATE
        pcie_lib)
