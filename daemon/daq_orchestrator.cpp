//
// Created by Jon Sensenig on 3/31/25.
//

#include "controller.h"
#include "tcp_protocol.h"
#include "daq_comp_monitor.h"
#include "CommunicationCodes.hh"
// #include "../ReadoutDataMonitor/src/common/data_monitor.h"
#include "data_monitor.h"

#include "FlightOps/GRAMS_TOF_DAQController.h"

#include <asio.hpp>
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
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

const char* kHubIp = "192.168.100.100";  // Hub Computer IP

const uint16_t kControllerCommandPort = 50003; // TPC Readout software port, for commands
const uint16_t kControllerStatusPort = 50002; // TPC Readout software port, for status

const uint16_t kDaemonCommandPort = 50001; // Daemon software port, for commands
const uint16_t kDaemonStatusPort = 50000; // Daemon software port, for status

const uint16_t kMonitorCommandPort = 50005; // Data Monitor software port, for commands
const uint16_t kMonitorStatusPort = 50004; // Data Monitor software port, for status

const uint16_t kTofCommandPort = 50007; // TOF software port, for commands
const uint16_t kTofStatusPort = 50006; // TOF software port, for status

const size_t NUM_DAQ = 3; // Number of DAQ processes

// --- Global Variables ---
namespace pgrams::orchestrator { // Use anonymous namespace for internal linkage

using namespace communication;
using TOF_ControllerPtr = std::unique_ptr<GRAMS_TOF_DAQController>;

std::atomic_bool g_running(true);
std::atomic_bool g_status_running(false);
std::condition_variable g_shutdown_cv;
std::mutex g_shutdown_mutex;

// Keep a global monitor instance
DaqCompMonitor g_daq_monitor{};

// --- Struct for DAQ processes ---
template <typename DAQ>
struct DAQProcess {
        std::unique_ptr<DAQ> daq_ptr{};
        std::thread daq_thread;
        std::function<void(DAQProcess &daq_process, quill::Logger *logger, asio::io_context &io_context)> run_function;
    };

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
        status_client_ptr->WriteSendBuffer(to_u16(CommunicationCodes::ORC_Hardware_Status), status);
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

void TpcRunController(DAQProcess<controller::Controller> &daq_process, quill::Logger *logger, asio::io_context &io_context) {

    try {
        QUILL_LOG_DEBUG(logger, "Starting controller initialization...");
        // FIXME add status io context
        daq_process.daq_ptr = std::make_unique<controller::Controller>(
            io_context, io_context, kHubIp,
            kControllerCommandPort, kControllerStatusPort, false, true);
        if (!daq_process.daq_ptr->Init()) {
            QUILL_LOG_CRITICAL(logger, "Failed to initialize controller. Service will shut down.");
        } else {
            QUILL_LOG_INFO(logger, "Controller initialized successfully.");
            try {
                daq_process.daq_ptr->Run(); // This loop should check g_running internally
            } catch (const std::exception& e) {
                QUILL_LOG_CRITICAL(logger, "Exception in Controller::Run(): {}", e.what());
            } catch (...) {
                QUILL_LOG_CRITICAL(logger, "Unknown exception in Controller::Run()");
            }
        }
    } catch (const std::exception& e) {
        QUILL_LOG_CRITICAL(logger, "Exception during Controller initialization phase: {}", e.what());
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::start_tpc_daq);
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during initialization phase.");
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::start_tpc_daq);
    }
    if (daq_process.daq_ptr) {
        QUILL_LOG_DEBUG(logger, "Resetting controller pointer");
        daq_process.daq_ptr.reset();
    }
}

