//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef DMA_CONTROL_H
#define DMA_CONTROL_H

#include "hardware_device.h"

namespace dma_control {

class DmaControl : public HardwareDevice {
public:

    DmaControl();
    ~DmaControl() override = default;

    bool Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;

private:

    bool SetRecvBuffer(pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2);

    uint32_t dma_buf_size_ = 100000;

    pcie_int::DMABufferHandle  pbuf_rec1_;
    pcie_int::DMABufferHandle pbuf_rec2_;

    quill::Logger* logger_;

};

} // dma_control

#endif //DMA_CONTROL_H
