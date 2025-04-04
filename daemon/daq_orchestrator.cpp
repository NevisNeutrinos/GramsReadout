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

// --- Configuration ---
// Best practice: Load these from a config file or environment variables
// For simplicity, using constants here. Consider systemd Environment= directive.

const char* kControllerIp = "127.0.0.1";
const uint16_t kControllerPort = 1738; //1735;
const uint16_t kDaemonPort = 1740; //1745;

// --- Global Variables ---
namespace { // Use anonymous namespace for internal linkage

std::atomic_bool g_running(true);
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
    struct sigaction sa{nullptr};
    // std::memset(&sa, 0, sizeof(sa));
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

void RunController(std::unique_ptr<controller::Controller> &controller_ptr, asio::io_context &io_context, quill::Logger *logger) {

    try {
        QUILL_LOG_INFO(logger, "Starting controller initialization...");
        controller_ptr = std::make_unique<controller::Controller>(
            io_context, kControllerIp, kControllerPort, false, g_running);
        if (!controller_ptr->Init()) {
            QUILL_LOG_CRITICAL(logger, "Failed to initialize controller. Service will shut down.");
        } else {
            QUILL_LOG_INFO(logger, "Controller initialized successfully.");
            try {
                controller_ptr->Run(); // This loop should check g_running internally
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
    if (controller_ptr) {
        QUILL_LOG_INFO(logger, "Resetting controller pointer");
        controller_ptr.reset();
    }
}

void JoinThread(std::thread &thread, quill::Logger *logger) {
    QUILL_LOG_INFO(logger, "Joining thread...");
    if (thread.joinable()) {
        try {
            thread.join();
            QUILL_LOG_INFO(logger, "Thread joined.");
        } catch (const std::system_error& e) {
            QUILL_LOG_ERROR(logger, "Error joining thread: {}", e.what());
        }
    } else {
        QUILL_LOG_INFO(logger, "Thread was not joinable (likely never started or already finished).");
    }
}

void DAQHandler(std::unique_ptr<TCPConnection> &client_ptr, asio::io_context &io_context, quill::Logger *logger) {

    // Readout DAQ Controller
    std::unique_ptr<controller::Controller> controller_ptr;
    std::thread ctrl_thread;
    std::thread io_thread;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard(io_context.get_executor());

    while (g_running.load()) {
        Command cmd = client_ptr->ReadRecvBuffer();
        LOG_INFO(logger, "Received command [{}] num_args: [{}] \n", cmd.command, cmd.arguments.size());

        switch (cmd.command) {
            case 1: { // start readout DAQ
                if (!ctrl_thread.joinable()) {
                    io_context.restart();
                    LOG_INFO(logger, "Starting Control thread");
                    ctrl_thread = std::thread([&]() { RunController(controller_ptr, io_context, logger); });
                    io_thread = std::thread([&]() { io_context.run(); });
                } else {
                    LOG_WARNING(logger, "Readout controller already running!");
                }
                break;
            }
            case 2: { // stop readout DAQ
                if (controller_ptr) {
                    controller_ptr->SetRunning(false);
                    io_context.stop();
                    JoinThread(ctrl_thread, logger);
                    JoinThread(io_thread, logger);
                    controller_ptr.reset();
                } else {
                    LOG_WARNING(logger, "Readout controller not running!");
                }
                break;
            }
            default: {
                LOG_WARNING(logger, "Unknown command {}", cmd.command);
                break;
            }
        }
    }

    LOG_INFO(logger, "Run Daemon stopped, shutting it all down!");
    if (controller_ptr) {
        controller_ptr->SetRunning(false);
        JoinThread(ctrl_thread, logger);
    }
}

} // anonymous namespace

// --- Main Entry Point ---
int main(int argc, char* argv[]) {
    // 1. Setup Signal Handlers Early
    SetupSignalHandlers();

    // 2. Initialize Logging
    SetupLogging(); // Configures Quill to log to stdout
    quill::Logger* logger = quill::Frontend::create_or_get_logger("readout_logger");

    QUILL_LOG_INFO(logger, "Controller service starting up...");
    QUILL_LOG_INFO(logger, "Target Controller IP: {}, Port: {}", kControllerIp, kControllerPort);

    // 3. Initialize ASIO and Controller
    asio::io_context io1, io2;
    std::thread io_thread;
    std::thread daq_thread;
    std::unique_ptr<TCPConnection> client_ptr;

    try {
        // Start IO context thread first
        io_thread = std::thread([&]() {
            QUILL_LOG_INFO(logger, "ASIO io_context thread started...");
            // Prevent io_context::run() from returning if there's initially no work
            auto work_guard = asio::make_work_guard(io1);
            try {
                io1.run(); // Blocks until io_context.stop() or work_guard is reset
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
            QUILL_LOG_INFO(logger, "ASIO io_context thread finished.");
        });

        LOG_INFO(logger, "Starting control connection \n");
        client_ptr = std::make_unique<TCPConnection>(io1, "127.0.0.1", kDaemonPort, false);
        daq_thread = std::thread([&]() { DAQHandler(client_ptr, io1, logger); });

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
        client_ptr->setStopCmdRead(true); // stop the blocking message receiver so the DAQ thread can terminate
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
    QUILL_LOG_INFO(logger, "Stopping ASIO io_context.");
    io1.stop();

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

    QUILL_LOG_INFO(logger, "Joining IO thread...");
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

    // Quill backend shutdown happens automatically at exit.
    QUILL_LOG_INFO(logger, "Shutdown sequence complete. Exiting.");

    return EXIT_SUCCESS;
}
