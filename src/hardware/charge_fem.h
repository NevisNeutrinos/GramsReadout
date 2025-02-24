//
// Created by sabertooth on 2/24/25.
//

#ifndef CHARGE_FEM_H
#define CHARGE_FEM_H

#include "hardware_device.h"

namespace charge_fem {

class ChargeFem : public HardwareDevice {

    ChargeFem() = default;
    ~ChargeFem() override = default;

    bool Configure(pcie_int::PCIeInterface *pcie_interface) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;

private:

};

} // charge_fem

#endif //CHARGE_FEM_H
