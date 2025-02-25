//
// Created by Jon Sensenig on 2/24/25.
//

#include "light_fem.h"
#include <unistd.h>
#include <cmath>

namespace light_fem {

    bool LightFem::Configure(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {
        static long imod, ichip;
        static uint32_t i, k, iprint, ik, is;
        static int count, counta, nword;
        static int ij, nsend;
        static int ichip_c, dummy1;
        unsigned char charchannel;
        static int ihuff = 0;
        struct timespec tim ={0, 100}, tim2 = {0, 100};

        static int idelay0, idelay1, threshold0, threshold1, pmt_words;
        static int cos_mult, cos_thres, en_top, en_upper, en_lower, hg, lg;
        static int threshold3hg, threshold3lg, threshold1hg, threshold1lg;
        static int threshold3;
        static int lg_ch, hg_ch, trig_ch, bg_ch, beam_mult, beam_thres, pmt_wordslg, pmt_wordshg, pmt_precount;
        static int pmt_deadtimehg, pmt_deadtimelg, pmt_mich_window, bg, bge, tre, beam_size, pmt_width;
        static int pmt_deadtime;


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

                    /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ LIGHT FEM BOOT  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */

    time(&rawtime);
    strftime(timestr, sizeof(timestr), "%Y_%m_%d", localtime(&rawtime));
    printf("\n ######## SiPM+TPC XMIT Readout \n\n Enter SUBRUN NUMBER or NAME:\t");
    scanf("%s", &subrun);
    printf("\n Enter 0 for EXT trigger or 1 for triggers issued by SiPM ADC:\t");
    scanf("%i", &trigtype);

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
    // fprintf(outinfo, "Hardware config: Gate input = %d\n", bg);
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
    // fprintf(outinfo, "Hardware config: LG_ch = %d, HG_ch = %d, TRIG_ch = %d, BG_ch = %d\n", lg_ch, hg_ch, trig_ch, bg_ch);
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
    // fprintf(outinfo, "RUN MODE = %d\n", mode);
    // fprintf(outinfo, "pmt_deadtime HG,LG = %d,%d\n", pmt_deadtimehg, pmt_deadtimelg);
    // fprintf(outinfo, "pmt_deadtime BG,TRIG channels = %d\n", pmt_deadtime);
    // fprintf(outinfo, "pmt_width = %d\n", pmt_width);
    // fprintf(outinfo, "pmt_mich_window = %d\n", pmt_mich_window);
    // fprintf(outinfo, "threshold0 = %d\n", threshold0);
    // fprintf(outinfo, "threshold3 HG,LG = %d,%d\n", threshold3hg, threshold3lg);
    // fprintf(outinfo, "threshold1 HG,LG = %d,%d\n", threshold1hg, threshold1lg);
    // fprintf(outinfo, "threshold3 BG,TRIG channels = %d\n", threshold3);
    // fprintf(outinfo, "threshold1 BG,TRIG channels = %d\n", threshold1);
    // fprintf(outinfo, "cos_mult = %d\n", cos_mult);
    // fprintf(outinfo, "cos_thres = %d\n", cos_thres);
    // fprintf(outinfo, "beam_mult = %d\n", beam_mult);
    // fprintf(outinfo, "beam_thres = %d\n", beam_thres);
    // fprintf(outinfo, "pmt_precount = %d\n", pmt_precount);
    // fprintf(outinfo, "pmt_words HG,LG = %d,%d\n", pmt_wordshg, pmt_wordslg);
    // fprintf(outinfo, "pmt_words BG,TRIG channels = %d\n", pmt_words);
    // turn on the Stratix III power supply
    buffers.psend = buffers.buf_send.data(); //&buf_send[0];
    buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];

    imod = imod_fem;
    ichip = 1;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_power_add + (0x0 << 16); // turn module 11 power on
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    usleep(200000); // wait for 200 ms
    //inpf = fopen("/home/uboonedaq/pmt_fpga_code/module1x_pmt_64MHz_new_head_07162013.rbf","r");
    inpf = fopen("/home/sabertooth/GramsReadoutFirmware/pmt_fem/module1x_pmt_64MHz_new_head_07162013.rbf", "r");
    // fprintf(outinfo, "PMT FEM FPGA: /home/sabertooth/GramsReadoutFirmware/pmt_fem/module1x_pmt_64MHz_new_head_07162013.rbf");
    imod = imod_fem;
    ichip = hw_consts::mb_feb_conf_add;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 0x0 + (0x0 << 16); // turn conf to be on
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    printf("\nLoading FEM FPGA...\n");
    usleep(5000); // wait for a while (1ms->5ms JLS)
    nsend = 500;
    count = 0;
    counta = 0;
    ichip_c = 7; // set ichip_c to stay away from any other command in the
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
                printf(" counta = %d, first word = %x, %x, %x %x %x \n", counta, buffers.buf_send[0], buffers.carray[0],
                    buffers.carray[1], buffers.carray[2], buffers.carray[3]);
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
            i = pcie_interface->PCIeSendBuffer(1, i, nword, buffers.psend);
            nanosleep(&tim, &tim2);
            dummy1 = dummy1 + 1;
            count = 0;
        }
    }
    if (feof(inpf))
    {
        printf("\tend-of-file word count= %d %d\n", counta, count);
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
            printf("\tik= %d\n", ik);
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
            printf("\tlast data = %d, %x\n", ij, buffers.buf_send[ij]);
        }

        nword = ik + 1;
        i = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, nword, buffers.psend);
    }
    usleep(5000); // wait for 2ms to cover the packet time plus fpga init time (2ms->5ms JLS)
    fclose(inpf);

    printf("FEM FPGA configuration done\n");
    ik = 1;
    if (ik == 1)
    {
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 31 + (0x1 << 16); // turm the DRAM reset on
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 31 + (0x0 << 16); // turm the DRAM reset off
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        usleep(5000); // wait for 5 ms for DRAM to be initialized
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 6 + (imod << 16); // set module number
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    }

    nword = 1;
    i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv); // init the receiver

    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 20 + (0x0 << 16); // read out status
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
    i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv); // read out 2 32 bits words
    printf("\nFEM STATUS:\n receive data word = %x, %x \n\n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);

    for (ik = 0; ik < 40; ik++)
    {

        printf("Configuration for channel %d in progress...\n", ik);
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_ch_set + (ik << 16); // set channel number for configuration
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        usleep(1000); // (0.1ms->1ms JLS)

        imod = imod_fem;
        ichip = 3;
        idelay0 = 4;
        if (ik == 0)
            // fprintf(outinfo, "\nidelay0 = %d\n", idelay0);
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_delay0 + (idelay0 << 16); // set delay0
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT delay1 to 12 (default)
        imod = imod_fem;
        ichip = 3;
        idelay1 = 12;
        if (ik == 0)
            // fprintf(outinfo, "idelay1 = %d\n", idelay1);
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_delay1 + (idelay1 << 16); // set delay 1
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT precount
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_precount + (pmt_precount << 16); // set pmt precount
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT threshold 0
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_thresh0 + (threshold0 << 16); // set threshold 0
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT threshold 3
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh3+(threshold3hg<<16);
        //    else if (ik==lg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh3+(threshold3lg<<16);
        //    else
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_thresh3 + (threshold3 << 16); // set threshold 1
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT threshold 1
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh1+(threshold1hg<<16);
        //    else if (ik==lg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_thresh1+(threshold1lg<<16);
        //    else
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_thresh1 + (threshold1 << 16); // set threshold 1
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT data words
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_words+(pmt_wordshg<<16);  // set pmt_words
        //    else if (ik==lg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_words+(pmt_wordslg<<16);
        //    else
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_words + (pmt_words << 16);
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

        // set PMT deadtime
        imod = imod_fem;
        ichip = 3;
        //    if (ik==hg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_deadtime+(pmt_deadtimehg<<16);  // set pmt dead timr
        //    else if (ik==lg_ch) buffers.buf_send[0]=(imod<<11)+(ichip<<8)+mb_feb_pmt_deadtime+(pmt_deadtimelg<<16);
        //    else
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_deadtime + (pmt_deadtime << 16);
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT Michel window
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_window + (pmt_mich_window << 16); // set pmt Michel window
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        // set PMT width (for trigger generation); default is supposed to be 100ns; here, ~78ns
        imod = imod_fem;
        ichip = 3;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_width + (pmt_width << 16); // set pmt discr width
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    }
    printf("\nConfiguring PMT Trigger parameters...\n");
    // set PMT cosmic ray trigger multiplicity
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_cos_mul + (cos_mult << 16); // set cosmic ray trigger mul
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // set PMT cosmic ray trigger pulse height
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_cos_thres + (cos_thres << 16); // set cosmic ray trigger peak
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // set PMT beam trigger multiplicity
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_beam_mul + (beam_mult << 16); // set beam trigger mul
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // set PMT beam trigger pulse height
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_beam_thres + (beam_thres << 16); // set beam trigger peak
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    printf("\nEnabling/disabling channels...\n");
    // disable the top channels
    imod = imod_fem;
    ichip = 3;
    //en_top=0xffff;
    en_top = 0x0; // turn these off, channel 32-47 -JLS
    // fprintf(outinfo, "en_top = 0x%x\n", en_top);
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_en_top + (en_top << 16); // enable/disable channel
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // disable the upper channels
    imod = imod_fem;
    ichip = 3;
    en_upper = 0xffff;
    //en_upper = 0x0; // turn off middle channel, ch 16-31
    // fprintf(outinfo, "en_upper = 0x%x\n", en_upper);
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_en_upper + (en_upper << 16); // enable/disable channel
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
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
    // fprintf(outinfo, "en_lower = 0x%x\n", en_lower);
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_en_lower + (en_lower << 16); // enable/disable channel
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // set maximum block size
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_blocksize + (0xffff << 16); // set max block size
    // fprintf(outinfo, "pmt_blocksize = 0x%x\n", 0xffff);
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    imod = imod_fem;
    ichip = 3;
    if (bg == 0)
        beam_size = 0x0; // this will actually output a few samples, doesn't completely disable it; that's why the request to unplug beam gate input from shaper, above
    else
    {
        printf("\nEnter beam size (1500 is nominal):\t");
        scanf("%d", &beam_size);
    }
    // fprintf(outinfo, "gate_readout_size = %d\n", beam_size);
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_gate_size + (beam_size << 16); // set gate size = 1500
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // set beam delay
    imod = imod_fem;
    ichip = 3; // this inhibits the cosmic (discr 1) before the beam gate (64MHz clock)
    // 0x18=24 samples (should see no overlap)
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_beam_delay + (0x18 << 16); // set gate size
    // fprintf(outinfo, "beam_delay = %d\n", 0x18);
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    // set beam size window
    imod = imod_fem;
    ichip = 3;
    beam_size = 0x66;                                                                     // This is the gate size (1.6us in 64MHz corresponds to 102.5 time ticks; use 102=0x66)
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_beam_size + (beam_size << 16); // set gate size
    // fprintf(outinfo, "beam_size = %d\n", beam_size);
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    a_id = 0x20;
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_a_id + (a_id << 16); // set a_id
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    // enable hold
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_hold_enable + (0x1 << 16); // enable the hold
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    // work on the ADC -- set reset pulse
    imod = imod_fem;
    ichip = 5;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_reset) + (0x0 << 16); // reset goes high
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    imod = imod_fem;
    ichip = 5;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_reset) + (0x1 << 16); // reset goes low
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    imod = imod_fem;
    ichip = 5;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_reset) + (0x0 << 16); // reset goes high
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // enable ADC clock,
    imod = imod_fem;
    ichip = 5;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_spi_add) + (0x7 << 16); // set spi address
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    imod = imod_fem;
    ichip = 5;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0xffff << 16); // load spi data, clock gate enable
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    // load ADC sync data pattern  + set MSB 1st
    for (is = 1; is < 7; is++)
    {
        imod = imod_fem;
        ichip = 5;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        imod = imod_fem;
        ichip = 5;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0b00 << 16); // sync pattern, b for sync, 7 for skew, 3 for normal
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        imod = imod_fem;
        ichip = 5;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x1400 << 16); // msb 1st
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    }
    usleep(5000); // (1ms->5ms JLS)

    // send FPGA ADC receiver reset
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_rxreset + (0x1 << 16); // FPGA ADC receiver reset on
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    // readback status
    i = pcie_interface->PCIeRecvBuffer(1, 0, 1, nword, iprint, buffers.precv); // init the receiver

    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + 20 + (0x0 << 16); // read out status
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
    i = pcie_interface->PCIeRecvBuffer(1, 0, 2, nword, iprint, buffers.precv);
    printf("\nFEM STATUS -- after reset = %x, %x \n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1]);
    printf(" module = %d, command = %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 11) & 0x1f), (pcie_int::PcieBuffers::read_array[0] & 0xff));
    printf(" ADC right dpa lock     %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 17) & 0x1));
    printf(" ADC left  dpa lock     %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 18) & 0x1));
    printf(" block error 2          %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 19) & 0x1));
    printf(" block error 1          %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 20) & 0x1));
    printf(" pll locked             %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 21) & 0x1));
    printf(" supernova mem ready    %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 22) & 0x1));
    printf(" beam      mem ready    %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 23) & 0x1));
    printf(" ADC right PLL locked   %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 24) & 0x1));
    printf(" ADC left  PLL locked   %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 25) & 0x1));
    printf(" ADC align cmd right    %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 26) & 0x1));
    printf(" ADC align cmd left     %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 27) & 0x1));
    printf(" ADC align done right   %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 28) & 0x1));
    printf(" ADC align done left    %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 29) & 0x1));
    printf(" Neutrino data empty    %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 30) & 0x1));
    printf(" Neutrino header empty  %d \n", ((pcie_int::PcieBuffers::read_array[0] >> 31) & 0x1));

    // send FPGA ADC align
    imod = imod_fem;
    ichip = 3;
    buffers.buf_send[0] = (imod << 11) + (ichip << 8) + hw_consts::mb_feb_pmt_align_pulse + (0x0 << 16); // FPGA ADC receiver reset off
    i = 1;
    k = 1;
    i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);

    usleep(5000); // wait for 5ms

    for (is = 1; is < 7; is++)
    {
        imod = imod_fem;
        ichip = 5;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_spi_add) + ((is & 0xf) << 16); // set spi address
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
        imod = imod_fem;
        ichip = 5;
        buffers.buf_send[0] = (imod << 11) + (ichip << 8) + (hw_consts::mb_pmt_adc_data_load) + (0x0300 << 16); // sync pattern, b for sync, 7 for skew, 3 for normal
        i = 1;
        k = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, k, buffers.psend);
    }

        return true;
    }

    std::vector<uint32_t> LightFem::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool LightFem::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }

} // light_fem