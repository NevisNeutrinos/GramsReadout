cmake_minimum_required(VERSION 3.15)
project(GramsReadout)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_BUILD_TYPE Debug)

# Enable pedantic compile mode, we want to be as strict as possible
# at compile time rather than find problems at run time which could be too late.
add_compile_options(-Wall -Wextra -pedantic)

# Set the expected WinDriver installation and make sure it exists
# The WinDriver _must_ exist to communicate with the hardware, hence
# an error if not found.
set(WINDRIVER_ROOT $ENV{HOME}/WinDriver)
if (EXISTS ${WINDRIVER_ROOT})
    message(STATUS "WinDriver installation directory is: ${WINDRIVER_ROOT}")
else ()
    message(FATAL_ERROR "WinDriver installation directory does not exist: ${WINDRIVER_ROOT}")
endif ()

# For Grafana metrics
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)

add_subdirectory(quill)
add_subdirectory(lib/pcie_driver)
add_subdirectory(networking)

include_directories(/usr/include)
include_directories(networking
                    src/control
                    src/hardware
                    lib/monitoring
                    lib/pcie_driver
                    lib/folly
                    lib/nlohmann_json)

file(GLOB_RECURSE CONTROL_SRC src/control/*.cpp)
file(GLOB_RECURSE DATA_HANDLER_SRC src/data/*.cpp)
file(GLOB_RECURSE HARDWARE_SRC src/hardware/*.cpp)
file(GLOB NETWORK_SRC networking/tcp*.cpp)

# Create a static library containing DataMonitor
add_library(data_monitor STATIC lib/monitoring/data_monitor.cpp)
target_include_directories(data_monitor PRIVATE ${ZeroMQ_INCLUDE_DIRS})
target_link_libraries(data_monitor PRIVATE ${ZeroMQ_LIBRARIES})

# Compile the main program into an executable and library
add_executable(GramsReadout main.cpp
        ${CONTROL_SRC}
        ${DATA_HANDLER_SRC}
        ${HARDWARE_SRC}
        ${NETWORK_SRC})

target_compile_definitions(GramsReadout PUBLIC
        -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO)

target_link_libraries(GramsReadout PRIVATE
        data_monitor
        quill::quill
        pcie_lib)


add_library(gramsreadout STATIC src/control/controller.cpp
                                    ${CONTROL_SRC}
                                    ${DATA_HANDLER_SRC}
                                    ${HARDWARE_SRC}
                                    ${NETWORK_SRC})

target_compile_definitions(gramsreadout PUBLIC
        -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO)

target_link_libraries(gramsreadout PRIVATE
        data_monitor
        quill::quill
        pcie_lib)

#######################
# Daemon

add_executable(daq_orchestrator daemon/daq_orchestrator.cpp)

target_compile_definitions(daq_orchestrator PUBLIC
        -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO)

target_link_libraries(daq_orchestrator PRIVATE
        gramsreadout
        data_monitor
        quill::quill
        pcie_lib
        statgrab)

# Define the installation location for the daemon
install(TARGETS daq_orchestrator DESTINATION /usr/local/bin)

#######################
# Test

add_executable(PcieReadWrite tests/pcie_read_write.cpp)
target_link_libraries(PcieReadWrite PRIVATE
        pcie_lib)
