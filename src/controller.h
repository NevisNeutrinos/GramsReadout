//
// Created by Jon Sensenig on 1/22/25.
//

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <iostream>
#include <string>

namespace controller {

    // Enum for states
    enum class State {
        kIdle = 0,
        kConfigured = 1,
        kRunning = 2,
        kStopped = 3
      };

    // Enum for states
    enum class Transitions {
        kReset = 0,
        kConfigure = 1,
        kStart = 2,
        kStop = 3
      };

    // Converts state enum to a string.
    inline std::string StateToString(const State state) {
        switch (state) {
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

    class Controller {

    public:

        Controller() : current_state_(State::kIdle) {};
        // ~Controller();

        // Initialize class
        bool Init();

        // Control state machine
        bool HandleCommand(Transitions command);

        // Current state
        std::string GetStateName() const;

    private: // data members

    State current_state_;


    };


} // controller

#endif //CONTROLLER_H