void RunTpcMonitorController(DAQProcess<data_monitor::DataMonitor> &daq_process, quill::Logger *logger, asio::io_context &io_context) {

    try {
        QUILL_LOG_DEBUG(logger, "Starting data monitor initialization...");
        // FIXME add status io context
        daq_process.daq_ptr = std::make_unique<data_monitor::DataMonitor>(io_context, kHubIp,
                                                            kMonitorCommandPort, kMonitorStatusPort, false, true);

        try {
            daq_process.daq_ptr->SetRunning(true);
            daq_process.daq_ptr->Run();
        } catch (const std::exception& e) {
            QUILL_LOG_CRITICAL(logger, "Exception in DataMonitor::Run(): {}", e.what());
        } catch (...) {
            QUILL_LOG_CRITICAL(logger, "Unknown exception in DataMonitor::Run()");
        }
    } catch (const std::exception& e) {
        QUILL_LOG_CRITICAL(logger, "Exception during DataMonitor initialization phase: {}", e.what());
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::start_tpc_dm);
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during DataMonitor initialization phase.");
        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::start_tpc_dm);
    }
    if (daq_process.daq_ptr) {
        QUILL_LOG_DEBUG(logger, "Resetting DataMonitor pointer");
        daq_process.daq_ptr.reset();
    }
}

template<typename DAQ>
void StartDaqProcess(DAQProcess<DAQ> &daq_process, quill::Logger *logger, asio::io_context &io_context) {
    if (daq_process.daq_thread.joinable()) {
        LOG_WARNING(logger, "DAQ controller already running!");
        return;
    }
    QUILL_LOG_DEBUG(logger, "Starting Control thread");
    daq_process.daq_thread = std::thread([&]() { daq_process.run_function(daq_process, logger, io_context); });
}

template<typename DAQ>
void StopDaqProcess(DAQProcess<DAQ> &daq_process, quill::Logger *logger) {
    if (daq_process.daq_ptr) {
        daq_process.daq_ptr->SetRunning(false);
        // JoinThread(daq_process.daq_thread, logger);
        WaitForThreadJoin(daq_process.daq_thread, logger);
        daq_process.daq_ptr.reset(nullptr);
        QUILL_LOG_DEBUG(logger, "Stopped DAQ process..");
    } else {
        QUILL_LOG_WARNING(logger, "DAQ process not running!");
    }
}


void StartTofProcess(TOF_ControllerPtr &tof_ptr, std::thread &tof_thread, quill::Logger *logger) {
    if (tof_thread.joinable()) {
       LOG_WARNING(logger, "GRAMS TOF DAQ controller already running!");
       return;
    }
    QUILL_LOG_DEBUG(logger, "Starting GRAMS TOF DAQ Control thread");

    // --- 1. Define Configuration  ---
    GRAMS_TOF_DAQController::Config config;
    //config.noFpgaMode = false;         // Enable FPGA interaction
    config.noFpgaMode = true;         // Diable FPGA interaction 
    config.commandListenPort = kTofCommandPort;
    config.eventTargetPort = kTofStatusPort;
    config.remoteEventHub = kHubIp;
    config.configFile = "";

    if (config.configFile.empty()) {
        if (!GRAMS_TOF_Config::loadDefaultConfig()) {
            throw std::runtime_error("Configuration file not specified and default GLIB path failed to load.");
        } else config.configFile =  GRAMS_TOF_Config::instance().getConfigFilePath();
    }

    try {
        // --- 2. Create and Initialize the Controller ---
        tof_ptr = std::make_unique<GRAMS_TOF_DAQController>(config);

        if (!tof_ptr->initialize()) {
            LOG_ERROR(logger, "GRAMS TOF DAQ Controller initialization failed!");
            tof_ptr.reset(nullptr); // Clear the pointer on failure
            return;
        }

        // --- 3. Start the execution thread ---
        // The thread executes the GRAMS_TOF_DAQController::run() method.
        tof_thread = std::thread([&]() { 
            tof_ptr->run(); 
        });

        QUILL_LOG_INFO(logger, "GRAMS TOF DAQ Controller thread successfully started.");

    } catch (const std::exception& e) {
        LOG_ERROR(logger, "Failed to start GRAMS TOF DAQ Controller: {}", e.what());
        tof_ptr.reset(nullptr);
    }
}


