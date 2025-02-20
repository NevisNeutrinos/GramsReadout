//
// Created by Jon Sensenig on 1/29/25.
//

#include "configure_hardware.h"
#include <iostream>

namespace hw_config {

    ConfigureHardware::ConfigureHardware(pcie_int::PCIeInterface *pcie_interface) :
    pcie_interface_(pcie_interface),
    buffer_send_(std::make_unique<uint32_t[]>(ARR_SIZE)),
    send_array_(std::make_unique<uint32_t[]>(ARR_SIZE)),
    read_array_(std::make_unique<uint32_t[]>(ARR_SIZE)),
    char_array_(std::make_unique<unsigned char[]>(ARR_SIZE))
    {}

    ConfigureHardware::~ConfigureHardware() {
        delete pcie_interface_;
    }

    bool ConfigureHardware::Configure(Config& config) {
        std::cout << "ConfigureHardware::Configure" << std::endl;

        // Configure XMIT hardware
        XMITLoadFPGA(config);
        return false;
    }


    bool ConfigureHardware::XMITLoadFPGA(Config &config) {

        bool print_debug = false;
        static int ij;
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

        FILE *inpf = fopen("/home/sabertooth/GramsReadoutFirmware/xmit/readcontrol_110601_v3_play_header_hist_l1block_9_21_2018.rbf", "r"); // tpc readout akshay

        printf("\nBooting XMIT module...\n\n");
        // imod = imod_xmit;
        // ichip = mb_xmit_conf_add;
        buffer_send_[0] = (imod_xmit << 11) + (mb_xmit_conf_add << 8) + 0x0 + (0x0 << 16); // turn conf to be on
        i = 1;
        k = 1;
        // i = pcie_send(hDev, i, 0, k, p_send_);
        i = pcie_interface_->PCIeSendBuffer(kDev1, i, k, p_send_);

        // printf("\n 2am_hDev: %x",hDev);
        printf("\n 2am_i: %x",i);
        printf("\n 2am_px: %p\n",p_send_);

        usleep(2000); // wait for a while (1ms->2ms JLS)
        static int count = 0;
        static int counta = 0;
        static int nsend = 500;
        static int ichip_c = 7; // set ichip_c to stay away from any other command in the
        static int nword = 1;
        static int imod = imod_xmit;
        static int dummy1 = 0;

        if (print_debug == true)
        {
            printf("\n boot_xmit debug : \n");
            // printf("dmabufsize %x\n", dmabufsize);
            // printf("imod_xmit_in %x\n", imod_xmit_in);
            printf("mb_xmit_conf_add %x\n", mb_xmit_conf_add);
        }
        while (fread(&charchannel, sizeof(char), 1, inpf) == 1)
        {
            char_array_[count] = charchannel;
            count++;
            counta++;
            if ((count % (nsend * 2)) == 0)
            {
                buffer_send_[0] = (imod << 11) + (ichip_c << 8) + (char_array_[0] << 16);
                send_array_[0] = buffer_send_[0];
                if (dummy1 <= 5)
                    printf("\tcounta = %d, first word = %x, %x, %x %x %x \n", counta, buffer_send_[0], char_array_[0], char_array_[1], char_array_[2], char_array_[3]);

                for (static int ij = 0; ij < nsend; ij++)
                {
                    if (ij == (nsend - 1))
                        buffer_send_[ij + 1] = char_array_[2 * ij + 1] + (0x0 << 16);
                    else
                        buffer_send_[ij + 1] = char_array_[2 * ij + 1] + (char_array_[2 * ij + 2] << 16);
                    send_array_[ij + 1] = buffer_send_[ij + 1];
                }
                nword = nsend + 1;
                i = 1;
                // ij = pcie_send(hDev, i, 0, nword, p_send_);
                ij = pcie_interface_->PCIeSendBuffer(kDev1, i, nword, p_send_);
                nanosleep(&tim, &tim2);
                dummy1 = dummy1 + 1;
                count = 0;
            }
        }
        if (feof(inpf))
        {
            printf("\tend-of-file word count= %d %d\n", counta, count);
            buffer_send_[0] = (imod << 11) + (ichip_c << 8) + (char_array_[0] << 16);
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
                for (static int ij = 0; ij < ik; ij++)
                {
                    if (ij == (ik - 1))
                        buffer_send_[ij + 1] = char_array_[(2 * ij) + 1] + (((imod << 11) + (mb_xmit_conf_add << 8) + 0x0) << 16);
                    else
                        buffer_send_[ij + 1] = char_array_[(2 * ij) + 1] + (char_array_[(2 * ij) + 2] << 16);
                    send_array_[ij + 1] = buffer_send_[ij + 1];
                }
            }
            else
                ik = 1;
            for (static int ij = ik - 10; ij < ik + 1; ij++)
            {
                printf("\tlast data = %d, %x\n", ij, buffer_send_[ij]);
            }
            nword = ik + 1;
            i = 1;
            // i = pcie_send(hDev, i, 0, nword, p_send_);
            i = pcie_interface_->PCIeSendBuffer(kDev1, i, nword, p_send_);
        }
        usleep(3000); // wait for 2ms to cover the packet time plus fpga init time (change to 3ms JLS)
        fclose(inpf);

        nword = 1;
        // i = pcie_rec(hDev, 0, 1, nword, iprint, p_recv_); // init the receiver
        i = pcie_interface_->PCIeRecvBuffer(kDev1, 0, 1, nword, iprint, p_recv_);
        nword = 1;
        // i = pcie_rec(hDev, 0, 1, nword, iprint, p_recv_); // init the receiver
        i = pcie_interface_->PCIeRecvBuffer(kDev1, 0, 1, nword, iprint, p_recv_);
        imod = imod_xmit;
        static long ichip = 3;
        buffer_send_[0] = (imod << 11) + (ichip << 8) + mb_xmit_rdstatus + (0x0 << 16); // read out status

        i = 1; // This is status read
        k = 1;
        // i = pcie_send(hDev, i, 0, k, p_send_);
        i = pcie_interface_->PCIeSendBuffer(kDev1, i, k, p_send_);
        p_recv_ = &read_array_[0];
        // i = pcie_rec(hDev, 0, 2, nword, iprint, p_recv_); // read out 2 32 bits words
        i = pcie_interface_->PCIeRecvBuffer(kDev1, 0, 2, nword, iprint, p_recv_);
        printf("XMIT status word = %x, %x \n", read_array_[0], read_array_[1]);
        uint32_t first_word = read_array_[0];
        if( ((first_word>>5) & 0x7) || (((first_word>>8) & 0xff) != 0xff) || ((first_word>>21) & 0x1) ) {
            printf("Unexpected XMIT status word!");
        }
        printf("\nXMIT STATUS -- Pre-Setup = %x, %x \n", read_array_[0], read_array_[1]);
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
    ///////////////////////////////
        return false;
    }

} // hw_config
