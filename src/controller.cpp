//
// Created by Jon Sensenig on 1/22/25.
//

#include "controller.h"
#include "quill/LogMacros.h"
#include "quill/Frontend.h"
#include "quill/sinks/ConsoleSink.h"

namespace controller {

    Controller::Controller(asio::io_context& io_context, const std::string& ip_address,
                           const short port, const bool is_server, const bool is_running) :
        tcp_connection_(io_context, ip_address, port, is_server),
        is_running_(is_running) {
        // io_thread_ = new std::thread([&]() { io_context_.run(); });
        current_state_ = State::kIdle;
        logger_ = quill::Frontend::create_or_get_logger("root",
                 quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));
    }

    Controller::~Controller() {
        LOG_INFO(logger_, "Destructing controller");
    }


    bool Controller::Init() {
        current_state_ = State::kIdle;
        // Start listening to the TCP receiver for any
        // commands.

        // Set up PCIe Tx/Rx

        // If something fails
        //return false;

        return true;
    }

    void Controller::SetRunning(bool run) {
        is_running_ = run;
        tcp_connection_.setStopCmdRead(!run);
    }

    void Controller::Run() {
        is_running_ = true;
        ReceiveCommand();
        LOG_INFO(logger_, "Stopping controller");
    }

    void Controller::ReceiveCommand() {
        while (is_running_) {
            Command cmd = tcp_connection_.ReadRecvBuffer();
            HandleCommand(cmd);
            LOG_INFO(logger_, " \n Current state: [{}]", GetStateName());
        }
    }

    // Handle user commands
    bool Controller::HandleCommand(const Command& command) {
        LOG_INFO(logger_, " \n Sending command: [{}]", command.command);
        if (command.command == CommandCodes::kConfigure && current_state_ == State::kIdle) {
            // command.arguments // which configuration to use
            current_state_ = State::kConfigured;
            return current_state_ == State::kConfigured;

        } if (command.command == CommandCodes::kStartRun && current_state_ == State::kConfigured) {
            current_state_ = State::kRunning;
            return current_state_ == State::kRunning;

        } if (command.command == CommandCodes::kStopRun && current_state_ == State::kRunning) {
            current_state_ = State::kStopped;
            return current_state_ == State::kStopped;

        } if (command.command == CommandCodes::kReset && current_state_ == State::kStopped) {
            current_state_ = State::kIdle;
            return current_state_ == State::kIdle;
        }

        std::cout << "Invalid command or transition from state: " << GetStateName() << std::endl;

        return false;
    }

    // Get the current state as a string
    std::string Controller::GetStateName() const {
        switch (current_state_) {
            case State::kIdle:
                return "Idle";
            case State::kConfigured:
                return "Configure";
            case State::kRunning:
                return "Running";
            case State::kStopped:
                return "Stopped";
        }
        return "Unknown";
    }

} // controller
