//
// Created by Jon Sensenig on 1/29/25.
//

#include "configure_hardware.h"
#include <iostream>
#include <cmath>
#include <cassert>

namespace hw_config {

    std::array<uint32_t, 40000> ConfigureHardware::send_array = {};
    std::array<uint32_t, 10000000> ConfigureHardware::read_array = {};

    ConfigureHardware::ConfigureHardware() {}

    bool ConfigureHardware::Configure(Config& config, pcie_int::PCIeInterface *pcie_interface) {
        std::cout << "ConfigureHardware::Configure here!" << std::endl;
        std::cout << "pcie_interface: " << pcie_interface << std::endl;

        // // Init send bufs
        // bool successful_config = PCIeDeviceConfigure(pcie_interface);
        bool successful_config = FullConfigure(pcie_interface);
        // // Configure XMIT hardware
        // successful_config = XMITLoadFPGA(config, pcie_interface);
        // successful_config = XMITConfigure(config, pcie_interface);
        return successful_config;
    }

    bool ConfigureHardware::PCIeDeviceConfigure(pcie_int::PCIeInterface *pcie_interface) {


        return true;
    }

    bool ConfigureHardware::XMITLoadFPGA(Config &config, pcie_int::PCIeInterface *pcie_interface) {

        return true;
    }

    bool ConfigureHardware::XMITConfigure(Config &config, pcie_int::PCIeInterface *pcie_interface) {
        // XMIT SETUP

        bool print_debug = false;
        static long imod, ichip;
        static int ij, nword;
        uint32_t i, j, k;
        static uint32_t ik;
        static uint32_t iprint = 1;
        unsigned char charchannel;
        timespec tim, tim2;

        static int imod_xmit   = 12;
        static int imod_pmt    = 16;
        static int imod_st1    = 16;  //st1 corresponds to last pmt slot (closest to xmit)
        static int imod_st2    = 15;  //st2 corresponds to last tpc slot (farthest to XMIT)
        static int imod_tpc    = 13;  // tpc slot closest to XMIT
        static int imod_trig   = 11;
        static int imod_shaper = 17;

        // std::array<uint32_t, 40000> pcie_int::PCIeInterface::send_array_ = {};
        std::array<uint32_t, 10000000> read_array_ = {};

        printf("Setting up XMIT module\n");
        imod = imod_xmit; // XMIT setup
        ichip = 3;

        // p_send_ = &send_array_[0];
        // p_recv_ = &read_array_[0];

        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_modcount + ((imod_st1 - imod_xmit - 1) << 16); //                  -- number of FEM module -1, counting start at 0
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);

