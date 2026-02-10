//
// Created by Jon Sensenig on 3/31/25.
//

#include "controller.h"
#include "tcp_protocol.h"
#include "daq_comp_monitor.h"
#include "CommunicationCodes.hh"
// #include "../ReadoutDataMonitor/src/common/data_monitor.h"
//#include "data_monitor.h"

#include <systemd/sd-bus.h>
#include <asio.hpp>
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/FileSink.h"

#include <csignal> // For signal handling
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring> // For strerror
#include <exception>
#include <iostream> // For initial errors before logging is setup
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
// Status calls
#include <sys/vfs.h>
#include <sys/sysinfo.h>
#include <statgrab.h>
#include <fstream>
#include <string>

// --- Configuration ---
// Best practice: Load these from a config file or environment variables
// For simplicity, using constants here. Consider systemd Environment= directive.

const char* kHubIp = "127.0.0.1";  // Hub Computer IP

const uint16_t kDaemonCommandPort = 50001; // Daemon software port, for commands
const uint16_t kDaemonStatusPort = 50000; // Daemon software port, for status

// Daemon control constants
const std::string kTpcDaq = "tpc_daq.service";
const std::string kDataMonitor = "data_monitor.service";
const std::string kTofDaq = "tof_daq.service";
const std::string kStartUnit = "StartUnit";
const std::string kStopUnit = "StopUnit";

// --- Global Variables ---
namespace pgrams::orchestrator { // Use anonymous namespace for internal linkage

using namespace communication;

std::atomic_bool g_running(true);
std::atomic_bool g_status_running(false);
std::condition_variable g_shutdown_cv;
std::mutex g_shutdown_mutex;

// Keep a global monitor instance
DaqCompMonitor g_daq_monitor{};

// --- Signal Handling ---
void SignalHandler(int signum) {
    // This function must be async-signal-safe.
    // Setting an atomic bool is safe. Notifying a CV might not be strictly
    // safe but is common. Logging here might be unsafe depending on logger.
    // Best practice is to log about the signal *after* returning to the main loop.
    if (signum == SIGTERM || signum == SIGINT) {
        // Signal the main loop to shut down
        g_running.store(false);
        // Notify the main thread to wake up if it's waiting on the CV.
        {
            // Lock briefly only to satisfy condition_variable requirements
            std::lock_guard<std::mutex> lock(g_shutdown_mutex);
        }
        g_shutdown_cv.notify_one();
    } else if (signum == SIGHUP) {
    // Optional: Reload configuration or just log and continue/shutdown
    // For now, treat like TERM/INT for simplicity
    g_running.store(false);
    {
        std::lock_guard<std::mutex> lock(g_shutdown_mutex);
    }
        g_shutdown_cv.notify_one();
    }
}

void SetupSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = nullptr;
    sa.sa_mask = {};
    sa.sa_flags = 0;

    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask); // Don't block signals during handling
    sa.sa_flags = SA_RESTART; // Restart syscalls if possible after handler

    constexpr int signals_to_handle[] = {SIGTERM, SIGINT, SIGHUP};
    for (int sig : signals_to_handle) {
        if (sigaction(sig, &sa, nullptr) == -1) {
            // Log directly to stderr before Quill is potentially initialized
            std::cerr << "FATAL: Failed to set signal handler for " << sig << ": " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
      }
    }

    // Ignore SIGPIPE, common in network apps
    sa.sa_handler = SIG_IGN; // Ignore the signal
    if (sigaction(SIGPIPE, &sa, nullptr) == -1) {
        // Non-fatal, but log it
        std::cerr << "WARNING: Failed to ignore SIGPIPE: " << strerror(errno) << std::endl;
    }
}

