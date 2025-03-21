//
// Created by Jon Sensenig on 2/25/25.
//

#include "trigger_control.h"
#include <unistd.h>
#include <iostream>
#include <chrono>

namespace trig_ctrl {

    bool TriggerControl::Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {
    /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ TRIGGER_SETUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
        static uint32_t i, k;
        static long imod, ichip;
        static int iframe, itrig_delay;

        software_trigger_rate_ = config["trigger"]["software_trigger_rate_hz"].get<int>();
        trigger_module_ = config["crate"]["trig_slot"].get<int>();
        static int iframe_length = config["readout_windows"]["frame_length"].get<int>();

        std::string trig_src = config["trigger"]["trigger_source"].get<std::string>();
        //this is either 0x1 (PMT beam) 0x4 (PMT cosmic) or 0x8 (PMT all)
        ext_trig_ = trig_src == "ext" ? 1 : 0;
        light_trig_ = trig_src == "light" ? 1 : 0;
        software_trig_ = trig_src == "software" ? 1 : 0;
        static int mask1 = trig_src == "ext" ? 0x0 : 0x8;
        static int mask8 = trig_src == "ext" ? 0x2 : 0x0;

        std::cout << "Trigger source [" << trig_src << "]" << std::endl;

      /* FROM TPC PART*/
      imod = 0; // set offline test
      ichip = 1;
      buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_cntrl_test_on) + (0x0 << 16); // enable offline run on
      i = 1;
      k = 1;
      i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

      imod = trigger_module_;
      buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x0) << 16); // set up trigger module run off
      i = 1;
      k = 1;
      i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

      imod = trigger_module_;
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

      imod = trigger_module_; // Set number of ADC samples to (iframe+1)/8;
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

      if(light_trig_) {
      //Begin PMT Trigger setup

        imod = trigger_module_;
        buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_mask1)+((mask1&0xf)<<16); //set mask1[3] on.
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
//        fprintf(outinfo,"trig_mask1 = 0x%x\n",mask1);

        imod=trigger_module_;
        buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_prescale1)+(0x0<<16); //set prescale1 0
//        fprintf(outinfo,"trig_prescale1 = 0x%x\n",0x0);
        i = 1;
        k = 1;
      i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod=trigger_module_;
        buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_mask8)+((mask8&0xff)<<16);
//        fprintf(outinfo,"trig_mask8 = 0x%x\n",mask8);
        i = 1;
        k = 1;
      i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod=trigger_module_;
        buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_prescale8)+(0x0<<16); //set prescale8 0
//        fprintf(outinfo,"trig_prescale8 = 0x%x\n",0x0);
        i = 1;
        k = 1;
      i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        //End PMT Trigger setup
      }
      else
      {
        //begin EXT Trigger setup as of 11/26/2024
        imod = trigger_module_;
        buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_mask8) + (0x2 << 16); // set mask1[3] on.
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod = trigger_module_;
        buffers.buf_send[0] = (imod << 11) + (hw_consts::mb_trig_prescale8) + (0x0 << 16); // set prescale1 0
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        //End EXT Trigger setup as of 11/26/2024
      }
      return true;
    }

    std::vector<uint32_t> TriggerControl::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool TriggerControl::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }

    void TriggerControl::SendStartTrigger(pcie_int::PCIeInterface *pcie_interface, int itrig_c, int itrig_ext, int trigger_module) {
        static int imod, ichip;
        static int imod_trig   = 11;
        std::array<uint32_t, 2> buf_send{0,0};
        uint32_t *psend{};

        psend = buf_send.data();

        if (itrig_c == 1) {
            imod = imod_trig; /* trigger module */
            buf_send[0] = (imod << 11) + hw_consts::mb_trig_pctrig + ((0x0) << 16);
            pcie_interface->PCIeSendBuffer(kDev1, 1, 1, psend);
            usleep(5000); // 3x frame size
        }
        else if (itrig_ext == 1) {
            // only need to restart the run if we use the test data or 1st run
            imod = imod_trig;
            buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x1) << 16); // set up run
            pcie_interface->PCIeSendBuffer(kDev1, 1, 1, psend);
        }
        else {
            imod = 0;
            ichip = 1;
            buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_cntrl_set_trig1 + (0x0 << 16); // send trigger
            pcie_interface->PCIeSendBuffer(kDev1, 1, 1, psend);
            usleep(5000);
        }
    }

    void TriggerControl::SendStopTrigger(pcie_int::PCIeInterface *pcie_interface, int itrig_c, int itrig_ext, int trigger_module) {
        static int imod;
        static int imod_trig   = 11;
        std::array<uint32_t, 2> buf_send{0,0};
        uint32_t *psend{};

        psend = buf_send.data();

        if (itrig_c == 1) {
            //******************************************************
            // stop run
            imod = imod_trig;
            buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x0) << 16); // set up run off
            pcie_interface->PCIeSendBuffer(kDev1, 1, 1, psend);
        } else if ((itrig_ext == 1)) {
            //  set trigger module run off
            imod = imod_trig;
            buf_send[0] = (imod << 11) + (hw_consts::mb_trig_run) + ((0x0) << 16); // set up run off
            pcie_interface->PCIeSendBuffer(kDev1, 1, 1, psend);
        }
    }

    void TriggerControl::SendSoftwareTrigger(pcie_int::PCIeInterface *pcie_interface) {
        std::array<uint32_t, 2> buf_send{0,0};
        uint32_t *psend{};
        psend = buf_send.data();

        auto sleep_for = std::chrono::microseconds(software_trigger_rate_ * 1000000);
        buf_send[0] = (trigger_module_ << 11) + (hw_consts::mb_trig_pctrig) + ((0x0) << 16);

        while (is_running_.load()) {
            std::this_thread::sleep_for(sleep_for);
            pcie_interface->PCIeSendBuffer(kDev1, 1, 1, psend);
        }
    }


} // trig_ctrl