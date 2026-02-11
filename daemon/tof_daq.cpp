//
// Created by Jon Sensenig on 3/31/25.
//
#include "controller.h"
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

const char* kHubIp = "127.0.0.1";  // Hub Computer IP

const uint16_t kTofCommandPort = 50007; // TOF software port, for commands
const uint16_t kTofStatusPort = 50006; // TOF software port, for status

// --- Global Variables ---
namespace pgrams::tofdaq { // Use anonymous namespace for internal linkage

using namespace communication;
using TOF_ControllerPtr = std::unique_ptr<GRAMS_TOF_DAQController>;

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
    config.remoteCommandHub = kHubIp;
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


} // anonymous namespace

// --- Main Entry Point ---
int main() {
    // 1. Setup Signal Handlers Early
    pgrams::tofdaq::SetupSignalHandlers();

    // 2. Initialize Logging
    pgrams::tofdaq::SetupLogging(); // Configures Quill to log to stdout
    quill::Logger* logger = quill::Frontend::create_or_get_logger("readout_logger");

    QUILL_LOG_INFO(logger, "TOF DAQ service starting up...");
    QUILL_LOG_INFO(logger, "Server IP: {}, Cmd Port: [{}], Status Port: [{}]", kHubIp, kTofCommandPort, kTofStatusPort);


    // 3. Initialize ASIO and Controller
    std::thread daq_thread;
    std::unique_ptr<GRAMS_TOF_DAQController> tof_ptr;

    try {
        QUILL_LOG_DEBUG(logger, "Starting control connection \n");
        pgrams::tofdaq::StartTofProcess(tof_ptr, daq_thread, logger);
    } catch (const std::exception& e) {
        QUILL_LOG_CRITICAL(logger, "Exception during initialization phase: {}", e.what());
        pgrams::tofdaq::g_running.store(false); // Signal shutdown
    } catch (...) {
        QUILL_LOG_CRITICAL(logger, "Unknown exception during initialization phase.");
        pgrams::tofdaq::g_running.store(false); // Signal shutdown
    }

    // 4. Main Wait Loop (only if initialization was okay)
    // Wait until g_running is false (due to signal or error)
    if (pgrams::tofdaq::g_running.load()) {
        QUILL_LOG_INFO(logger, "Service running. Waiting for termination signal...");
        std::unique_lock<std::mutex> lock(pgrams::tofdaq::g_shutdown_mutex);
        pgrams::tofdaq::g_shutdown_cv.wait(lock, [] { return !pgrams::tofdaq::g_running.load(); });
        // When woken up, g_running is false
        QUILL_LOG_INFO(logger, "Shutdown signal received or error detected. Initiating shutdown sequence.");
    } else {
        // g_running was already false due to an initialization error exception
        QUILL_LOG_WARNING(logger, "Initialization error detected. Proceeding directly to cleanup.");
    }

    // 5. Shutdown Sequence ---
    QUILL_LOG_INFO(logger, "Starting graceful shutdown sequence...");

    pgrams::tofdaq::StopTofProcess(tof_ptr, daq_thread, logger);

    // Quill backend shutdown happens automatically at exit.
    QUILL_LOG_INFO(logger, "Shutdown sequence complete. Exiting...");

    return EXIT_SUCCESS;
}