// --- Logging Setup ---
void SetupLogging() {
    quill::Logger* logger = nullptr;
    try {
        quill::BackendOptions backend_options;
        // 100us uses very little CPU when not actively logging
        backend_options.sleep_duration = std::chrono::microseconds{100};
        quill::Backend::start(backend_options);

        // Configure the default logger to use stdout
        // systemd's journald will capture this output.
        logger = quill::Frontend::create_or_get_logger("readout_logger",
                                quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id1"));
        // Ensure logging level (e.g., INFO and above)
         logger->set_log_level(quill::LogLevel::Info);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Failed to initialize Quill logging: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    } catch (...) {
        std::cerr << "FATAL: Unknown error initializing Quill logging." << std::endl;
        exit(EXIT_FAILURE);
    }
    // First log message using Quill
    QUILL_LOG_INFO(logger, "Logging initialized successfully. Outputting to stdout.");
}

void GetCoreDiskTemp() {

    std::array<uint32_t, NUM_CPUS> core_temp{};
    // Read disk temperature
    try {
        int disk = 1;
        std::ifstream hwmon("/sys/class/hwmon/hwmon1/temp" + std::to_string(disk) + "_input");
        if (!hwmon.is_open()) {
            std::cerr << "Could not open hwmon" << std::endl;
            g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::cpu_temp_status);
            return;
        }
        int tempMilli;
        hwmon >> tempMilli;
        // core_disk_temp.at(NUM_CPUS) = static_cast<int>(tempMilli / 1000.0);
        g_daq_monitor.setDiskTemp(static_cast<int>(tempMilli / 1000.0));
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Failed to read Disk temp" << std::endl;
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::cpu_temp_status);
    }
    // Read CPU temperatures
    for (int i = 0; i < static_cast<int>(NUM_CPUS); ++i) {
        try {
            std::ifstream hwmon("/sys/class/hwmon/hwmon2/temp" + std::to_string(i+1) + "_input");
            if (!hwmon.is_open()) {
                std::cerr << "Could not open hwmon" << std::endl;
                g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::disk_temp_status);
                continue;
            }
            int tempMilli;
            hwmon >> tempMilli;
            core_temp.at(i) = static_cast<int>(tempMilli / 1000.0);
        } catch (const std::exception& e) {
            std::cerr << "FATAL: Failed to read CPU temp" << std::endl;
            g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::disk_temp_status);
        }
    }
    g_daq_monitor.setCpuTemp(core_temp);
}

void GetMemoryStats(quill::Logger *logger) {
    size_t mem_entries;
    sg_mem_stats *mem_stats = sg_get_mem_stats(&mem_entries);
    if (!mem_stats) {
        g_daq_monitor.setMemoryUsage(0);
        QUILL_LOG_ERROR(logger, "Error getting memory stats: {}", strerror(errno));
    }
    try {
        if (mem_entries > 0) {
            const size_t total_memory_bytes = mem_stats->total;
            const size_t free_memory_bytes = mem_stats->free;
            const size_t used_memory_bytes = total_memory_bytes - free_memory_bytes;
            const double memory_usage = (total_memory_bytes > 0) ?
                        (100.0 * static_cast<double>(used_memory_bytes) / total_memory_bytes) : 0.0;
            g_daq_monitor.setMemoryUsage(static_cast<uint32_t>(memory_usage));
        } else {
            QUILL_LOG_WARNING(logger, "No memory stats available!");
        }
    } catch (std::exception& e) {
        QUILL_LOG_ERROR(logger, "Error getting memory stats: {}", e.what());
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::memory_usage_status);
    }
}

void GetCpuStats(quill::Logger *logger) {
    size_t cpu_entries1;
    QUILL_LOG_DEBUG(logger, "Getting CPU Stat 1");
    sg_cpu_stats *cpu_stats = sg_get_cpu_stats(&cpu_entries1);
    if (!cpu_stats) {
        g_daq_monitor.setCpuUsage(0);
        QUILL_LOG_ERROR(logger, "Error getting CPU stats: {}", strerror(errno));
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::cpu_usage_status);
        return;
    }
    double total_cpu1 = cpu_stats->user + cpu_stats->nice;
    double cpu_idle1 = cpu_stats->idle;

    // Sleep for 1s to get the average CPU usage
    sleep(1);

    size_t cpu_entries2;
    QUILL_LOG_DEBUG(logger, "Getting CPU Stat 2");
    cpu_stats = sg_get_cpu_stats(&cpu_entries2);
    if (!cpu_stats) {
        QUILL_LOG_ERROR(logger, "Error getting CPU stats: {}", strerror(errno));
        g_daq_monitor.setCpuUsage(0);
    }
    double total_cpu2 = cpu_stats->user + cpu_stats->nice;
    double cpu_idle2 = cpu_stats->idle;

    try {
        if (cpu_entries1 > 0 && cpu_entries2 > 0) {
            double total_cpu_diff = total_cpu2 - total_cpu1;
            double cpu_idle_diff = cpu_idle2 - cpu_idle1;
            QUILL_LOG_DEBUG(logger, "Total total/idle diff {}/{}", total_cpu_diff, cpu_idle_diff);
            double cpu_usage = ((total_cpu_diff + total_cpu_diff) > 0) ?
                                (100.0 * total_cpu_diff / (total_cpu_diff + cpu_idle_diff)) : 0.0;
            g_daq_monitor.setCpuUsage(static_cast<uint32_t>(cpu_usage));
        } else {
            QUILL_LOG_WARNING(logger, "No memory stats available!");
        }
    } catch (std::exception& e) {
        QUILL_LOG_ERROR(logger, "Error getting memory stats: {}", e.what());
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::cpu_usage_status);
    }
}

