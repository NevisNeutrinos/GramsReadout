//
// Created by Jon Sensenig on 1/29/25.
//

#ifndef PCIE_INTERFACE_H
#define PCIE_INTERFACE_H
#include <cstdint>

namespace pcie_int {

class PCIeInterface {

public:

    PCIeInterface();
    ~PCIeInterface();

    // Initialize both devices, there should always be two since
    // we assume this is for the Nevis electronics readout.
    bool InitPCIeDevices(uint32_t dev1, uint32_t dev2);

    bool PCIeDeviceConfigure();

    // The main method to send buffers through PCIe
    int PCIeSendBuffer(uint32_t dev, int mode, int nword, uint32_t *buff_send);

    int PCIeRecvBuffer(uint32_t dev, int mode, int istart, int nword, int ipr_status, uint32_t *buff_rec);

    [[nodiscard]] bool getInitStatus() const { return is_initialized; };

private:

    // The pointer to the device in memory
    typedef void* PCIeDeviceHandle;
    PCIeDeviceHandle dev_handle_1;
    PCIeDeviceHandle dev_handle_2;
    PCIeDeviceHandle hDev;

    bool is_initialized;

    // Magic numbers for DMA R/W
    const uint32_t t1_tr_bar = 0;
    const uint32_t t2_tr_bar = 4;
    const uint32_t cs_bar = 2;
    /**  command register location **/
    const uint32_t tx_mode_reg = 0x28;
    const uint32_t t1_cs_reg = 0x18;
    const uint32_t r1_cs_reg = 0x1c;
    const uint32_t t2_cs_reg = 0x20;
    const uint32_t r2_cs_reg = 0x24;
    const uint32_t tx_md_reg = 0x28;
    const uint32_t cs_dma_add_low_reg = 0x0;
    const uint32_t cs_dma_add_high_reg = 0x4;
    const uint32_t cs_dma_by_cnt = 0x8;
    const uint32_t cs_dma_cntrl = 0xc;
    const uint32_t cs_dma_msi_abort = 0x10;

};

} // pcie_int

#endif //PCIE_INTERFACE_H
