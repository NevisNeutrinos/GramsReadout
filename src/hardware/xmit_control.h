//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef XMIT_CONTROL_H
#define XMIT_CONTROL_H

#include "hardware_device.h"

namespace xmit_control {

class XmitControl : public HardwareDevice {
public:

    XmitControl();
    ~XmitControl() override = default;

    uint32_t Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) override;
    std::vector<uint32_t> GetStatus() override;

private:

    quill::Logger* logger_;

};

} // xmit_control

#endif //XMIT_CONTROL_H