void GetComputerStatus(quill::Logger *logger) {
    // Define the scaling factor for load averages if not readily available
    // Usually it's (1 << SI_LOAD_SHIFT), and SI_LOAD_SHIFT is typically 16
    // constexpr double LOAD_SCALE = 65536.0; // 2^16
    auto data_basedir = std::string(std::getenv("DATA_BASE_DIR"));
    constexpr unsigned long long GB_divisor = 1024 * 1024 * 1024;

    try {
        struct statfs data_disk_info{};
        if (statfs(data_basedir.c_str(), &data_disk_info) == 0) {
            const auto free_space = static_cast<unsigned long>(data_disk_info.f_bavail * data_disk_info.f_frsize);
            g_daq_monitor.setTpcDisk(free_space / GB_divisor);
        } else {
            QUILL_LOG_ERROR(logger, "Failed to get data disk space with error {}", strerror(errno));
            g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::disk_free_status);
        }
        struct statfs tof_data_disk_info{};
        if (statfs((data_basedir + "/tof_data").c_str(), &tof_data_disk_info) == 0) {
            const auto free_space = static_cast<unsigned long>(tof_data_disk_info.f_bavail * tof_data_disk_info.f_frsize);
            g_daq_monitor.setTofDisk(free_space / GB_divisor);
        } else {
            QUILL_LOG_ERROR(logger, "Failed to get TOF data disk space with error {}", strerror(errno));
            g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::disk_free_status);
        }
        struct statfs main_disk_info{};
        if (statfs("/", &main_disk_info) == 0) {
            const auto free_space = static_cast<unsigned long>(main_disk_info.f_bavail * main_disk_info.f_frsize);
            g_daq_monitor.setSysDisk(free_space / GB_divisor);
        } else {
            QUILL_LOG_ERROR(logger, "Failed to get main disk space with error {}", strerror(errno));
            g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::disk_free_status);
        }

       // Initialize libstatgrab
        if (!sg_init(0)) {
            // The 1s load average across 12 CPUs
            GetCpuStats(logger);
            // RAM usage
            GetMemoryStats(logger);
            sg_shutdown();
        } else {
            QUILL_LOG_ERROR(logger, "Failed to Init libstatgrab with error code {}", strerror(errno));
            g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::cpu_usage_status);
            g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::memory_usage_status);
        }
        GetCoreDiskTemp();
    } catch (const std::exception& e) {
        QUILL_LOG_ERROR(logger, "Failed to get disk space with exception: {}", e.what());
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::disk_free_status);
    }
}

void SendStatus(std::unique_ptr<TCPConnection> &status_client_ptr, quill::Logger *logger) {
    while (g_status_running.load()) {
        QUILL_LOG_INFO(logger, "Sending status...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        GetComputerStatus(logger); // returns vector of 0's on failure
        auto status = g_daq_monitor.serialize();
        status_client_ptr->WriteSendBuffer(to_telem_u16(TelemetryCodes::ORC_Hardware_Status), status);
    }
}

void JoinThread(std::thread &thread, quill::Logger *logger) {
    // We do not want things to hang here if the Readout goes bad
    QUILL_LOG_DEBUG(logger, "Joining thread...");
    if (thread.joinable()) {
        try {
            thread.join();
            QUILL_LOG_DEBUG(logger, "Thread joined.");
        } catch (const std::system_error& e) {
            QUILL_LOG_ERROR(logger, "Error joining thread: {}", e.what());
        }
    } else {
        QUILL_LOG_DEBUG(logger, "Thread was not joinable (likely never started or already finished).");
    }
}

bool WaitForThreadJoin(std::thread & thread, quill::Logger *logger) {
    // Join thread with increasing aggressiveness if thread does not join within 2s..
    // FIXME add try except
    auto fut = std::async(std::launch::async, [&] { JoinThread(thread, logger); });

    if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
        QUILL_LOG_WARNING(logger, "Thread not terminating, cancelling..");
        pthread_cancel(thread.native_handle());
        if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            QUILL_LOG_CRITICAL(logger, "Thread not terminating AFTER a cancel, forcing a SIGTERM..");
            pthread_kill(thread.native_handle(), SIGTERM);
            if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                QUILL_LOG_CRITICAL(logger, "Thread not terminating AFTER a SIGTERM, forcing a SIGKILL.");
                pthread_kill(thread.native_handle(), SIGKILL);
                if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                    QUILL_LOG_CRITICAL(logger, "If still running it is very bad.. :( ");
                }
            }
        }
    }
    return true;
}

