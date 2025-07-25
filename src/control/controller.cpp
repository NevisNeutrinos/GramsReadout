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

    Controller::Controller(asio::io_context& io_context, asio::io_context& status_io_context, const std::string& ip_address,
        const uint16_t command_port, const uint16_t status_port, const bool is_server, const bool is_running) :
        command_client_(io_context, ip_address, command_port, is_server, true),
        status_client_(status_io_context, ip_address, status_port, is_server, false),
        is_running_(is_running),
        is_configured_(false),
        enable_monitoring_(false) {
        // metrics_(data_monitor::DataMonitor::GetInstance()) {

        current_state_ = State::kIdle;

        std::string config_file("/home/pgrams/GramsReadout/config/test_1.json");
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
        enable_monitoring_ = config_["controller"]["enable_monitoring"].get<bool>();
        // metrics_->EnableMonitoring(enable_metrics);

        data_handler_ = std::make_unique<data_handler::DataHandler>();
        pcie_ctrl_ = std::make_unique<pcie_control::PcieControl>();
        xmit_ctrl_ = std::make_unique<xmit_control::XmitControl>();
        light_fem_ = std::make_unique<light_fem::LightFem>();
        charge_fem_ = std::make_unique<charge_fem::ChargeFem>();
        trigger_ctrl_ = std::make_unique<trig_ctrl::TriggerControl>();
        status_ = std::make_unique<status::Status>();
        pcie_interface_ = std::make_unique<pcie_int::PCIeInterface>();
        buffers_ = std::make_unique<pcie_int::PcieBuffers>();

        LOG_INFO(logger_, "Initialized Controller \n");
        if (enable_monitoring_) {
            LOG_INFO(logger_, "Starting Monitoring \n");
            StartMonitoring();
        }
    }

    Controller::~Controller() {
        LOG_INFO(logger_, "Destructing Controller \n");
        // data_monitor::DataMonitor::ResetInstance();
        run_status_.store(false);
        if (status_thread_.joinable()) status_thread_.join();

        // Manually release the memory to make sure it's released
        // in the correct order.
        xmit_ctrl_.reset();
        light_fem_.reset();
        charge_fem_.reset();
        trigger_ctrl_.reset();
        status_.reset();
        data_handler_.reset();
        pcie_ctrl_.reset();
        pcie_interface_.reset();
        buffers_.reset();

        if (enable_monitoring_) {
            LOG_INFO(logger_, "Starting Monitoring \n");
            ShutdownMonitoring();
        }

        LOG_INFO(logger_, "Destructed all hardware \n");
        // End logging session and close file
        // quill::Frontend::remove_logger(logger_);
    }

    void Controller::ShutdownMonitoring() {
        std::cout << "Destructing metrics.. " << std::endl;
        constexpr int linger_value = 0;
        // Discard any lingering messages so we can close the socket
        socket_->setsockopt(ZMQ_LINGER, &linger_value, sizeof(linger_value));
        socket_->close();
        context_->close();
        std::cout << "Closed metric socket.. " << std::endl;
    }

    void Controller::StartMonitoring() {
        context_ = std::make_unique<zmq::context_t>(1); // Single I/O thread context
        socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUSH); // PUSH socket type
        socket_->connect("tcp://localhost:1750"); // Connect to the given endpoint
    }

    template<typename T>
    void Controller::SendMetrics(T &data, const size_t size) {
        zmq::message_t message(size);
        memcpy(message.data(), data.data(), size); // Copy counter to message data
        socket_->send(message, zmq::send_flags::none); // Send the message
        //std::cout << "Sent metric: " << data << std::endl;
    }

    bool Controller::PersistRunId() {
        bool read_write_success = false;
        std::fstream run_id_file_in("/home/pgrams/data/run_id.txt", std::ios::in);
        if (run_id_file_in.is_open()) {
            run_id_file_in >> run_id_;
            if (run_id_file_in.fail()) {
                LOG_WARNING(logger_, "Error reading Run ID from file.");
                run_id_ = 0;
            } else {
                LOG_INFO(logger_, "Previous Run ID [{}]", run_id_);
                run_id_++;
            }
            run_id_file_in.close(); // Close after reading

            std::ofstream run_id_file_out("/home/pgrams/data/run_id.txt", std::ios::out); // Open for writing (truncates if exists)
            if (run_id_file_out.is_open()) {
                run_id_file_out << run_id_ << std::endl;
                LOG_INFO(logger_, "Current Run ID [{}]", run_id_);
                run_id_file_out.close();
                read_write_success = true;
            } else {
                run_id_ = 0;
                LOG_WARNING(logger_, "Error opening Run ID file for writing.");
            }
        } else {
            run_id_ = 0;
            LOG_WARNING(logger_, "Error opening Run ID file for reading.");
        }
        return read_write_success;
    }

    bool Controller::LoadConfig(std::string &config_file) {
        std::ifstream f;
        try {
            f.open(config_file);
        } catch (const std::ifstream::failure& e) {
            if (f.is_open()) f.close();
            std::cerr << "Error opening config file: " << config_file << "IfStream failure: " << e.what() << "\n";
            return false;
        } catch (const std::exception& e) {
            if (f.is_open()) f.close();
            std::cerr << "Error opening config file: " << config_file << "Exception: " << e.what() << "\n";
            return false;
        } catch (...) {
            if (f.is_open()) f.close();
            return false;
        }

        if (!f.is_open()) {
            std::cerr << "Could not open config file " << config_file << std::endl;
            return false;
        }

        try {
            config_ = json::parse(f);
        } catch (json::parse_error& ex) {
            std::cerr << "Parse error: " << ex.what() << std::endl;
            std::cerr << "Parse error at byte: " << ex.byte << std::endl;
            if (f.is_open()) f.close();
            return false;
        } catch (...) {
            std::cerr << "Unknown error occurred while parsing config file!" << std::endl;
            if (f.is_open()) f.close();
            return false;
        }

        std::cout << "Successfully opened config file.." << std::endl;
        std::cout << config_.dump() << std::endl;
        return true;
    }

    bool Controller::Init() {
        current_state_ = State::kIdle;
        return true;
    }

    bool Controller::Configure(const std::vector<int32_t>& args) {

        PersistRunId();

        if (args.size() != 2) {
            LOG_ERROR(logger_, "Wrong number of arguments! {}", args.size());
            return false;
        }

        // Load requested config file
        std::string config_file("/home/pgrams/GramsReadout/config/test_");
        config_file += std::to_string(args.at(1)) + ".json";
        LOG_INFO(logger_, "Loading config {}", config_file);
        if (!LoadConfig(config_file)) {
            std::cerr << "Config load failed! \n";
            return  false;
        }

        // Specify the output file name to save the config file to for reference
        std::string filename = "run_" + std::to_string(run_id_) + ".json";
        // Open a file stream for writing
        std::ofstream outputFile(filename);
        if (outputFile.is_open()) {
            // Dump the JSON object to the file with indentation for readability
            outputFile << std::setw(4) << config_ << std::endl;
            outputFile.close();
        }

        // int32_t subrun_number = args.at(0);
        config_["data_handler"]["subrun"] = run_id_;
        LOG_INFO(logger_, "Configuring run number {}", run_id_);

        print_status_ = config_["controller"]["print_status"].get<bool>();
        status_->SetPrintStatus(print_status_);
        board_slots_.push_back(config_["crate"]["xmit_slot"].get<int>());
        board_slots_.push_back(config_["crate"]["charge_fem_slot"].get<int>());
        board_slots_.push_back(config_["crate"]["charge_fem_slot"].get<int>() + 1);
        board_slots_.push_back(config_["crate"]["last_charge_slot"].get<int>());
        board_slots_.push_back(config_["crate"]["light_fem_slot"].get<int>());

        // Connect to the PCIe bus handle
        int device_id_0 = 4, device_id_1 = 6;
        if (!pcie_interface_->InitPCIeDevices(device_id_0, device_id_1)) {
            LOG_ERROR(logger_, "PCIe device initialization failed!");
            return false;
        }
        LOG_INFO(logger_, "PCIe devices initialized!");
        LOG_INFO(logger_, "Initializing hardware...");
        pcie_ctrl_->Configure(config_, pcie_interface_.get(), *buffers_);
        xmit_ctrl_->Configure(config_, pcie_interface_.get(), *buffers_);
        light_fem_->Configure(config_, pcie_interface_.get(), *buffers_);
        charge_fem_->Configure(config_, pcie_interface_.get(), *buffers_);
        trigger_ctrl_->Configure(config_, pcie_interface_.get(), *buffers_);
        data_handler_->Configure(config_);
        LOG_INFO(logger_, "Configured Hardware! \n");
        is_configured_ = true;
        run_status_.store(true);
        status_thread_ = std::thread(&Controller::StatusControl, this);
        return true;
    }

    void Controller::ReadStatus() {

        status_->SetDataHandlerStatus(data_handler_.get());
        auto status_vec = status_->ReadStatus(board_slots_, pcie_interface_.get(), false);
        if (!print_status_) {
            // Construct and send a status packet
            Command cmd(static_cast<uint16_t>(CommandCodes::kStatusPacket), status_vec.size());
            cmd.arguments = std::move(status_vec);
            status_client_.WriteSendBuffer(cmd);
        } else {
            for (auto stat : status_vec)  LOG_INFO(logger_, "Data Handler Status: {} \n", stat);
        }
    }

    void Controller::StatusControl() {

        while (run_status_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            status_->SetDataHandlerStatus(data_handler_.get());
            auto status_vec = status_->ReadStatus(board_slots_, pcie_interface_.get(), false);
            if (!print_status_) {
                // Construct and send a status packet
                Command cmd(static_cast<uint16_t>(CommandCodes::kStatusPacket), status_vec.size());
                cmd.arguments = std::move(status_vec);
                status_client_.WriteSendBuffer(cmd);
                // Get the same metrics but in json string form
                if (enable_monitoring_) {
                    auto msg = status_->JsonHandlerStatus(data_handler_.get());
                    SendMetrics(msg, msg.size());
                }
            } else {
                for (auto stat : status_vec)  LOG_INFO(logger_, "Data Handler Status: {} \n", stat);
            }
        }
    }

    bool Controller::StartRun() {

        LOG_INFO(logger_, "Starting Run! \n");
        try {
            data_handler_->SetRun(true);
            data_thread_ = std::thread(&data_handler::DataHandler::CollectData, data_handler_.get(), pcie_interface_.get(), buffers_.get());
            // run_status_.store(true);
            // status_thread_ = std::thread(&Controller::StatusControl, this);
        } catch (std::exception& ex) {
            LOG_ERROR(logger_, "Exception occurred starting DataHandler: {}", ex.what());
            data_handler_->SetRun(false);
            return false;
        } catch (...) {
            LOG_ERROR(logger_, "Unknown exception occurred starting DataHandler!");
            data_handler_->SetRun(false);
            return false;
        }
        return true;
    }

    void Controller::TestRead(const std::vector<int32_t>& args) {

        const uint32_t imod_fem = args.at(0);
        const uint32_t ichip = args.at(1);
        const uint32_t command = args.at(2);
        const uint32_t nword = args.at(3);

        int iprint = 1;
        pcie_interface_->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers_->precv); // init the receiver

        buffers_->buf_send[0] = (imod_fem << 11) + (ichip << 8) + command + (0x0 << 16); // read out status
        pcie_interface_->PCIeSendBuffer(1, 1, 1, buffers_->psend);

        buffers_->precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
        pcie_interface_->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers_->precv);
        LOG_INFO(logger_, "\n Received TPC FEB [{}] (slot={}) status data word = 0x{:X}, 0x{:X} \n",
                            imod_fem, imod_fem, pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);
    }

    bool Controller::StopRun() {
        run_status_.store(false);
        if (status_thread_.joinable()) status_thread_.join();

        data_handler_->SetRun(false);
        if (data_thread_.joinable()) data_thread_.join();

        LOG_INFO(logger_, "Collection thread stopped successfully...\n");
        return true;
    }

    bool Controller::GetStatus() {
        return true;
    }

    bool Controller::Reset() {
        data_handler_->Reset(pcie_interface_.get());
        return true;
    }

    void Controller::SetRunning(bool run) {
        is_running_ = run;
        command_client_.setStopCmdRead(!run);
        status_client_.setStopCmdRead(!run);
    }

    void Controller::Run() {
        LOG_INFO(logger_, "Listening for commands..\n");
        is_running_ = true;
        ReceiveCommand();
        LOG_INFO(logger_, "Stopping controller\n");
    }

    void Controller::ReceiveCommand() {
        while (is_running_) {
            Command cmd = command_client_.ReadRecvBuffer();
            std::cout << "Received command: " << cmd.command << std::endl;
            // command_client_.WriteSendBuffer(cmd); //ack
            bool response = HandleCommand(cmd);
            if (cmd.command == CommandCodes::kHeartBeat) continue;
            // SendAckCommand(response);
            LOG_INFO(logger_, " \n Current state: [{}] \n", GetStateName());
        }
    }

    void Controller::SendAckCommand(bool success) {
        auto acknowledge = success ? CommandCodes::kCmdSuccess : CommandCodes::kCmdFailure;
        Command cmd(static_cast<uint16_t>(acknowledge), 1);
        cmd.arguments.at(0) = static_cast<int>(current_state_);
        command_client_.WriteSendBuffer(cmd);
    }

    // Handle user commands
    bool Controller::HandleCommand(const Command& command) {
        LOG_INFO(logger_, " \n Sending command: [{}] \n", command.command);

        if (command.command == CommandCodes::kConfigure && current_state_ == State::kIdle) {
            LOG_INFO(logger_, " \n State [Idle] \n");
            if (!is_configured_) {
                if (!Configure(command.arguments)) return false;
            }
            current_state_ = State::kConfigured;
            return true;

        } if (command.command == CommandCodes::kStartRun && current_state_ == State::kConfigured) {
            LOG_INFO(logger_, " \n State [Configure] \n ");
            current_state_ = State::kRunning;
            StartRun();
            return true;

        } if (command.command == CommandCodes::kStopRun && current_state_ == State::kRunning) {
            LOG_INFO(logger_, " \n State [Running] \n");
            current_state_ = State::kStopped;
            StopRun();
            return true;

        } if (command.command == CommandCodes::kReset && current_state_ == State::kStopped) {
            LOG_INFO(logger_, " \n State [Stopped] \n");
            Reset();
            is_configured_ = false;
            current_state_ = State::kIdle;
            return true;
        }

        if (command.command == CommandCodes::kStatusPacket) {
            LOG_INFO(logger_, " \n State [ReadStatus] \n");
            if (is_configured_) ReadStatus();
            else LOG_WARNING(logger_, "Cant read status before configuration!\n");
            // don't change from the previous state
            return true;
        }

        if (command.command == 0x10) {
            LOG_INFO(logger_, " \n State [TestRead] \n");
            TestRead(command.arguments);
            // don't change from the previous state
            return true;
        }

        LOG_INFO(logger_, "Invalid command or transition from state: {}", GetStateName());
        // metrics_->ControllerState(static_cast<int>(current_state_));
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
