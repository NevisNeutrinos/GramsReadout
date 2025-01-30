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
        device_id_0_ = 4;
        device_id_1_ = 5;
        std::cout << "Set Device IDs to 0" << std::endl;
        pcie_interface = new pcie_int::PCIeInterface;

        logger_ = quill::Frontend::create_or_get_logger("root",
         quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));
        LOG_INFO(logger_, "Destructing controller");
    }

    PCIeControl::~PCIeControl() {
        // std::cout << "Setting Device IDs to 0" << std::endl;
        LOG_INFO(logger_, "Setting Device IDs to 0");
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

        if (!pcie_interface->InitPCIeDevices(device_id_0_, device_id_1_)) {
            std::cerr << "PCIe device initialization failed!" << std::endl;
            return false;
        }
        return true;
    }

} // pcie_ctrl