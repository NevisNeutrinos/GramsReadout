//
// Created by Jon Sensenig on 3/31/25.
//

#include "controller.h"
#include "tcp_protocol.h"

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

// --- Configuration ---
// Best practice: Load these from a config file or environment variables
// For simplicity, using constants here. Consider systemd Environment= directive.

//const char* kControllerIp = "192.168.1.100";  // Readout software IP
const char* kControllerIp = "127.0.0.1";  // Readout software IP
const uint16_t kControllerCommandPort = 50003; // Readout software port, for commands
const uint16_t kControllerStatusPort = 50002; // Readout software port, for status

const char* kDaemonIp = "127.0.0.1";  // Daemon software IP
const uint16_t kDaemonCommandPort = 50001; // Daemon software port, for commands
const uint16_t kDaemonStatusPort = 50000; // Daemon software port, for status

const size_t NUM_DAQ = 3; // Number of DAQ processes

// --- Global Variables ---
namespace { // Use anonymous namespace for internal linkage

std::atomic_bool g_running(true);
std::atomic_bool g_status_running(false);
std::condition_variable g_shutdown_cv;
std::mutex g_shutdown_mutex;

// --- Struct for DAQ processes ---
template <typename DAQ>
struct DAQProcess {
        std::unique_ptr<DAQ> daq_ptr;
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

int32_t GetMemoryStats(quill::Logger *logger) {
    int32_t memory_usage_percent = 0;
    size_t mem_entries;
    sg_mem_stats *mem_stats = sg_get_mem_stats(&mem_entries);
    if (!mem_stats) {
        QUILL_LOG_ERROR(logger, "Error getting memory stats: {}", strerror(errno));
        return memory_usage_percent;
    }
    try {
        if (mem_entries > 0) {
            const size_t total_memory_bytes = mem_stats->total;
            const size_t free_memory_bytes = mem_stats->free;
            const size_t used_memory_bytes = total_memory_bytes - free_memory_bytes;
            const double memory_usage = (total_memory_bytes > 0) ?
                        (100.0 * static_cast<double>(used_memory_bytes) / total_memory_bytes) : 0.0;
            memory_usage_percent = static_cast<int32_t>(memory_usage);
        } else {
            QUILL_LOG_WARNING(logger, "No memory stats available!");
        }
    } catch (std::exception& e) {
        QUILL_LOG_ERROR(logger, "Error getting memory stats: {}", e.what());
    }
    return memory_usage_percent;
}

int32_t GetCpuStats(quill::Logger *logger) {
    int32_t cpu_usage_percent = 0;
    size_t cpu_entries1;
    QUILL_LOG_DEBUG(logger, "Getting CPU Stat 1");
    sg_cpu_stats *cpu_stats = sg_get_cpu_stats(&cpu_entries1);
    if (!cpu_stats) {
        QUILL_LOG_ERROR(logger, "Error getting CPU stats: {}", strerror(errno));
        return cpu_usage_percent;
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
        return cpu_usage_percent;
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
            cpu_usage_percent = static_cast<int32_t>(cpu_usage);
        } else {
            QUILL_LOG_WARNING(logger, "No memory stats available!");
        }
    } catch (std::exception& e) {
        QUILL_LOG_ERROR(logger, "Error getting memory stats: {}", e.what());
    }
    return cpu_usage_percent;
}

std::vector<int32_t> GetComputerStatus(quill::Logger *logger) {
    // Define the scaling factor for load averages if not readily available
    // Usually it's (1 << SI_LOAD_SHIFT), and SI_LOAD_SHIFT is typically 16
    // constexpr double LOAD_SCALE = 65536.0; // 2^16
    constexpr unsigned long long GB_divisor = 1024 * 1024 * 1024;
    std::vector<int32_t> status_vec(4, 0);

    try {
        struct statfs data_disk_info{};
        if (statfs("/home/pgrams/data", &data_disk_info) == 0) {
            const auto free_space = static_cast<unsigned long>(data_disk_info.f_bavail * data_disk_info.f_frsize);
            status_vec.at(0) = free_space / GB_divisor;
        } else {
            QUILL_LOG_ERROR(logger, "Failed to get data disk space with error {}", strerror(errno));
        }
        struct statfs main_disk_info{};
        if (statfs("/", &main_disk_info) == 0) {
            const auto free_space = static_cast<unsigned long>(main_disk_info.f_bavail * main_disk_info.f_frsize);
            status_vec.at(1) = free_space / GB_divisor;
        } else {
            QUILL_LOG_ERROR(logger, "Failed to get main disk space with error {}", strerror(errno));
        }

       // Initialize libstatgrab
        if (!sg_init(0)) {
            // The 1s load average across 12 CPUs
            status_vec.at(2) = GetCpuStats(logger);
            // RAM usage
            status_vec.at(3) = GetMemoryStats(logger);
            sg_shutdown();
        } else {
            QUILL_LOG_ERROR(logger, "Failed to Init libstatgrab with error code {}", strerror(errno));
        }
    } catch (const std::exception& e) {
        QUILL_LOG_ERROR(logger, "Failed to get disk space with exception: {}", e.what());
    }
    return status_vec;
}

// Not working yet
// void KillThread(std::thread &thread, quill::Logger *logger) {
//     // WARNING: This is very dangerous way to deal with threads, only using as a very
//     // last resort.
//     if (thread.joinable()) {
//         QUILL_LOG_CRITICAL(logger, "Thread not terminating, forcing a SIGTERM.");
//         pthread_kill(thread.native_handle(), SIGTERM);
//     }
//     if (thread.joinable()) {
//         QUILL_LOG_CRITICAL(logger, "Thread not terminating AFTER a SIGTERM, forcing a SIGKILL.");
//         pthread_kill(thread.native_handle(), SIGKILL);
//     }
// }

void JoinThread(std::thread &thread, quill::Logger *logger) {
    // FIXME add way to force thread to end if it is still running
    // We do not want things to hang here if the Readout goes bad
    QUILL_LOG_DEBUG(logger, "Joining thread...");
    if (thread.joinable()) {
        try {
            thread.join();
            // my_thread.join_for(timeout);
            // auto end_time = std::chrono::steady_clock::now();
            // auto elapsed_time = end_time - start_time;
            QUILL_LOG_DEBUG(logger, "Thread joined.");
        } catch (const std::system_error& e) {
            QUILL_LOG_ERROR(logger, "Error joining thread: {}", e.what());
        }
    } else {
        QUILL_LOG_DEBUG(logger, "Thread was not joinable (likely never started or already finished).");
    }
}

void TpcRunController(DAQProcess<controller::Controller> &daq_process, quill::Logger *logger, asio::io_context &io_context) {

    try {
        QUILL_LOG_DEBUG(logger, "Starting controller initialization...");
        // FIXME add status io context
        daq_process.daq_ptr = std::make_unique<controller::Controller>(
            io_context, io_context, kControllerIp,
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
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during initialization phase.");
    }
    if (daq_process.daq_ptr) {
        QUILL_LOG_DEBUG(logger, "Resetting controller pointer");
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
        JoinThread(daq_process.daq_thread, logger);
        daq_process.daq_ptr.reset(nullptr);
        QUILL_LOG_DEBUG(logger, "Stopped DAQ process..");
    } else {
        LOG_WARNING(logger, "DAQ process not running!");
    }
}

void SendStatus(std::unique_ptr<TCPConnection> &status_client_ptr, quill::Logger *logger) {
    while (g_status_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::vector<int32_t> status = GetComputerStatus(logger); // returns vector of 0's on failure
        status_client_ptr->WriteSendBuffer(0x20, status);
    }
}

void DAQHandler(std::unique_ptr<TCPConnection> &command_client_ptr, std::unique_ptr<TCPConnection> &status_client_ptr,
                asio::io_context &io_ctx, quill::Logger *logger) {

    // DAQ process threads
    std::vector<std::thread> daq_worker_threads;
    std::thread status_thread;

    // DAQ Processes
    /** TPC & SiPM Controller */
    DAQProcess<controller::Controller> tpc_controller_daq;
    tpc_controller_daq.run_function = TpcRunController;
    /** TPC & SiPM Data Monitor Controller */
    // DAQProcess<controller::Controller> tpc_monitor_controller_daq;
    // tpc_monitor_controller_daq.use_io_ctx = true;
    // tpc_monitor_controller_daq.run_function = RunTpcMonitorController;

    while (g_running.load()) {
        QUILL_LOG_DEBUG(logger, "Waiting for command...");
        Command cmd = command_client_ptr->ReadRecvBuffer();
        QUILL_LOG_INFO(logger, "Received command [{}] num_args: [{}] \n", cmd.command, cmd.arguments.size());

        switch (cmd.command) {
            case 0: {
                // std::vector<int32_t> status = GetComputerStatus(logger);
                // status_client_ptr->WriteSendBuffer(0x0, status);
                // Start thread to send periodic status
                if (!status_thread.joinable()) {
                    g_status_running.store(true);
                    status_thread = std::thread([&]() { SendStatus(status_client_ptr, logger); });
                }
                break;
            }
            case 1: { // start DAQ process
                StartDaqProcess(tpc_controller_daq, logger, io_ctx);
                break;
            }
            case 2: { // stop DAQ process
                StopDaqProcess(tpc_controller_daq, logger);
                break;
            }
            case 3: { // stop the status thread
                g_status_running.store(false);
                JoinThread(status_thread, logger);
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
    JoinThread(status_thread, logger);
}

} // anonymous namespace

// --- Main Entry Point ---
int main() {
    // 1. Setup Signal Handlers Early
    SetupSignalHandlers();

    // 2. Initialize Logging
    SetupLogging(); // Configures Quill to log to stdout
    quill::Logger* logger = quill::Frontend::create_or_get_logger("readout_logger");

    QUILL_LOG_INFO(logger, "Connection service starting up...");
    QUILL_LOG_INFO(logger, "Daemon IP: {}, Cmd Port: [{}], Status Port: [{}]", kDaemonIp, kDaemonCommandPort, kDaemonStatusPort);
    QUILL_LOG_INFO(logger, "Controller IP: {}, Cmd Port: [{}], Status Port: [{}]", kControllerIp,
                                                                        kControllerCommandPort, kControllerStatusPort);

    // 3. Initialize ASIO and Controller
    asio::io_context io_context;
    size_t num_io_ctx_threads = 3;
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
        QUILL_LOG_DEBUG(logger, "Starting control connection \n");
        command_client_ptr = std::make_unique<TCPConnection>(io_context, kDaemonIp, kDaemonCommandPort, false, true, false);
        status_client_ptr = std::make_unique<TCPConnection>(io_context, kDaemonIp, kDaemonStatusPort, false, false, true);
        daq_thread = std::thread([&]() { DAQHandler(command_client_ptr, status_client_ptr, io_context, logger); });

    } catch (const std::exception& e) {
        QUILL_LOG_CRITICAL(logger, "Exception during initialization phase: {}", e.what());
        g_running.store(false); // Signal shutdown
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during initialization phase.");
        g_running.store(false); // Signal shutdown
    }

    // 4. Main Wait Loop (only if initialization was okay)
    // Wait until g_running is false (due to signal or error)
    if (g_running.load()) {
        QUILL_LOG_INFO(logger, "Service running. Waiting for termination signal...");
        std::unique_lock<std::mutex> lock(g_shutdown_mutex);
        g_shutdown_cv.wait(lock, [] { return !g_running.load(); });
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
