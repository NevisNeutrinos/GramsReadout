//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef LIGHT_FEM_H
#define LIGHT_FEM_H

#include "hardware_device.h"

namespace light_fem {

class LightFem : public HardwareDevice {
public:

    LightFem();
    ~LightFem() override = default;

    bool Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) override;
    std::vector<uint32_t> GetStatus() override;
    bool Reset(pcie_int::PCIeInterface *pcie_interface) override;

private:

    quill::Logger* logger_;

};

} // light_fem

#endif //LIGHT_FEM_H