void StopTofProcess(TOF_ControllerPtr &tof_ptr, std::thread &tof_thread, quill::Logger *logger) {
    if (tof_ptr) {
        // --- 1. Signal the Controller to Stop ---
        QUILL_LOG_DEBUG(logger, "Signaling GRAMS TOF DAQ process to stop...");
        tof_ptr->stop();

        // --- 2. Wait for the Thread to Exit ---
        WaitForThreadJoin(tof_thread, logger);

        // --- 3. Clean up the pointer ---
        tof_ptr.reset(nullptr);
        QUILL_LOG_DEBUG(logger, "Stopped GRAMS TOF DAQ process and cleaned up resources.");
    } else {
        QUILL_LOG_WARNING(logger, "GRAMS TOF DAQ process not running!");
    }
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

void DAQHandler(std::unique_ptr<TCPConnection> &command_client_ptr, std::unique_ptr<TCPConnection> &status_client_ptr,
                asio::io_context &io_ctx, quill::Logger *logger) {

    // DAQ process threads
    std::vector<std::thread> daq_worker_threads;
    std::thread tof_thread;
    std::thread status_thread;

    // DAQ Processes
    /** TPC & SiPM Controller */
    DAQProcess<controller::Controller> tpc_controller_daq;
    tpc_controller_daq.run_function = TpcRunController;
    /** TPC & SiPM Data Monitor Controller */
    DAQProcess<data_monitor::DataMonitor> tpc_monitor_controller_daq;
    tpc_monitor_controller_daq.run_function = RunTpcMonitorController;
    /** FIXME add TOF DAQ Controller */
    TOF_ControllerPtr g_tof_ptr{};

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
            case to_u16(CommunicationCodes::ORC_Boot_All_DAQ): { // start DAQ processes TODO add monitor!
                StartDaqProcess(tpc_controller_daq, logger, io_ctx);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc);
                break;
            }
            case to_u16(CommunicationCodes::ORC_Shutdown_All_DAQ): { // stop DAQ processes TODO add monitor!
                StopDaqProcess(tpc_controller_daq, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc, true);
                break;
            }
            case to_u16(CommunicationCodes::ORC_Boot_Tof_Daq): { // FIXME add TOF
                StartTofProcess(g_tof_ptr, tof_thread, logger);
                QUILL_LOG_INFO(logger, "Booted TOF DAQ...");
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tof);
                break;
            } case to_u16(CommunicationCodes::ORC_Shutdown_Tof_Daq): { // FIXME add TOF
                StopTofProcess(g_tof_ptr, tof_thread, logger);
                QUILL_LOG_INFO(logger, "Shutdown TOF DAQ...");
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tof, true);
                break;
            }
            case to_u16(CommunicationCodes::ORC_Stop_Computer_Status): {
                // stop the status thread
                g_status_running.store(false);
                WaitForThreadJoin(status_thread, logger);
                break;
            } case to_u16(CommunicationCodes::ORC_Exec_CPU_Restart): {
                // FIXME add shutdown DAQ here
                QUILL_LOG_INFO(logger, "Rebooting DAQ Computer!");
                RebootComputer();
                break;
            } case to_u16(CommunicationCodes::ORC_Exec_CPU_Shutdown): {
                // FIXME add shutdown DAQ here
                QUILL_LOG_INFO(logger, "Shutting down DAQ Computer!");
                // ShutdownComputer();
                break;
            } case to_u16(CommunicationCodes::ORC_Init_PCIe_Driver): {
                QUILL_LOG_INFO(logger, "Initializing PCIe Driver!");
                InitPcieDriver();
                break;
            } case to_u16(CommunicationCodes::ORC_Boot_Monitor): {
                StartDaqProcess(tpc_monitor_controller_daq, logger, io_ctx);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor);
                break;
            } case to_u16(CommunicationCodes::ORC_Shutdown_Monitor): {
                StopDaqProcess(tpc_monitor_controller_daq, logger);
                g_daq_monitor.setDaqBitWord(DaqCompMonitor::tpc_monitor, true);
                break;
            }
            // TODO add case for booting _all_ DAQ (status msg shows if they are running)
            default: {
                QUILL_LOG_WARNING(logger, "Unknown command {}", cmd.command);
                break;
            }
        }
    }

    QUILL_LOG_INFO(logger, "Run Daemon stopped, shutting it all down!");
    g_status_running.store(false);
    StopDaqProcess(tpc_controller_daq, logger);
    StopDaqProcess(tpc_monitor_controller_daq, logger);
    // StopTofProcess(tof_ptr, tof_thread, logger); // FIXME add TOF
    WaitForThreadJoin(status_thread, logger);
}

} // anonymous namespace

