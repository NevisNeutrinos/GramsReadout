//
// Created by Jon Sensenig on 2/24/25.
//

#include "xmit_control.h"
#include <memory>
#include <iostream>
#include <unistd.h>

namespace xmit_control {

    bool XmitControl::Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {

        static long imod, ichip;
        static uint32_t i, k, iprint;
        static int count, nword;
        bool print_debug = true;
        iprint = 0;

        static int imod_xmit = config["crate"]["xmit_slot"].get<int>();
        static int imod_st1  = config["crate"]["last_light_slot"].get<int>();  //st1 corresponds to last pmt slot (closest to xmit)
        std::string fw_file = config["xmit"]["fpga_bitfile"].get<std::string>();

        /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ XMIT BOOT  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

        printf("\nBooting XMIT module...\n\n");
        imod = imod_xmit;
        ichip = hw_consts::mb_xmit_conf_add;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 0x0 + (0x0 << 16); // turn conf to be on
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        std::cout << "2am i: " << i << std::endl;
        std::cout << "2am psend: " << buffers.psend << std::endl;

        LoadFirmware(imod_xmit, hw_consts::mb_xmit_conf_add, fw_file, pcie_interface, buffers);

        nword = 1;
        i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv);
        nword = 1;
        i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv);
        imod = imod_xmit;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_rdstatus + (0x0 << 16); // read out status

        i = 1; // This is status read
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
        i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv);
        printf("XMIT status word = %x, %x \n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);
        // printf("\nDo you want to read out TPC or PMT? Enter 0 for TPC, 1 for PMT, 2 for both\n");
        // scanf("%i", &readtype);

        uint32_t first_word = pcie_int::PcieBuffers::read_array[0];
        if( ((first_word>>5) & 0x7) || (((first_word>>8) & 0xff) != 0xff) || ((first_word>>21) & 0x1) ) {
          printf("Unexpected XMIT status word!");
        }
        printf("\nXMIT STATUS -- Pre-Setup = %x, %x \n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);
        printf(" pll locked          %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 16) & 0x1));
        printf(" receiver lock       %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 17) & 0x1));
        printf(" DPA lock            %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 18) & 0x1));
        printf(" NU Optical lock     %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 19) & 0x1));
        printf(" SN Optical lock     %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 20) & 0x1));
        printf(" SN Busy             %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 22) & 0x1));
        printf(" NU Busy             %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 23) & 0x1));
        printf(" SN2 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 24) & 0x1));
        printf(" SN1 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 25) & 0x1));
        printf(" NU2 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 26) & 0x1));
        printf(" NU1 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 27) & 0x1));
        printf(" Timeout1            %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 28) & 0x1));
        printf(" Timeout2            %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 29) & 0x1));
        printf(" Align1              %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 30) & 0x1));
        printf(" Align2              %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 31) & 0x1));

        /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ XMIT SETTUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

        // uint32_t read_array[dmabufsize];
        printf("Setting up XMIT module\n");
        imod = imod_xmit; // XMIT setup
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_modcount + ((imod_st1 - imod_xmit - 1) << 16); //                  -- number of FEM module -1, counting start at 0
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod = imod_xmit; //     reset optical
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_opt_dig_reset + (0x1 << 16); // set optical reset on
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod = imod_xmit; //     enable Neutrino Token Passing
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_enable_1 + (0x1 << 16); // enable token 1 pass
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_enable_2 + (0x0 << 16); // disable token 2 pass
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod = imod_xmit; //       reset XMIT LINK IN DPA
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_link_pll_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        usleep(10000); // 1ms ->10ms JLS

        imod = imod_xmit; //     reset XMIT LINK IN DPA
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_link_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        usleep(10000); //     wait for 10ms just in case

        imod = imod_xmit; //     reset XMIT FIFO reset
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_dpa_fifo_reset + (0x1 << 16); //  reset XMIT LINK IN DPA
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod = imod_xmit; //      test re-align circuit
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_dpa_word_align + (0x1 << 16); //  send alignment pulse
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        usleep(10000); // wait for 5 ms  (5ms ->10ms JLS)

        nword = 1;
        i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv); // init the receiver
        imod = imod_xmit;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_rdstatus + (0x0 << 16); // read out status

        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
        i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv); // read out 2 32 bits words
        printf("XMIT status word = %x, %x \n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);

        printf("\nXMIT STATUS -- Post-Setup = %x, %x \n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);
        printf(" Crate Number        %d \n", ((pcie_int::PcieBuffers::read_array[0]) & 0xE0));
        printf(" pll locked          %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 16) & 0x1));
        printf(" receiver lock       %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 17) & 0x1));
        printf(" DPA lock            %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 18) & 0x1));
        printf(" NU Optical lock     %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 19) & 0x1));
        printf(" SN Optical lock     %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 20) & 0x1));
        printf(" SN Busy             %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 22) & 0x1));
        printf(" NU Busy             %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 23) & 0x1));
        printf(" SN2 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 24) & 0x1));
        printf(" SN1 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 25) & 0x1));
        printf(" NU2 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 26) & 0x1));
        printf(" NU1 Optical         %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 27) & 0x1));
        printf(" Timeout1            %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 28) & 0x1));
        printf(" Timeout2            %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 29) & 0x1));
        printf(" Align1              %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 30) & 0x1));
        printf(" Align2              %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 31) & 0x1));

        //////////
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_xmit_rdcounters + (0x0 << 16); // read out counters

        i = 1;
        k = 1;
        nword = 5;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        buffers.precv = pcie_int::PcieBuffers::read_array.data();
        i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv); // read out 2 32 bits words
        printf("XMIT counter word = %x, %x \n", pcie_int::PcieBuffers::read_array[0],
               pcie_int::PcieBuffers::read_array[1]);

        printf("\nXMIT Counter -- Post-Setup = %x, %x \n", pcie_int::PcieBuffers::read_array[0],
               pcie_int::PcieBuffers::read_array[1]);
        printf(" Crate Number        %d \n", ((pcie_int::PcieBuffers::read_array[0]) & 0x1F));
        printf(" SN Frame Ctr        %d \n", ((pcie_int::PcieBuffers::read_array[1]) & 0xFFFFFF));
        printf(" Nu Frame Ctr        %d \n", ((pcie_int::PcieBuffers::read_array[2]) & 0xFFFFFF));
        printf(" Token Path 1 Ctr    %d \n", ((pcie_int::PcieBuffers::read_array[3]) & 0xFFFFFF));
        printf(" Token Path 2 Ctr    %d \n", ((pcie_int::PcieBuffers::read_array[4]) & 0xFFFFFF));

        usleep(10000);
        printf("\n...XMIT setup complete\n");

        if (print_debug == true) {
            printf("\n setup_xmit debug : \n");
            printf("mb_xmit_modcount %x\n", hw_consts::mb_xmit_modcount);
            printf("mb_opt_dig_reset %x\n", hw_consts::mb_opt_dig_reset);
            printf("mb_xmit_enable_1 %x\n", hw_consts::mb_xmit_enable_1);
            printf("mb_xmit_link_pll_reset %x\n", hw_consts::mb_xmit_link_pll_reset);
            printf("mb_xmit_link_reset %x\n", hw_consts::mb_xmit_link_reset);
            printf("mb_xmit_dpa_fifo_reset %x\n", hw_consts::mb_xmit_dpa_fifo_reset);
            printf("mb_xmit_dpa_word_align %x\n", hw_consts::mb_xmit_dpa_word_align);
            printf("mb_xmit_rdstatus %x\n", hw_consts::mb_xmit_rdstatus);
            printf("mb_xmit_rdstatus %x\n", hw_consts::mb_xmit_rdstatus);
        }

        return true;
    }

    std::vector<uint32_t> XmitControl::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool XmitControl::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }
} // xmit_control
