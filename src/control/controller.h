    //
// Created by Jon Sensenig on 1/22/25.
//

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <iostream>
#include <string>
#include <thread>
#include <zmq.hpp>
#include "quill/Logger.h"
#include "json.hpp"
#include "../../networking/tcp_connection.h"
#include "../data/data_handler.h"
#include "light_fem.h"
#include "charge_fem.h"
#include "trigger_control.h"
#include "xmit_control.h"
#include "../status/status.h"
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
        kReset = 0x0,
        kConfigure = 0x1,
        kStartRun = 0x2,
        kStopRun = 0x3,
        kGetStatus = 0x4,
        kPrepareRestart = 0x5,
        kRestart = 0x6,
        kPrepareShutdown = 0x7,
        kShutdown = 0x8,
        kReadStatus = 0x9,
        kStatusPacket = 0x27,
        kCmdSuccess = 0x1000,
        kCmdFailure = 0x2000,
        kHeartBeat = 0xFFFF,
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

        Controller(asio::io_context& io_context, asio::io_context& status_io_context, const std::string& ip_address,
                    uint16_t command_port, uint16_t status_port, bool is_server, bool is_running);
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

        Controller(const Controller&) = delete;
        Controller& operator=(const Controller&) = delete;

        // State machine functions
        void TestRead(const std::vector<int32_t>& args);
        bool Configure(const std::vector<int32_t>& args);
        bool StartRun();
        bool StopRun();
        bool GetStatus();
        bool Reset();
        void StatusControl();
        void ReadStatus();

        void ReceiveCommand();
        void SendAckCommand(bool success);
        json LoadConfig(const std::string &config_file);
        bool PersistRunId();
        bool JoinDataThread();

        bool is_configured_;
        bool print_status_;
        bool enable_monitoring_;
        uint32_t run_id_;

        // TCPConnection tcp_connection_;
        TCPConnection command_client_;
        TCPConnection status_client_;
        std::thread data_thread_;
        std::thread status_thread_;
        std::vector<int> board_slots_{};

        std::unique_ptr<zmq::context_t> context_;
        std::unique_ptr<zmq::socket_t> socket_;
        void StartMonitoring();
        void ShutdownMonitoring();
        template<typename T>
        void SendMetrics(T &data, size_t size);
        void  GetEnvVariables();

        // data_monitor::DataMonitor &metrics_;
        // std::shared_ptr<data_monitor::DataMonitor> metrics_;

        // Environment variables for base directory locations
        std::string readout_basedir_;
        std::string data_basedir_;

        std::unique_ptr<pcie_control::PcieControl> pcie_ctrl_;
        std::unique_ptr<xmit_control::XmitControl> xmit_ctrl_;
        std::unique_ptr<light_fem::LightFem> light_fem_;
        std::unique_ptr<charge_fem::ChargeFem> charge_fem_;
        std::unique_ptr<trig_ctrl::TriggerControl> trigger_ctrl_;
        std::unique_ptr<data_handler::DataHandler> data_handler_;
        std::unique_ptr<pcie_int::PCIeInterface> pcie_interface_;
        std::unique_ptr<pcie_int::PcieBuffers> buffers_;
        std::unique_ptr<status::Status> status_;

        std::atomic_bool is_running_;
        std::atomic_bool run_status_;

        quill::Logger* logger_;
        State current_state_;

        json setup_config_;
        json config_;

    };


} // controller

#endif //CONTROLLER_H
