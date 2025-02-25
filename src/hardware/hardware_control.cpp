//
// Created by Jon Sensenig on 1/23/25.
//

#include "hardware_control.h"
#include "quill/LogMacros.h"
#include "quill/Frontend.h"
#include "quill/sinks/ConsoleSink.h"

#include <iostream>
#include <ostream>

namespace hardware_ctrl {

    HardwareControl::HardwareControl() : pcie_interface_(nullptr) {
        device_id_0_ = 5;
        device_id_1_ = 4;
        std::cout << "Set Device IDs to 0" << std::endl;
        pcie_interface_ = new pcie_int::PCIeInterface;
        configure_hardware_ = new hw_config::ConfigureHardware();

        logger_ = quill::Frontend::create_or_get_logger("root",
         quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));
        buffers_ = new pcie_int::PcieBuffers;

    }

    HardwareControl::~HardwareControl() {
        LOG_INFO(logger_, "Setting PCIe Device IDs to 0");
        device_id_0_ = 0;
        device_id_1_ = 0;
        delete pcie_interface_;
        delete configure_hardware_;
        delete buffers_;
    }

    bool HardwareControl::Initialize() {

        std::cout << "Initializing PCIe Bus and Device IDs" << std::endl;
        std::cout << std::hex;
        std::cout << "Device ID 1: " << device_id_0_ << std::endl;
        std::cout << "Device ID 2: " << device_id_1_ << std::endl;
        std::cout << std::dec;

        // Connect to the PCIe bus handle
        if (!pcie_interface_->InitPCIeDevices(device_id_0_, device_id_1_)) {
            LOG_ERROR(logger_, "PCIe device initialization failed!");
            return false;
        }
        LOG_INFO(logger_, "PCIe devices initialized!");
        std::cout << "PCIe devices initialized!" << std::endl;
        //Initialize the PCIe cards for Tx/Rx
        // if (!pcie_interface_->PCIeDeviceConfigure()) {
        //     LOG_ERROR(logger_, "PCIe device configuration failed!");
        //     return false;
        // }
        LOG_INFO(logger_, "PCIe devices ready!");
        return true;
    }

    bool HardwareControl::InitializeHardware() {
        auto* pconfig = new pcie_control::PcieControl();
        pconfig->Configure(pcie_interface_, *buffers_);

        auto* xconfig = new xmit_control::XmitControl();
        xconfig->Configure(pcie_interface_, *buffers_);

        auto* lconfig = new light_fem::LightFem();
        lconfig->Configure(pcie_interface_, *buffers_);

        auto* tconfig = new charge_fem::ChargeFem();
        tconfig->Configure(pcie_interface_, *buffers_);

        // Config config;
        // if (!configure_hardware_->Configure(config, pcie_interface_)) {
        //     LOG_ERROR(logger_, "Hardware Configuration failed!");
        //     return false;
        // }

        delete pconfig;
        delete xconfig;
        return true;
    }

} // hardware_ctrl