void ShutdownComputer() {
    // Call systemd's shutdown command (graceful, syncs filesystems automatically)
    int ret = std::system("systemctl shutdown");
    if (ret != 0) {
        std::cerr << "Shutdown command failed with code: " << ret << "\n";
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::shutdown_computer);
    }
}

void RebootComputer() {
    // Call systemd's reboot command (graceful, syncs filesystems automatically)
    int ret = std::system("systemctl reboot");
    if (ret != 0) {
        std::cerr << "Reboot command failed with code: " << ret << "\n";
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::reboot_computer);
    }
}

void InitPcieDriver() {
    // Returns null if not found
    auto readout_basedir = std::string(std::getenv("TPC_DAQ_BASEDIR"));
    if (readout_basedir.data() == nullptr) {
        std::cerr << "Could not find env variable TPC_DAQ_BASEDIR " << std::endl;
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::init_pcie);
        return;
    }

    int ret = std::system((readout_basedir + "/scripts/setup_windriver.sh").c_str());
    if (ret != 0) {
        std::cerr << "PCIe Init failed with code: " << ret << "\n";
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::init_pcie);
    }
}

bool ControlService(const std::string& unit_name, const std::string& method, quill::Logger *logger) {
    sd_bus *bus = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = nullptr;

    // 1. Connect to the system bus
    int ret = sd_bus_default_system(&bus);
    if (ret < 0) {
        QUILL_LOG_ERROR(logger, "Failed to connect to systemd {}", strerror(-ret));
        return false;
    }

    // 2. Call the StartUnit or StopUnit method on systemd manager
    // Destination: org.freedesktop.systemd1
    // Path: /org/freedesktop/systemd1
    // Interface: org.freedesktop.systemd1.Manager
    // Method: StartUnit or StopUnit
    // Signature: "ss" (String: unit name, String: mode)
    ret = sd_bus_call_method(bus,
                           "org.freedesktop.systemd1",           // Service to contact
                           "/org/freedesktop/systemd1",          // Object path
                           "org.freedesktop.systemd1.Manager",   // Interface name
                           method.c_str(),                       // Method name
                           &error,                               // Error return object
                           &m,                                   // Return message
                           "ss",                                 // Input signature
                           unit_name.c_str(),                    // Arg 1: Unit Name
                           "replace");                           // Arg 2: Mode (replace existing jobs)

    if (ret < 0) {
        QUILL_LOG_ERROR(logger, "Failed to issue systemd command {}", error.message);
        sd_bus_error_free(&error);
        sd_bus_message_unref(m);
        sd_bus_unref(bus);
        return false;
    }

    // Cleanup
    sd_bus_error_free(&error);
    sd_bus_message_unref(m);
    sd_bus_unref(bus);
    return true;
}

