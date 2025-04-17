//
// Created by Jon Sensenig on 2/24/25.
//

#include "charge_fem.h"

#include <iostream>
#include <ostream>
#include <unistd.h>
#include "quill/LogMacros.h"

namespace charge_fem {

    ChargeFem::ChargeFem() {
        logger_ = quill::Frontend::create_or_get_logger("readout_logger");
    }

    uint32_t ChargeFem::ConstructSendWord(uint32_t module, uint32_t chip, uint32_t command, uint32_t data) {
        return (module << 11) + (chip << 8) + command +data;
    }


    bool ChargeFem::Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {

        static long imod, ichip;
        static uint32_t i, k, iprint, ik, is;
        static int nword;
        static int ihuff = 0;
        iprint = 0;

        static int imod_fem;
        static int imod_xmit = config["crate"]["xmit_slot"].get<int>();
        static int imod_st1  = config["crate"]["last_light_slot"].get<int>();  //st1 corresponds to last SiPM slot (closest to xmit)
        static int imod_st2  = config["crate"]["last_charge_slot"].get<int>();  //st2 corresponds to last TPC slot (farthest to XMIT)
        static int imod_tpc  = config["crate"]["charge_fem_slot"].get<int>();  // tpc slot closest to XMIT

        static int a_id;
        static int timesize = config["readout_windows"]["timesize"].get<int>();
        std::string fw_file = config["charge_fem"]["fpga_bitfile"].get<std::string>();

        ///////////////////////////  BOOT CHARGE FEM //////////////////////////////////////////

        // //imod_xmit = imod_xmit + 1;
        for (imod_fem = (imod_tpc); imod_fem < (imod_st2 + 1); imod_fem++)
        { // loop over module numbers
            imod = imod_fem;
            LOG_INFO(logger_, "Booting module in slot [{}] \n", imod);


            // turn on the Stratix III power supply
            ichip = 1;
            // turn module 11 power on
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_power_add, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            usleep(200000); // wait for 200 ms

            LOG_INFO(logger_, "Loading FPGA bitfile: \n", fw_file);
            // FILE *inpf = fopen(config["charge_fem"]["fpga_bitfile"].get<std::string>().c_str(), "r");

            ichip = hw_consts::mb_feb_conf_add;    // ichip=mb_feb_config_add(=2) is for configuration chip
            // turn conf to be on
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, 0x0, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            usleep(2000); // wait for a while (1ms->2ms JLS)

            LoadFirmware(imod, hw_consts::mb_feb_conf_add, fw_file, pcie_interface, buffers);
            LOG_INFO(logger_, "Configuration for module in slot [{}] COMPLETE \n", imod);
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

        LOG_INFO(logger_, "LIGHT FEB booting done.. \n");

        /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ CHARGE FEM SETUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

        // // settup TPC FEM
        int adcdata = 1;
        int fakeadcdata = 0;
        int femfakedata = 0;
        // imod_xmit = imod_xmit+1;
        LOG_INFO(logger_, "Setting up TPC FEB's...\n");
        for (imod_fem = (imod_tpc); imod_fem < (imod_st2 + 1); imod_fem++) // ++ -> -- JLS
        //for (imod_fem = (imod_tpc); imod_fem > (imod_st2 - 1); imod_fem--) // ++ -> -- JLS
        {
            imod = imod_fem;
            ichip = 3;
            // reset FEB DRAM (step 1) // turm the DRAM reset on
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_dram_reset, (0x1 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            ichip = 3;
            // reset FEB DRAM (step 2) // turm the DRAM reset off
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_dram_reset, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            usleep(5000); // wait for 5 ms for DRAM to be initialized

            ichip = 3;
            // set module number
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_mod_number, (imod << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            if (adcdata == 1)
            {
                LOG_INFO(logger_, "Setting up ADC module [{}] \n", imod);
                // Set ADC address 1
                for (is = 0; is < 8; is++)
                {

                    ichip = 5; // ADC control
                    // set ADC address (same op id =2 for PMT and TPC); second 16bit word is the address; the loop sets first 0-7 ADC addresses
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_spi_add, ((is & 0xf) << 16));
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000); // sleep for 2ms
                    // The ADC spi stream: The 16 bits data before the last word is r/w, w1, w2 and 13 bits address;
                    //                     the last 16 bits.upper byte = 8 bits data and lower 8 bits ignored.
                    ichip = 5;
                    // op id = 3; first data word is 16bit 1st next word will be overwrite by the next next word //sync pattern, b for sync, 7 for skew, 3 for normal
                    // 8 more bits to fill 24-bit data word; data is in <<24 operation; r/w op goes to 13 0xd is address and data is <<24; top 8 bits bottom 8 bits discarded; 0xc is mixed frequency pattern  (04/25/12)
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_adc_data_load, (0x0300 << 16));
                    buffers.buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16);
                    i = 1;
                    k = 2;
                    usleep(5000); // sleep for 2ms

                    ichip = 5;
                    // 1st next word will be overwrite by the next next word
                    // write to transfer register:  set r/w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1;
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_adc_data_load, (0x0300 << 16));
                    buffers.buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);
                    i = 1;
                    k = 2;
                    usleep(5000); // sleep for 2ms

                    // double terminate ADC driver
                    ichip = 5;  // ADC control
                    // set ADC address (same op id =2 for PMT and TPC); second 16bit word is the address; the loop sets first 0-7 ADC addresses
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_spi_add, (is & 0xf) << 16);
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000); // sleep for 2ms

                } // end ADC address set

                for (is = 0; is < 8; is++) // Set ADC address 2
                {
                    ichip = 5;
                    // set spi address
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_spi_add, (is & 0xf) << 16);
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000); // sleep for 2ms
                    //       printf(" spi port %d \n",is);
                    //       scanf("%d",&ik);

                    ichip = 5;
                    // 1st next word will be overwrite by the next next word
                    // 0x0<<24 is for baseline //  set /w=0, w1,w2=0, a12-a0 = 0xd, data=0xc (mixed frequency);
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_adc_data_load, (0x0300 << 16));
                    buffers.buf_send[1] = (((0x0) << 13) + (0xd)) + ((0xc) << 24) + ((0x0) << 16);
                    i = 1;
                    k = 2;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000); // sleep for 2ms
                    //       printf(" spi port 2nd command %d \n",is);
                    //       scanf("%d",&ik);

                    ichip = 5;
                    // 1st next word will be overwrite by the next next word
                    //  set /w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1; //  write to transfer register
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_adc_data_load, (0x0300 << 16));
                    buffers.buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);
                    i = 1;
                    k = 2;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000); // sleep for 2ms

                } // end ADC address set
                // Reset ADC receiver
                ichip = 3; // stratix III
                // FPGA ADC receiver reset on, cmd=1 on mb_feb_adc_reset=33
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_adc_reset, (0x1 << 16));
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                usleep(5000);
                // Align ADC receiver
                ichip = 3;  // stratix III
                // FPGA ADC align, cmd=0 on mb_feb_adc_align=1
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_adc_align, (0x0 << 16));
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                usleep(5000);

                // Set ADC address 3
                for (is = 0; is < 8; is++)
                {
                    ichip = 5;
                    // set spi address
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_spi_add, ((is & 0xf) << 16));
                    i = 1;
                    k = 1;
                    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
                    usleep(5000); // sleep for 2ms
                    //       printf(" spi port %d \n",is);
                    //       scanf("%d",&ik);

                    ichip = 5;
                    // 1st next word will be overwrite by the next next word
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_adc_data_load, (0x0300 << 16));
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
                    // 1st next word will be overwrite by the next next word
                    //  set /w =0, w1,w2 =0, a12-a0 = 0xff, data =0x1; //  write to transfer register
                    buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_pmt_adc_data_load, (0x0300 << 16));
                    buffers.buf_send[1] = (((0x0) << 13) + (0xff)) + ((0x1) << 24) + ((0x0) << 16);
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
            if (femfakedata == 1) // set test source to 2
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_test_source, (0x2 << 16));
            else
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_test_source, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            // set compression state
            imod = imod_fem;
            ichip = 3;
            if (ihuff == 1)
                // turn the compression
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_b_nocomp, (0x0 << 16));
            else
                // set b channel no compression
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_b_nocomp, (0x1 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            // sn loop xxx
            imod = imod_fem;
            ichip = 3;
            if (ihuff == 1)
                // turn the compression
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_a_nocomp, (0x0 << 16));
            else
                // set b channel no compression
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_a_nocomp, (0x1 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            //******************************************************
            // set drift size
            ichip = 3;
            // set drift time size
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_timesize, (timesize << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            // set id
            a_id = 0xf;
            ichip = 3;
            // set a_id
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_b_id, (a_id << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            ichip = 4;
            // set b_id
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_b_id, (a_id << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            // set max word in the pre-buffer memory
            ik = 8000;
            ichip = 3;
            // set pre-buffer max word
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_max, (ik << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            // enable hold
            ichip = 3;
            // enable the hold
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_hold_enable, (0x1 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            // this was in SN loop
            if (imod == imod_st1)
            {
                LOG_INFO(logger_, "Set last module ON, module address: [{}] \n", imod);
                ichip = 4;
                // set last module on
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_lst_on, (0x0 << 16));
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            }
            else
            {
                LOG_INFO(logger_, "Set last module OFF, module address: [{}] \n", imod);
                ichip = 4;
                // set last module on
                buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_lst_off, (0x0 << 16));
                i = 1;
                k = 1;
                i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            }
        } // end loop over FEMs
        // imod_xmit = imod_xmit-1;

        ////////////////////////////////////////
        ///
        /// Reset and status
        ////////////////////////////////////////

        /*   here is common block from tpc */
        for (imod_fem = (imod_st2); imod_fem > imod_xmit; imod_fem--) //     now reset all the link port receiver PLL
        {
            imod = imod_fem;
            LOG_INFO(logger_, "Resetting TPC link PLL for module [{}] \n", imod);
            ichip = 4;
            // reset LINKIN PLL
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_pll_reset, (0x0 << 16));

            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            usleep(2000); // give PLL time to reset (1ms->2ms JLS)
        }
        // FIXME: why does this work to get the first word?
        buffers.buf_send[0] = ConstructSendWord((imod_xmit + 1), 3, 20, (0x0 << 16));
        i = pcie_interface->PCIeSendBuffer(1, 1, 1, buffers.psend);
        buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
        i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv);

        for (imod_fem = (imod_xmit + 1); imod_fem < (imod_st2 + 1); imod_fem++) // read back status
        {
            LOG_INFO(logger_, "FEM [{}]\n", imod_fem);

            i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv); // init the receiver
            imod = imod_fem;
            ichip = 3;
            // read out status
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, 20, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

            buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
            i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv);
            LOG_INFO(logger_, "\n Received TPC FEB [{}] (slot={}) status data word = 0x{:X}, 0x{:X} \n",
                                imod, imod, pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);

            LOG_INFO(logger_, "----------------------------\n");
            LOG_INFO(logger_, "TPC FEB {} (slot {}) status \n", imod, imod);
            LOG_INFO(logger_, "----------------------------\n");
            LOG_INFO(logger_, "cmd return (20)       : {} \n", (pcie_int::PcieBuffers::read_array[0] & 0xFF));               // bits 7:0
            LOG_INFO(logger_, "check bits 10:8 (0)   : {}  \n", ((pcie_int::PcieBuffers::read_array[0] >> 8) & 0x7));         // bits 10:8
            LOG_INFO(logger_, "module number ({})    : {}  \n", imod, ((pcie_int::PcieBuffers::read_array[0] >> 11) & 0x1F)); // bits 15:11
            LOG_INFO(logger_, "----------------------------\n");
            LOG_INFO(logger_, "check bit  0 (0)      : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 16) & 0x1);
            LOG_INFO(logger_, "Right ADC DPA locked  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 17) & 0x1);
            LOG_INFO(logger_, "Left  ADC DPA locked  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 18) & 0x1);
            LOG_INFO(logger_, "SN pre-buf err        : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 19) & 0x1);
            LOG_INFO(logger_, "Neutrino pre-buf err  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 20) & 0x1);
            LOG_INFO(logger_, "PLL locked            : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 21) & 0x1);
            LOG_INFO(logger_, "SN memory ready       : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 22) & 0x1);
            LOG_INFO(logger_, "Neutrino memory ready : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 23) & 0x1);
            LOG_INFO(logger_, "ADC lock right        : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 24) & 0x1);
            LOG_INFO(logger_, "ADC lock left         : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 25) & 0x1);
            LOG_INFO(logger_, "ADC align right       : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 26) & 0x1);
            LOG_INFO(logger_, "ADC align left        : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 27) & 0x1);
            LOG_INFO(logger_, "check bits 15:12 (0)  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 28) & 0xf);
            LOG_INFO(logger_, "----------------------------\n");
        }

        for (imod_fem = (imod_st2); imod_fem > imod_xmit; imod_fem--)
        {
            LOG_INFO(logger_, "Module [{}] \n", imod_fem);
            imod = imod_fem;
            LOG_INFO(logger_, "Resetting TPC module [{}] link..", imod);
            ichip = 4;
            // reset LINKIN DPA
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_rxreset, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            usleep(1000); // give DPA time to reset (1ms  JLS)

            ichip = 4;
            // send alignment command
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, hw_consts::mb_feb_align, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            usleep(1000); // give DPA time to align (1ms  JLS)
        }
        // FIXME: why does this work to get the first word?
        buffers.buf_send[0] = ConstructSendWord((imod_xmit + 1), 3, 20, (0x0 << 16));
        i = pcie_interface->PCIeSendBuffer(1, 1, 1, buffers.psend);
        buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
        i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv);

        for (imod_fem = (imod_xmit + 1); imod_fem < (imod_st2 + 1); imod_fem++) {
            i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv);
            imod = imod_fem;
            ichip = 3;
            // read out status
            buffers.buf_send[0] = ConstructSendWord(imod, ichip, 20, (0x0 << 16));
            i = 1;
            k = 1;
            i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
            buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
            i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv); // read out 2 32 bits words

            LOG_INFO(logger_, "\n Received TPC FEB [{}] (slot={}) status data word = 0x{:X}, 0x{:X} \n",
                                imod, imod, pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);
            LOG_INFO(logger_, "----------------------------\n");
            LOG_INFO(logger_, "TPC FEB {} (slot {}) status \n", imod, imod);
            LOG_INFO(logger_, "----------------------------\n");
            LOG_INFO(logger_, "cmd return (20)       : {} \n", (pcie_int::PcieBuffers::read_array[0] & 0xFF));               // bits 7:0
            LOG_INFO(logger_, "check bits 10:8 (0)   : {}  \n", ((pcie_int::PcieBuffers::read_array[0] >> 8) & 0x7));         // bits 10:8
            LOG_INFO(logger_, "module number ({})    : {}  \n", imod, ((pcie_int::PcieBuffers::read_array[0] >> 11) & 0x1F)); // bits 15:11
            LOG_INFO(logger_, "----------------------------\n");
            LOG_INFO(logger_, "check bit  0 (0)      : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 16) & 0x1);
            LOG_INFO(logger_, "Right ADC DPA locked  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 17) & 0x1);
            LOG_INFO(logger_, "Left  ADC DPA locked  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 18) & 0x1);
            LOG_INFO(logger_, "SN pre-buf err        : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 19) & 0x1);
            LOG_INFO(logger_, "Neutrino pre-buf err  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 20) & 0x1);
            LOG_INFO(logger_, "PLL locked            : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 21) & 0x1);
            LOG_INFO(logger_, "SN memory ready       : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 22) & 0x1);
            LOG_INFO(logger_, "Neutrino memory ready : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 23) & 0x1);
            LOG_INFO(logger_, "ADC lock right        : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 24) & 0x1);
            LOG_INFO(logger_, "ADC lock left         : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 25) & 0x1);
            LOG_INFO(logger_, "ADC align right       : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 26) & 0x1);
            LOG_INFO(logger_, "ADC align left        : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 27) & 0x1);
            LOG_INFO(logger_, "check bits 15:12 (0)  : {}  \n", (pcie_int::PcieBuffers::read_array[0] >> 28) & 0xf);
            LOG_INFO(logger_, "----------------------------\n");
        }

        return true;
    }

    std::vector<uint32_t> ChargeFem::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool ChargeFem::Reset(pcie_int::PCIeInterface *pcie_interface) {
        int i = 5;
        int j = 6;
        return i > j;
    }
} // charge_fem