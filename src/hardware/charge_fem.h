//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef CHARGE_FEM_H
#define CHARGE_FEM_H

#include "hardware_device.h"

namespace charge_fem {

class ChargeFem : public HardwareDevice {
public:

    ChargeFem();
    ~ChargeFem() override = default;

    bool Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) override;
    std::vector<uint32_t> GetStatus() override;
    bool Reset(pcie_int::PCIeInterface *pcie_interface) override;

private:

    uint32_t ConstructSendWord(uint32_t module, uint32_t chip, uint32_t command, uint32_t data);

    quill::Logger* logger_;
};

} // charge_fem

#endif //CHARGE_FEM_H
