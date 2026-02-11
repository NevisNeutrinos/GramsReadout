//
// Created by Jon Sensenig on 3/31/25.
//

#include "controller.h"
#include "tcp_protocol.h"
#include "CommunicationCodes.hh"

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

const char* kHubIp = "127.0.0.1";  // Hub Computer IP
const uint16_t kControllerCommandPort = 50003; // TPC Readout software port, for commands
const uint16_t kControllerStatusPort = 50002; // TPC Readout software port, for status

// --- Global Variables ---
namespace pgrams::tpcdaq { // Use anonymous namespace for internal linkage

std::atomic_bool g_running(true);
std::atomic_bool g_status_running(false);
std::condition_variable g_shutdown_cv;
std::mutex g_shutdown_mutex;

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

void TpcRunController(std::unique_ptr<controller::Controller> &daq, quill::Logger *logger, asio::io_context &io_context) {

    try {
        QUILL_LOG_DEBUG(logger, "Starting controller initialization...");
        // FIXME add status io context
        daq = std::make_unique<controller::Controller>(io_context, io_context, kHubIp,
                                                       kControllerCommandPort, kControllerStatusPort, false, true);
        if (!daq->Init()) {
            QUILL_LOG_CRITICAL(logger, "Failed to initialize controller. Service will shut down.");
        } else {
            QUILL_LOG_INFO(logger, "Controller initialized successfully.");
            try {
                daq->Run(); // This loop should check g_running internally
            } catch (const std::exception& e) {
                QUILL_LOG_CRITICAL(logger, "Exception in Controller::Run(): {}", e.what());
            } catch (...) {
                QUILL_LOG_CRITICAL(logger, "Unknown exception in Controller::Run()");
            }
        }
    } catch (const std::exception& e) {
        QUILL_LOG_CRITICAL(logger, "Exception during Controller initialization phase: {}", e.what());
//        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::start_tpc_daq);
        // FIXME return errors here
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during initialization phase.");
//        g_daq_monitor.setErrorBitWord(DaqCompMonitor::ErrorBits::start_tpc_daq);
    }
    if (daq) {
        QUILL_LOG_DEBUG(logger, "Resetting controller pointer");
        daq.reset();
    }
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
    pgrams::tpcdaq::SetupSignalHandlers();

    // 2. Initialize Logging
    pgrams::tpcdaq::SetupLogging(); // Configures Quill to log to stdout
    quill::Logger* logger = quill::Frontend::create_or_get_logger("readout_logger");

    QUILL_LOG_INFO(logger, "TPC DAQ service starting up...");
    QUILL_LOG_INFO(logger, "Server IP: {}, Cmd Port: [{}], Status Port: [{}]", kHubIp,
                                                                        kControllerCommandPort, kControllerStatusPort);

    // 3. Initialize ASIO and Controller
    asio::io_context io_context;
    size_t num_io_ctx_threads = 2; // one thread for each link
    std::unique_ptr<controller::Controller> tpc_daq{};
    std::thread daq_thread;

    // Here we start the IO context on a few threads. This just gives the context access to
    // these threads so it can handle asyn operations on multiple sockets at once. This is all
    // handled under the hood by ASIO
    std::vector<std::thread> io_ctx_threads;
    try {
        QUILL_LOG_DEBUG(logger, "Starting control connection \n");
        pgrams::tpcdaq::StartContext(io_ctx_threads, logger, io_context, num_io_ctx_threads);
        daq_thread = std::thread([&]() { pgrams::tpcdaq::TpcRunController(tpc_daq, logger, io_context); });
    } catch (const std::exception& e) {
        QUILL_LOG_CRITICAL(logger, "Exception during initialization phase: {}", e.what());
        pgrams::tpcdaq::g_running.store(false); // Signal shutdown
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during initialization phase.");
        pgrams::tpcdaq::g_running.store(false); // Signal shutdown
    }

    // 4. Main Wait Loop (only if initialization was okay)
    // Wait until g_running is false (due to signal or error)
    if (pgrams::tpcdaq::g_running.load()) {
        QUILL_LOG_INFO(logger, "TPC DAQ Service running. Waiting for termination signal...");
        std::unique_lock<std::mutex> lock(pgrams::tpcdaq::g_shutdown_mutex);
        pgrams::tpcdaq::g_shutdown_cv.wait(lock, [] { return !pgrams::tpcdaq::g_running.load(); });
        // stop the blocking message receiver so the DAQ thread can terminate
        if (tpc_daq) { tpc_daq->SetRunning(false); }
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

    QUILL_LOG_INFO(logger, "Joining IO threads...");
    for (auto &io_thread : io_ctx_threads) {
        QUILL_LOG_INFO(logger, "Joining IO thread");
        if (io_thread.joinable()) {
            QUILL_LOG_INFO(logger, "IO thread joinable");
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

    // The last thing, free the pointer
    if (tpc_daq) { tpc_daq.reset(nullptr); }

    // Quill backend shutdown happens automatically at exit.
    QUILL_LOG_INFO(logger, "Shutdown sequence complete. Exiting...");

    return EXIT_SUCCESS;
}
