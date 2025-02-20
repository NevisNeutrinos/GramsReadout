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
    static constexpr uint32_t t1_tr_bar = 0;
    static constexpr uint32_t t2_tr_bar = 4;
    static constexpr uint32_t cs_bar = 2;
    /**  command register location **/
    static constexpr uint32_t tx_mode_reg = 0x28;
    static constexpr uint32_t t1_cs_reg = 0x18;
    static constexpr uint32_t r1_cs_reg = 0x1c;
    static constexpr uint32_t t2_cs_reg = 0x20;
    static constexpr uint32_t r2_cs_reg = 0x24;
    static constexpr uint32_t tx_md_reg = 0x28;
    static constexpr uint32_t cs_dma_add_low_reg = 0x0;
    static constexpr uint32_t cs_dma_add_high_reg = 0x4;
    static constexpr uint32_t cs_dma_by_cnt = 0x8;
    static constexpr uint32_t cs_dma_cntrl = 0xc;
    static constexpr uint32_t cs_dma_msi_abort = 0x10;
    /** define status bits **/
    static constexpr uint32_t cs_init = 0x20000000;
    static constexpr uint32_t cs_mode_p = 0x8000000;
    static constexpr uint32_t cs_mode_n = 0x0;
    static constexpr uint32_t cs_start = 0x40000000;
    static constexpr uint32_t cs_done = 0x80000000;
    static constexpr uint32_t dma_tr1 = 0x100000;
    static constexpr uint32_t dma_tr2 = 0x200000;
    static constexpr uint32_t dma_tr12 = 0x300000;
    static constexpr uint32_t dma_3dw_trans = 0x0;
    static constexpr uint32_t dma_4dw_trans = 0x0;
    static constexpr uint32_t dma_3dw_rec = 0x40;
    static constexpr uint32_t dma_4dw_rec = 0x60;
    static constexpr uint32_t dma_in_progress = 0x80000000;

};

} // pcie_int

#endif //PCIE_INTERFACE_H
