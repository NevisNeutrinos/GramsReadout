//
// Created by Jon Sensenig on 1/29/25.
//

#ifndef PCIE_INTERFACE_H
#define PCIE_INTERFACE_H
#include <cstdint>
#include <array>
#include <memory>


namespace pcie_int {

    typedef void *PCIeDeviceHandle;
    typedef void *DMABufferHandle;

    struct PcieBuffers {
        static constexpr size_t SEND_SIZE = 40000;
        static constexpr size_t READ_SIZE = 10000000;

        std::array<uint32_t, SEND_SIZE> buf_send{};
        std::array<unsigned char, SEND_SIZE> carray{};
        static std::array<uint32_t, SEND_SIZE> send_array;
        static std::array<uint32_t, READ_SIZE> read_array;

        uint32_t *psend{};
        uint32_t *precv{};
    };

// Forward declaring the struct so as not
// to introduce dependecies.
struct DmaBuffStruct;

class PCIeInterface {

public:

    PCIeInterface();
    ~PCIeInterface();

    // Initialize both devices, there should always be two since
    // we assume this is for the Nevis electronics readout.
    bool InitPCIeDevices(uint32_t dev1, uint32_t dev2);

    bool PCIeDeviceConfigure();

    // The main method to send buffers through PCIe
    uint32_t PCIeSendBuffer(uint32_t dev, uint32_t mode, uint32_t nword, uint32_t *buff_send);

    uint32_t PCIeRecvBuffer(uint32_t dev, uint32_t mode, uint32_t istart, uint32_t nword, uint32_t ipr_status, uint32_t *buff_rec);

    [[nodiscard]] bool getInitStatus() const { return is_initialized_; };

    void ReadReg32(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint32_t *data);
    bool WriteReg32(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint32_t data);
    void ReadReg64(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, unsigned long long *data);
    bool WriteReg64(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint64_t data);

    bool DmaContigBufferLock(uint32_t dev_handle, uint32_t dwDMABufSize, DMABufferHandle *pbuf_rec);
    bool FreeDmaContigBuffers();
    bool DmaSyncCpu(uint32_t buffer_handle);
    bool DmaSyncIo(uint32_t buffer_handle);
    uint32_t GetBufferPageAddrUpper(uint32_t buffer_handle);
    uint32_t GetBufferPageAddrLower(uint32_t buffer_handle);

    static constexpr uint32_t kDev1 = 1;
    static constexpr uint32_t kDev2 = 2;

    std::array<uint32_t, 40000> buf_send_{};
    std::array<unsigned char, 40000> carray_{};
    static std::array<uint32_t, 40000> send_array_;
    static std::array<uint32_t, 10000000> read_array_;

private:

    bool is_initialized_;
    PCIeDeviceHandle GetDeviceHandle(uint32_t dev_handle);

    // The pointer to the device in memory
    PCIeDeviceHandle dev_handle_1;
    PCIeDeviceHandle dev_handle_2;
    PCIeDeviceHandle hDev;

    // Warning! Make sure the DMA buffer size is memory aligned to 32b
    // since we cast it from number of bytes into a 32b buffer. In other
    // words the number set here must be a multiple of 4, if it's not a
    // compile error will be thrown.
    static constexpr uint32_t CONFIGDMABUFFSIZE = 14000; // in bytes

    static_assert((CONFIGDMABUFFSIZE % sizeof(uint32_t)) == 0,
        "CONFIGDMABUFFSIZE must be a multiple of 4!");

    std::unique_ptr<DmaBuffStruct> buffer_info_struct_send_;
    std::unique_ptr<DmaBuffStruct> buffer_info_struct_recv_;
    uint32_t *buffer_send_ = nullptr;
    uint32_t *buffer_recv_ = nullptr;
    void *pbuf_send_;
    void *pbuf_recv_;

    // DMA Data Aquisition
    std::unique_ptr<DmaBuffStruct> buffer_info_struct1;
    std::unique_ptr<DmaBuffStruct> buffer_info_struct2;
    std::unique_ptr<DmaBuffStruct> buffer_info_struct_trig;

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
