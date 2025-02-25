//
// Created by Jon Sensenig on 2/24/25.
//

#include "charge_fem.h"

#include <iostream>
#include <ostream>
#include <unistd.h>

namespace charge_fem {

    bool ChargeFem::Configure(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {

        static long imod, ichip;
        static uint32_t i, k, iprint, ik, is;
        static int count, counta, nword;
        static int ij, nsend;
        static int ichip_c, dummy1;
        unsigned char charchannel;
        static int ihuff = 0;
        struct timespec tim ={0, 100}, tim2 = {0, 100};

        FILE *inpf;
        iprint = 0;
        bool print_debug = true;

        static int imod_fem;
        static int imod_xmit   = 12;
        static int imod_st1    = 16;  //st1 corresponds to last pmt slot (closest to xmit)
        static int imod_st2    = 15;  //st2 corresponds to last tpc slot (farthest to XMIT)
        static int imod_pmt    = 16;
        static int imod_tpc    = 13;  // tpc slot closest to XMIT
        static int imod_trig   = 11;
        static int imod_shaper = 17;

        static int timesize, a_id, itrig_delay;
        timesize = 255;

        static int iframe, iframe_length;
        iframe_length = 2047;

        struct timeval;
        time_t rawtime;
        char timestr[500];
        char subrun[50];
        int trigtype;
        time(&rawtime);

        strftime(timestr, sizeof(timestr), "%Y_%m_%d", localtime(&rawtime));
        printf("\n ######## SiPM+TPC XMIT Readout \n\n Enter SUBRUN NUMBER or NAME:\t");
        scanf("%s", &subrun);
        printf("\n Enter 0 for EXT trigger or 1 for triggers issued by SiPM ADC:\t");
        scanf("%i", &trigtype);

        static int mask1, mask8;
        if(trigtype){
            //PMT Trigger
            mask8 = 0x0;
            mask1 = 0x8;  //this is either 0x1 (PMT beam) 0x4 (PMT cosmic) or 0x8 (PMT all)
        } else{
            //EXT trigger
            mask1 = 0x0;
            mask8 = 0x2;
        }

    ///////////////////////////  BOOT CHARGE FEM //////////////////////////////////////////
    //
    //
            // //imod_xmit = imod_xmit + 1;
            for (imod_fem = (imod_tpc); imod_fem < (imod_st2 + 1); imod_fem++) //++ -> -- JLS
            //for (imod_fem = (imod_tpc); imod_fem > (imod_st2 - 1); imod_fem--) //++ -> -- JLS
            { // loop over module numbers
                imod = imod_fem;
                std::cout << " Booting module in slot " << imod << std::endl;

                // turn on the Stratix III power supply
                ichip = 1;
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_power_add + (0x0 << 16); // turn module 11 power on
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                usleep(200000); // wait for 200 ms
                FILE *inpf = fopen("/home/sabertooth/GramsReadoutFirmware/fem/module1x_140820_deb_fixbase_nf_8_31_2018.rbf", "r");
                ichip = hw_consts::mb_feb_conf_add;                                       // ichip=mb_feb_config_add(=2) is for configuration chip
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 0x0 + (0x0 << 16); // turn conf to be on
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                usleep(2000); // wait for a while (1ms->2ms JLS)

                count = 0;   // keeps track of config file data sent
                counta = 0;  // keeps track of total config file data read
                ichip_c = 7; // this chip number is actually a "ghost"; it doesn't exist; it's just there so the config chip
                // doesn't treat the first data word i'll be sending as a command designated for the config chip (had i used ichip_c=1)
                nsend = 500; // this defines number of 32bit-word-sized packets I'm allowed to use to send config file data
                dummy1 = 0;

                while (fread(&charchannel, sizeof(char), 1, inpf) == 1)
                {
                    buffers.carray[count] = charchannel;
                    count++;
                    counta++;
                    if ((count % (nsend * 2)) == 0)
                    {
                        buffers.buf_send[0] = (imod << 11) + (ichip_c << 8) + (buffers.carray[0] << 16);
                        pcie_int::PcieBuffers::send_array[0] = buffers.buf_send[0];
                        if (dummy1 <= 5)
                            printf(" counta = %d, first word = %x, %x, %x %x %x \n", counta, buffers.buf_send[0],
                                buffers.carray[0], buffers.carray[1], buffers.carray[2], buffers.carray[3]);
                        for (ij = 0; ij < nsend; ij++)
                        {
                            if (ij == (nsend - 1))
                                buffers.buf_send[ij + 1] = buffers.carray[2 * ij + 1] + (0x0 << 16);
                            else
                                buffers.buf_send[ij + 1] = buffers.carray[2 * ij + 1] + (buffers.carray[2 * ij + 2] << 16);
                            pcie_int::PcieBuffers::send_array[ij + 1] = buffers.buf_send[ij + 1];
                        }
                        nword = nsend + 1;
                        i = 1;
                        ij = pcie_interface->PCIeSendBuffer(1, i, nword, buffers.psend);
                        nanosleep(&tim, &tim2);
                        dummy1 = dummy1 + 1;
                        count = 0;
                    }
                }

                if (feof(inpf))
                {
                    printf(" You have reached the end-of-file word count= %d %d\n", counta, count);
                    buffers.buf_send[0] = (imod << 11) + (ichip_c << 8) + (buffers.carray[0] << 16);
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
                        // std::cout << " ik= " << ik << std::endl;
                        for (ij = 0; ij < ik; ij++)
                        {
                            if (ij == (ik - 1))
                                buffers.buf_send[ij + 1] = buffers.carray[(2 * ij) + 1] + (((imod << 11) + (ichip << 8) + 0x0) << 16);
                            else
                                buffers.buf_send[ij + 1] = buffers.carray[(2 * ij) + 1] + (buffers.carray[(2 * ij) + 2] << 16);
                            pcie_int::PcieBuffers::send_array[ij + 1] = buffers.buf_send[ij + 1];
                        }
                    }
                    else
                        ik = 1;

                    for (ij = ik - 10; ij < ik + 1; ij++)
                    {
                        printf(" Last data = %d, %x\n", ij, buffers.buf_send[ij]);
                    }

                    nword = ik + 1;
                    i = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, nword, buffers.psend);
                }
                usleep(5000); // wait for 2ms to cover the packet time plus fpga init time (2ms->5ms JLS)
                fclose(inpf);
                std::cout << " Configuration for module in slot " << imod << " COMPLETE." << std::endl;
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
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_dram_reset + (0x1 << 16); // reset FEB DRAM (step 1) // turm the DRAM reset on
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                ichip = 3;
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_dram_reset + (0x0 << 16); // reset FEB DRAM (step 2) // turm the DRAM reset off
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                usleep(5000); // wait for 5 ms for DRAM to be initialized

                ichip = 3;                                                                    // set module number
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_mod_number + (imod << 16); // set module number
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                if (adcdata == 1)
                {
                    std::cout << "Setting up ADC, module " << imod << std::endl;
                    // Set ADC address 1
                    for (is = 0; is < 8; is++)
                    {

                        ichip = 5;                                                                         // ADC control
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_spi_add) + ((is & 0xf) << 16); // set ADC address (same op id =2 for PMT and TPC); second 16bit word is the address; the loop sets first 0-7 ADC addresses
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms
                        // The ADC spi stream: The 16 bits data before the last word is r/w, w1, w2 and 13 bits address;
                        //                     the last 16 bits.upper byte = 8 bits data and lower 8 bits ignored.
                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0300 << 16); // op id = 3; first data word is 16bit 1st next word will be overwrite by the next next word //sync pattern, b for sync, 7 for skew, 3 for normal
                        buffers.buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16);               // 8 more bits to fill 24-bit data word; data is in <<24 operation; r/w op goes to 13 0xd is address and data is <<24; top 8 bits bottom 8 bits discarded; 0xc is mixed frequency pattern  (04/25/12)
                        i = 1;
                        k = 2;
                        usleep(5000); // sleep for 2ms

                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        buffers.buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);              // write to transfer register:  set r/w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1;
                        i = 1;
                        k = 2;
                        usleep(5000); // sleep for 2ms

                        // double terminate ADC driver
                        ichip = 5;                                                                         // ADC control
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_spi_add) + ((is & 0xf) << 16); // set ADC address (same op id =2 for PMT and TPC); second 16bit word is the address; the loop sets first 0-7 ADC addresses
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms

                    } // end ADC address set

                    for (is = 0; is < 8; is++) // Set ADC address 2
                    {
                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms
                        //       printf(" spi port %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        //            buffers.buf_send[1]=(((0x0)<<13)+(0xd))+((0x9)<<24)+((0x0)<<16);
                        buffers.buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16); // 0x0<<24 is for baseline //  set /w =0, w1,w2 =0, a12-a0 = 0xd, data =0xc (mixed frequency);
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms
                        //       printf(" spi port 2nd command %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        buffers.buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);              //  set /w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1; //  write to transfer register
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms

                    } // end ADC address set
                    // Reset ADC receiver
                    ichip = 3;                                                                  // stratix III
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_adc_reset + (0x1 << 16); // FPGA ADC receiver reset on, cmd=1 on mb_feb_adc_reset=33
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000);
                    // Align ADC receiver
                    ichip = 3;                                                                  // stratix III
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_adc_align + (0x0 << 16); // FPGA ADC align, cmd=0 on mb_feb_adc_align=1
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000);

                    // Set ADC address 3
                    for (is = 0; is < 8; is++)
                    {
                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
                        i = 1;
                        k = 1;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms
                        //       printf(" spi port %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        //            buffers.buf_send[1]=(((0x0)<<13)+(0xd))+((0x9)<<24)+((0x0)<<16);
                        if (fakeadcdata == 1)
                            buffers.buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16);
                        else
                            buffers.buf_send[1] = (((0x0) << 13) + (0xd)) + ((0x0) << 24) + ((0x0) << 16); // 0x0<<24 is for baseline //  set /w =0, w1,w2 =0, a12-a0 = 0xd, data =0xc (mixed frequency);
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms
                                      //       printf(" spi port 2nd command %d \n",is);
                        //       scanf("%d",&ik);

                        ichip = 5;
                        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0300 << 16); // 1st next word will be overwrite by the next next word
                        buffers.buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);              //  set /w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1; //  write to transfer register
                        i = 1;
                        k = 2;
                        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                        usleep(5000); // sleep for 2ms
                    } // end ADC address set

                } // end if ADC set

                nword = 1;
                i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv); // init the receiver

                // set test mode
                ichip = hw_consts::mb_feb_pass_add;
                if (femfakedata == 1)
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_test_source + (0x2 << 16); // set test source to 2
                else
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_test_source + (0x0 << 16);
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                // set compression state
                imod = imod_fem;
                ichip = 3;
                if (ihuff == 1)
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_b_nocomp + (0x0 << 16); // turn the compression
                else
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_b_nocomp + (0x1 << 16); // set b channel no compression
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                // sn loop xxx
                imod = imod_fem;
                ichip = 3;
                if (ihuff == 1)
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_a_nocomp + (0x0 << 16); // turn the compression
                else
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_a_nocomp + (0x1 << 16); // set b channel no compression
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                //******************************************************
                // set drift size
                ichip = 3;
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_timesize + (timesize << 16); // set drift time size
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                // set id
                a_id = 0xf;
                ichip = 3;
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_b_id + (a_id << 16); // set a_id
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                ichip = 4;
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_b_id + (a_id << 16); // set b_id
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                // set max word in the pre-buffer memory
                ik = 8000;
                ichip = 3;
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_max + (ik << 16); // set pre-buffer max word
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                // enable hold
                ichip = 3;
                buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_hold_enable + (0x1 << 16); // enable the hold
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

                // this was in SN loop
                if (imod == imod_st1)
                {
                    std::cout << "set last module, module address: " << imod << std::endl;
                    ichip = 4;
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_lst_on + (0x0 << 16); // set last module on
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                }
                else
                {
                    std::cout << "set last module off, module address: " << imod << std::endl;
                    ichip = 4;
                    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_lst_off + (0x0 << 16); // set last module on
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                }
            } // end loop over FEMs
            // imod_xmit = imod_xmit-1;

            /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ TRIGGER_SETUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

            /* FROM TPC PART*/
            imod = 0; // set offline test
            ichip = 1;
            buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_cntrl_test_on) + (0x0 << 16); // enable offline run on
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            imod = imod_trig;
            buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x0) << 16); // set up trigger module run off
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            imod = imod_trig;
            buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_deadtime_size) + ((250 & 0xff) << 16); // set trigger module deadtime size
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            imod = 0; // set offline test
            ichip = 1;
            buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_cntrl_test_off) + (0x0 << 16); // set controller test off
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            imod = imod_trig; // Set number of ADC samples to (iframe+1)/8;
            iframe = iframe_length;
            buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_frame_size) + ((iframe & 0xffff) << 16);
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            imod = 0; // load trig 1 position relative to the frame..
            ichip = 1;
            buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_cntrl_load_trig_pos) + ((itrig_delay & 0xffff) << 16); // enable test mode
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);


            if(trigtype){
            //Begin PMT Trigger setup

              imod = imod_trig;
              buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_mask1)+((mask1&0xf)<<16); //set mask1[3] on.
              i = 1;
              k = 1;
              i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

              imod=imod_trig;
              buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_prescale1)+(0x0<<16); //set prescale1 0
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

              imod=imod_trig;
              buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_mask8)+((mask8&0xff)<<16);
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

              imod=imod_trig;
              buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_prescale8)+(0x0<<16); //set prescale8 0
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

              //End PMT Trigger setup
            }
            else{
              //begin EXT Trigger setup as of 11/26/2024
              imod = imod_trig;
              buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_mask8) + (0x2 << 16); // set mask1[3] on.
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

              imod = imod_trig;
              buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_prescale8) + (0x0 << 16); // set prescale1 0
              i = 1;
              k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
              //End EXT Trigger setup as of 11/26/2024
            }

        return true;
    }

    std::vector<uint32_t> ChargeFem::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool ChargeFem::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }
} // charge_fem