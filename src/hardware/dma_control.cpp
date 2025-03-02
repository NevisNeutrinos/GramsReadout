//
// Created by Jon Sensenig on 2/24/25.
//

#include "dma_control.h"
#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "trigger_control.h"

#include "quill/LogMacros.h"

namespace dma_control {

    DmaControl::DmaControl() {
        logger_ = quill::Frontend::create_or_get_logger("root",
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));
    }

    bool DmaControl::SetRecvBuffer(pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2) {

        static uint32_t data_32_;
        static uint32_t addr_space_ = hw_consts::cs_bar;

        // first dma
        LOG_INFO(logger_, "\n First DMA .. \n");
        LOG_INFO(logger_, "Buffer 1 & 2 allocation size: {} \n", dma_buf_size_);

        pcie_interface->DmaContigBufferLock(1, dma_buf_size_, pbuf_rec1);
        pcie_interface->DmaContigBufferLock(2, dma_buf_size_, pbuf_rec2);

        /* set tx mode register */
        data_32_ = 0x00002000;
        pcie_interface->WriteReg32(kDev2, addr_space_, hw_consts::tx_md_reg, data_32_);

        /* write this will abort previous DMA */
        data_32_ = hw_consts::dma_abort;
        pcie_interface->WriteReg32(kDev2, addr_space_, hw_consts::cs_dma_msi_abort, data_32_);

        /* clear DMA register after the abort */
        data_32_ = 0;
        pcie_interface->WriteReg32(kDev2, addr_space_, hw_consts::cs_dma_msi_abort, data_32_);

        // /** initialize the receiver ***/
        // for (size_t is = 1; is < 3; is++) {
        //     static int r_cs_reg = is == 1 ? hw_consts::r1_cs_reg : hw_consts::r2_cs_reg;
        //     pcie_interface->WriteReg32(kDev2,  hw_consts::cs_bar, r_cs_reg, hw_consts::cs_init);
        // }
        return true;
    }


     bool DmaControl::Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {

        bool idebug = false;
        static uint32_t nevent, iv, ijk;
        static int ndma_loop;
        static int irawprint = 0;
        static int nwrite_byte;
        static uint32_t ifr, iwrite, ik, is;
        static int idone, r_cs_reg;
        static int ntot_rec, nred;
        static int itrig_c = 0;
        static uint32_t iwritem, nwrite;
        static int ibytec, fd, itrig_ext;

        std::string name;
        std::string subrun;

        uint32_t data;
        static unsigned long long u64Data;
        static uint32_t *buffp_rec32;
        static int n_write;

        pcie_int::DMABufferHandle  pbuf_rec1;
        pcie_int::DMABufferHandle pbuf_rec2;

        itrig_ext = 1;
        iwrite = 1;

        LOG_INFO(logger_, "\n Enter desired DMA size (<{}) \t", dma_buf_size_);
        std::cin >> ibytec;
        dma_buf_size_ = ibytec;

        LOG_INFO(logger_, "Enter number of events to read out and save:\n");
        std::cin >> nevent;

        LOG_INFO(logger_, "\nEnter 1 to set the RUN on \n");
        std::cin >> ik;
        ndma_loop = 1;
        ifr = 0;

        LOG_INFO(logger_, "DMA will run {} loop(s) per event\n", (ndma_loop + 1));
        LOG_INFO(logger_, "\t {} loops with {} words and {} loop with {} words\n\n", ndma_loop, dma_buf_size_ / 4, 1,
                                                                                dma_buf_size_ / 4);

        if (iwrite == 1) {
            iwritem = 0; // grams
            LOG_INFO(logger_, "\n ######## SiPM+TPC Readout \n Enter SUBRUN NUMBER or NAME:\t");
            std::cin >> subrun;

            name = "data/pGRAMS_bin_" + std::string(subrun) + ".dat";
            mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // Permissions: rw-r--r--
            fd = open(name.data(), O_WRONLY | O_CREAT | O_TRUNC, mode);
            if (fd == -1) {
                LOG_ERROR(logger_, "Failed to open file {} aborting run! \n", name);
                return false;
            }
            LOG_INFO(logger_, "\n Output file: {}", name);
        }

         /*TPC DMA*/
        SetRecvBuffer(pcie_interface, &pbuf_rec1, &pbuf_rec2);

        for (ijk = 0; ijk < nevent; ijk++) {
            if (ijk % 10 == 0) LOG_INFO(logger_, " \n ===================> Event No. [{}] \n", ijk);
            ntot_rec = 0; // keeps track of number of words received in event
            for (iv = 0; iv < (ndma_loop + 1); iv++) { // note: ndma_loop=1
                static uint32_t dma_num = (iv % 2) == 0 ? 1 : 2;
                buffp_rec32 = (iv % 2) == 0 ? static_cast<uint32_t *>(pbuf_rec1) : static_cast<uint32_t *>(pbuf_rec2);

                /* sync CPU cache */
                pcie_interface->DmaSyncCpu(dma_num);

                nwrite_byte = dma_buf_size_;
                if (idebug) LOG_INFO(logger_, "DMA loop {} with DMA length {}B \n", iv, nwrite_byte);

                for (is = 1; is < 3; is++) {
                    r_cs_reg = is == 1 ? hw_consts::r1_cs_reg : hw_consts::r2_cs_reg;
                    if (ifr == 0) {
                        pcie_interface->WriteReg32(kDev2,  hw_consts::cs_bar, r_cs_reg, hw_consts::cs_init);
                    }
                    /** start the receiver **/
                    data = hw_consts::cs_start + nwrite_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
                    pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, r_cs_reg, data);
                }

                ifr = 1;

                /** set up DMA for both transceiver together **/
                data = pcie_interface->GetBufferPageAddrLower(dma_num);
                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_low_reg, data);

                data = pcie_interface->GetBufferPageAddrUpper(dma_num);
                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_high_reg, data);

                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, nwrite_byte);

                /* write this will start DMA */
                is = pcie_interface->GetBufferPageAddrUpper(dma_num);
                data = is == 0 ? hw_consts::dma_tr12 + hw_consts::dma_3dw_rec : hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;

                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
                if (idebug) LOG_INFO(logger_, "DMA set up done, byte count = {} \n", nwrite_byte);

                // send trigger
                if (iv == 0) trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, buffers, itrig_c, itrig_ext);

                // extra wait just to be certain --- WK
                // usleep(20000);
                /***    check to see if DMA is done or not **/
                idone = 0;
                for (is = 0; is < 6000000000; is++) {
                    u64Data = 0;
                    pcie_interface->ReadReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, &data);
                    if ((data & hw_consts::dma_in_progress) == 0) {
                        idone = 1;
                        if (idebug) LOG_INFO(logger_, "Receive DMA complete...  (iter={}) \n", is);
                        break;
                    }
                }
                if (idone == 0) {
                    LOG_INFO(logger_, " loop [{}] DMA is not finished, aborting...  \n", iv);
                    pcie_interface->ReadReg64(kDev2,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
                    pcie_interface->DmaSyncIo(dma_num);
                    nred = (nwrite_byte - (u64Data & 0xffff)) / 4;

                    ntot_rec = ntot_rec + nred;
                    if (iwrite == 1) {
                        for (is = 0; is < nred; is++) {
                            pcie_int::PcieBuffers::read_array[is] = *buffp_rec32++;
                            LOG_INFO(logger_, "{%x} \n", pcie_int::PcieBuffers::read_array[is]);
                        }
                        n_write = write(fd, pcie_int::PcieBuffers::read_array.data(), nred * 4);
                    }
                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

                    /* write this will abort previous DMA */
                    data = hw_consts::dma_abort;
                    pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data);

                    /* clear DMA register after the abort */
                    data = 0;
                    pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data);

                    // If DMA did not finish, abort loop!
                    break;
                }

                /* synch DMA i/O cache **/
                pcie_interface->DmaSyncIo(dma_num);

                if (idebug) {
                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));
                }

                nwrite = nwrite_byte / 4;
                if (iwrite == 1) {
                    for (is = 0; is < nwrite; is++) { pcie_int::PcieBuffers::read_array[is] = *buffp_rec32++; }
                    n_write = write(fd, pcie_int::PcieBuffers::read_array.data(), nwrite * 4);
                }
                ntot_rec = ntot_rec + nwrite;
            } // end dma loop
        } // end loop over events

        trig_ctrl::TriggerControl::SendStopTrigger(pcie_interface, buffers, itrig_c, itrig_ext);

        if (iwrite == 1 && iwritem == 0)
            if(close(fd) == -1) {
                LOG_ERROR(logger_, "Failed to close file {} \n", name);
                return false;
            }

        LOG_INFO(logger_, "Closed file after writing {}B to file {} \n", ntot_rec, name);
        return true;
     }

    std::vector<uint32_t> DmaControl::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool DmaControl::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }
} // dma_control
