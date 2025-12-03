//
// Created by Jon Sensenig on 1/29/25.
//

#include "pcie_interface.h"
#include <iostream>

#include "gramsreadout_lib.h"

namespace pcie_int {

    std::array<uint32_t, PcieBuffers::SEND_SIZE> PcieBuffers::send_array = {};
    std::array<uint32_t, PcieBuffers::READ_SIZE> PcieBuffers::read_array = {};

    struct DmaBuffStruct {
        WD_DMA *dma_buff;
    };

    PCIeInterface::PCIeInterface() : buffer_info_struct_send_(std::make_unique<DmaBuffStruct>()),
                                     buffer_info_struct_recv_(std::make_unique<DmaBuffStruct>()),
                                     buffer_info_struct1(std::make_unique<DmaBuffStruct>()),
                                     buffer_info_struct2(std::make_unique<DmaBuffStruct>()),
                                     buffer_info_struct_trig(std::make_unique<DmaBuffStruct>()) {
        dev_handle_1 = nullptr;
        dev_handle_2 = nullptr;
        hDev = nullptr;
        is_initialized_ = false;
    }

    PCIeInterface::~PCIeInterface() {
        std::cout << "Closing PCIe Devices..." << std::endl;
        std::cout << "Closing DMA Buffers..." << std::endl;
        std::cout << std::hex;
        std::cout << "Dev1: " << dev_handle_1 << std::endl;
        std::cout << "Dev2: " << dev_handle_2 << std::endl;
        std::cout << "Buf1: " << buffer_info_struct1->dma_buff << std::endl;
        std::cout << "Buf2: " << buffer_info_struct2->dma_buff << std::endl;
        std::cout << "Trig Buf: " << buffer_info_struct_trig->dma_buff << std::endl;
        std::cout << std::dec;

        // Make sure the DMA buffer memory is free
        // FreeDmaContigBuffers(); // moved to DataHandler

        if (buffer_info_struct_send_->dma_buff) {
            std::cout << "Freeing Send DMA buffer.." << std::endl;
            if(GRAMSREADOUT_DmaBufUnlock(buffer_info_struct_send_->dma_buff) != WD_STATUS_SUCCESS) {
                std::cerr << "DMA SEND Buffer close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            }
            buffer_info_struct_send_->dma_buff = nullptr;
        }
        if (buffer_info_struct_recv_->dma_buff) {
            std::cout << "Freeing Receive DMA buffer.." << std::endl;
            if(GRAMSREADOUT_DmaBufUnlock(buffer_info_struct_recv_->dma_buff) != WD_STATUS_SUCCESS) {
                std::cerr << "DMA RECV Buffer close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            }
            buffer_info_struct_recv_->dma_buff = nullptr;
        }

        if (dev_handle_1) {
            std::cout << "Freeing Dev Handle 1.." << std::endl;
            if (!GRAMSREADOUT_DeviceClose(dev_handle_1)) {
                std::cerr << "Device 1 close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            }
        }

        if (dev_handle_2) {
            std::cout << "Freeing Dev Handle 2.." << std::endl;
            if (!GRAMSREADOUT_DeviceClose(dev_handle_2)) {
                std::cerr << "Device 2 close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            }
        }

        // Uninitialize the Windriver library but ONLY if it is already initialized.
        // If not we mess up WinDriver's library instance tracking
        if (is_initialized_) {
            std::cout << "Uninitializing Driver Library.." << std::endl;
            if (WD_STATUS_SUCCESS != GRAMSREADOUT_LibUninit()) {
                std::cerr << "GRAMSREADOUT_LibUninit() failed:" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            }
        }

        is_initialized_ = false;
    }

    uint32_t PCIeInterface::InitPCIeDevices(uint32_t dev1, uint32_t dev2, int slot_id_0, int slot_id_1) {

        // If the PCIe hardware is already initialized we don't, and should not, initialize it again!
        if (is_initialized_) {
            std::cout << "PCIe devices already initialized, skipping.. " << std::endl;
            return true;
        }
        /* Initialize the GRAMSREADOUT library */
        DWORD dwStatus = GRAMSREADOUT_LibInit();
        if (WD_STATUS_SUCCESS != dwStatus) {
            std::cerr << "GRAMSREADOUT_LibInit() failed!" << std::endl;
            std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            return 0x4;
        }

        constexpr uint32_t vendor_id = GRAMSREADOUT_DEFAULT_VENDOR_ID;

        dev_handle_1 = GRAMSREADOUT_DeviceOpen(vendor_id, dev1, slot_id_0);
        dev_handle_2 = GRAMSREADOUT_DeviceOpen(vendor_id, dev2, slot_id_1);

        if (!dev_handle_1 || !dev_handle_2) {
            std::cout << "Dev Handle 1: " << dev_handle_1 << " Dev Handle 2: " << dev_handle_2 << std::endl;
            std::cout << "Addr: Dev Handle 1: " << &dev_handle_1 << " Dev Handle 2: " << &dev_handle_2 << std::endl;
            std::cerr << "Failed to open PCIe devices.." << std::endl;
            std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
            if (dev_handle_1) {
                GRAMSREADOUT_DeviceClose(dev_handle_1);
                dev_handle_1 = nullptr;
            }
            if (dev_handle_2) {
                GRAMSREADOUT_DeviceClose(dev_handle_2);
                dev_handle_2 = nullptr;
            }
            GRAMSREADOUT_LibUninit();
            return 0x5;
        }

        std::cout << std::hex;
        std::cout << "Dev Handle 1: " << dev_handle_1 << " Dev Handle 2: " << dev_handle_2 << std::endl;
        std::cout << "Addr: Dev Handle 1: " << &dev_handle_1 << " Dev Handle 2: " << &dev_handle_2 << std::endl;
        std::cout << std::dec;

        // Open the buffer for the DMAs communication
        dwStatus = WDC_DMAContigBufLock(dev_handle_1, &pbuf_send_, DMA_TO_DEVICE,
                            CONFIGDMABUFFSIZE, &buffer_info_struct_send_->dma_buff);
        if (WD_STATUS_SUCCESS != dwStatus) {
            printf("Failed locking SEND Contiguous DMA buffer. Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
            return 0x6;
        }
        dwStatus = WDC_DMAContigBufLock(dev_handle_1, &pbuf_recv_, DMA_FROM_DEVICE,
                            CONFIGDMABUFFSIZE, &buffer_info_struct_recv_->dma_buff);
        if (WD_STATUS_SUCCESS != dwStatus) {
            printf("Failed locking RECEIVE Contiguous DMA buffer. Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
            return 0x6;
        }
        std::cout << "Opened configuration send/receive DMA buffers.." << std::endl;
        buffer_send_ = static_cast<uint32_t*>(pbuf_send_);
        buffer_recv_ = static_cast<uint32_t *>(pbuf_recv_);

        // Initialize the send and receive buffers
        for (size_t i = 0; i < CONFIGDMABUFFSIZE/sizeof(uint32_t); i++) {
            *(buffer_send_ + i) = 0x0;
            *(buffer_recv_ + i) = 0x0;
        }

        is_initialized_ = true;
        return 0x0;
    }

    void PCIeInterface::ReadReg32(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint32_t *data) {
        EnforceDeadTime();
        // Apply a mutex to make sure we dont get multiple accesses to the underlying hardware
        std::unique_lock<std::mutex> lock(handle_mutex);
        hDev = GetDeviceHandle(dev_handle);
        uint32_t read_status = WDC_ReadAddr32(hDev, addr_space, adr_offset, data);
        lock.unlock(); // make sure the mutex is unlocked to allow other users
        if (WD_STATUS_SUCCESS != read_status) std::cerr << "ReadReg32() failed" << std::endl;
    }

    bool PCIeInterface::WriteReg32(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint32_t data) {
        EnforceDeadTime();
        // Apply a mutex to make sure we dont get multiple accesses to the underlying hardware
        std::unique_lock<std::mutex> lock(handle_mutex);
        hDev = GetDeviceHandle(dev_handle);
        uint32_t write_status = WDC_WriteAddr32(hDev, addr_space, adr_offset, data);
        lock.unlock(); // make sure the mutex is unlocked to allow other users
        return write_status == WD_STATUS_SUCCESS;
    }

    void PCIeInterface::ReadReg64(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, unsigned long long *data) {
        EnforceDeadTime();
        // Apply a mutex to make sure we dont get multiple accesses to the underlying hardware
        std::unique_lock<std::mutex> lock(handle_mutex);
        hDev = GetDeviceHandle(dev_handle);
        uint32_t read_status = WDC_ReadAddr64(hDev, addr_space, adr_offset, data);
        lock.unlock(); // make sure the mutex is unlocked to allow other users
        if (WD_STATUS_SUCCESS != read_status) std::cerr << "ReadReg64() failed" << std::endl;
    }

    bool PCIeInterface::WriteReg64(uint32_t dev_handle, uint32_t addr_space, uint32_t adr_offset, uint64_t data) {
        EnforceDeadTime();
        std::unique_lock<std::mutex> lock(handle_mutex);
        // Apply a mutex to make sure we dont get multiple accesses to the underlying hardware
        hDev = GetDeviceHandle(dev_handle);
        uint32_t write_status = WDC_WriteAddr64(hDev, addr_space, adr_offset, data);
        lock.unlock(); // make sure the mutex is unlocked to allow other users
        return write_status == WD_STATUS_SUCCESS;
    }

    bool PCIeInterface::PCIeDeviceConfigure() {
        static DWORD dwAddrSpace;
        static DWORD dwOffset;
        static UINT32 u32Data;
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
        return wr_stat == WD_STATUS_SUCCESS;
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
        // Apply a mutex to make sure we dont get multiple accesses to the underlying hardware
        EnforceDeadTime();
        std::unique_lock<std::mutex> lock(handle_mutex);

        /* imode =0 single word transfer, imode =1 DMA */
        hDev = GetDeviceHandle(dev);

        static DWORD dwAddrSpace;
        static DWORD dwOffset;
        static UINT32 u32Data;
        uint32_t nwrite;
        uint32_t iprint = 0;
        uint32_t i = 0;
        uint32_t j = 0;
        struct timespec req;
        req.tv_sec = 0; // No whole seconds for short delays
        req.tv_nsec = 25;

        if (mode == 1) {
            for (i = 0; i < nword; i++) {
                *(buffer_send_ + i) = *(buff_send + i);
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
                u32Data = *(buff_send + j);
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
            // WDC_DMASyncCpu(pDma_send);
            WDC_DMASyncCpu(buffer_info_struct_send_->dma_buff);
            /*setup transmitter */
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
            u32Data = buffer_info_struct_send_->dma_buff->Page->pPhysicalAddr & 0xffffffff;
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000;
            dwOffset = cs_dma_add_high_reg; //0x4
            u32Data = (buffer_info_struct_send_->dma_buff->Page->pPhysicalAddr >> 32) & 0xffffffff;
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
            // works in standalone script
            //usleep(50);
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
                nanosleep(&req, NULL);
            }
            WDC_DMASyncIo(buffer_info_struct_send_->dma_buff);
        }
        lock.unlock(); // make sure the mutex is unlocked to allow other users
        return i;
    }

    uint32_t PCIeInterface::PCIeRecvBuffer(uint32_t dev, uint32_t mode, uint32_t istart, uint32_t nword, uint32_t ipr_status, uint32_t *buff_rec) {
        /* imode =0 single word transfer, imode =1 DMA */
        // Apply a mutex to make sure we dont get multiple accesses to the underlying hardware
        EnforceDeadTime();
        std::unique_lock<std::mutex> lock(handle_mutex);

        hDev = GetDeviceHandle(dev);

        static DWORD dwAddrSpace;
        static DWORD dwOffset;
        static UINT32 u32Data;
        static UINT64 u64Data;
        uint32_t nread, i, j, icomp;
        uint32_t iprint = 0;

        /** set up the receiver **/
        if ((istart == 1) | (istart == 3))
        {
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
            }
            lock.unlock(); // make sure the mutex is unlocked to allow other users
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
                    *(buff_rec + j) = (u64Data & 0xffffffff);
                    *(buff_rec + j + 1) = u64Data >> 32;
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
                }
                lock.unlock(); // make sure the mutex is unlocked to allow other users
                return 0;
            }
            if (mode == 1)
            {
                nread = nword * 4;
                WDC_DMASyncCpu(buffer_info_struct_recv_->dma_buff);

                /* set up sending DMA starting address */

                dwAddrSpace = 2;
                u32Data = cs_init; //0x20000000;
                dwOffset = cs_dma_add_low_reg; //0x0
                u32Data = buffer_info_struct_recv_->dma_buff->Page->pPhysicalAddr & 0xffffffff;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

                dwAddrSpace = 2;
                u32Data = cs_init; //0x20000000;
                dwOffset = cs_dma_add_high_reg; //0x4
                u32Data = (buffer_info_struct_recv_->dma_buff->Page->pPhysicalAddr >> 32) & 0xffffffff;
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
                    lock.unlock(); // make sure the mutex is unlocked to allow other users
                    return 1;
                }
                WDC_DMASyncIo(buffer_info_struct_recv_->dma_buff);
                for (i = 0; i < nword; i++)
                {
                    *buff_rec++ = *(buffer_recv_ + i);
                }
            }
        }
        lock.unlock(); // make sure the mutex is unlocked to allow other users
        return 0;
    }

    bool PCIeInterface::DmaContigBufferLock(uint32_t dev_handle, uint32_t dwDMABufSize, DMABufferHandle *pbuf_rec) {
        static DWORD dwStatus;
        static int is;

        DWORD dwOptions_rec = DMA_FROM_DEVICE | DMA_ALLOW_64BIT_ADDRESS;

        if (dev_handle == 1) {
            dwStatus = WDC_DMAContigBufLock(dev_handle_2, pbuf_rec, dwOptions_rec, dwDMABufSize,
                                            &buffer_info_struct1->dma_buff);
        } else if (dev_handle == 2) {
            dwStatus = WDC_DMAContigBufLock(dev_handle_2, pbuf_rec, dwOptions_rec, dwDMABufSize,
                                            &buffer_info_struct2->dma_buff);
        } else if (dev_handle == 3) {
            // Apply a mutex to make sure we dont get multiple accesses to the underlying hardware
            std::unique_lock<std::mutex> lock(handle_mutex);
            dwStatus = WDC_DMAContigBufLock(dev_handle_1, pbuf_rec, dwOptions_rec, dwDMABufSize,
                                            &buffer_info_struct_trig->dma_buff);
            lock.unlock();
        } else {
            std::cerr << "Unknown dev handle!" << std::endl;
            return false;
        }

        // FIXME handle this error automatically
        if (WD_STATUS_SUCCESS != dwStatus) {
            printf("Failed locking recv Contiguous DMA buffer. Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
            printf("enter 1 to continue \n");
            scanf("%d", &is);
            if (is != 1) return false;
        }
        std::cout << std::hex;
        std::cout << "Pointer of DMA recv buffer " << dev_handle << ": " << *pbuf_rec << std::endl;
        std::cout << "buffer_info_struct1->dma_buff: " << buffer_info_struct1->dma_buff << std::endl;
        std::cout << "buffer_info_struct2->dma_buff: " << buffer_info_struct2->dma_buff << std::endl;
        std::cout << "buffer_info_struct_trig->dma_buff: " << buffer_info_struct_trig->dma_buff << std::endl;
        std::cout << std::dec;

        return true;
    }

    bool PCIeInterface::FreeDmaContigBuffers() {
        bool buffers_free = true;
        if (buffer_info_struct1->dma_buff) {
            if(GRAMSREADOUT_DmaBufUnlock(buffer_info_struct1->dma_buff) != WD_STATUS_SUCCESS) {
                std::cerr << "DMA Buffer 1 close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
                buffers_free = false;
            }
            buffer_info_struct1->dma_buff = nullptr;
        }

        if (buffer_info_struct2->dma_buff) {
            if(GRAMSREADOUT_DmaBufUnlock(buffer_info_struct2->dma_buff) != WD_STATUS_SUCCESS) {
                std::cerr << "DMA Buffer 2 close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
                buffers_free = false;
            }
            buffer_info_struct2->dma_buff = nullptr;
        }

        if (buffer_info_struct_trig->dma_buff) {
            if(GRAMSREADOUT_DmaBufUnlock(buffer_info_struct_trig->dma_buff) != WD_STATUS_SUCCESS) {
                std::cerr << "DMA Trig Buffer close failed" << std::endl;
                std::cerr << std::string(GRAMSREADOUT_GetLastErr()) << std::endl;
                buffers_free = false;
            }
            buffer_info_struct_trig->dma_buff = nullptr;
        }
        return buffers_free;
    }

    bool PCIeInterface::DmaSyncCpu(uint32_t buffer_handle) {

        uint32_t status = 1;
        if (buffer_handle == 1)      status = WDC_DMASyncCpu(buffer_info_struct1->dma_buff);
        else if (buffer_handle == 2) status = WDC_DMASyncCpu(buffer_info_struct2->dma_buff);
        else if (buffer_handle == 3) status = WDC_DMASyncCpu(buffer_info_struct_trig->dma_buff);

        if (status != WD_STATUS_SUCCESS) {
            std::cerr << "DMA Sync failed for buffer: " << buffer_handle << std::endl;
            return false;
        }
        return true;
    }

    bool PCIeInterface::DmaSyncIo(uint32_t buffer_handle) {

        uint32_t status = 1;
        if (buffer_handle == 1)      status = WDC_DMASyncIo(buffer_info_struct1->dma_buff);
        else if (buffer_handle == 2) status = WDC_DMASyncIo(buffer_info_struct2->dma_buff);
        else if (buffer_handle == 3) status = WDC_DMASyncIo(buffer_info_struct_trig->dma_buff);

        if (status != WD_STATUS_SUCCESS) {
            std::cerr << "DMA Sync failed for buffer: " << buffer_handle << std::endl;
            return false;
        }
        return true;
    }

    uint32_t PCIeInterface::GetBufferPageAddrUpper(uint32_t buffer_handle) {
        if (buffer_handle == 1) return (buffer_info_struct1->dma_buff->Page->pPhysicalAddr >> 32) & 0xffffffff;
        if (buffer_handle == 2) return (buffer_info_struct2->dma_buff->Page->pPhysicalAddr >> 32) & 0xffffffff;
        if (buffer_handle == 3) return (buffer_info_struct_trig->dma_buff->Page->pPhysicalAddr >> 32) & 0xffffffff;
        std::cerr << "Unknown dev handle!" << std::endl;
        return false;
    }

    uint32_t PCIeInterface::GetBufferPageAddrLower(uint32_t buffer_handle) {
        if (buffer_handle == 1) return buffer_info_struct1->dma_buff->Page->pPhysicalAddr & 0xffffffff;
        if (buffer_handle == 2) return buffer_info_struct2->dma_buff->Page->pPhysicalAddr & 0xffffffff;
        if (buffer_handle == 3) return buffer_info_struct_trig->dma_buff->Page->pPhysicalAddr & 0xffffffff;
        std::cerr << "Unknown dev handle!" << std::endl;
        return false;
    }

} // pcie_int
