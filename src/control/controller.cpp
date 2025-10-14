//
// Created by Jon Sensenig on 1/22/25.
//

#include "controller.h"
#include "quill/LogMacros.h"
#include "quill/Frontend.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/FileSink.h"
#include <fstream>
#include <cstdlib>

namespace controller {

    Controller::Controller(asio::io_context& io_context, asio::io_context& status_io_context, const std::string& ip_address,
        const uint16_t command_port, const uint16_t status_port, const bool is_server, const bool is_running) :
        command_client_(io_context, ip_address, command_port, is_server, true, false),
        status_client_(status_io_context, ip_address, status_port, is_server, false, true),
        is_configured_(false),
        enable_monitoring_(false),
        run_id_(0),
        is_running_(is_running),
        run_status_(false) {
        // metrics_(data_monitor::DataMonitor::GetInstance()) {

        current_state_ = State::kIdle;

        // The environment variables should tell us where to find the config and data
        GetEnvVariables();
        std::string config_file = readout_basedir_ + "/tpc_configs/setup_config/setup_params.json";
        setup_config_ = LoadConfig(config_file);
        if (setup_config_.is_null()) {
            std::cerr << "Config load failed! \n";
        }

        const bool log_to_file = setup_config_["controller"]["log_to_file"].get<bool>();
        if (log_to_file) {
            const std::string file_name = data_basedir_ + "/logs/readout_log.log";
            quill::FileSinkConfig cfg;
            cfg.set_open_mode('w');
            cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);
            logger_ = quill::Frontend::create_or_get_logger("readout_logger",
                quill::Frontend::create_or_get_sink<quill::FileSink>(file_name, cfg));
        } else {
            logger_ = quill::Frontend::create_or_get_logger("readout_logger",
             quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id1"));
        }

        LOG_DEBUG(logger_, "Set-Up config dump: {} \n", setup_config_.dump());

        // const bool enable_metrics = setup_config_["data_handler"]["enable_metrics"].get<bool>();
        enable_monitoring_ = setup_config_["controller"]["enable_monitoring"].get<bool>();
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

    void Controller::GetEnvVariables() {
        const char *readout_basedir = std::getenv("TPC_DAQ_BASEDIR");
        const char *data_basedir = std::getenv("DATA_BASE_DIR");

        if (readout_basedir != nullptr) {
            readout_basedir_ = readout_basedir;
        } else {
            std::cerr << "Environment variable TPC_DAQ_BASEDIR does not exist!" << std::endl;
        }
        if (data_basedir != nullptr) {
            data_basedir_ = data_basedir;
        } else {
            std::cerr << "Environment variable DATA_BASE_DIR does not exist!" << std::endl;
        }
        std::cout << "Readout base directory: " << readout_basedir_ << std::endl;
        std::cout << "Data base directory: " << data_basedir_ << std::endl;
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
    }

    bool Controller::PersistRunId() {
        bool read_write_success = false;
        std::string run_id_file = data_basedir_ + "/run_id.txt";

        std::fstream run_id_file_in(run_id_file, std::ios::in);
        if (run_id_file_in.is_open()) {
            run_id_file_in >> run_id_;
            if (run_id_file_in.fail()) {
                LOG_WARNING(logger_, "Error reading Run ID from file.");
                run_id_ = 0;
            } else {
                LOG_DEBUG(logger_, "Previous Run ID [{}]", run_id_);
                run_id_++;
            }
            run_id_file_in.close(); // Close after reading

            std::ofstream run_id_file_out(run_id_file, std::ios::out); // Open for writing (truncates if exists)
            if (run_id_file_out.is_open()) {
                run_id_file_out << run_id_ << std::endl;
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
        LOG_INFO(logger_, "Run ID [{}]", run_id_);
        return read_write_success;
    }

    json Controller::LoadConfig(const std::string &config_file) {
        json config;
        std::ifstream f;
        try {
            f.open(config_file);
        } catch (const std::ifstream::failure& e) {
            if (f.is_open()) f.close();
            std::cerr << "Error opening config file: " << config_file << "IfStream failure: " << e.what() << "\n";
            return config;
        } catch (const std::exception& e) {
            if (f.is_open()) f.close();
            std::cerr << "Error opening config file: " << config_file << "Exception: " << e.what() << "\n";
            return config;
        } catch (...) {
            if (f.is_open()) f.close();
            return config;
        }

        if (!f.is_open()) {
            std::cerr << "Could not open config file " << config_file << std::endl;
            return config;
        }


        try {
            config = json::parse(f);
        } catch (json::parse_error& ex) {
            std::cerr << "Parse error: " << ex.what() << std::endl;
            std::cerr << "Parse error at byte: " << ex.byte << std::endl;
            if (f.is_open()) f.close();
            return config;
        } catch (...) {
            std::cerr << "Unknown error occurred while parsing config file!" << std::endl;
            if (f.is_open()) f.close();
            return config;
        }

        std::cout << "Successfully opened config file.." << std::endl;
        // std::cout << config.dump() << std::endl;
        return config;
    }

    bool Controller::Init() {
        current_state_ = State::kIdle;
        return true;
    }

    json Controller::SetConfigFromComm(json& config, const std::vector<int32_t> &config_vec, size_t skip_words) {
        try {
            tpc_configs_.deserialize(config_vec.begin()+skip_words, config_vec.end());
            LOG_INFO(logger_, "Config serialized.. {} params \n", config_vec.size());
        } catch (const std::exception& e) {
            LOG_ERROR(logger_, "Error serializing config.. {}", e.what());
            return nullptr;
        }
        catch (...) {
            LOG_ERROR(logger_, "Error serializing config..");
            return nullptr;
        }
        try {
            // light_fem
            config["light_fem"]["cosmic_summed_adc_thresh"] = tpc_configs_.getSummedPeakThresh();
            config["light_fem"]["cosmic_multiplicity"]      = tpc_configs_.getChannelMultiplicity();
            config["light_fem"]["pmt_delay_0"]              = tpc_configs_.getRoiDelay0();
            config["light_fem"]["pmt_delay_1"]              = tpc_configs_.getRoiDelay1();

            std::vector<int> sipm_precount(NUM_LIGHT_CHANNELS, tpc_configs_.getRoiPrecount());
            config["light_fem"]["pmt_precount"]             = sipm_precount;
            config["light_fem"]["pmt_window"]               = tpc_configs_.getRoiPeakWindow();

            config["light_fem"]["pmt_enable_top"]           = tpc_configs_.getEnableTop();
            config["light_fem"]["pmt_enable_middle"]        = tpc_configs_.getEnableMiddle();
            config["light_fem"]["pmt_enable_lower"]         = tpc_configs_.getEnableBottom();

            config["light_fem"]["sipm_words"]               = tpc_configs_.getNumRoiWords();

            std::vector<int> sipm_deadtime(NUM_LIGHT_CHANNELS, tpc_configs_.getRoiDeadtime());
            config["light_fem"]["sipm_deadtime"]            = sipm_deadtime;

            config["light_fem"]["pmt_gate_size"]            = tpc_configs_.getPmtGateSize();
            config["light_fem"]["pmt_beam_size"]            = tpc_configs_.getPmtBeamSize();
            config["light_fem"]["pmt_blocksize"]            = tpc_configs_.getFifoBlocksize();

            // thresholds (just first channel as scalar)
            config["light_fem"]["channel_thresh0"]          = tpc_configs_.getDiscThreshold0();
            config["light_fem"]["channel_thresh1"]          = tpc_configs_.getDiscThreshold1();

            // trigger
            config["trigger"]["trigger_source"]             = tpc_configs_.toTriggerSourceString(tpc_configs_.getTriggerSource());
            config["trigger"]["software_trigger_rate_hz"]   = tpc_configs_.getSoftwareTriggerRateHz();
            config["trigger"]["dead_time"]                  = tpc_configs_.getTpcDeadTime();

            // prescale (array → JSON array)
            auto& prescales = tpc_configs_.getPrescale();
            // element 2 is the light trigger
            prescales.at(1) = tpc_configs_.getLightTrigPrescale();
            config["trigger"]["prescale"] = prescales;

            // const auto& prescales = tpc_configs_.getPrescale();
            // config["trigger"]["prescale"] = nlohmann::json::array();
            // for (size_t i = 0; i < prescales.size(); ++i) {
            //     config["trigger"]["prescale"].push_back(prescales.at(i));
            // }
        } catch (const std::exception& e) {
            LOG_ERROR(logger_, "Error setting up config.. {}", e.what());
            return nullptr;
        }
        catch (...) {
            LOG_ERROR(logger_, "Error setting up config structure.");
            return nullptr;
        }
        return config;
    }

    bool Controller::Configure(const std::vector<int32_t>& args) {

        PersistRunId();

        if (args.size() < 1) {
            LOG_ERROR(logger_, "Wrong number of arguments! {}", args.size());
            return false;
        }

        // Load requested config file
        std::string config_file = readout_basedir_ + "/tpc_configs/data_config/test_" + std::to_string(args.at(0)) + ".json";
        LOG_INFO(logger_, "Loading config {}", config_file);
        config_ = LoadConfig(config_file);
        if (config_.is_null()) {
            std::cerr << "Config load failed! \n";
            return  false;
        }

        if (args.size() > 1) {
            LOG_INFO(logger_, "Setting config from ground comms \n");
            size_t skip_words = 1;
            config_ = SetConfigFromComm(config_, args, skip_words);
            if (config_.is_null()) {
                return false;
            }
        } else {
            // Not making these vectors for convenience but keeping them here in case it is needed
            std::vector<int> sipm_precount(NUM_LIGHT_CHANNELS, config_["light_fem"]["pmt_precount"]);
            config_["light_fem"]["pmt_precount"] = sipm_precount;
            std::vector<int> sipm_deadtime(NUM_LIGHT_CHANNELS, config_["light_fem"]["sipm_deadtime"]);
            config_["light_fem"]["sipm_deadtime"] = sipm_deadtime;
            std::vector<int> channel_thresh0(NUM_LIGHT_CHANNELS, config_["light_fem"]["channel_thresh0"]);
            config_["light_fem"]["channel_thresh0"] = channel_thresh0;
            std::vector<int> channel_thresh1(NUM_LIGHT_CHANNELS, config_["light_fem"]["channel_thresh1"]);
            config_["light_fem"]["channel_thresh1"] = channel_thresh1;
            // element 2 is the light trigger
            config_["trigger"]["prescale"].at(1) = config_["trigger"]["light_trig_prescale"];
        }

        // Add the setup config to this so we only have to pass around a single config object
        config_.update(setup_config_);

        // Specify the output file name to save the config file to for reference
        std::string filename = data_basedir_ + "/config_logs/run_" + std::to_string(run_id_) + ".json";
        // Open a file stream for writing
        std::ofstream outputFile(filename);
        if (outputFile.is_open()) {
            // Dump the JSON object to the file with indentation for readability
            outputFile << std::setw(4) << config_ << std::endl;
            outputFile.close();
        }

        // Set the basedir for the data
        config_["data_handler"]["data_basedir"] = data_basedir_;
        config_["hardware"]["readout_basedir"] = readout_basedir_;

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
        const int device_id_0 = config_["controller"]["device_id_0"].get<int>();
        const int device_id_1 = config_["controller"]["device_id_1"].get<int>();
        if (!pcie_interface_->InitPCIeDevices(device_id_0, device_id_1)) {
            LOG_ERROR(logger_, "PCIe device initialization failed!");
            return false;
        }

        LOG_INFO(logger_, "Config dump: {} \n", config_.dump());

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
        // This line essentially samples the metrics that are accumulating in the data handler
        status_->SetDataHandlerStatus(data_handler_.get());
        tpc_readout_monitor_.setReadoutState(static_cast<int32_t>(current_state_));
        status_->ReadStatus(tpc_readout_monitor_, board_slots_, pcie_interface_.get(), false);
        if (!print_status_) {
            // Construct and send a status packet
            // Command cmd(to_u16(CommunicationCodes::COL_Hardware_Status), status_vec.size());
            // cmd.arguments = std::move(status_vec);
            auto tmp_vec = tpc_readout_monitor_.serialize();
            status_client_.WriteSendBuffer(to_u16(CommunicationCodes::COL_Hardware_Status), tmp_vec);
        } else {
            // for (auto stat : status_vec)  LOG_INFO(logger_, "Data Handler Status: {} \n", stat);
            tpc_readout_monitor_.print();
        }
    }

    void Controller::StatusControl() {

        while (run_status_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            status_->SetDataHandlerStatus(data_handler_.get());
            tpc_readout_monitor_.setReadoutState(static_cast<int32_t>(current_state_));
            status_->ReadStatus(tpc_readout_monitor_, board_slots_, pcie_interface_.get(), false);
            if (!print_status_) {
                auto tmp_vec = tpc_readout_monitor_.serialize();
                status_client_.WriteSendBuffer(to_u16(CommunicationCodes::COL_Hardware_Status), tmp_vec);
                // Get the same metrics but in json string form
                if (enable_monitoring_) { // for MQTT direct tests, not for flight
                    auto msg = status_->JsonHandlerStatus(data_handler_.get());
                    SendMetrics(msg, msg.size());
                }
            } else {
                tpc_readout_monitor_.print();
            }
        }
    }

    bool Controller::StartRun() {

        LOG_INFO(logger_, "Starting Run! \n");
        try {
            data_handler_->SetRun(true);
            data_thread_ = std::thread(&data_handler::DataHandler::CollectData, data_handler_.get(), pcie_interface_.get());
            // Start status thread but only if it's not already running
            if (!status_thread_.joinable()) {
                run_status_.store(true);
                status_thread_ = std::thread(&Controller::StatusControl, this);
            }
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
        PersistRunId();
        config_["data_handler"]["subrun"] = run_id_;
        data_handler_->Reset(run_id_);
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
            if (cmd.command == CommunicationCodes::COM_HeartBeat) continue;
            SendAckCommand(response);
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

        if (command.command == CommunicationCodes::COL_Configure && current_state_ == State::kIdle) {
            LOG_INFO(logger_, " \n State [Idle] \n");
            if (!is_configured_) {
                if (!Configure(command.arguments)) return false;
            }
            current_state_ = State::kConfigured;
            return true;

        } if (command.command == CommunicationCodes::COL_Start_Run && current_state_ == State::kConfigured) {
            LOG_INFO(logger_, " \n State [Configure] \n ");
            current_state_ = State::kRunning;
            StartRun();
            return true;

        } if (command.command == CommunicationCodes::COL_Stop_Run && current_state_ == State::kRunning) {
            LOG_INFO(logger_, " \n State [Running] \n");
            current_state_ = State::kStopped;
            StopRun();
            return true;

        } if (command.command == CommunicationCodes::COL_Reset_Run && current_state_ == State::kStopped) {
            LOG_INFO(logger_, " \n State [Stopped] \n");
            Reset();
            // is_configured_ = false;
            // current_state_ = State::kIdle;
            current_state_ = State::kConfigured;
            return true;
        }

        if (command.command == CommunicationCodes::COL_Query_Hardware_Status) {
            LOG_INFO(logger_, " \n State [ReadStatus] \n");
            if (is_configured_) ReadStatus();
            else LOG_WARNING(logger_, "Cant read status before configuration!\n");
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
