//
// Created by Jon Sensenig on 1/29/25.
//

#include <iostream>
#include "pcie_interface.h"
#include "gramsreadout_lib.h"

namespace pcie_int {

    PCIeInterface::PCIeInterface() {
        dev_handle_1 = nullptr;
        dev_handle_2 = nullptr;
        hDev = nullptr;
        is_initialized = false;
    }

    PCIeInterface::~PCIeInterface() {
        if (!GRAMSREADOUT_DeviceClose(dev_handle_1)) {
            std::cerr << "Device 1 close failed" << std::endl;
        }

        if (!GRAMSREADOUT_DeviceClose(dev_handle_2)) {
            std::cerr << "Device 2 close failed" << std::endl;
        }
    }

    bool PCIeInterface::InitPCIeDevices(uint32_t dev1, uint32_t dev2) {

        uint32_t vendor_id = GRAMSREADOUT_DEFAULT_VENDOR_ID;

        dev_handle_1 = GRAMSREADOUT_DeviceOpen(vendor_id, dev1);
        dev_handle_2 = GRAMSREADOUT_DeviceOpen(vendor_id, dev2);

        if (!dev_handle_1 || !dev_handle_2) { return false; }

        is_initialized = PCIeDeviceConfigure();

        return is_initialized;
    }

    bool PCIeInterface::PCIeDeviceConfigure() {
        static DWORD dwAddrSpace;
        static DWORD dwOffset;
        static UINT32 u32Data;
        DWORD wr_stat = 0;

        dwAddrSpace = 2;
        u32Data = 0x20000000; // initial transmitter, no hold
        dwOffset = t1_cs_reg; //0x18;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = 0x20000000; // initial transmitter, no hold
        dwOffset = t2_cs_reg; //0x20;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = 0x20000000; // initial receiver
        dwOffset = r1_cs_reg; //0x1c;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = 0x20000000; // initial receiver
        dwOffset = r2_cs_reg; //0x24;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        dwAddrSpace = 2;
        u32Data = 0xfff; // set mode off with 0xfff...
        dwOffset = tx_md_reg; //0x28;
        wr_stat += WDC_WriteAddr32(dev_handle_2, dwAddrSpace, dwOffset, u32Data);

        // If all writes succeed it should be 0
        return wr_stat > 0;
    }


    int PCIeInterface::PCIeSendBuffer(const uint32_t dev, int mode, int nword, uint32_t *buff_send) {
        /* imode =0 single word transfer, imode =1 DMA */
        if (dev == 1) {
            hDev = dev_handle_1;
        }
        else if (dev == 2) {
            hDev = dev_handle_2;
        }
        else {
            std::cerr << "PCIeInterface::PCIeSendBuffer: Invalid device handle: " << dev << std::endl;
            return 0;
        }

        static DWORD dwAddrSpace;
        static DWORD dwDMABufSize;

        static UINT32 *buf_send;
        static WD_DMA *pDma_send;
        static DWORD dwStatus;
        static DWORD dwOptions_send = DMA_TO_DEVICE;
        static DWORD dwOffset;
        static UINT32 u32Data;
        static PVOID pbuf_send;
        int nwrite, iprint;
        int i = 0;
        int j = 0;
        static int ifr = 0;

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
            buf_send = (uint32_t *)pbuf_send; //FIXME c++ style cast
        }

