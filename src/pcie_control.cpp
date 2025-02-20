//
// Created by Jon Sensenig on 1/23/25.
//

#include "pcie_control.h"
#include "quill/LogMacros.h"
#include "quill/Frontend.h"
#include "quill/sinks/ConsoleSink.h"

#include <iostream>
#include <ostream>

namespace pcie_ctrl {

    PCIeControl::PCIeControl() {
        device_id_0_ = 5;
        device_id_1_ = 4;
        std::cout << "Set Device IDs to 0" << std::endl;
        pcie_interface = new pcie_int::PCIeInterface;

        logger_ = quill::Frontend::create_or_get_logger("root",
         quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));
    }

    PCIeControl::~PCIeControl() {
        LOG_INFO(logger_, "Setting PCIe Device IDs to 0");
        device_id_0_ = 0;
        device_id_1_ = 0;
        delete pcie_interface;
    }

    bool PCIeControl::Initialize() {

        std::cout << "Initializing PCIe Bus and Device IDs" << std::endl;
        std::cout << std::hex;
        std::cout << "Device ID 1: " << device_id_0_ << std::endl;
        std::cout << "Device ID 2: " << device_id_1_ << std::endl;
        std::cout << std::dec;

        // Connect to the PCIe bus handle
        if (!pcie_interface->InitPCIeDevices(device_id_0_, device_id_1_)) {
            LOG_ERROR(logger_, "PCIe device initialization failed!");
            return false;
        }
        // Initialize the PCIe cards for Tx/Rx
        if (!pcie_interface->PCIeDeviceConfigure()) {
            LOG_ERROR(logger_, "PCIe device configuration failed!");
            return false;
        }
        LOG_INFO(logger_, "PCIe devices ready!");
        return true;
    }

} // pcie_ctrl