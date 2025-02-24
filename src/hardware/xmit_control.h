//
// Created by sabertooth on 2/24/25.
//

#ifndef XMIT_CONTROL_H
#define XMIT_CONTROL_H

#include "hardware_device.h"

namespace xmit_control {

class XmitControl : public HardwareDevice {
public:

    XmitControl() = default;
    ~XmitControl() override = default;

    bool Configure(pcie_int::PCIeInterface *pcie_interface) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;

private:

};

} // xmit_control

#endif //XMIT_CONTROL_H
