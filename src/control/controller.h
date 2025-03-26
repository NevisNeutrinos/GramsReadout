    //
// Created by Jon Sensenig on 1/22/25.
//

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <iostream>
#include <string>
#include <thread>
#include "quill/Logger.h"
#include "json.hpp"
#include "../../networking/tcp_connection.h"
#include "../data/data_handler.h"
#include "light_fem.h"
#include "charge_fem.h"
#include "trigger_control.h"
#include "xmit_control.h"
#include "data_monitor.h"


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

        // State machine functions
        bool Configure();
        bool StartRun();
        bool StopRun();
        bool GetStatus();
        bool Reset();

        void ReceiveCommand();
        void SendAckCommand(bool success);
        bool LoadConfig(std::string &config_file);
        bool JoinDataThread();

        bool is_configured_;

        TCPConnection tcp_connection_;
        std::thread data_thread_;

        data_monitor::DataMonitor &metrics_;
        pcie_control::PcieControl *pcie_ctrl_;
        xmit_control::XmitControl *xmit_ctrl_;
        light_fem::LightFem *light_fem_;
        charge_fem::ChargeFem *charge_fem_;
        trig_ctrl::TriggerControl *trigger_ctrl_;
        data_handler::DataHandler *data_handler_;

        pcie_int::PCIeInterface *pcie_interface_;
        pcie_int::PcieBuffers *buffers_;

        std::atomic_bool is_running_;

        quill::Logger* logger_;
        State current_state_;

        json config_;

    };


} // controller

#endif //CONTROLLER_H