        if (mode == 1)
        {
            for (i = 0; i < nword; i++)
            {
                *(buf_send + i) = *buff_send++;
                // std::cout << *(buf_send + i) << std::endl;
            }
        }
        if (mode == 0)
        {
            nwrite = nword * 4;
            /*setup transmiiter */
            dwAddrSpace = 2;
            u32Data = 0x20000000;
            dwOffset = t1_cs_reg; // 0x18
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            dwAddrSpace = 2;
            u32Data = 0x40000000 + nwrite;
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
                if (((u32Data & 0x80000000) == 0) && iprint == 1)
                    std::cout << "Data transfer complete: " << i << std::endl;
                if ((u32Data & 0x80000000) == 0)
                    break;
            }
        }
        if (mode == 1)
        {
            nwrite = nword * 4;
            WDC_DMASyncCpu(pDma_send);
            /*
        printf(" nwrite = %d \n", nwrite);
        printf(" pcie_send hDev = %d\n", hDev);
        printf(" buf_send = %X\n",*buf_send);
            */
            /*setup transmiiter */
            dwAddrSpace = 2;
            u32Data = 0x20000000;
            dwOffset = t1_cs_reg; //0x18
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            dwAddrSpace = 2;
            u32Data = 0x40000000 + nwrite;
            dwOffset = t1_cs_reg; //0x18
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            /* set up sending DMA starting address */

            dwAddrSpace = 2;
            u32Data = 0x20000000;
            dwOffset = cs_dma_add_low_reg; //0x0
            u32Data = pDma_send->Page->pPhysicalAddr & 0xffffffff;
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            dwAddrSpace = 2;
            u32Data = 0x20000000;
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
            u32Data = 0x00100000;
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            usleep(400);
            for (i = 0; i < 20000; i++)
                //for (i = 0; i < 100; i++)
            {
                dwAddrSpace = 2;
                dwOffset = cs_dma_cntrl; //0xC
                WDC_ReadAddr32(hDev, dwAddrSpace, dwOffset, &u32Data);
                if (iprint == 1)
                    std::cout << "DMA status read: " << i << " " << u32Data << std::endl;
                if (((u32Data & 0x80000000) == 0) && iprint == 1)
                    std::cout << "DMA complete: " << i << std::endl;
                if ((u32Data & 0x80000000) == 0)
                    break;
                // continue;
            }
            WDC_DMASyncIo(pDma_send);
        }
        return i;
    }

    int PCIeInterface::PCIeRecvBuffer(uint32_t dev, int mode, int istart, int nword, int ipr_status, uint32_t *buff_rec) {
        /* imode =0 single word transfer, imode =1 DMA */
        if (dev == 1) {
            hDev = dev_handle_1;
        }
        else if (dev == 2) {
            hDev = dev_handle_2;
        }
        else {
            std::cerr << "PCIeInterface::PCIeSendBuffer: Invalid device handle: " << dev << std::endl;
            return 0;
        }

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
        int nread, i, j, iprint, icomp;
        static int ifr = 0;

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
            buf_rec = (uint32_t *)pbuf_rec; //FIXME c++ style cast
        }
        iprint = 0;
        //    printf(" istart = %d\n", istart);
        //   printf(" mode   = %d\n", mode);
        /** set up the receiver **/
        if ((istart == 1) | (istart == 3))
        {
            // initalize transmitter mode register...
            std::cout << "nword = " << nword << std::endl;
            /*
                 if(ipr_status ==1) {
                  dwAddrSpace =2;
                  u64Data =0;
                  dwOffset = 0x18;
                  WDC_ReadAddr64(hDev, dwAddrSpace, dwOffset, &u64Data);
                  printf (" status word before set = %x, %x \n",(u64Data>>32), (u64Data &0xffff));
                 }
            */
            dwAddrSpace = 2;
            u32Data = 0xf0000008;
            dwOffset = tx_md_reg; //0x28
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

            /*initialize the receiver */
            dwAddrSpace = 2;
            u32Data = 0x20000000;
            dwOffset = r1_cs_reg; //0x1c
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            /* write byte count **/
            dwAddrSpace = 2;
            u32Data = 0x40000000 + nword * 4;
            dwOffset = r1_cs_reg; //0x1c
            WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
            if (ipr_status == 1)
            {
                dwAddrSpace = 2;
                u64Data = 0;
                dwOffset = t1_cs_reg; //0x18
                WDC_ReadAddr64(hDev, dwAddrSpace, dwOffset, &u64Data);
                std::cout << "status word before read = "
                << std::hex << (u64Data >> 32) << ", " << (u64Data & 0xffff) << std::endl;
            }

            return 0;
        }
        if ((istart == 2) | (istart == 3))
        {
            //     if(ipr_status ==1) {
            //      dwAddrSpace =2;
            //      u64Data =0;
            //      dwOffset = 0x18;
            //      WDC_ReadAddr64(hDev, dwAddrSpace, dwOffset, &u64Data);
            //      printf (" status word before read = %x, %x \n",(u64Data>>32), (u64Data &0xffff));
            //     }
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
                    //       printf("u64Data = %16X\n",u64Data);
                    *buff_rec++ = (u64Data & 0xffffffff);
                    *buff_rec++ = u64Data >> 32;
                    //       printf("%x \n",(u64Data &0xffffffff));
                    //       printf("%x \n",(u64Data >>32 ));
                    //       if(j*2+1 > nword) *buff_rec++ = (u64Data)>>32;
                    //       *buff_rec++ = 0x0;
                }
                if (ipr_status == 1)
                {
                    dwAddrSpace = 2;
                    u64Data = 0;
                    dwOffset = t1_cs_reg; //0x18
                    WDC_ReadAddr64(hDev, dwAddrSpace, dwOffset, &u64Data);
                    std::cout << "status word after read = "
                << std::hex << (u64Data >> 32) << ", " << (u64Data & 0xffff) << std::endl;
                }
                return 0;
            }
            if (mode == 1)
            {
                nread = nword * 4;
                WDC_DMASyncCpu(pDma_rec);
                /*
                      printf(" nwrite = %d \n", nwrite);
                      printf(" pcie_send hDev = %d\n", hDev);
                      printf(" buf_send = %X\n",*buf_send);
                */
                /*setup receiver
                      dwAddrSpace =2;
                      u32Data = 0x20000000;
                      dwOffset = 0x1c;
                      WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
                      dwAddrSpace =2;
                      u32Data = 0x40000000+nread;
                      dwOffset = 0x1c;
                      WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
                */
                /* set up sending DMA starting address */

                dwAddrSpace = 2;
                u32Data = 0x20000000;
                dwOffset = cs_dma_add_low_reg; //0x0
                u32Data = pDma_rec->Page->pPhysicalAddr & 0xffffffff;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);

                dwAddrSpace = 2;
                u32Data = 0x20000000;
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
                u32Data = 0x00100040;
                WDC_WriteAddr32(hDev, dwAddrSpace, dwOffset, u32Data);
                icomp = 0;
                for (i = 0; i < 20000; i++)
                {
                    dwAddrSpace = 2;
                    dwOffset = cs_dma_cntrl; //0xC;
                    WDC_ReadAddr32(hDev, dwAddrSpace, dwOffset, &u32Data);
                    if (iprint == 1)
                        std::cout << " DMA status read: " << i << " " << u32Data << std::endl;
                    if (((u32Data & 0x80000000) == 0))
                    {
                        icomp = 1;
                        if (iprint == 1)
                            std::cout << " DMA complete " << i << std::endl;
                    }
                    if ((u32Data & 0x80000000) == 0)
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
                    // std::cout << *(buff_rec+i) << std::endl;
                }
            }
        }
        return 0;
    }

} // pcie_int