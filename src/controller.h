    //
// Created by Jon Sensenig on 1/22/25.
//

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <iostream>
#include <string>
#include "quill/Logger.h"
#include "../networking/tcp_connection.h"
#include "pcie_control.h"

namespace controller {

    // Enum for states
    enum class State : int {
        kIdle = 0,
        kConfigured = 1,
        kRunning = 2,
        kStopped = 3
      };

    // These are the codes which will be received/sent
    // from/to the HUB computer.
    enum class CommandCodes : uint16_t {
        kReset = 0,
        kConfigure = 1,
        kStartRun = 2,
        kStopRun = 3,
        kGetStatus = 4,
        kPrepareRestart = 5,
        kRestart = 6,
        kPrepareShutdown = 7,
        kShutdown = 8,
        kCmdSuccess = 1000,
        kCmdFailure = 2000,
        kInvalid = UINT16_MAX
    };

    // Explicitly handle the type conversion for equality check, for safety.
    inline bool operator==(const uint16_t lhs, const CommandCodes& rhs) {
        return lhs == static_cast<uint16_t>(rhs);
    }
    inline bool operator==(const CommandCodes& lhs, const uint16_t rhs) {
        return static_cast<uint16_t>(lhs) == rhs;
    }

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

        Controller(asio::io_context& io_context, const std::string& ip_address,
                   const short port, const bool is_server, const bool is_running);
        ~Controller();

        // Initialize class
        bool Init();

        void Run();

        // Control state machine
        bool HandleCommand(const Command& command);

        // Current state
        std::string GetStateName() const;

        bool GetRunning() const { return is_running_; };
        void SetRunning(bool run);

    private: // data members

        void ReceiveCommand();
        void SendAckCommand(bool success);

        TCPConnection tcp_connection_;
        pcie_ctrl::PCIeControl* pcie_controller_;

        std::atomic_bool is_running_;

        quill::Logger* logger_;
        State current_state_;

    };


} // controller

#endif //CONTROLLER_H