void DAQHandler(std::unique_ptr<TCPConnection> &command_client_ptr, std::unique_ptr<TCPConnection> &status_client_ptr,
                quill::Logger *logger) {

    // DAQ process threads
    std::thread status_thread;

    while (g_running.load()) {
        QUILL_LOG_DEBUG(logger, "Waiting for command...");
        Command cmd = command_client_ptr->ReadRecvBuffer();
        QUILL_LOG_INFO(logger, "Received command [{}] num_args: [{}] \n", cmd.command, cmd.arguments.size());

        switch (cmd.command) {
            case to_u16(CommunicationCodes::ORC_Start_Computer_Status): {
                // Start thread to send periodic status
                if (!status_thread.joinable()) {
                    g_status_running.store(true);
                    status_thread = std::thread([&]() { SendStatus(status_client_ptr, logger); });
                }
                break;
            }
            case to_u16(CommunicationCodes::ORC_Boot_All_DAQ): {
                ControlService(kTpcDaq, kStartUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc);
                ControlService(kTofDaq, kStartUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tof);
                ControlService(kDataMonitor, kStartUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor);
                QUILL_LOG_INFO(logger, "Booted All DAQ...");
                break;
            }
            case to_u16(CommunicationCodes::ORC_Shutdown_All_DAQ): {
                ControlService(kTpcDaq, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc, true);
                ControlService(kDataMonitor, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor, true);
                ControlService(kDataMonitor, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor, true);
                QUILL_LOG_INFO(logger, "Shutdown All DAQ...");
                break;
            }
            case to_u16(CommunicationCodes::ORC_Boot_Tpc_Daq): {
                ControlService(kTpcDaq, kStartUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc);
                break;
            }
            case to_u16(CommunicationCodes::ORC_Shutdown_Tpc_Daq): { 
                ControlService(kTpcDaq, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc, true);
                break;
            }
            case to_u16(CommunicationCodes::ORC_Boot_Tof_Daq): { 
                ControlService(kTofDaq, kStartUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tof);
                QUILL_LOG_INFO(logger, "Booted TOF DAQ...");
                break;
            } case to_u16(CommunicationCodes::ORC_Shutdown_Tof_Daq): { 
                ControlService(kTofDaq, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tof, true);
                QUILL_LOG_INFO(logger, "Shutdown TOF DAQ...");
                break;
            }
            case to_u16(CommunicationCodes::ORC_Stop_Computer_Status): {
                // stop the status thread
                g_status_running.store(false);
                WaitForThreadJoin(status_thread, logger);
                break;
            } case to_u16(CommunicationCodes::ORC_Exec_CPU_Restart): {
                // Make sure all DAQ are shutdown first
                ControlService(kTpcDaq, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc, true);
                ControlService(kTofDaq, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tof, true);
                ControlService(kDataMonitor, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor, true);
                QUILL_LOG_INFO(logger, "Rebooting DAQ Computer!");
                RebootComputer();
                break;
            } case to_u16(CommunicationCodes::ORC_Exec_CPU_Shutdown): {
                // Make sure all DAQ are shutdown first
                ControlService(kTpcDaq, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc, true);
                ControlService(kTofDaq, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tof, true);
                ControlService(kDataMonitor, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor, true);
                QUILL_LOG_INFO(logger, "Shutting down DAQ Computer!");
                // ShutdownComputer();
                break;
            } case to_u16(CommunicationCodes::ORC_Init_PCIe_Driver): {
                QUILL_LOG_INFO(logger, "Initializing PCIe Driver!");
                InitPcieDriver();
                break;
            } case to_u16(CommunicationCodes::ORC_Boot_Monitor): {
                ControlService(kDataMonitor, kStartUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor);
                break;
            } case to_u16(CommunicationCodes::ORC_Shutdown_Monitor): {
                ControlService(kDataMonitor, kStopUnit, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor, true);
                break;
            }
            default: {
                QUILL_LOG_WARNING(logger, "Unknown command {}", cmd.command);
                break;
            }
        }
    }

    QUILL_LOG_INFO(logger, "Run Daemon stopped, shutting it all down!");
    g_status_running.store(false);
    ControlService(kTpcDaq, kStopUnit, logger);
    ControlService(kTofDaq, kStopUnit, logger);
    ControlService(kDataMonitor, kStopUnit, logger);
    WaitForThreadJoin(status_thread, logger);
}

void StartContext(std::vector<std::thread> &ctx_threads, quill::Logger *logger, asio::io_context &io_context, size_t num_ctx_threads) {
    for (size_t i = 0; i < num_ctx_threads; i++) {
        // Start IO context thread first
        ctx_threads.emplace_back( std::thread([&]() {
            QUILL_LOG_DEBUG(logger, "ASIO io_context thread started...");
            // Prevent io_context::run() from returning if there's initially no work
            auto work_guard = asio::make_work_guard(io_context);
            try {
                io_context.run(); // Blocks until io_context.stop() or work_guard is reset
            } catch (const std::exception& e) {
                QUILL_LOG_CRITICAL(logger, "Exception in io_context.run(): {}", e.what());
                // Signal shutdown if the IO context fails critically
                g_running.store(false);
                g_shutdown_cv.notify_one();
            } catch (...) {
                QUILL_LOG_CRITICAL(logger, "Unknown exception in io_context.run()");
                g_running.store(false);
                g_shutdown_cv.notify_one();
            }
            QUILL_LOG_DEBUG(logger, "ASIO io_context thread finished.");
        })
        );
    }
}

} // anonymous namespace

