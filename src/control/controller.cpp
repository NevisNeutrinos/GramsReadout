//
// Created by Jon Sensenig on 1/22/25.
//

#include "controller.h"
#include "quill/LogMacros.h"
#include "quill/Frontend.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/FileSink.h"
#include <fstream>

namespace controller {

    Controller::Controller(asio::io_context& io_context, const std::string& ip_address,
                           const short port, const bool is_server, const bool is_running) :
        tcp_connection_(io_context, ip_address, port, is_server),
        is_running_(is_running),
        is_configured_(false),
        metrics_(data_monitor::DataMonitor::GetInstance()) {

        current_state_ = State::kIdle;

        std::string config_file("../config/test.json");
        if (!LoadConfig(config_file)) {
            std::cerr << "Config load failed! \n";
        }

        const bool log_to_file = config_["controller"]["log_to_file"].get<bool>();
        if (log_to_file) {
            const std::string file_path = config_["controller"]["log_file_path"].get<std::string>();
            const std::string file_name = file_path + "/readout_log.log";
            quill::FileSinkConfig cfg;
            cfg.set_open_mode('w');
            cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);
            logger_ = quill::Frontend::create_or_get_logger("readout_logger",
                quill::Frontend::create_or_get_sink<quill::FileSink>(file_name, cfg));
        } else {
            logger_ = quill::Frontend::create_or_get_logger("readout_logger",
             quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id1"));
        }

        LOG_INFO(logger_, "Opened config file: {} \n", config_file);
        LOG_INFO(logger_, "Config dump: {} \n", config_.dump());

        const bool enable_metrics = config_["data_handler"]["enable_metrics"].get<bool>();
        metrics_.EnableMonitoring(enable_metrics);
        data_handler_ = new data_handler::DataHandler;

        pcie_ctrl_ = new pcie_control::PcieControl;
        xmit_ctrl_ = new xmit_control::XmitControl;
        light_fem_ = new light_fem::LightFem;
        charge_fem_ = new charge_fem::ChargeFem;
        trigger_ctrl_ = new trig_ctrl::TriggerControl;
        pcie_interface_ = new pcie_int::PCIeInterface;
        buffers_ = new pcie_int::PcieBuffers;

        LOG_INFO(logger_, "Initialized Controller \n");
    }

    Controller::~Controller() {
        LOG_INFO(logger_, "Destructing Controller \n");
        delete xmit_ctrl_;
        delete light_fem_;
        delete charge_fem_;
        delete trigger_ctrl_;
        delete data_handler_;
        delete pcie_ctrl_;
        delete pcie_interface_;
        delete buffers_;
        LOG_INFO(logger_, "Destructed all hardware \n");
        // End logging session and close file
        quill::Frontend::remove_logger(logger_);
    }

    bool Controller::LoadConfig(std::string &config_file) {
        std::ifstream f(config_file);
        if (!f.is_open()) {
            std::cerr << "Could not open config file " << config_file << std::endl;
            return false;
        }
        try {
            config_ = json::parse(f);
        } catch (json::parse_error& ex) {
            std::cerr << "Parse error at byte: " << ex.byte << std::endl;
        }
        std::cout << "Successfully opened config file.." << std::endl;
        std::cout << config_.dump() << std::endl;
        return true;
    }

    bool Controller::Init() {
        current_state_ = State::kIdle;
        return true;
    }

    bool Controller::Configure() {
        // Connect to the PCIe bus handle
        int device_id_0 = 5, device_id_1 = 4;
        if (!pcie_interface_->InitPCIeDevices(device_id_0, device_id_1)) {
            LOG_ERROR(logger_, "PCIe device initialization failed!");
            return false;
        }
        LOG_INFO(logger_, "PCIe devices initialized!");
        usleep(10000);

        LOG_INFO(logger_, "Initializing hardware...");
        pcie_ctrl_->Configure(config_, pcie_interface_, *buffers_);
        xmit_ctrl_->Configure(config_, pcie_interface_, *buffers_);
        light_fem_->Configure(config_, pcie_interface_, *buffers_);
        charge_fem_->Configure(config_, pcie_interface_, *buffers_);
        trigger_ctrl_->Configure(config_, pcie_interface_, *buffers_);
        data_handler_->Configure(config_);
        LOG_INFO(logger_, "Configured Hardware! \n");
        is_configured_ = true;
        return true;
    }

    bool Controller::StartRun() {
        LOG_INFO(logger_, "Starting Run...\n");

        data_handler_->SetRun(true);
        data_thread_ = std::thread(&data_handler::DataHandler::CollectData, data_handler_, pcie_interface_);
        return true;
    }

    bool Controller::JoinDataThread() {
        data_thread_.join();
        return true;
    }

    bool Controller::StopRun() {
        data_handler_->SetRun(false);
        data_thread_.join();
        LOG_INFO(logger_, "Collection thread stopped successfully...\n");
        return true;
    }

    bool Controller::GetStatus() {
        return true;
    }

    bool Controller::Reset() {
        return true;
    }

    void Controller::SetRunning(bool run) {
        is_running_ = run;
        tcp_connection_.setStopCmdRead(!run);
    }

    void Controller::Run() {
        LOG_INFO(logger_, "Listening for commands..\n");
        is_running_ = true;
        ReceiveCommand();
        LOG_INFO(logger_, "Stopping controller\n");
    }

    void Controller::ReceiveCommand() {
        while (is_running_) {
            Command cmd = tcp_connection_.ReadRecvBuffer();
            bool response = HandleCommand(cmd);
            SendAckCommand(response);
            LOG_INFO(logger_, " \n Current state: [{}] \n", GetStateName());
        }
    }

    void Controller::SendAckCommand(bool success) {
        auto acknowledge = success ? CommandCodes::kCmdSuccess : CommandCodes::kCmdFailure;
        Command cmd(static_cast<uint16_t>(acknowledge), 1);
        cmd.arguments.at(0) = static_cast<int>(current_state_);
        tcp_connection_.WriteSendBuffer(cmd);
    }

    // Handle user commands
    bool Controller::HandleCommand(const Command& command) {
        LOG_INFO(logger_, " \n Sending command: [{}] \n", command.command);
        metrics_.ControllerState(command.command);
        metrics_.LoadMetrics();
        if (command.command == CommandCodes::kConfigure && current_state_ == State::kIdle) {
            LOG_INFO(logger_, " \n State [Idle] \n");
            // command.arguments // which configuration to use
            current_state_ = State::kConfigured;
            if (!is_configured_) Configure();
            return current_state_ == State::kConfigured;

        } if (command.command == CommandCodes::kStartRun && current_state_ == State::kConfigured) {
            LOG_INFO(logger_, " \n State [Configure] \n ");
            current_state_ = State::kRunning;
            StartRun();
            return current_state_ == State::kRunning;

        } if (command.command == CommandCodes::kStopRun && current_state_ == State::kRunning) {
            LOG_INFO(logger_, " \n State [Running] \n");
            current_state_ = State::kStopped;
            StopRun();
            return current_state_ == State::kStopped;

        } if (command.command == CommandCodes::kReset && current_state_ == State::kStopped) {
            LOG_INFO(logger_, " \n State [Stopped] \n");
            is_configured_ = false;
            current_state_ = State::kIdle;
            return current_state_ == State::kIdle;
        }

        LOG_INFO(logger_, "Invalid command or transition from state: {}", GetStateName());
        metrics_.ControllerState(static_cast<int>(current_state_));
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
