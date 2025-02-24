//
// Created by sabertooth on 2/24/25.
//

#ifndef LIGHT_FEM_H
#define LIGHT_FEM_H

#include "hardware_device.h"

namespace light_fem {

class LightFem : public HardwareDevice {

    LightFem() = default;
    ~LightFem() override = default;

    bool Configure(pcie_int::PCIeInterface *pcie_interface) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;

private:

};

} // light_fem

#endif //LIGHT_FEM_H