// --- Main Entry Point ---
int main() {
    // 1. Setup Signal Handlers Early
    pgrams::orchestrator::SetupSignalHandlers();

    // 2. Initialize Logging
    pgrams::orchestrator::SetupLogging(); // Configures Quill to log to stdout
    quill::Logger* logger = quill::Frontend::create_or_get_logger("readout_logger");

    QUILL_LOG_INFO(logger, "Orchestrator starting up...");
    QUILL_LOG_INFO(logger, "Orchestrator IP: {}, Cmd Port: [{}], Status Port: [{}]", kHubIp, kDaemonCommandPort, kDaemonStatusPort);

    // 3. Initialize ASIO and Controller
    asio::io_context io_context;
    size_t num_io_ctx_threads = 2; // one thread for each link (2 links/DAQ + Orchestrator)
    std::thread command_thread;
    std::unique_ptr<TCPConnection> command_client_ptr;
    std::unique_ptr<TCPConnection> status_client_ptr;

    // Here we start the IO context on a few threads. This just gives the context access to
    // these threads so it can handle asyn operations on multiple sockets at once. This is all
    // handled under the hood by ASIO
    std::vector<std::thread> io_ctx_threads;
    try {
        pgrams::orchestrator::StartContext(io_ctx_threads, logger, io_context, num_io_ctx_threads);
        QUILL_LOG_DEBUG(logger, "Starting control connection \n");
        command_client_ptr = std::make_unique<TCPConnection>(io_context, kHubIp, kDaemonCommandPort, false, true, false);
        status_client_ptr = std::make_unique<TCPConnection>(io_context, kHubIp, kDaemonStatusPort, false, false, true);
        command_thread = std::thread([&]() { pgrams::orchestrator::DAQHandler(command_client_ptr, status_client_ptr, logger); });

    } catch (const std::exception& e) {
        QUILL_LOG_CRITICAL(logger, "Exception during initialization phase: {}", e.what());
        pgrams::orchestrator::g_running.store(false); // Signal shutdown
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during initialization phase.");
        pgrams::orchestrator::g_running.store(false); // Signal shutdown
    }

    // 4. Main Wait Loop (only if initialization was okay)
    // Wait until g_running is false (due to signal or error)
    if (pgrams::orchestrator::g_running.load()) {
        QUILL_LOG_INFO(logger, "Service running. Waiting for termination signal...");
        std::unique_lock<std::mutex> lock(pgrams::orchestrator::g_shutdown_mutex);
        pgrams::orchestrator::g_shutdown_cv.wait(lock, [] { return !pgrams::orchestrator::g_running.load(); });
        // stop the blocking message receiver so the DAQ thread can terminate
        command_client_ptr->setStopCmdRead(true);
        status_client_ptr->setStopCmdRead(true);
        // When woken up, g_running is false
        QUILL_LOG_INFO(logger, "Shutdown signal received or error detected. Initiating shutdown sequence.");
    } else {
        // g_running was already false due to an initialization error exception
        QUILL_LOG_WARNING(logger, "Initialization error detected. Proceeding directly to cleanup.");
    }

    // 5. Shutdown Sequence ---
    QUILL_LOG_INFO(logger, "Starting graceful shutdown sequence...");

    // Stop the ASIO io_context. This will unblock the io_thread.
    // Safe to call multiple times.
    QUILL_LOG_INFO(logger, "Stopping ASIO io_contexts.");
    io_context.stop();

    // Join threads (wait for them to finish)
    QUILL_LOG_INFO(logger, "Joining DAQ thread...");
    if (command_thread.joinable()) {
        try {
            command_thread.join();
            QUILL_LOG_INFO(logger, "DAQ thread joined.");
        } catch (const std::system_error& e) {
            QUILL_LOG_ERROR(logger, "Error joining DAQ thread: {}", e.what());
        }
    } else {
     QUILL_LOG_INFO(logger, "DAQ thread was not joinable (likely never started or already finished).");
    }

    QUILL_LOG_INFO(logger, "Joining IO threads...");
    for (auto &io_thread : io_ctx_threads) {
        if (io_thread.joinable()) {
            try {
                io_thread.join();
                QUILL_LOG_INFO(logger, "IO thread joined.");
            } catch (const std::system_error& e) {
                QUILL_LOG_ERROR(logger, "Error joining IO thread: {}", e.what());
            }
        } else {
            QUILL_LOG_INFO(logger, "IO thread was not joinable (likely never started or already finished).");
        }
    }

    // Quill backend shutdown happens automatically at exit.
    QUILL_LOG_INFO(logger, "Shutdown sequence complete. Exiting...");

    return EXIT_SUCCESS;
}
