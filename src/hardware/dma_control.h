//
// Created by sabertooth on 2/24/25.
//

#ifndef DMA_CONTROL_H
#define DMA_CONTROL_H

#include "hardware_device.h"

namespace dma_control {

class DmaControl : public HardwareDevice {
public:

    DmaControl() = default;
    ~DmaControl() override = default;

    bool Configure(pcie_int::PCIeInterface *pcie_interface) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;

private:


};

} // dma_control

#endif //DMA_CONTROL_H
