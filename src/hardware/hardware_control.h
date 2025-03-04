//
// Created by Jon Sensenig on 1/23/25.
//

#ifndef PCIECONTROL_H
#define PCIECONTROL_H

#include "pcie_interface.h"
#include "configure_hardware.h"
#include "quill/Logger.h"
#include "json.hpp"
#include "pcie_control.h"
#include "xmit_control.h"
#include "charge_fem.h"
#include "light_fem.h"
#include "trigger_control.h"
#include "../data/data_handler.h"

namespace hardware_ctrl {

class HardwareControl {
    public:

    HardwareControl();
    ~HardwareControl();

    // FIXME placeholder for config object
    typedef std::vector<std::string> Config;
    typedef std::vector<std::string> Status;

    // Do not allow copying or copy assignments
    HardwareControl(const HardwareControl &) = delete;
    HardwareControl &operator=(const HardwareControl &) = delete;

    // Initialize class
    bool Initialize(json &config);
    bool InitializeHardware(json &config);

private:

    quill::Logger* logger_;

    pcie_int::PCIeInterface *pcie_interface_;
    hw_config::ConfigureHardware *configure_hardware_;
    pcie_int::PcieBuffers *buffers_;

    // PCIe device bus and IDs
    int pcie_bus_{};
    int device_id_0_;
    int device_id_1_;

};

} // hardware_ctrl

#endif //PCIECONTROL_H
