//
// Created by Jon Sensenig on 2/25/25.
//

#include "trigger_control.h"

namespace trig_ctrl {

    bool TriggerControl::Configure(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {
    /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ TRIGGER_SETUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
      static uint32_t i, k;
      static long imod, ichip;
      static int mask1, mask8;
      int trigtype;
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
//        fprintf(outinfo,"trig_mask1 = 0x%x\n",mask1);

        imod=imod_trig;
        buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_prescale1)+(0x0<<16); //set prescale1 0
//        fprintf(outinfo,"trig_prescale1 = 0x%x\n",0x0);
        i = 1;
        k = 1;
      i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod=imod_trig;
        buffers.buf_send[0]=(imod<<11)+(hw_consts::mb_trig_mask8)+((mask8&0xff)<<16);
//        fprintf(outinfo,"trig_mask8 = 0x%x\n",mask8);
        i = 1;
        k = 1;
      i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        imod=imod_trig;
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
} // trig_ctrl