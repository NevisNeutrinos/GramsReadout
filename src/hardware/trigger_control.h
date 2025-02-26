//
// Created by Jon Sensenig on 2/25/25.
//

#ifndef TRIGGER_CONTROL_H
#define TRIGGER_CONTROL_H

#include "hardware_device.h"

namespace trig_ctrl {

class TriggerControl : HardwareDevice {
public:

    TriggerControl() = default;
    ~TriggerControl() override = default;

    bool Configure(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers& buffers) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;
};

} // trig_ctrl

#endif //TRIGGER_CONTROL_H
