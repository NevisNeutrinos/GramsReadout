//
// Created by Jon Sensenig on 2/24/25.
//

#include "dma_control.h"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

namespace dma_control {
    bool DmaControl::Configure(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {

        static uint32_t nevent, iv, ijk;
        static int ndma_loop;
        static int irawprint, nwrite_byte;
        static uint32_t i, j, k, ifr, iprint, iwrite, ik, il, is;
        static int idone, tr_bar, t_cs_reg, r_cs_reg, dma_tr;
        static int idebug, ntot_rec, nred;
        static int itrig_c = 0;
        uint32_t dwDMABufSize = 100000;
        static uint32_t iwritem, nwrite, dmasizewrite;
        static int ibytec, fd, itrig_ext, ichip;
        static long tnum;

        static std::array<uint32_t, 40000000> read_array_compare;
        static std::array<uint32_t, 40000000> read_array_1st;
        std::array<char, 150> name{};
        // std::array<char, 50> subrun{};
        char subrun[50];

        uint32_t data;
        static unsigned long long u64Data;
        uint32_t dwOffset;
        uint32_t dwAddrSpace;
        static uint32_t *buffp_rec32;
        static int n_read, n_write;

        pcie_int::DMABufferHandle  pbuf_rec1;
        pcie_int::DMABufferHandle pbuf_rec2;

        static int imod;
        static int imod_trig   = 11;

        itrig_ext = 1;
        iwrite = 1;
        dmasizewrite = 1;

        printf("\nEnter desired DMA size (<%d)\t", dwDMABufSize);
        scanf("%d", &ibytec);
        dwDMABufSize = ibytec;

        printf(" Enter number of events to read out and save:\n ");
        scanf("%d", &nevent);
        printf("\nEnter 1 to set the RUN on \n");
        scanf("%d", &ik);
        ndma_loop = 1;
        ifr = 0;
        printf(" DMA will run %d loop(s) per event\n", (ndma_loop + 1));
        printf("\t%d loops with %d words and %d loop with %d words\n\n", ndma_loop, dwDMABufSize / 4, 1, dwDMABufSize / 4);

        idebug = 0;

        if (iwrite == 1)
        {
            iwritem = 0; // grams
            if (iwritem == 0)
            {
                dmasizewrite = 1; // grams
                printf("\n ######## SiPM+TPC XMIT Readout \n\n Enter SUBRUN NUMBER or NAME:\t");
                scanf("%s", &subrun);

                sprintf(name.data(), "data/pGRAMS_bin_%s.dat", subrun);
                //sprintf(name, "xmit_trig_bin_grams_%s.dat", subrun);
                fd = creat(name.data(), 0755);
                printf("\n\tOutput file: %s\n", name);
            }
        }

        /*TPC DMA*/

    for (ijk = 0; ijk < nevent; ijk++)
    {
        if (ijk % 10 == 0)
            printf("\n===================> EVENT No. %i\n\n", ijk);
        ntot_rec = 0; // keeps track of number of words received in event
        for (iv = 0; iv < (ndma_loop + 1); iv++)
        {
            if (ifr == 0)
            { // first dma
                printf("\n\n\n First DMA\n\n\n");

                if (idebug == 1)
                    printf(" buffer allocation 1\n");

                pcie_interface->DmaContigBufferLock(1, dwDMABufSize, &pbuf_rec1);

                if (idebug == 1)
                    printf(" buffer allocation 2\n");

                pcie_interface->DmaContigBufferLock(2, dwDMABufSize, &pbuf_rec2);

                /* set tx mode register */
                data = 0x00002000;
                dwOffset = hw_consts::tx_md_reg;
                dwAddrSpace = hw_consts::cs_bar;
                pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);

                /* write this will abort previous DMA */
                dwAddrSpace = 2;
                dwOffset = hw_consts::cs_dma_msi_abort;
                data = hw_consts::dma_abort;
                pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);

                /* clear DMA register after the abort */
                dwAddrSpace = 2;
                dwOffset = hw_consts::cs_dma_msi_abort;
                data = 0;
                pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);

                if (idebug == 1)
                    printf(" initial abort finished \n");
            } // end if first dma


            if ((iv % 2) == 0)
                buffp_rec32 = static_cast<uint32_t *>(pbuf_rec1);
            else
                buffp_rec32 = static_cast<uint32_t *>(pbuf_rec2);

            /* synch cache */
            // not needed in SL6?
            if (idebug == 1) printf("\n\n\n Right before Sync\n\n\n");
            if ((iv % 2) == 0) {
                pcie_interface->DmaSyncCpu(1);
            } else {
                pcie_interface->DmaSyncCpu(2);
                if(idebug ==1) printf(" sync CPU \n");
            }
            if (idebug == 1) printf("\n\n\n Right after Sync\n\n\n");

            nwrite_byte = dwDMABufSize;
            // if (iv != ndma_loop) nwrite_byte = dwDMABufSize;
            // else nwrite_byte = last_dma_loop_size;

            for (is = 1; is < 3; is++)
            {
                tr_bar = hw_consts::t1_tr_bar;
                r_cs_reg = hw_consts::r1_cs_reg;
                dma_tr = hw_consts::dma_tr1;
                if (is == 2)
                {
                    tr_bar = hw_consts::t2_tr_bar;
                    r_cs_reg = hw_consts::r2_cs_reg;
                    dma_tr = hw_consts::dma_tr2;
                }
                if (idebug == 1)
                    printf(" is = %d\n", is);
                /** initialize the receiver ***/
                data = hw_consts::cs_init;
                dwOffset = r_cs_reg;
                dwAddrSpace = hw_consts::cs_bar;

                // receiver only gets initialize for the 1st time
                if (ifr == 0)
                {
                    if (idebug == 1)
                        printf(" initialize the input fifo\n");
                    pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);
                }

                /** start the receiver **/
                dwAddrSpace = hw_consts::cs_bar;
                data = hw_consts::cs_start + nwrite_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
                if (idebug == 1)
                    printf(" DMA loop %d with DMA data length %d \n", iv, nwrite_byte);
                dwOffset = r_cs_reg;
                pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);
            }

            ifr = 1;
            //printf("\n\n\n ifr = 1\n\n\n");

            /** set up DMA for both transceiver together **/
            dwAddrSpace = hw_consts::cs_bar;
            dwOffset = hw_consts::cs_dma_add_low_reg;
            if ((iv % 2) == 0)
                data = pcie_interface->GetBufferPageAddrLower(1);
            else
                data = pcie_interface->GetBufferPageAddrLower(2);
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);

            dwAddrSpace = hw_consts::cs_bar;
            dwOffset = hw_consts::cs_dma_add_high_reg;
            if ((iv % 2) == 0)
                pcie_interface->GetBufferPageAddrUpper(1);
            else
                pcie_interface->GetBufferPageAddrUpper(2);
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);

            /* byte count */
            dwAddrSpace = hw_consts::cs_bar;
            dwOffset = hw_consts::cs_dma_by_cnt;
            data = nwrite_byte;
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);

            /* write this will start DMA */
            dwAddrSpace = 2;
            dwOffset = hw_consts::cs_dma_cntrl;
            if ((iv % 2) == 0)
                is = pcie_interface->GetBufferPageAddrUpper(1);
            else
                is = pcie_interface->GetBufferPageAddrUpper(2);
            if (is == 0)
            {
                if (idebug == 1)
                    printf(" use 3dw \n");
                data = hw_consts::dma_tr12 + hw_consts::dma_3dw_rec;
            }
            else
            {
                data = hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;
                if (idebug == 1)
                    printf(" use 4dw \n");
            }
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);
            if (idebug == 1)
                printf(" DMA set up done, byte count = %d\n", nwrite_byte);
            // send trigger
            if (iv == 0)
            {
                if (itrig_c == 1)
                {
                    imod = imod_trig; /* trigger module */
                    buffers.buf_send[0] = (imod << 11) + hw_consts::mb_trig_pctrig + ((0x0) << 16);
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(kDev1, i, k, buffers.psend);
                    usleep(5000); // 3x frame size
                }
                else if (itrig_ext == 1)
                {
                    //
                    //     only need to restart the run if the we use the test data or 1st run
                    //
                    imod = imod_trig;
                    buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x1) << 16); // set up run
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(kDev1, i, k, buffers.psend);
                }
                else
                {
                    imod = 0;
                    ichip = 1;
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_cntrl_set_trig1 + (0x0 << 16); // send trigger
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(kDev1, i, k, buffers.psend);
                    if (idebug == 1)
                        printf(" trigger send  \n");
                    usleep(5000);
                }
            }

            //printf("\n\n\n Now we wait for DMA to fill \n\n\n");

            // extra wait just to be certain --- WK
            // usleep(20000);
            /***    check to see if DMA is done or not **/
            idone = 0;
            for (is = 0; is < 6000000000; is++)
            {
                dwAddrSpace = hw_consts::cs_bar;
                u64Data = 0;
                dwOffset = hw_consts::cs_dma_cntrl;
                pcie_interface->ReadReg32(kDev2, dwAddrSpace, dwOffset, &data);
                // if (idebug == 1)
                //   printf(" receive DMA status word %d %X \n", is, data);
                if ((data & hw_consts::dma_in_progress) == 0)
                {
                    idone = 1;
                    if (idebug == 1)
                        printf(" receive DMA complete %d \n", is);
                }
                if ((data & hw_consts::dma_in_progress) == 0)
                    break;
            }
            if (idone == 0)
            {

                printf("\n loop %d, DMA is not finished \n", iv);
                dwAddrSpace = hw_consts::cs_bar;
                dwOffset = hw_consts::cs_dma_by_cnt;
                pcie_interface->ReadReg64(kDev2, dwAddrSpace, dwOffset, &u64Data);
                //printf(" DMA word count = %x, %x \n", (u64Data >> 32), (u64Data & 0xffff));
                if ((iv % 2) == 0)
                    pcie_interface->DmaSyncIo(1);
                else
                    pcie_interface->DmaSyncIo(2);
                nred = (nwrite_byte - (u64Data & 0xffff)) / 4;
                //printf(" number of 32-bit words received: %d (this DMA) + %d (total before this DMA)\n", nred, ntot_rec);

                //   for (is=0; is < (nwrite_byte/4); is++) {

                //     if((is%8) ==0) printf("%4d", is);
                //     printf(" %8X", pcie_int::PcieBuffers::read_array[is]);
                //     if((is+1)%8 == 0) printf("\n");
                //   }

                if (iwrite == 1 || irawprint == 1)
                {
                    for (is = 0; is < nred; is++)
                    {
                        pcie_int::PcieBuffers::read_array[is] = *buffp_rec32++;
                        printf(" %8X", pcie_int::PcieBuffers::read_array[is]);

                        if (irawprint == 1)
                            read_array_compare[ntot_rec + is] = pcie_int::PcieBuffers::read_array[is];
                        if (irawprint == 1 && ijk == 0)
                            read_array_1st[ntot_rec + is] = pcie_int::PcieBuffers::read_array[is];
                        /*if (iwrited == 1)
                        {
                          fprintf(outf, " %8x", pcie_int::PcieBuffers::read_array[is]);
                          if ((((ntot_rec + is + 1) % 8) == 0))
                            fprintf(outf, "\n");
                        }*/
                    }
                    //        scanf("%d",&ik);
                }
                ntot_rec = ntot_rec + nred;
                if (iwritem == 1)
                {
                    printf("\n\n\n Why are we here? \n\n\n");

                    sprintf(name.data(), "./data/xmit_trig_bin_grams_dma_%i_%i_%i.dat", tnum, ijk, iv);
                    fd = creat(name.data(), 0755);
                    n_write = write(fd, pcie_int::PcieBuffers::read_array.data(), nred * 4);
                    close(fd);
                }
                else if (iwrite == 1 && dmasizewrite == 1)
                {

                   // printf("\n\n\n Writing to file \n\n\n");

                    n_write = write(fd, pcie_int::PcieBuffers::read_array.data(), nred * 4);

                   // printf("\n\n\n writing done\n\n\n");
                }

                dwAddrSpace = hw_consts::cs_bar;
                u64Data = 0;
                dwOffset = hw_consts::t1_cs_reg;
                pcie_interface->ReadReg64(kDev2, dwAddrSpace, dwOffset, &u64Data);
                printf(" Status word for channel 1 after read = %x, %x \n", (u64Data >> 32), (u64Data & 0xffff));
                dwAddrSpace = hw_consts::cs_bar;
                u64Data = 0;
                dwOffset = hw_consts::t2_cs_reg;
                pcie_interface->ReadReg64(kDev2, dwAddrSpace, dwOffset, &u64Data);
                printf(" status word for channel 2 after read = %x, %x \n", (u64Data >> 32), (u64Data & 0xffff));

                /* write this will abort previous DMA */
                dwAddrSpace = 2;
                dwOffset = hw_consts::cs_dma_msi_abort;
                data = hw_consts::dma_abort;
                pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);

                /* clear DMA register after the abort */
                dwAddrSpace = 2;
                dwOffset = hw_consts::cs_dma_msi_abort;
                data = 0;
                pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, data);
            }

            if (idone == 0)
                break;

            //printf("\n\n\n Right before Sync\n\n\n");

            /* synch DMA i/O cache **/
            // SL6
            if ((iv % 2) == 0)
                pcie_interface->DmaSyncIo(1);
            else
                pcie_interface->DmaSyncIo(2);

            //printf("\n\n\n Right after Sync\n\n\n");

            if (idebug == 1)
            {
                dwAddrSpace = hw_consts::cs_bar;
                u64Data = 0;
                dwOffset = hw_consts::t1_cs_reg;
                // WDC_ReadAddr64(hDev2, dwAddrSpace, dwOffset, &u64Data);
                pcie_interface->ReadReg64(kDev2, dwAddrSpace, dwOffset, &u64Data);
                printf(" status word for Channel 1 after read = %x, %x \n", (u64Data >> 32), (u64Data & 0xffff));
                dwAddrSpace = hw_consts::cs_bar;
                u64Data = 0;
                dwOffset = hw_consts::t2_cs_reg;
                // WDC_ReadAddr64(hDev2, dwAddrSpace, dwOffset, &u64Data);
                pcie_interface->ReadReg64(kDev2, dwAddrSpace, dwOffset, &u64Data);
                printf(" status word for channel 2 after read = %x, %x \n", (u64Data >> 32), (u64Data & 0xffff));
            }

            //printf("\n\n\n writing 123 \n\n\n");

            nwrite = nwrite_byte / 4;
            if (iwrite == 1 || irawprint == 1)
            {
                for (is = 0; is < nwrite; is++)
                {
                    pcie_int::PcieBuffers::read_array[is] = *buffp_rec32++;
                    // if (irawprint == 1)
                    //   read_array_compare[ntot_rec + is] = pcie_int::PcieBuffers::read_array[is];
                    // if (irawprint == 1 && ijk == 0)
                    //   read_array_1st[ntot_rec + is] = pcie_int::PcieBuffers::read_array[is];
                    /*if (iwrited == 1)
                    {
                      fprintf(outf, " %8x", pcie_int::PcieBuffers::read_array[is]);
                      if ((((ntot_rec + is + 1) % 8) == 0))
                        fprintf(outf, "\n");
                    }*/
                }
            }
            if (iwritem == 1)
            {
                sprintf(name.data(), "./data/xmit_trig_bin_grams_dma_%i_%i_%i.dat", tnum, ijk, iv);
                fd = creat(name.data(), 0755);
                n_write = write(fd, pcie_int::PcieBuffers::read_array.data(), nwrite * 4);
                close(fd);
            }
            else if (iwrite == 1 && dmasizewrite == 1)
            {

                /*for (is=0; is < (nwrite_byte/4); is++) {

                  if((is%8) ==0) printf("%4d", is);
                  printf(" %8X", pcie_int::PcieBuffers::read_array[is]);
                  if((is+1)%8 == 0) printf("\n");
                }*/

                n_write = write(fd, pcie_int::PcieBuffers::read_array.data(), nwrite * 4);
            }
            ntot_rec = ntot_rec + nwrite;
        } // end dma loop

        } // end loop over events


        if (itrig_c == 1)
        {
            //******************************************************
            // stop run
            imod = imod_trig;
            buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x0) << 16); // set up run off
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(kDev1, i, k, buffers.psend);
        }
        else if ((itrig_ext == 1))
        {
            //
            //  set trigger module run off
            //
            imod = imod_trig;
            buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x0) << 16); // set up run off
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(kDev1, i, k, buffers.psend);
        }

        if (iwrite == 1 && iwritem == 0)
            close(fd);


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