#include <iostream>
#include <ctime>
#include "src/control/controller.h"
#include "networking/tcp_protocol.h"
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/FileSink.h"


// Gets user input safely.
int GetUserInput() {
    int choice;
    std::cin >> choice;

    // Handle invalid input (non-numeric)
    if (std::cin.fail()) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid input. Please enter a number.\n";
        return GetUserInput();  // Retry
    }

    return choice;
}

// Prints the current state and available options.
void PrintState(const std::string& state) {
    std::cout << "\nCurrent state: " << state << "\n";
    std::cout << "Select a command:\n";
    std::cout << "  [0] Reset\n";
    std::cout << "  [1] Configure\n";
    std::cout << "  [2] Start\n";
    std::cout << "  [3] Stop\n";
    std::cout << "  [39] Status\n";
    std::cout << "  [-1] Exit\n";
    std::cout << "Enter choice: ";
}

// Runs the command-line interface for the state machine.
void Run(controller::Controller& ctrl) {

    quill::Logger* logger = quill::Frontend::create_or_get_logger("readout_logger");

    Command cmd(0,0);
    while (true) {
        PrintState(ctrl.GetStateName());
        int input = GetUserInput();
        if (input == -1) {
            std::cout << "Exiting...\n";
            break;
        }

        cmd.command = static_cast<uint16_t>(input);
        cmd.arguments = {1, 1};
        if (ctrl.HandleCommand(cmd)) {
            std::cout << "State changed to: " << ctrl.GetStateName() << "\n";
            LOG_INFO(logger, "State changed to {}! \n", std::string_view{ctrl.GetStateName()});
        } else {
            LOG_INFO(logger, "Invalid state transition! \n");
        }
    }
}

int main() {

    // TODO
    // 100us uses very little CPU when not actively logging, better than the default 0.5us
    // Check if there are any log messages dropped but this is unlikely as each
    // frontend buffers up to 2^16 messages which should be more than enough unless
    // the logging system is being abused.
    quill::BackendOptions backend_options;
    backend_options.sleep_duration = std::chrono::microseconds{100}; //100us
    quill::Backend::start(backend_options);

    asio::io_context io_context;
    asio::io_context status_io_context;

    bool run = true;
    std::string ip_addr = "192.168.1.100";
    // std::string ip_addr = "127.0.0.1";
    std::cout << "Starting controller..." << std::endl;
    controller::Controller controller(io_context, status_io_context, ip_addr, 50003, 50002, false, run);
    // controller::Controller controller(io_context, "127.0.0.1", 12345, true, run);
    std::cout << "Starting Command IO thread..." << std::endl;
    std::thread io_thread([&]() { io_context.run(); });
    std::cout << "Starting Status IO thread..." << std::endl;
    std::thread status_io_thread([&]() { status_io_context.run(); });

    if (!controller.Init()) {
        std::cerr << "Failed to initialize controller. Shutting down...\n";
        controller.SetRunning(false);
        io_context.stop();
        status_io_context.stop();
        io_thread.join();
        status_io_thread.join();
        return 1;
    }
    std::thread ctrl_thread([&]() { controller.Run(); });

    Run(controller);
    controller.SetRunning(false);
    std::cout << "Controller Run stopped!\n";

    io_context.stop();
    status_io_context.stop();
    ctrl_thread.join();
    io_thread.join();
    status_io_thread.join();

    return 0;
}
