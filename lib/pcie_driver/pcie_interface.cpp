//
// Created by Jon Sensenig on 1/29/25.
//

#include <iostream>
#include "pcie_interface.h"
#include "gramsreadout_lib.h"

namespace pcie_int {

    std::array<uint32_t, PcieBuffers::SEND_SIZE> PcieBuffers::send_array = {};
    std::array<uint32_t, PcieBuffers::READ_SIZE> PcieBuffers::read_array = {};

    PCIeInterface::PCIeInterface() {
        dev_handle_1 = nullptr;
        dev_handle_2 = nullptr;
        hDev = nullptr;
        is_initialized = false;
    }

    PCIeInterface::~PCIeInterface() {
        std::cout << "Closing PCIe Devices..." << std::endl;
        std::cout << std::hex;
        std::cout << "Dev1: " << dev_handle_1 << std::endl;
        std::cout << "Dev2: " << dev_handle_2 << std::endl;
        std::cout << std::dec;

        if (dev_handle_1) {
            if (!GRAMSREADOUT_DeviceClose(dev_handle_1)) {
                std::cerr << "Device 1 close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            }
        }

        if (dev_handle_2) {
            if (!GRAMSREADOUT_DeviceClose(dev_handle_2)) {
                std::cerr << "Device 2 close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            }
        }

        // Uninitialize the Windriver library
        if (WD_STATUS_SUCCESS != GRAMSREADOUT_LibUninit()) {
            std::cerr << "GRAMSREADOUT_LibUninit() failed:" << std::endl;
            std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
        }
    }

    bool PCIeInterface::InitPCIeDevices(uint32_t dev1, uint32_t dev2) {
        /* Initialize the GRAMSREADOUT library */
        DWORD dwStatus = GRAMSREADOUT_LibInit();
        if (WD_STATUS_SUCCESS != dwStatus) {
            std::cerr << "GRAMSREADOUT_LibInit() failed!" << std::endl;
            std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            return false;
        }

        constexpr uint32_t vendor_id = GRAMSREADOUT_DEFAULT_VENDOR_ID;

        dev_handle_1 = GRAMSREADOUT_DeviceOpen(vendor_id, dev1);
        dev_handle_2 = GRAMSREADOUT_DeviceOpen(vendor_id, dev2);

        if (!dev_handle_1 || !dev_handle_2) { return false; }

        std::cout << std::hex;
        std::cout << "Dev Handle 1: " << dev_handle_1 << " Dev Handle 2: " << dev_handle_2 << std::endl;
        std::cout << "Addr: Dev Handle 1: " << &dev_handle_1 << " Dev Handle 2: " << &dev_handle_2 << std::endl;
        std::cout << std::dec;

        return true;
    }

    void PCIeInterface::ReadReg32(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint32_t *data) {
        hDev = GetDeviceHandle(dev_handle);
        uint32_t read_status = WDC_ReadAddr32(hDev, addr_space, adr_offset, data);
        if (WD_STATUS_SUCCESS != read_status) std::cerr << "ReadReg32() failed" << std::endl;
    }

    bool PCIeInterface::WriteReg32(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint32_t data) {
        hDev = GetDeviceHandle(dev_handle);
        uint32_t write_status = WDC_WriteAddr32(hDev, addr_space, adr_offset, data);
        return write_status == WD_STATUS_SUCCESS;
    }

    void PCIeInterface::ReadReg64(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, unsigned long long *data) {
        hDev = GetDeviceHandle(dev_handle);
        uint32_t read_status = WDC_ReadAddr64(hDev, addr_space, adr_offset, data);
        if (WD_STATUS_SUCCESS != read_status) std::cerr << "ReadReg64() failed" << std::endl;
    }

    bool PCIeInterface::WriteReg64(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint64_t data) {
        hDev = GetDeviceHandle(dev_handle);
        uint32_t write_status = WDC_WriteAddr64(hDev, addr_space, adr_offset, data);
        return write_status == WD_STATUS_SUCCESS;
    }

    bool PCIeInterface::PCIeDeviceConfigure() {
        static DWORD dwAddrSpace;
        static DWORD dwOffset;
        static UINT32 u32Data;
        // UINT32 buf_send[40000];
        // static UINT32 i, k;
        // UINT32 *px;
        DWORD wr_stat = 0;

        dwAddrSpace = 2;
        u32Data = cs_init; //20000000; // initial transmitter, no hold
        dwOffset = t1_cs_reg; //0x18;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = cs_init; //20000000; // initial transmitter, no hold
        dwOffset = t2_cs_reg; //0x20;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = cs_init; //20000000; // initial receiver
        dwOffset = r1_cs_reg; //0x1c;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = cs_init; //20000000; // initial receiver
        dwOffset = r2_cs_reg; //0x24;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = 0xfff; // set mode off with 0xfff... ffff
        dwOffset = tx_md_reg; //0x28;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        // px = &buf_send[0]; // RUN INITIALIZATION
        // buf_send[0] = 0x0; // INITIALIZE
        // buf_send[1] = 0x0;
        // i = 1;
        // k = 1;
        // i = PCIeSendBuffer(kDev1, i, k, px);
        //
        // If all writes succeed it should be 0
        is_initialized = wr_stat == WD_STATUS_SUCCESS;
        return is_initialized;
    }

    PCIeDeviceHandle PCIeInterface::GetDeviceHandle(uint32_t dev_handle) {
        if (dev_handle == 1) {
            return dev_handle_1;
        }
        if (dev_handle == 2) {
            return  dev_handle_2;
        }

        std::cerr << "PCIeInterface::PCIeSendBuffer: Invalid device handle: " << dev_handle << std::endl;
        return nullptr;
    }

    uint32_t PCIeInterface::PCIeSendBuffer(uint32_t dev, uint32_t mode, uint32_t nword, uint32_t *buff_send) {
        /* imode =0 single word transfer, imode =1 DMA */

        hDev = GetDeviceHandle(dev);

        static DWORD dwAddrSpace;
        static DWORD dwDMABufSize;

        static UINT32 *buf_send;
        static WD_DMA *pDma_send;
        static DWORD dwStatus;
        static DWORD dwOptions_send = DMA_TO_DEVICE;
        static DWORD dwOffset;
        static UINT32 u32Data;
        static PVOID pbuf_send;
        uint32_t nwrite, iprint;
        uint32_t i = 0;
        uint32_t j = 0;
        static uint32_t ifr = 0;

        iprint = 0;

        if (ifr == 0)
        {
            ifr = 1;
            dwDMABufSize = 140000;
            dwStatus = WDC_DMAContigBufLock(hDev, &pbuf_send, dwOptions_send, dwDMABufSize, &pDma_send);
            if (WD_STATUS_SUCCESS != dwStatus)
            {
                std::cerr << "Failed locking a send Contiguous DMA buffer. Error "
                <<  dwStatus << " = " << std::string(Stat2Str(dwStatus)) << std::endl;
            }
            buf_send = static_cast<uint32_t *>(pbuf_send);
        }
        if (mode == 1)
        {
            for (i = 0; i < nword; i++)
            {
                *(buf_send + i) = *buff_send++;
            }
        }
        if (mode == 0)
        {
            nwrite = nword * 4;
            /*setup transmiiter */
            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000;
            dwOffset = t1_cs_reg; // 0x18
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            dwAddrSpace = 2;
            u32Data = cs_start + nwrite; //0x40000000 + nwrite;
            dwOffset = t1_cs_reg; // 0x18
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            for (j = 0; j < nword; j++)
            {
                dwAddrSpace = 0;
                dwOffset = cs_dma_add_low_reg; //0x0
                u32Data = *buff_send++;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            }
            for (i = 0; i < 20000; i++)
            {
                dwAddrSpace = 2;
                dwOffset = cs_dma_cntrl; //0xC
                WDC_ReadAddr32(hDev, dwAddrSpace, dwOffset, &u32Data);
                if (iprint == 1)
                    std::cout << "status read: " << i << " " << u32Data << std::endl;
                if (((u32Data & dma_in_progress) == 0) && iprint == 1)
                    std::cout << "Data transfer complete: " << i << std::endl;
                if ((u32Data & dma_in_progress) == 0)
                    break;
            }
        }
        if (mode == 1)
        {
            nwrite = nword * 4;
            WDC_DMASyncCpu(pDma_send);
            /*setup transmiiter */
            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000;
            dwOffset = t1_cs_reg; //0x18
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            dwAddrSpace = 2;
            u32Data = cs_start + nwrite; //0x40000000 + nwrite;
            dwOffset = t1_cs_reg; //0x18
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            /* set up sending DMA starting address */

            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000;
            dwOffset = cs_dma_add_low_reg; //0x0
            u32Data = pDma_send->Page->pPhysicalAddr & 0xffffffff;
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000;
            dwOffset = cs_dma_add_high_reg; //0x4
            u32Data = (pDma_send->Page->pPhysicalAddr >> 32) & 0xffffffff;
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            /* byte count */
            dwAddrSpace = 2;
            dwOffset = cs_dma_by_cnt; //0x8
            u32Data = nwrite;
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            /* write this will start DMA */
            dwAddrSpace = 2;
            dwOffset = cs_dma_cntrl; //0xc
            u32Data = dma_tr1; //0x00100000;
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            // usleep(50); // works in standalone script
            usleep(50);
            for (i = 0; i < 20000; i++)
            {
                dwAddrSpace = 2;
                dwOffset = cs_dma_cntrl; //0xC
                WDC_ReadAddr32(hDev, dwAddrSpace, dwOffset, &u32Data);
                if (iprint == 1)
                    std::cout << "DMA status read: " << i << " " << u32Data << std::endl;
                if (((u32Data & dma_in_progress) == 0) && iprint == 1)
                    std::cout << "DMA complete: " << i << std::endl;
                if ((u32Data & dma_in_progress) == 0)
                    break;
                // continue;
            }
            WDC_DMASyncIo(pDma_send);
        }
        return i;
    }

    uint32_t PCIeInterface::PCIeRecvBuffer(uint32_t dev, uint32_t mode, uint32_t istart, uint32_t nword, uint32_t ipr_status, uint32_t *buff_rec) {
        /* imode =0 single word transfer, imode =1 DMA */

        hDev = GetDeviceHandle(dev);

        static DWORD dwAddrSpace;
        static DWORD dwDMABufSize;

        static UINT32 *buf_rec;
        static WD_DMA *pDma_rec;
        static DWORD dwStatus;
        static DWORD dwOptions_rec = DMA_FROM_DEVICE;
        static DWORD dwOffset;
        static UINT32 u32Data;
        static UINT64 u64Data;
        static PVOID pbuf_rec;
        uint32_t nread, i, j, iprint, icomp;
        static uint32_t ifr = 0;

        if (ifr == 0)
        {
            ifr = 1;
            dwDMABufSize = 140000;
            dwStatus = WDC_DMAContigBufLock(hDev, &pbuf_rec, dwOptions_rec, dwDMABufSize, &pDma_rec);
            if (WD_STATUS_SUCCESS != dwStatus)
            {
                std::cerr << "Failed locking a receive Contiguous DMA buffer. Error "
                <<  dwStatus << " = " << std::string(Stat2Str(dwStatus)) << std::endl;
            }
            buf_rec = static_cast<uint32_t *>(pbuf_rec);
        }
        iprint = 0;
        /** set up the receiver **/
        if ((istart == 1) | (istart == 3))
        {
            // initalize transmitter mode register...
            std::cout << "nword = " << nword << std::endl;

            dwAddrSpace = 2;
            u32Data = 0xf0000008; // f0000008
            dwOffset = tx_md_reg; //0x28
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            /*initialize the receiver */
            dwAddrSpace = 2;
            u32Data = cs_init; // 20000000;
            dwOffset = r1_cs_reg; //0x1c
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            /* write byte count **/
            dwAddrSpace = 2;
            u32Data = cs_start + nword * 4; //0x40000000 + nword * 4;  40000004
            dwOffset = r1_cs_reg; //0x1c
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            if (ipr_status == 1)
            {
                dwAddrSpace = 2;
                u64Data = 0;
                dwOffset = t1_cs_reg; //0x18
                WDC_ReadAddr64(hDev, dwAddrSpace, dwOffset, &u64Data);
                std::cout << std::hex;
                std::cout << "status word before read = " << (u64Data >> 32) << ", " << (u64Data & 0xffff) << std::endl;
                std::cout << std::dec;
                printf(" status word before read = %I64u, %I64u \n", (u64Data >> 32), (u64Data & 0xffff));
            }

            return 0;
        }
        if ((istart == 2) | (istart == 3))
        {

            if (mode == 0)
            {
                nread = nword / 2 + 1;
                if (nword % 2 == 0)
                    nread = nword / 2;
                for (j = 0; j < nread; j++)
                {
                    dwAddrSpace = 0;
                    dwOffset = cs_dma_add_low_reg; //0x0
                    u64Data = 0xbad;
                    WDC_ReadAddr64(hDev, dwAddrSpace, dwOffset, &u64Data);
                    *buff_rec++ = (u64Data & 0xffffffff);
                    *buff_rec++ = u64Data >> 32;
                }
                if (ipr_status == 1)
                {
                    dwAddrSpace = 2;
                    u64Data = 0;
                    dwOffset = t1_cs_reg; //0x18
                    WDC_ReadAddr64(hDev, dwAddrSpace, dwOffset, &u64Data);
                    std::cout << std::hex;
                    std::cout << "status word after read = " << (u64Data >> 32) << ", " << (u64Data & 0xffff) << std::endl;
                    std::cout << std::dec;
                    // printf("printf status word after read = %x, %x \n", (u64Data >> 32), (u64Data & 0xffff));
                }
                return 0;
            }
            if (mode == 1)
            {
                nread = nword * 4;
                WDC_DMASyncCpu(pDma_rec);

                /* set up sending DMA starting address */

                dwAddrSpace = 2;
                u32Data = cs_init; //0x20000000;
                dwOffset = cs_dma_add_low_reg; //0x0
                u32Data = pDma_rec->Page->pPhysicalAddr & 0xffffffff;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

                dwAddrSpace = 2;
                u32Data = cs_init; //0x20000000;
                dwOffset = cs_dma_add_high_reg; //0x4
                u32Data = (pDma_rec->Page->pPhysicalAddr >> 32) & 0xffffffff;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

                /* byte count */
                dwAddrSpace = 2;
                dwOffset = cs_dma_by_cnt; //0x8
                u32Data = nread;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

                /* write this will start DMA */
                dwAddrSpace = 2;
                dwOffset = cs_dma_cntrl; //0xc
                u32Data = dma_tr1 + dma_3dw_rec; //0x00100040;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
                icomp = 0;
                for (i = 0; i < 20000; i++)
                {
                    dwAddrSpace = 2;
                    dwOffset = cs_dma_cntrl; //0xC;
                    WDC_ReadAddr32(hDev, dwAddrSpace, dwOffset, &u32Data);
                    if (iprint == 1) std::cout << " DMA status read: " << i << " " << u32Data << std::endl;
                    if (((u32Data & dma_in_progress) == 0))
                    {
                        icomp = 1;
                        if (iprint == 1) std::cout << " DMA complete " << i << std::endl;
                    }
                    if ((u32Data & dma_in_progress) == 0)
                        break;
                }
                if (icomp == 0)
                {
                    std::cout <<"DMA timeout" << std::endl;
                    return 1;
                }
                WDC_DMASyncIo(pDma_rec);
                for (i = 0; i < nword; i++)
                {
                    *buff_rec++ = *(buf_rec + i);
                }
            }
        }
        return 0;
    }

} // pcie_int