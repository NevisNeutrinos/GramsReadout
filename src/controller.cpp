//
// Created by Jon Sensenig on 1/22/25.
//

#include "controller.h"

namespace controller {
    bool Controller::Init() {
        current_state_ = State::kIdle;

        // Set up PCIe Tx/Rx

        // If something fails
        //return false;

        return true;
    }

    // Handle user commands
    bool Controller::HandleCommand(const Transitions command) {
        if (command == Transitions::kConfigure && current_state_ == State::kIdle) {
            current_state_ = State::kConfigured;
            return current_state_ == State::kConfigured;

        } if (command == Transitions::kStart && current_state_ == State::kConfigured) {
            current_state_ = State::kRunning;
            return current_state_ == State::kRunning;

        } if (command == Transitions::kStop && current_state_ == State::kRunning) {
            current_state_ = State::kStopped;
            return current_state_ == State::kStopped;

        } if (command == Transitions::kReset && current_state_ == State::kStopped) {
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