// --- Main Entry Point ---
int main() {
    // 1. Setup Signal Handlers Early
    pgrams::orchestrator::SetupSignalHandlers();

    // 2. Initialize Logging
    pgrams::orchestrator::SetupLogging(); // Configures Quill to log to stdout
    quill::Logger* logger = quill::Frontend::create_or_get_logger("readout_logger");

    QUILL_LOG_INFO(logger, "Connection service starting up...");
    QUILL_LOG_INFO(logger, "Daemon IP: {}, Cmd Port: [{}], Status Port: [{}]", kHubIp, kDaemonCommandPort, kDaemonStatusPort);
    QUILL_LOG_INFO(logger, "Controller IP: {}, Cmd Port: [{}], Status Port: [{}]", kHubIp,
                                                                        kControllerCommandPort, kControllerStatusPort);
    QUILL_LOG_INFO(logger, "Data Monitor IP: {}, Cmd Port: [{}], Status Port: [{}]", kHubIp, kMonitorCommandPort, kMonitorStatusPort);

    // 3. Initialize ASIO and Controller
    asio::io_context io_context;
    size_t num_io_ctx_threads = NUM_DAQ * 2 + 2; // one thread for each link (2 links/DAQ + Orchestrator)
    std::thread daq_thread;
    std::unique_ptr<TCPConnection> command_client_ptr;
    std::unique_ptr<TCPConnection> status_client_ptr;

    // Here we start the IO context on a few threads. This just gives the context access to
    // these threads so it can handle asyn operations on multiple sockets at once. This is all
    // handled under the hood by ASIO
    std::vector<std::thread> io_ctx_threads;
    try {
        for (size_t i = 0; i < num_io_ctx_threads; i++) {
            // Start IO context thread first
            io_ctx_threads.emplace_back( std::thread([&]() {
                QUILL_LOG_DEBUG(logger, "ASIO io_context thread started...");
                // Prevent io_context::run() from returning if there's initially no work
                auto work_guard = asio::make_work_guard(io_context);
                try {
                    io_context.run(); // Blocks until io_context.stop() or work_guard is reset
                } catch (const std::exception& e) {
                    QUILL_LOG_CRITICAL(logger, "Exception in io_context.run(): {}", e.what());
                    // Signal shutdown if the IO context fails critically
                    pgrams::orchestrator::g_running.store(false);
                    pgrams::orchestrator::g_shutdown_cv.notify_one();
                } catch (...) {
                    QUILL_LOG_CRITICAL(logger, "Unknown exception in io_context.run()");
                    pgrams::orchestrator::g_running.store(false);
                    pgrams::orchestrator::g_shutdown_cv.notify_one();
                }
                QUILL_LOG_DEBUG(logger, "ASIO io_context thread finished.");
            })
            );
        }
        QUILL_LOG_DEBUG(logger, "Starting control connection \n");
        command_client_ptr = std::make_unique<TCPConnection>(io_context, kHubIp, kDaemonCommandPort, false, true, false);
        status_client_ptr = std::make_unique<TCPConnection>(io_context, kHubIp, kDaemonStatusPort, false, false, true);
        daq_thread = std::thread([&]() { pgrams::orchestrator::DAQHandler(command_client_ptr, status_client_ptr, io_context, logger); });

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
    // } else if (!g_successful_init.load()) {
    //     QUILL_LOG_WARNING(logger, "Initialization failed. Proceeding directly to cleanup.");
    //     // Ensure io_context stops if io_thread was started but controller init failed
    //     io_context.stop();
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
    if (daq_thread.joinable()) {
        try {
            daq_thread.join();
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
