//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef CHARGE_FEM_H
#define CHARGE_FEM_H

#include "hardware_device.h"

namespace charge_fem {

class ChargeFem : public HardwareDevice {
public:

    ChargeFem() = default;
    ~ChargeFem() override = default;

    bool Configure(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;

private:

};

} // charge_fem

#endif //CHARGE_FEM_H