        imod = imod_xmit; //     reset optical
        ichip = 3;
        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_opt_dig_reset + (0x1 << 16); // set optical reset on
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);

        imod = imod_xmit; //     enable Neutrino Token Passing
        ichip = 3;
        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_enable_1 + (0x1 << 16); // enable token 1 pass
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);

        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_enable_2 + (0x0 << 16); // disable token 2 pass
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);

        imod = imod_xmit; //       reset XMIT LINK IN DPA
        ichip = 3;
        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_link_pll_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);
        usleep(10000); // 1ms ->10ms JLS

        imod = imod_xmit; //     reset XMIT LINK IN DPA
        ichip = 3;
        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_link_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);

        usleep(10000); //     wait for 10ms just in case

        imod = imod_xmit; //     reset XMIT FIFO reset
        ichip = 3;
        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_dpa_fifo_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);

        imod = imod_xmit; //      test re-align circuit
        ichip = 3;
        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_dpa_word_align + (0x1 << 16); //  send alignment pulse
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);
        usleep(10000); // wait for 5 ms  (5ms ->10ms JLS)

        nword = 1;
        i = pcie_interface->PCIeRecvBuffer(kDev1, 0, 1, nword, iprint, p_recv_);
        imod = imod_xmit;
        ichip = 3;
        pcie_interface->buf_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_rdstatus + (0x0 << 16); // read out status

        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(kDev1, i, k, p_send_);
        p_recv_ = read_array_.data();
        i = pcie_interface->PCIeRecvBuffer(kDev1, 0, 2, nword, iprint, p_recv_);
        printf("XMIT status word = %x, %x \n", read_array_[0], read_array_[1]);

        printf("\nXMIT STATUS -- Post-Setup = %x, %x \n", read_array_[0], read_array_[1]);
        printf(" pll locked          %d \n", ((read_array_[0] >> 16) & 0x1));
        printf(" receiver lock       %d \n", ((read_array_[0] >> 17) & 0x1));
        printf(" DPA lock            %d \n", ((read_array_[0] >> 18) & 0x1));
        printf(" NU Optical lock     %d \n", ((read_array_[0] >> 19) & 0x1));
        printf(" SN Optical lock     %d \n", ((read_array_[0] >> 20) & 0x1));
        printf(" SN Busy             %d \n", ((read_array_[0] >> 22) & 0x1));
        printf(" NU Busy             %d \n", ((read_array_[0] >> 23) & 0x1));
        printf(" SN2 Optical         %d \n", ((read_array_[0] >> 24) & 0x1));
        printf(" SN1 Optical         %d \n", ((read_array_[0] >> 25) & 0x1));
        printf(" NU2 Optical         %d \n", ((read_array_[0] >> 26) & 0x1));
        printf(" NU1 Optical         %d \n", ((read_array_[0] >> 27) & 0x1));
        printf(" Timeout1            %d \n", ((read_array_[0] >> 28) & 0x1));
        printf(" Timeout2            %d \n", ((read_array_[0] >> 29) & 0x1));
        printf(" Align1              %d \n", ((read_array_[0] >> 30) & 0x1));
        printf(" Align2              %d \n", ((read_array_[0] >> 31) & 0x1));

        usleep(10000);
        printf("\n...XMIT setup complete\n");

        if (print_debug == true)
        {
            printf("\n setup_xmit debug : \n");
            // printf("dmabufsize %x\n", dmabufsize);
            // printf("imod_xmit_in %x\n", imod_xmit_in);
            printf("mb_xmit_modcount %x\n", mb_xmit_modcount);
            printf("mb_opt_dig_reset %x\n", mb_opt_dig_reset);
            printf("mb_xmit_enable_1 %x\n", mb_xmit_enable_1);
            printf("mb_xmit_link_pll_reset %x\n", mb_xmit_link_pll_reset);
            printf("mb_xmit_link_reset %x\n", mb_xmit_link_reset);
            printf("mb_xmit_dpa_fifo_reset %x\n", mb_xmit_dpa_fifo_reset);
            printf("mb_xmit_dpa_word_align %x\n", mb_xmit_dpa_word_align);
            printf("mb_xmit_rdstatus %x\n", mb_xmit_rdstatus);
            printf("mb_xmit_rdstatus %x\n", mb_xmit_rdstatus);
        }
        return true;
    }

    bool ConfigureHardware::FullConfigure(pcie_int::PCIeInterface *pcie_interface) {

            static uint32_t dwAddrSpace;
            static uint32_t u32Data;
            static uint32_t dwOffset;
            static long imod, ichip;
            static uint32_t i, k, iprint, ik, is;
            static uint32_t imod_trig, imod_shaper;
            static int ihuff;
            uint32_t dwDMABufSize;
            static int count, counta, nword;
            static int ij, nsend;
            static int iframe, ichip_c, dummy1;
            static int timesize, a_id, itrig_delay;
            static int imod_xmit;
            static int iframe_length;
            static int idelay0, idelay1, threshold0, threshold1, pmt_words;
            static int cos_mult, cos_thres, en_top, en_upper, en_lower, hg, lg;
            static int imod_fem, imod_st, imod_st1, imod_st2;
            static int ibytec;
            static int mask1, mask8;
            static int threshold3hg, threshold3lg, threshold1hg, threshold1lg;
            static int threshold3;
            static int lg_ch, hg_ch, trig_ch, bg_ch, beam_mult, beam_thres, pmt_wordslg, pmt_wordshg, pmt_precount;
            static int pmt_deadtimehg, pmt_deadtimelg, pmt_mich_window, bg, bge, tre, beam_size, pmt_width;
            static int pmt_deadtime;

            unsigned char charchannel;
            struct timespec tim, tim2;
            uint32_t *px, *py;

            FILE *outf, *inpf, *outinfo;
            static int imod_pmt, imod_tpc;
            iprint = 0;
            dwDMABufSize = 100000;
            bool print_debug = true;
            std::cout << "ConfigureHardware::FullConfigure2" << std::endl;

            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ MISC...  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
            // newcmd = 1;

            struct timeval;
            time_t rawtime;
            char timestr[500];
            char subrun[50];
            int trigtype;
            time(&rawtime);

            strftime(timestr, sizeof(timestr), "%Y_%m_%d", localtime(&rawtime));
            printf("\n ######## SiPM+TPC XMIT Readout \n\n Enter SUBRUN NUMBER or NAME:\t");
            scanf("%s", &subrun);
            printf("\nEnter desired DMA size (<%d)\t", dwDMABufSize);
            scanf("%d", &ibytec);
            dwDMABufSize = ibytec;
            printf("\n Enter 0 for EXT trigger or 1 for triggers issued by SiPM ADC:\t");
            scanf("%i", &trigtype);

            if(trigtype){
              //PMT Trigger
              mask8 = 0x0;
              mask1 = 0x8;  //this is either 0x1 (PMT beam) 0x4 (PMT cosmic) or 0x8 (PMT all)
            } else{
              //EXT trigger
              mask1 = 0x0;
              mask8 = 0x2;
            }

            // uint32_t read_array[dwDMABufSize];
            char _buf[200];
            sprintf(_buf, "info/xmit_subrun_%s_%s_dma_no_1.bin", timestr, subrun);
            outf = fopen(_buf, "wb");
            //fd = creat("test_file.dat", 0755);
            sprintf(_buf, "info/xmit_subrun_%s_%s.info", timestr, subrun);
            outinfo = fopen(_buf, "w");

            //Crate 2 (production)
            imod_xmit   = 12; // 10;
            imod_pmt    = 16; //16
            imod_st1    = 16; //16  //st1 corresponds to last pmt slot (closest to xmit)
            imod_st2    = 15;  //st2 corresponds to last tpc slot (farthest to XMIT)
            imod_tpc    = 13;  // tpc slot closest to XMIT
            imod_trig   = 11;  // 16;
            imod_shaper = 17; // 6;*/

            // Crate 1 (test)
            //imod_xmit   = 14; // 10;
            //imod_pmt    = 16;
            //imod_st1    = 16;  //st1 corresponds to last pmt slot
            //imod_st2    = 15;  //st2 corresponds to last tpc slot (before XMIT)
            //imod_tpc    = 15;
            //imod_trig   = 13;  // 16;
            //imod_shaper = 17; // 6;*/


            //iframe_length = 25599;
            iframe_length = 2047;
            //iframe_length = 1599;   // 16MHz samples

            iframe = iframe_length;
            itrig_delay = 10; // 10
            //timesize = 3199;
            timesize = 255;
            //timesize = 199;  //2MHz samples
            // icheck = 0;
            // ifr = 0;
            // irand = 0;
            // islow_read = 0;
            iprint = 1;
            nsend = 500;
            // itrig_c = 0;
            // itrig_ext = 1;

            // if (iwrite == 1)
            // {
            //     iwritem = 0; // grams
            //     if (iwritem == 0)
            //     {
            //         dmasizewrite = 1; // grams
            //         sprintf(name, "/data/readout_data/pGRAMS_bin_%s.dat", subrun);
            //         //sprintf(name, "xmit_trig_bin_grams_%s.dat", subrun);
            //         fd = creat(name, 0755);
            //         printf("\n\tOutput file: %s\n", name);
            //     }
            // }
            fprintf(outinfo, "imod_xmit = %d\n", imod_xmit);
            fprintf(outinfo, "imod_st   = %d\n", imod_st);
            fprintf(outinfo, "imod_trig = %d\n", imod_trig);
            fprintf(outinfo, "imod_shaper = %d\n", imod_shaper);
            printf("\n");
            printf(" XMIT module address = %d\n", imod_xmit);
            printf(" PMT ADC module address = %d \n", imod_pmt);
            printf(" Shaper address = %d \n", imod_shaper);
            printf(" Trigger module address = %d \n", imod_trig);
            printf("\nFrame length (16MHz time ticks)?\n\t1.6ms --> Enter 25599 (nominal)\n\t512us --> Enter 8191 (if LED calibration)\n\t");
            fprintf(outinfo, "iframe_length (16MHz) = %d\n", iframe_length);
            fprintf(outinfo, "itrig_delay = %d\n", itrig_delay);
            printf("\tframe size = %d\n", iframe_length);
            fprintf(outinfo, "ibytec = %d (DMA size)\n", ibytec);
            fprintf(outinfo, "dwDMABufSize = %d\n", dwDMABufSize);
            fprintf(outinfo, "dma_buffer_size (read_array) = %d\n", ibytec);


            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ CONTROLLER-PCIE SETTUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000; // initial transmitter, no hold
            dwOffset = t1_cs_reg; //0x18;
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);
            dwAddrSpace = 2;

            u32Data = cs_init; //0x20000000; // initial transmitter, no hold
            dwOffset = t2_cs_reg; //0x20;
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000; // initial receiver
            dwOffset = r1_cs_reg; //0x1c;
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

            dwAddrSpace = 2;
            u32Data = cs_init; //0x20000000; // initial receiver
            dwOffset = r2_cs_reg; //0x24;
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

            dwAddrSpace = 2;
            u32Data = 0xfff; // set mode off with 0xfff...
            dwOffset = tx_md_reg; //0x28;
            pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

            px = buf_send.data(); //&buf_send[0]; // RUN INITIALIZATION
            py = read_array.data(); //&read_array[0];
            imod = 0; // controller module

            buf_send[0] = 0x0; // INITIALIZE
            buf_send[1] = 0x0;
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(kDev1, i, k, px);

                // printf("\n 1am_hDev: %x",hDev);
                printf("\n 1am_i: %i",i);
                printf("\n 1am_px: %i",px);


            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ XMIT BOOT  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
            inpf = fopen("/home/sabertooth/GramsReadoutFirmware/xmit/readcontrol_110601_v3_play_header_hist_l1block_9_21_2018.rbf", "r"); // tpc readout akshay
            // inpf = fopen("/home/ub/WinDriver/wizard/GRAMS_project_am/uBooNE_Nominal_Firmware/xmit/readcontrol_11601_v3_play_header_6_21_2013.rbf", "r");//tpc readout akshay

            printf("\nBooting XMIT module...\n\n");
            imod = imod_xmit;
            ichip = mb_xmit_conf_add;
            buf_send[0] = (imod << 11) + (ichip << 8) + 0x0 + (0x0 << 16); // turn conf to be on
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                // printf("\n 2am_hDev: %x",hDev);
                printf("\n 2am_i: %i",i);
                printf("\n 2am_px: %i\n",px);

            usleep(2000); // wait for a while (1ms->2ms JLS)
            count = 0;
            counta = 0;
            nsend = 500;
            ichip_c = 7; // set ichip_c to stay away from any other command in the
            nword = 1;
            imod = imod_xmit;
            dummy1 = 0;

            if (print_debug == true)
            {
                printf("\n boot_xmit debug : \n");
                // printf("dmabufsize %x\n", dmabufsize);
                // printf("imod_xmit_in %x\n", imod_xmit_in);
                printf("mb_xmit_conf_add %x\n", mb_xmit_conf_add);
            }

            while (fread(&charchannel, sizeof(char), 1, inpf) == 1)
            {
                carray[count] = charchannel;
                count++;
                counta++;
                if ((count % (nsend * 2)) == 0)
                {
                    buf_send[0] = (imod << 11) + (ichip_c << 8) + (carray[0] << 16);
                    send_array[0] = buf_send[0];
                    if (dummy1 <= 5)
                        printf("\tcounta = %d, first word = %x, %x, %x %x %x \n", counta, buf_send[0], carray[0], carray[1], carray[2], carray[3]);

                    for (ij = 0; ij < nsend; ij++)
                    {
                        if (ij == (nsend - 1))
                            buf_send[ij + 1] = carray[2 * ij + 1] + (0x0 << 16);
                        else
                            buf_send[ij + 1] = carray[2 * ij + 1] + (carray[2 * ij + 2] << 16);
                        send_array[ij + 1] = buf_send[ij + 1];
                    }
                    nword = nsend + 1;
                    i = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, nword, px);

                    nanosleep(&tim, &tim2);
                    dummy1 = dummy1 + 1;
                    count = 0;
                }
            }
            if (feof(inpf))
            {
                printf("\tend-of-file word count= %d %d\n", counta, count);
                buf_send[0] = (imod << 11) + (ichip_c << 8) + (carray[0] << 16);
                if (count > 1)
                {
                    if (((count - 1) % 2) == 0)
                    {
                        ik = (count - 1) / 2;
                    }
                    else
                    {
                        ik = (count - 1) / 2 + 1;
                    }
                    ik = ik + 2; // add one more for safety
                    printf("\tik= %d\n", ik);
                    for (ij = 0; ij < ik; ij++)
                    {
                        if (ij == (ik - 1))
                            buf_send[ij + 1] = carray[(2 * ij) + 1] + (((imod << 11) + (ichip << 8) + 0x0) << 16);
                        else
                            buf_send[ij + 1] = carray[(2 * ij) + 1] + (carray[(2 * ij) + 2] << 16);
                        send_array[ij + 1] = buf_send[ij + 1];
                    }
                }
                else
                    ik = 1;
                for (ij = ik - 10; ij < ik + 1; ij++)
                {
                    printf("\tlast data = %d, %x\n", ij, buf_send[ij]);
                }
                nword = ik + 1;
                i = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, nword, px);
            }
            usleep(3000); // wait for 2ms to cover the packet time plus fpga init time (change to 3ms JLS)
            fclose(inpf);

            nword = 1;
            i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py);
            nword = 1;
            i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py);
            imod = imod_xmit;
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_rdstatus + (0x0 << 16); // read out status

            i = 1; // This is status read
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);
            py = read_array.data(); //&read_array[0];
            i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, py);
            printf("XMIT status word = %x, %x \n", read_array[0], read_array[1]);
            // printf("\nDo you want to read out TPC or PMT? Enter 0 for TPC, 1 for PMT, 2 for both\n");
            // scanf("%i", &readtype);

            uint32_t first_word = read_array[0];
            if( ((first_word>>5) & 0x7) || (((first_word>>8) & 0xff) != 0xff) || ((first_word>>21) & 0x1) ) {
              printf("Unexpected XMIT status word!");
            }
            printf("\nXMIT STATUS -- Pre-Setup = %x, %x \n", read_array[0], read_array[1]);
            printf(" pll locked          %d \n", ((read_array[0] >> 16) & 0x1));
            printf(" receiver lock       %d \n", ((read_array[0] >> 17) & 0x1));
            printf(" DPA lock            %d \n", ((read_array[0] >> 18) & 0x1));
            printf(" NU Optical lock     %d \n", ((read_array[0] >> 19) & 0x1));
            printf(" SN Optical lock     %d \n", ((read_array[0] >> 20) & 0x1));
            printf(" SN Busy             %d \n", ((read_array[0] >> 22) & 0x1));
            printf(" NU Busy             %d \n", ((read_array[0] >> 23) & 0x1));
            printf(" SN2 Optical         %d \n", ((read_array[0] >> 24) & 0x1));
            printf(" SN1 Optical         %d \n", ((read_array[0] >> 25) & 0x1));
            printf(" NU2 Optical         %d \n", ((read_array[0] >> 26) & 0x1));
            printf(" NU1 Optical         %d \n", ((read_array[0] >> 27) & 0x1));
            printf(" Timeout1            %d \n", ((read_array[0] >> 28) & 0x1));
            printf(" Timeout2            %d \n", ((read_array[0] >> 29) & 0x1));
            printf(" Align1              %d \n", ((read_array[0] >> 30) & 0x1));
            printf(" Align2              %d \n", ((read_array[0] >> 31) & 0x1));

            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ XMIT SETTUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

            // uint32_t read_array[dmabufsize];
            printf("Setting up XMIT module\n");
            imod = imod_xmit; // XMIT setup
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_modcount + ((imod_st1 - imod_xmit - 1) << 16); //                  -- number of FEM module -1, counting start at 0
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = imod_xmit; //     reset optical
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_opt_dig_reset + (0x1 << 16); // set optical reset on
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = imod_xmit; //     enable Neutrino Token Passing
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_enable_1 + (0x1 << 16); // enable token 1 pass
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_enable_2 + (0x0 << 16); // disable token 2 pass
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = imod_xmit; //       reset XMIT LINK IN DPA
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_link_pll_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);
            usleep(10000); // 1ms ->10ms JLS

            imod = imod_xmit; //     reset XMIT LINK IN DPA
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_link_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            usleep(10000); //     wait for 10ms just in case

            imod = imod_xmit; //     reset XMIT FIFO reset
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_dpa_fifo_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = imod_xmit; //      test re-align circuit
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_dpa_word_align + (0x1 << 16); //  send alignment pulse
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);
            usleep(10000); // wait for 5 ms  (5ms ->10ms JLS)

            nword = 1;
            i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py); // init the receiver
            imod = imod_xmit;
            ichip = 3;
            buf_send[0] = (imod << 11) + (ichip << 8) + mb_xmit_rdstatus + (0x0 << 16); // read out status

            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);
            py = read_array.data(); //&read_array[0];
            i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, py); // read out 2 32 bits words
            printf("XMIT status word = %x, %x \n", read_array[0], read_array[1]);

            printf("\nXMIT STATUS -- Post-Setup = %x, %x \n", read_array[0], read_array[1]);
            printf(" pll locked          %d \n", ((read_array[0] >> 16) & 0x1));
            printf(" receiver lock       %d \n", ((read_array[0] >> 17) & 0x1));
            printf(" DPA lock            %d \n", ((read_array[0] >> 18) & 0x1));
            printf(" NU Optical lock     %d \n", ((read_array[0] >> 19) & 0x1));
            printf(" SN Optical lock     %d \n", ((read_array[0] >> 20) & 0x1));
            printf(" SN Busy             %d \n", ((read_array[0] >> 22) & 0x1));
            printf(" NU Busy             %d \n", ((read_array[0] >> 23) & 0x1));
            printf(" SN2 Optical         %d \n", ((read_array[0] >> 24) & 0x1));
            printf(" SN1 Optical         %d \n", ((read_array[0] >> 25) & 0x1));
            printf(" NU2 Optical         %d \n", ((read_array[0] >> 26) & 0x1));
            printf(" NU1 Optical         %d \n", ((read_array[0] >> 27) & 0x1));
            printf(" Timeout1            %d \n", ((read_array[0] >> 28) & 0x1));
            printf(" Timeout2            %d \n", ((read_array[0] >> 29) & 0x1));
            printf(" Align1              %d \n", ((read_array[0] >> 30) & 0x1));
            printf(" Align2              %d \n", ((read_array[0] >> 31) & 0x1));

            usleep(10000);
            printf("\n...XMIT setup complete\n");

            if (print_debug == true)
            {
                printf("\n setup_xmit debug : \n");
                // printf("dmabufsize %x\n", dmabufsize);
                // printf("imod_xmit_in %x\n", imod_xmit_in);
                printf("mb_xmit_modcount %x\n", mb_xmit_modcount);
                printf("mb_opt_dig_reset %x\n", mb_opt_dig_reset);
                printf("mb_xmit_enable_1 %x\n", mb_xmit_enable_1);
                printf("mb_xmit_link_pll_reset %x\n", mb_xmit_link_pll_reset);
                printf("mb_xmit_link_reset %x\n", mb_xmit_link_reset);
                printf("mb_xmit_dpa_fifo_reset %x\n", mb_xmit_dpa_fifo_reset);
                printf("mb_xmit_dpa_word_align %x\n", mb_xmit_dpa_word_align);
                printf("mb_xmit_rdstatus %x\n", mb_xmit_rdstatus);
                printf("mb_xmit_rdstatus %x\n", mb_xmit_rdstatus);
            }
            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ LIGHT FEM BOOT  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

    //pmt_adc_setup(hDev, imod_pmt, iframe_length, 1, outinfo);
    static int mode;
    if(trigtype){
      mode = 3;
    }
    else{
      mode = 0;
    }

    iframe = iframe_length;
    imod_fem = imod_pmt;

    printf("\nIs there a BNB gate physically input to the FEM?\t"); //
    //  scanf("%d",&bg);
    bg = 0;
    fprintf(outinfo, "Hardware config: Gate input = %d\n", bg);
    if (bg == 0 && (mode == 4 || mode == 5 || mode == 7))
    {
        printf("\nWarning: The PMT Beam Trigger is disabled because the BNB gate is not physically input to the FEM\n");
        printf("Enter 1 to acknowledge this:\t");
        scanf("%d", &ik); // enter anything; this will be overwritten next
    }

    hg_ch = 2;
    lg_ch = 3;
    bg_ch = 0;
    trig_ch = 0;
    printf("\tLG_ch = %d, HG_ch = %d, TRIG_ch = %d, BG_ch = %d\n", lg_ch, hg_ch, trig_ch, bg_ch);
    fprintf(outinfo, "Hardware config: LG_ch = %d, HG_ch = %d, TRIG_ch = %d, BG_ch = %d\n", lg_ch, hg_ch, trig_ch, bg_ch);
    //  scanf("%d",&hg);
    hg = 1;
    //hg = 0;  //EXT Trigger setup as of 11/26/2024
    lg = 1;
    //lg = 0;  //EXT Trigger setup as of 11/26/2024
    if (bg == 1)
    {
        printf("\n\tEnable BNB gate channel (for readout)?\t");
        scanf("%d", &bge);
    }
    //  scanf("%d",&tre);
    tre = 0;
    //tre = 1; //EXT Trigger set up as of 11/26/2024
    pmt_mich_window = 0; // disable by hand
    threshold0 = 20;     // discriminator 0 threshold (original =40 JLS)
    printf("\nReadout parameter definitions:\n");
    printf("Discriminator thresholds assume 20 ADC/pe for HG, 2 ADC/pe for LG.\n");
    printf("\nThreshold for discr 0 = %d ADC counts\n", threshold0);
    /*if (hg == 1)
    {
        printf("\nEnter threshold for discr 3 (ADC) for HG (default=20):\t");
        scanf("%d", &threshold3hg);
        printf("\nEnter threshold for discr 1 (ADC) for HG (default=4):\t");
        scanf("%d", &threshold1hg);
        printf("Enter number of readout words for HG (default=20):\t");
        scanf("%d", &pmt_wordshg);
        printf("Enter PMT deadtime (64MHz samples) for HG (default=256; minimum recommended is %d):\t", pmt_wordshg + 4);
        scanf("%d", &pmt_deadtimehg);
    }
    if (lg == 1)
    {
        printf("\nEnter threshold for discr 3 (ADC) for LG (default=2):\t");
        scanf("%d", &threshold3lg);
        printf("\nEnter threshold for discr 1 (ADC) for LG (default=8):\t");
        scanf("%d", &threshold1lg);
        printf("Enter number of readout words for LG (default=20):\t");
        scanf("%d", &pmt_wordslg);
        printf("\nEnter PMT deadtime (64MHz samples) for LG (default=24; minimum recommended is %d):\t", pmt_wordslg + 4);
        scanf("%d", &pmt_deadtimelg);
    }*/
    // for all other channels
    pmt_words = 20;
    pmt_deadtime = 240;
    threshold1 = 40; // original =40 JLS
    threshold3 = 4095; // original =40 JLS
    //cos_thresh = 80;
    //threshold1 = 40;   //EXT Trigger setup as of 11/26/2024
    //threshold3 = 4095; //EXT Trigger setup as of 11/26/2024

    if ((mode == 3) || (mode == 5) || (mode == 7))
    {
        printf("\nTriggering on SiPMs:");
        printf("\nEnter SiPM trigger summed-ADC threshold:\t");
        scanf("%d", &cos_thres);
        printf("Enter multiplicity:\t");
        scanf("%d", &cos_mult);
    }
    else
    { // doesn't matter
        cos_thres = 100;
        cos_mult = 100;
    }
    if ((mode == 4) || (mode == 5) || (mode == 7))
    {
        printf("\nTriggering on PMT Beam Trigger:");
        printf("\nEnter beam trigger threshold (ADC); default is 40 (HG):\t");
        scanf("%d", &beam_thres);
        printf("Enter beam multiplicity; default is 1:\t");
        scanf("%d", &beam_mult);
    }
    else
    { // doesn't matter
        beam_thres = 100;
        beam_mult = 100;
    }
    pmt_precount = 2; // default
    pmt_width = 500;    // default
    fprintf(outinfo, "RUN MODE = %d\n", mode);
    fprintf(outinfo, "pmt_deadtime HG,LG = %d,%d\n", pmt_deadtimehg, pmt_deadtimelg);
    fprintf(outinfo, "pmt_deadtime BG,TRIG channels = %d\n", pmt_deadtime);
    fprintf(outinfo, "pmt_width = %d\n", pmt_width);
    fprintf(outinfo, "pmt_mich_window = %d\n", pmt_mich_window);
    fprintf(outinfo, "threshold0 = %d\n", threshold0);
    fprintf(outinfo, "threshold3 HG,LG = %d,%d\n", threshold3hg, threshold3lg);
    fprintf(outinfo, "threshold1 HG,LG = %d,%d\n", threshold1hg, threshold1lg);
    fprintf(outinfo, "threshold3 BG,TRIG channels = %d\n", threshold3);
    fprintf(outinfo, "threshold1 BG,TRIG channels = %d\n", threshold1);
    fprintf(outinfo, "cos_mult = %d\n", cos_mult);
    fprintf(outinfo, "cos_thres = %d\n", cos_thres);
    fprintf(outinfo, "beam_mult = %d\n", beam_mult);
    fprintf(outinfo, "beam_thres = %d\n", beam_thres);
    fprintf(outinfo, "pmt_precount = %d\n", pmt_precount);
    fprintf(outinfo, "pmt_words HG,LG = %d,%d\n", pmt_wordshg, pmt_wordslg);
    fprintf(outinfo, "pmt_words BG,TRIG channels = %d\n", pmt_words);
    // turn on the Stratix III power supply
    px = buf_send.data(); //&buf_send[0];
    py = read_array.data(); //&read_array[0];

    imod = imod_fem;
    ichip = 1;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_power_add + (0x0 << 16); // turn module 11 power on
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    usleep(200000); // wait for 200 ms
    //inpf = fopen("/home/uboonedaq/pmt_fpga_code/module1x_pmt_64MHz_new_head_07162013.rbf","r");
    inpf = fopen("/home/sabertooth/GramsReadoutFirmware/pmt_fem/module1x_pmt_64MHz_new_head_07162013.rbf", "r");
    //inpf = fopen("/home/ub/module1x_pmt_64MHz_link_4192013.rbf", "r");
    //inpf = fopen("/home/ub/module1x_pmt_64MHz_new_head_12192013_allch.rbf","r");
    //inpf = fopen("/home/ub/module1x_pmt_64MHz_fermi_08032012_2.rbf","r");
    // inpf = fopen("/home/ub/WinDriver/wizard/GRAMS_project_am/uBooNE_Nominal_Firmware/xmit/readcontrol_110601_v3_play_header_hist_l1block_9_21_2018.rbf", "r");
    //  fprintf(outinfo,"PMT FEM FPGA: /home/uboonedaq/pmt_fpga_code/module1x_pmt_64MHz_new_head_07162013.rbf\n");
    fprintf(outinfo, "PMT FEM FPGA: /home/sabertooth/GramsReadoutFirmware/pmt_fem/module1x_pmt_64MHz_new_head_07162013.rbf");
    //  fprintf(outinfo,"PMT FEM FPGA: /home/ub/module1x_pmt_64MHz_new_head_12192013_allch.rbf\n ");
    imod = imod_fem;
    ichip = mb_feb_conf_add;
    buf_send[0] = (imod << 11) + (ichip << 8) + 0x0 + (0x0 << 16); // turn conf to be on
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    printf("\nLoading FEM FPGA...\n");
    usleep(5000); // wait for a while (1ms->5ms JLS)
    nsend = 500;
    count = 0;
    counta = 0;
    ichip_c = 7; // set ichip_c to stay away from any other command in the
    dummy1 = 0;
    while (fread(&charchannel, sizeof(char), 1, inpf) == 1)
    {
        carray[count] = charchannel;
        count++;
        counta++;
        if ((count % (nsend * 2)) == 0)
        {
            buf_send[0] = (imod << 11) + (ichip_c << 8) + (carray[0] << 16);
            send_array[0] = buf_send[0];
            if (dummy1 <= 5)
                printf(" counta = %d, first word = %x, %x, %x %x %x \n", counta, buf_send[0], carray[0], carray[1], carray[2], carray[3]);
            for (ij = 0; ij < nsend; ij++)
            {
                if (ij == (nsend - 1))
                    buf_send[ij + 1] = carray[2 * ij + 1] + (0x0 << 16);
                else
                    buf_send[ij + 1] = carray[2 * ij + 1] + (carray[2 * ij + 2] << 16);
                send_array[ij + 1] = buf_send[ij + 1];
            }
            nword = nsend + 1;
            i = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, nword, px);
            nanosleep(&tim, &tim2);
            dummy1 = dummy1 + 1;
            count = 0;
        }
    }
    if (feof(inpf))
    {
        printf("\tend-of-file word count= %d %d\n", counta, count);
        buf_send[0] = (imod << 11) + (ichip_c << 8) + (carray[0] << 16);
        if (count > 1)
        {
            if (((count - 1) % 2) == 0)
            {
                ik = (count - 1) / 2;
            }
            else
            {
                ik = (count - 1) / 2 + 1;
            }
            ik = ik + 2; // add one more for safety
            printf("\tik= %d\n", ik);
            for (ij = 0; ij < ik; ij++)
            {
                if (ij == (ik - 1))
                    buf_send[ij + 1] = carray[(2 * ij) + 1] + (((imod << 11) + (ichip << 8) + 0x0) << 16);
                else
                    buf_send[ij + 1] = carray[(2 * ij) + 1] + (carray[(2 * ij) + 2] << 16);
                send_array[ij + 1] = buf_send[ij + 1];
            }
        }
        else
            ik = 1;

        for (ij = ik - 10; ij < ik + 1; ij++)
        {
            printf("\tlast data = %d, %x\n", ij, buf_send[ij]);
        }

        nword = ik + 1;
        i = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, nword, px);
    }
    usleep(5000); // wait for 2ms to cover the packet time plus fpga init time (2ms->5ms JLS)
    fclose(inpf);

    printf("FEM FPGA configuration done\n");
    ik = 1;
    if (ik == 1)
    {
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + 31 + (0x1 << 16); // turm the DRAM reset on
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + 31 + (0x0 << 16); // turm the DRAM reset off
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);

        usleep(5000); // wait for 5 ms for DRAM to be initialized
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + 6 + (imod << 16); // set module number
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    }

    nword = 1;
    i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py); // init the receiver

    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + 20 + (0x0 << 16); // read out status
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    py = read_array.data(); //&read_array[0];
    i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, py); // read out 2 32 bits words
    printf("\nFEM STATUS:\n receive data word = %x, %x \n\n", read_array[0], read_array[1]);

    for (ik = 0; ik < 40; ik++)
    {

        printf("Configuration for channel %d in progress...\n", ik);
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_ch_set + (ik << 16); // set channel number for configuration
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);

        usleep(1000); // (0.1ms->1ms JLS)

        imod = imod_fem;
        ichip = 3;
        idelay0 = 4;
        if (ik == 0)
            fprintf(outinfo, "\nidelay0 = %d\n", idelay0);
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_delay0 + (idelay0 << 16); // set delay0
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT delay1 to 12 (default)
        imod = imod_fem;
        ichip = 3;
        idelay1 = 12;
        if (ik == 0)
            fprintf(outinfo, "idelay1 = %d\n", idelay1);
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_delay1 + (idelay1 << 16); // set delay 1
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT precount
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_precount + (pmt_precount << 16); // set pmt precount
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT threshold 0
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_thresh0 + (threshold0 << 16); // set threshold 0
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT threshold 3
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh3+(threshold3hg<<16);
        //    else if (ik==lg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh3+(threshold3lg<<16);
        //    else
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_thresh3 + (threshold3 << 16); // set threshold 1
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT threshold 1
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh1+(threshold1hg<<16);
        //    else if (ik==lg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh1+(threshold1lg<<16);
        //    else
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_thresh1 + (threshold1 << 16); // set threshold 1
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT data words
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_words+(pmt_wordshg<<16);  // set pmt_words
        //    else if (ik==lg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_words+(pmt_wordslg<<16);
        //    else
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_words + (pmt_words << 16);
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);

        // set PMT deadtime
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_deadtime+(pmt_deadtimehg<<16);  // set pmt dead timr
        //    else if (ik==lg_ch) buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_deadtime+(pmt_deadtimelg<<16);
        //    else
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_deadtime + (pmt_deadtime << 16);
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT Michel window
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_window + (pmt_mich_window << 16); // set pmt Michel window
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        // set PMT width (for trigger generation); default is supposed to be 100ns; here, ~78ns
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_width + (pmt_width << 16); // set pmt discr width
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    }
    printf("\nConfiguring PMT Trigger parameters...\n");
    // set PMT cosmic ray trigger multiplicity
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_cos_mul + (cos_mult << 16); // set cosmic ray trigger mul
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // set PMT cosmic ray trigger pulse height
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_cos_thres + (cos_thres << 16); // set cosmic ray trigger peak
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // set PMT beam trigger multiplicity
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_beam_mul + (beam_mult << 16); // set beam trigger mul
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // set PMT beam trigger pulse height
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_beam_thres + (beam_thres << 16); // set beam trigger peak
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    printf("\nEnabling/disabling channels...\n");
    // disable the top channels
    imod = imod_fem;
    ichip = 3;
    //en_top=0xffff;
    en_top = 0x0; // turn these off, channel 32-47 -JLS
    fprintf(outinfo, "en_top = 0x%x\n", en_top);
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_en_top + (en_top << 16); // enable/disable channel
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // disable the upper channels
    imod = imod_fem;
    ichip = 3;
    en_upper = 0xffff;
    //en_upper = 0x0; // turn off middle channel, ch 16-31
    fprintf(outinfo, "en_upper = 0x%x\n", en_upper);
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_en_upper + (en_upper << 16); // enable/disable channel
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // enable lower channel(s) as indicated above
    imod = imod_fem;
    ichip = 3;
    en_lower = 0x0;
    if (bg == 1)
    {
        if (bge == 1)
        {
            en_lower = en_lower + pow(2, bg_ch);
        } // beam trigger readout
    }
    if (tre == 1)
    {
        en_lower = en_lower + pow(2, trig_ch);
    } // ext trigger readout
    if (hg == 1)
    {
        en_lower = en_lower + pow(2, hg_ch);
    }
    //  if (lg==1) {en_lower = en_lower+4;} //up to 08/15
    if (lg == 1)
    {
        en_lower = en_lower + pow(2, lg_ch);
    }
    // 0xff00 channels not used in PAB shaper
    en_lower = 0xffff;
    //en_lower = 0x0; // turn off -JLS ch 0-15
    fprintf(outinfo, "en_lower = 0x%x\n", en_lower);
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_en_lower + (en_lower << 16); // enable/disable channel
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // set maximum block size
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_blocksize + (0xffff << 16); // set max block size
    fprintf(outinfo, "pmt_blocksize = 0x%x\n", 0xffff);
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    imod = imod_fem;
    ichip = 3;
    if (bg == 0)
        beam_size = 0x0; // this will actually output a few samples, doesn't completely disable it; that's why the request to unplug beam gate input from shaper, above
    else
    {
        printf("\nEnter beam size (1500 is nominal):\t");
        scanf("%d", &beam_size);
    }
    fprintf(outinfo, "gate_readout_size = %d\n", beam_size);
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_gate_size + (beam_size << 16); // set gate size = 1500
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // set beam delay
    imod = imod_fem;
    ichip = 3; // this inhibits the cosmic (discr 1) before the beam gate (64MHz clock)
    // 0x18=24 samples (should see no overlap)
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_beam_delay + (0x18 << 16); // set gate size
    fprintf(outinfo, "beam_delay = %d\n", 0x18);
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    // set beam size window
    imod = imod_fem;
    ichip = 3;
    beam_size = 0x66;                                                                     // This is the gate size (1.6us in 64MHz corresponds to 102.5 time ticks; use 102=0x66)
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_beam_size + (beam_size << 16); // set gate size
    fprintf(outinfo, "beam_size = %d\n", beam_size);
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    a_id = 0x20;
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_a_id + (a_id << 16); // set a_id
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    // enable hold
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_hold_enable + (0x1 << 16); // enable the hold
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    // work on the ADC -- set reset pulse
    imod = imod_fem;
    ichip = 5;
    buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_reset) + (0x0 << 16); // reset goes high
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    imod = imod_fem;
    ichip = 5;
    buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_reset) + (0x1 << 16); // reset goes low
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    imod = imod_fem;
    ichip = 5;
    buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_reset) + (0x0 << 16); // reset goes high
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // enable ADC clock,
    imod = imod_fem;
    ichip = 5;
    buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_spi_add) + (0x7 << 16); // set spi address
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    imod = imod_fem;
    ichip = 5;
    buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0xffff << 16); // load spi data, clock gate enable
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    // load ADC sync data pattern  + set MSB 1st
    for (is = 1; is < 7; is++)
    {
        imod = imod_fem;
        ichip = 5;
        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        imod = imod_fem;
        ichip = 5;
        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0b00 << 16); // sync pattern, b for sync, 7 for skew, 3 for normal
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        imod = imod_fem;
        ichip = 5;
        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x1400 << 16); // msb 1st
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    }
    usleep(5000); // (1ms->5ms JLS)

    // send FPGA ADC receiver reset
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_rxreset + (0x1 << 16); // FPGA ADC receiver reset on
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    // readback status
    i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py); // init the receiver

    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + 20 + (0x0 << 16); // read out status
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    py = read_array.data(); //&read_array[0];
    i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, py);
    printf("\nFEM STATUS -- after reset = %x, %x \n", read_array[0], read_array[1]);
    printf(" module = %d, command = %d \n", ((read_array[0] >> 11) & 0x1f), (read_array[0] & 0xff));
    printf(" ADC right dpa lock     %d \n", ((read_array[0] >> 17) & 0x1));
    printf(" ADC left  dpa lock     %d \n", ((read_array[0] >> 18) & 0x1));
    printf(" block error 2          %d \n", ((read_array[0] >> 19) & 0x1));
    printf(" block error 1          %d \n", ((read_array[0] >> 20) & 0x1));
    printf(" pll locked             %d \n", ((read_array[0] >> 21) & 0x1));
    printf(" supernova mem ready    %d \n", ((read_array[0] >> 22) & 0x1));
    printf(" beam      mem ready    %d \n", ((read_array[0] >> 23) & 0x1));
    printf(" ADC right PLL locked   %d \n", ((read_array[0] >> 24) & 0x1));
    printf(" ADC left  PLL locked   %d \n", ((read_array[0] >> 25) & 0x1));
    printf(" ADC align cmd right    %d \n", ((read_array[0] >> 26) & 0x1));
    printf(" ADC align cmd left     %d \n", ((read_array[0] >> 27) & 0x1));
    printf(" ADC align done right   %d \n", ((read_array[0] >> 28) & 0x1));
    printf(" ADC align done left    %d \n", ((read_array[0] >> 29) & 0x1));
    printf(" Neutrino data empty    %d \n", ((read_array[0] >> 30) & 0x1));
    printf(" Neutrino header empty  %d \n", ((read_array[0] >> 31) & 0x1));

    // send FPGA ADC align
    imod = imod_fem;
    ichip = 3;
    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pmt_align_pulse + (0x0 << 16); // FPGA ADC receiver reset off
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, px);

    usleep(5000); // wait for 5ms

    for (is = 1; is < 7; is++)
    {
        imod = imod_fem;
        ichip = 5;
        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        imod = imod_fem;
        ichip = 5;
        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0300 << 16); // sync pattern, b for sync, 7 for skew, 3 for normal
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
    }



    ///////////////////////////  BOOT CHARGE FEM //////////////////////////////////////////
    //
    //
            // //imod_xmit = imod_xmit + 1;
            for (imod_fem = (imod_tpc); imod_fem < (imod_st2 + 1); imod_fem++) //++ -> -- JLS
            //for (imod_fem = (imod_tpc); imod_fem > (imod_st2 - 1); imod_fem--) //++ -> -- JLS
            { // loop over module numbers
                imod = imod_fem;
                printf("\n Booting module in slot %d \n", imod);

                // turn on the Stratix III power supply
                ichip = 1;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_power_add + (0x0 << 16); // turn module 11 power on
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                usleep(200000); // wait for 200 ms
                FILE *inpf = fopen("/home/sabertooth/GramsReadoutFirmware/fem/module1x_140820_deb_fixbase_nf_8_31_2018.rbf", "r");
                // FILE *inpf = fopen("/home/ub/WinDriver/wizard/GRAMS_project/uBooNE_Nominal_Firmware/pmt_fem/module1x_pmt_64MHz_new_head_07162013.rbf", "r");
                ichip = mb_feb_conf_add;                                       // ichip=mb_feb_config_add(=2) is for configuration chip
                buf_send[0] = (imod << 11) + (ichip << 8) + 0x0 + (0x0 << 16); // turn conf to be on
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                usleep(2000); // wait for a while (1ms->2ms JLS)

                count = 0;   // keeps track of config file data sent
                counta = 0;  // keeps track of total config file data read
                ichip_c = 7; // this chip number is actually a "ghost"; it doesn't exist; it's just there so the config chip
                // doesn't treat the first data word i'll be sending as a command designated for the config chip (had i used ichip_c=1)
                nsend = 500; // this defines number of 32bit-word-sized packets I'm allowed to use to send config file data
                dummy1 = 0;

                while (fread(&charchannel, sizeof(char), 1, inpf) == 1)
                {
                    carray[count] = charchannel;
                    count++;
                    counta++;
                    if ((count % (nsend * 2)) == 0)
                    {
                        buf_send[0] = (imod << 11) + (ichip_c << 8) + (carray[0] << 16);
                        send_array[0] = buf_send[0];
                        if (dummy1 <= 5)
                            printf(" counta = %d, first word = %x, %x, %x %x %x \n", counta, buf_send[0], carray[0], carray[1], carray[2], carray[3]);
                        for (ij = 0; ij < nsend; ij++)
                        {
                            if (ij == (nsend - 1))
                                buf_send[ij + 1] = carray[2 * ij + 1] + (0x0 << 16);
                            else
                                buf_send[ij + 1] = carray[2 * ij + 1] + (carray[2 * ij + 2] << 16);
                            send_array[ij + 1] = buf_send[ij + 1];
                        }
                        nword = nsend + 1;
                        i = 1;
                        ij = pcie_interface->PCIeSendBuffer(1, i, nword, px);
                        nanosleep(&tim, &tim2);
                        dummy1 = dummy1 + 1;
                        count = 0;
                    }
                }

                if (feof(inpf))
                {
                    printf(" You have reached the end-of-file word count= %d %d\n", counta, count);
                    buf_send[0] = (imod << 11) + (ichip_c << 8) + (carray[0] << 16);
                    if (count > 1)
                    {
                        if (((count - 1) % 2) == 0)
                        {
                            ik = (count - 1) / 2;
                        }
                        else
                        {
                            ik = (count - 1) / 2 + 1;
                        }
                        ik = ik + 2; // add one more for safety
                        printf(" ik= %d\n", ik);
                        for (ij = 0; ij < ik; ij++)
                        {
                            if (ij == (ik - 1))
                                buf_send[ij + 1] = carray[(2 * ij) + 1] + (((imod << 11) + (ichip << 8) + 0x0) << 16);
                            else
                                buf_send[ij + 1] = carray[(2 * ij) + 1] + (carray[(2 * ij) + 2] << 16);
                            send_array[ij + 1] = buf_send[ij + 1];
                        }
                    }
                    else
                        ik = 1;

                    for (ij = ik - 10; ij < ik + 1; ij++)
                    {
                        printf(" Last data = %d, %x\n", ij, buf_send[ij]);
                    }

                    nword = ik + 1;
                    i = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, nword, px);
                }
                usleep(5000); // wait for 2ms to cover the packet time plus fpga init time (2ms->5ms JLS)
                fclose(inpf);
                printf(" Configuration for module in slot %d COMPLETE.\n", imod);
            }

            // if (print_debug == true)
            // {
            //     printf("\n fem boot debug : \n\n");
            //     printf("hDev %x\n", &hDev);
            //     printf("imod_fem %x\n", imod_fem);
            //     //printf("imod_xmit_in %x\n", imod_xmit_in);
            //     printf("mb_feb_power_add %x\n", mb_feb_power_add);
            //     printf("mb_feb_conf_add %x\n", mb_feb_conf_add);
            // }

            // imod_xmit = imod_xmit - 1;

            printf("\n...LIGHT FEB booting done \n");

            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ CHARGE FEM SETUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

            // // settup TPC FEM
            int adcdata = 1;
            int fakeadcdata = 0;
            int femfakedata = 0;
            // imod_xmit = imod_xmit+1;
            printf("\nSetting up TPC FEB's...\n");
            for (imod_fem = (imod_tpc); imod_fem < (imod_st2 + 1); imod_fem++) // ++ -> -- JLS
            //for (imod_fem = (imod_tpc); imod_fem > (imod_st2 - 1); imod_fem--) // ++ -> -- JLS
            {
                imod = imod_fem;
                ichip = 3;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_dram_reset + (0x1 << 16); // reset FEB DRAM (step 1) // turm the DRAM reset on
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                ichip = 3;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_dram_reset + (0x0 << 16); // reset FEB DRAM (step 2) // turm the DRAM reset off
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                usleep(5000); // wait for 5 ms for DRAM to be initialized

                ichip = 3;                                                                    // set module number
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_mod_number + (imod << 16); // set module number
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                if (adcdata == 1)
                {
                    printf("\nSetting up ADC, module %d\n", imod);
                    // Set ADC address 1
                    for (is = 0; is < 8; is++)
                    {

                        ichip = 5;                                                                         // ADC control
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_spi_add) + ((is & 0xf) << 16); // set ADC address (same op id =2 for PMT and TPC); second 16bit word is the address; the loop sets first 0-7 ADC addresses
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms
                        // The ADC spi stream: The 16 bits data before the last word is r/w, w1, w2 and 13 bits address;
                        //                     the last 16 bits.upper byte = 8 bits data and lower 8 bits ignored.
                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0300 << 16); // op id = 3; first data word is 16bit 1st next word will be overwrite by the next next word //sync pattern, b for sync, 7 for skew, 3 for normal
                        buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16);               // 8 more bits to fill 24-bit data word; data is in <<24 operation; r/w op goes to 13 0xd is address and data is <<24; top 8 bits bottom 8 bits discarded; 0xc is mixed frequency pattern  (04/25/12)
                        i = 1;
                        k = 2;
                        usleep(5000); // sleep for 2ms

                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);              // write to transfer register:  set r/w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1;
                        i = 1;
                        k = 2;
                        usleep(5000); // sleep for 2ms

                        // double terminate ADC driver
                        ichip = 5;                                                                         // ADC control
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_spi_add) + ((is & 0xf) << 16); // set ADC address (same op id =2 for PMT and TPC); second 16bit word is the address; the loop sets first 0-7 ADC addresses
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms

                    } // end ADC address set

                    for (is = 0; is < 8; is++) // Set ADC address 2
                    {
                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms
                        //       printf(" spi port %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        //            buf_send[1]=(((0x0)<<13)+(0xd))+((0x9)<<24)+((0x0)<<16);
                        buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16); // 0x0<<24 is for baseline //  set /w =0, w1,w2 =0, a12-a0 = 0xd, data =0xc (mixed frequency);
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms
                        //       printf(" spi port 2nd command %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);              //  set /w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1; //  write to transfer register
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms

                    } // end ADC address set
                    // Reset ADC receiver
                    ichip = 3;                                                                  // stratix III
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_adc_reset + (0x1 << 16); // FPGA ADC receiver reset on, cmd=1 on mb_feb_adc_reset=33
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                    usleep(5000);
                    // Align ADC receiver
                    ichip = 3;                                                                  // stratix III
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_adc_align + (0x0 << 16); // FPGA ADC align, cmd=0 on mb_feb_adc_align=1
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                    usleep(5000);

                    // Set ADC address 3
                    for (is = 0; is < 8; is++)
                    {
                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms
                        //       printf(" spi port %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        //            buf_send[1]=(((0x0)<<13)+(0xd))+((0x9)<<24)+((0x0)<<16);
                        if (fakeadcdata == 1)
                            buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16);
                        else
                            buf_send[1] = (((0x0) << 13) + (0xd)) + ((0x0) << 24) + ((0x0) << 16); // 0x0<<24 is for baseline //  set /w =0, w1,w2 =0, a12-a0 = 0xd, data =0xc (mixed frequency);
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms
                                      //       printf(" spi port 2nd command %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buf_send[0] = (imod << 11) + (ichip << 8) + (mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);              //  set /w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1; //  write to transfer register
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                        usleep(5000); // sleep for 2ms
                    } // end ADC address set

                } // end if ADC set

                nword = 1;
                i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py); // init the receiver

                // set test mode
                ichip = mb_feb_pass_add;
                if (femfakedata == 1)
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_test_source + (0x2 << 16); // set test source to 2
                else
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_test_source + (0x0 << 16);
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                // set compression state
                imod = imod_fem;
                ichip = 3;
                if (ihuff == 1)
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_b_nocomp + (0x0 << 16); // turn the compression
                else
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_b_nocomp + (0x1 << 16); // set b channel no compression
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                // sn loop xxx
                imod = imod_fem;
                ichip = 3;
                if (ihuff == 1)
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_a_nocomp + (0x0 << 16); // turn the compression
                else
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_a_nocomp + (0x1 << 16); // set b channel no compression
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                //******************************************************
                // set drift size
                ichip = 3;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_timesize + (timesize << 16); // set drift time size
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                // set id
                a_id = 0xf;
                ichip = 3;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_b_id + (a_id << 16); // set a_id
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                ichip = 4;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_b_id + (a_id << 16); // set b_id
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                // set max word in the pre-buffer memory
                ik = 8000;
                ichip = 3;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_max + (ik << 16); // set pre-buffer max word
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                // enable hold
                ichip = 3;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_hold_enable + (0x1 << 16); // enable the hold
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);

                // this was in SN loop
                if (imod == imod_st1)
                {
                    printf(" set last module, module address %d\n", imod);
                    ichip = 4;
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_lst_on + (0x0 << 16); // set last module on
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                }
                else
                {
                    printf(" set last module off, module address %d\n", imod);
                    ichip = 4;
                    buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_lst_off + (0x0 << 16); // set last module on
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                }
            } // end loop over FEMs
            // imod_xmit = imod_xmit-1;

            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ TRIGGER_SETUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

            /* FROM TPC PART*/
            imod = 0; // set offline test
            ichip = 1;
            buf_send[0] = (imod << 11) + (ichip << 8) + (mb_cntrl_test_on) + (0x0 << 16); // enable offline run on
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = imod_trig;
            buf_send[0] = (imod << 11) + (mb_trig_run) + ((0x0) << 16); // set up trigger module run off
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = imod_trig;
            buf_send[0] = (imod << 11) + (mb_trig_deadtime_size) + ((250 & 0xff) << 16); // set trigger module deadtime size
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = 0; // set offline test
            ichip = 1;
            buf_send[0] = (imod << 11) + (ichip << 8) + (mb_cntrl_test_off) + (0x0 << 16); // set controller test off
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = imod_trig; // Set number of ADC samples to (iframe+1)/8;
            iframe = iframe_length;
            buf_send[0] = (imod << 11) + (mb_trig_frame_size) + ((iframe & 0xffff) << 16);
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

            imod = 0; // load trig 1 position relative to the frame..
            ichip = 1;
            buf_send[0] = (imod << 11) + (ichip << 8) + (mb_cntrl_load_trig_pos) + ((itrig_delay & 0xffff) << 16); // enable test mode
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);


            if(trigtype){
            //Begin PMT Trigger setup

              imod = imod_trig;
              buf_send[0]=(imod<<11)+(mb_trig_mask1)+((mask1&0xf)<<16); //set mask1[3] on.
              i = 1;
              k = 1;
              i = pcie_interface->PCIeSendBuffer(1, i, k, px);
              fprintf(outinfo,"trig_mask1 = 0x%x\n",mask1);

              imod=imod_trig;
              buf_send[0]=(imod<<11)+(mb_trig_prescale1)+(0x0<<16); //set prescale1 0
              fprintf(outinfo,"trig_prescale1 = 0x%x\n",0x0);
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

              imod=imod_trig;
              buf_send[0]=(imod<<11)+(mb_trig_mask8)+((mask8&0xff)<<16);
              fprintf(outinfo,"trig_mask8 = 0x%x\n",mask8);
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

              imod=imod_trig;
              buf_send[0]=(imod<<11)+(mb_trig_prescale8)+(0x0<<16); //set prescale8 0
              fprintf(outinfo,"trig_prescale8 = 0x%x\n",0x0);
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

              //End PMT Trigger setup
            }
            else{
              //begin EXT Trigger setup as of 11/26/2024
              imod = imod_trig;
              buf_send[0] = (imod << 11) + (mb_trig_mask8) + (0x2 << 16); // set mask1[3] on.
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);

              imod = imod_trig;
              buf_send[0] = (imod << 11) + (mb_trig_prescale8) + (0x0 << 16); // set prescale1 0
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, px);
              //End EXT Trigger setup as of 11/26/2024
            }

            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ TRIGGER_SETUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

            /*   here is common block from tpc */
            for (imod_fem = (imod_st1); imod_fem > imod_xmit; imod_fem--) //     now reset all the link port receiver PLL
            {
                printf("%i \n", imod_fem);
                imod = imod_fem;
                printf("Resetting link PLL for module %x \n", imod);
                ichip = 4;
                buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_pll_reset + (0x0 << 16); // reset LINKIN PLL
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                usleep(2000); // give PLL time to reset (1ms->2ms JLS)
            }

            for (imod_fem = (imod_xmit + 1); imod_fem < (imod_st1 + 1); imod_fem++) // read back status
            {
                printf("%i \n", imod_fem);

                i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py); // init the receiver
                imod = imod_fem;
                ichip = 3;
                buf_send[0] = (imod << 11) + (ichip << 8) + 20 + (0x0 << 16); // read out status
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, px);
                py = read_array.data(); //&read_array[0];
                i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, py);
                printf("\nReceived FEB %d (slot %d) status data word = %x, %x \n", imod, imod, read_array[0], read_array[1]);

                printf("----------------------------\n");
                printf("FEB %d (slot %d) status \n", imod, imod);
                printf("----------------------------\n");
                printf("cmd return (20)       : %d\n", (read_array[0] & 0xFF));               // bits 7:0
                printf("check bits 10:8 (0)   : %d\n", ((read_array[0] >> 8) & 0x7));         // bits 10:8
                printf("module number (%d)    : %d\n", imod, ((read_array[0] >> 11) & 0x1F)); // bits 15:11
                printf("----------------------------\n");
                printf("check bit  0 (0)      : %d\n", (read_array[0] >> 16) & 0x1);
                printf("Right ADC DPA locked  : %d\n", (read_array[0] >> 17) & 0x1);
                printf("Left  ADC DPA locked  : %d\n", (read_array[0] >> 18) & 0x1);
                printf("SN pre-buf err        : %d\n", (read_array[0] >> 19) & 0x1);
                printf("Neutrino pre-buf err  : %d\n", (read_array[0] >> 20) & 0x1);
                printf("PLL locked            : %d\n", (read_array[0] >> 21) & 0x1);
                printf("SN memory ready       : %d\n", (read_array[0] >> 22) & 0x1);
                printf("Neutrino memory ready : %d\n", (read_array[0] >> 23) & 0x1);
                printf("ADC lock right        : %d\n", (read_array[0] >> 24) & 0x1);
                printf("ADC lock left         : %d\n", (read_array[0] >> 25) & 0x1);
                printf("ADC align right       : %d\n", (read_array[0] >> 26) & 0x1);
                printf("ADC align left        : %d\n", (read_array[0] >> 27) & 0x1);
                printf("check bits 15:12 (0)  : %d\n", (read_array[0] >> 28) & 0xf);
                printf("----------------------------\n");
            }

    for (imod_fem = (imod_st1); imod_fem > imod_xmit; imod_fem--)
    {
        printf("%i \n", imod_fem);
        imod = imod_fem;
        printf(" reset the link for module %d \n", imod);
        ichip = 4;
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_rxreset + (0x0 << 16); // reset LINKIN DPA
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        usleep(1000); // give DPA time to reset (1ms  JLS)

        ichip = 4;
        buf_send[0] = (imod << 11) + (ichip << 8) + mb_feb_align + (0x0 << 16); // send alignment command
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        usleep(1000); // give DPA time to align (1ms  JLS)
    }

    for (imod_fem = (imod_xmit + 1); imod_fem < (imod_st1 + 1); imod_fem++) {
        i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, py);
        imod = imod_fem;
        ichip = 3;
        buf_send[0] = (imod << 11) + (ichip << 8) + 20 + (0x0 << 16); // read out status
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, px);
        py = read_array.data(); //&read_array[0];
        i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, py); // read out 2 32 bits words
        printf("\nReceived FEB %d (slot %d) status data word = %x, %x \n", imod, imod, read_array[0], read_array[1]);
        printf("----------------------------\n");
        printf("FEB %d (slot %d) status \n", imod, imod);
        printf("----------------------------\n");
        printf("cmd return (20)       : %d\n", (read_array[0] & 0xFF));               // bits 7:0
        printf("check bits 10:8 (0)   : %d\n", ((read_array[0] >> 8) & 0x7));         // bits 10:8
        printf("module number (%d)    : %d\n", imod, ((read_array[0] >> 11) & 0x1F)); // bits 15:11
        printf("----------------------------\n");
        printf("check bit  0 (0)      : %d\n", (read_array[0] >> 16) & 0x1);
        printf("Right ADC DPA locked  : %d\n", (read_array[0] >> 17) & 0x1);
        printf("Left  ADC DPA locked  : %d\n", (read_array[0] >> 18) & 0x1);
        printf("SN pre-buf err        : %d\n", (read_array[0] >> 19) & 0x1);
        printf("Neutrino pre-buf err  : %d\n", (read_array[0] >> 20) & 0x1);
        printf("PLL locked            : %d\n", (read_array[0] >> 21) & 0x1);
        printf("SN memory ready       : %d\n", (read_array[0] >> 22) & 0x1);
        printf("Neutrino memory ready : %d\n", (read_array[0] >> 23) & 0x1);
        printf("ADC lock right        : %d\n", (read_array[0] >> 24) & 0x1);
        printf("ADC lock left         : %d\n", (read_array[0] >> 25) & 0x1);
        printf("ADC align right       : %d\n", (read_array[0] >> 26) & 0x1);
        printf("ADC align left        : %d\n", (read_array[0] >> 27) & 0x1);
        printf("check bits 15:12 (0)  : %d\n", (read_array[0] >> 28) & 0xf);
        printf("----------------------------\n");

    }

    return true;

    }


} // hw_config
