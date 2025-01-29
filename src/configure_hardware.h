//
// Created by Jon Sensenig on 1/29/25.
//

#ifndef CONFIGURE_HARDWARE_H
#define CONFIGURE_HARDWARE_H

#include <vector>
#include <string>

namespace hw_config {

class ConfigureHardware {
public:
    ConfigureHardware() = default;
    ~ConfigureHardware() = default;

    // FIXME placeholder for config object
    typedef std::vector<std::string> Config;
    typedef std::vector<std::string> Status;

    bool Configure(Config& config);
    void ReadStatus(Status& status);

protected:

    bool XMITConfigure(Config& config);
    bool XMITReset();
    bool XMITStatus(Status& status);

    bool LightFEMConfigure(Config& config);
    bool LightFEMReset();
    bool LightFEMStatus(Status& status);

    bool ChargeFEMConfigure(Config& config);
    bool ChargeFEMReset();
    bool ChargeFEMStatus(Status& status);

    bool TriggerConfigure(Config& config);
    bool TriggerReset();
    bool TriggerStatus(Status& status);

private:


    // Define hardware magic numbers
    // TODO add short description
    const uint32_t poweroff = 0x0;
    const uint32_t poweron = 0x1;
    const uint32_t configure_s30 = 0x2;
    const uint32_t configure_s60 = 0x3;
    const uint32_t configure_cont = 0x20;
    const uint32_t rdstatus = 0x80;
    const uint32_t loopback = 0x04;
    const uint32_t dcm2_run_off = 254;
    const uint32_t dcm2_run_on = 255;
    const uint32_t dcm2_online = 2;
    const uint32_t dcm2_setmask = 3;
    const uint32_t dcm2_offline_busy = 4;
    const uint32_t dcm2_load_packet_a = 10;
    const uint32_t dcm2_load_packet_b = 11;
    const uint32_t dcm2_offline_load = 9;
    const uint32_t dcm2_status_read = 20;
    const uint32_t dcm2_led_sel = 29;
    const uint32_t dcm2_buffer_status_read = 30;
    const uint32_t dcm2_status_read_inbuf = 21;
    const uint32_t dcm2_status_read_evbuf = 22;
    const uint32_t dcm2_status_read_noevnt = 23;
    const uint32_t dcm2_zero = 12;
    const uint32_t dcm2_compressor_hold = 31;
    const uint32_t dcm2_5_readdata = 4;
    const uint32_t dcm2_5_firstdcm = 8;
    const uint32_t dcm2_5_lastdcm = 9;
    const uint32_t dcm2_5_status_read = 5;
    const uint32_t dcm2_5_source_id = 25;
    const uint32_t dcm2_5_lastchnl = 24;
    const uint32_t dcm2_packet_id_a = 25;
    const uint32_t dcm2_packet_id_b = 26;
    const uint32_t dcm2_hitformat_a = 27;
    const uint32_t dcm2_hitformat_b = 28;
    const uint32_t part_run_off = 254;
    const uint32_t part_run_on = 255;
    const uint32_t part_online = 2;
    const uint32_t part_offline_busy = 3;
    const uint32_t part_offline_hold = 4;
    const uint32_t part_status_read = 20;
    const uint32_t part_source_id = 25;
    const uint32_t t1_tr_bar = 0;
    const uint32_t t2_tr_bar = 4;
    const uint32_t cs_bar = 2;
    /**  command register location **/
    const uint32_t tx_mode_reg = 0x28;
    const uint32_t t1_cs_reg = 0x18;
    const uint32_t r1_cs_reg = 0x1c;
    const uint32_t t2_cs_reg = 0x20;
    const uint32_t r2_cs_reg = 0x24;
    const uint32_t tx_md_reg = 0x28;
    const uint32_t cs_dma_add_low_reg = 0x0;
    const uint32_t cs_dma_add_high_reg = 0x4;
    const uint32_t cs_dma_by_cnt = 0x8;
    const uint32_t cs_dma_cntrl = 0xc;
    const uint32_t cs_dma_msi_abort = 0x10;
    /** define status bits **/
    const uint32_t cs_init = 0x20000000;
    const uint32_t cs_mode_p = 0x8000000;
    const uint32_t cs_mode_n = 0x0;
    const uint32_t cs_start = 0x40000000;
    const uint32_t cs_done = 0x80000000;
    const uint32_t dma_tr1 = 0x100000;
    const uint32_t dma_tr2 = 0x200000;
    const uint32_t dma_tr12 = 0x300000;
    const uint32_t dma_3dw_trans = 0x0;
    const uint32_t dma_4dw_trans = 0x0;
    const uint32_t dma_3dw_rec = 0x40;
    const uint32_t dma_4dw_rec = 0x60;
    const uint32_t dma_in_progress = 0x80000000;
    const uint32_t dma_abort = 0x2;
    const uint32_t mb_cntrl_add = 0x1;
    const uint32_t mb_cntrl_test_on = 0x1;
    const uint32_t mb_cntrl_test_off = 0x0;
    const uint32_t mb_cntrl_set_run_on = 0x2;
    const uint32_t mb_cntrl_set_run_off = 0x3;
    const uint32_t mb_cntrl_set_trig1 = 0x4;
    const uint32_t mb_cntrl_set_trig2 = 0x5;
    const uint32_t mb_cntrl_load_frame = 0x6;
    const uint32_t mb_cntrl_load_trig_pos = 0x7;
    const uint32_t mb_feb_power_add = 0x1;
    const uint32_t mb_feb_conf_add = 0x2;
    const uint32_t mb_feb_pass_add = 0x3;
    const uint32_t mb_feb_lst_on = 1;
    const uint32_t mb_feb_lst_off = 0;
    const uint32_t mb_feb_rxreset = 2;
    const uint32_t mb_feb_align = 3;
    const uint32_t mb_feb_pll_reset = 5;
    const uint32_t mb_feb_adc_align = 1;
    const uint32_t mb_feb_a_nocomp = 2;
    const uint32_t mb_feb_b_nocomp = 3;
    const uint32_t mb_feb_blocksize = 4;
    const uint32_t mb_feb_timesize = 5;
    const uint32_t mb_feb_mod_number = 6;
    const uint32_t mb_feb_a_id = 7;
    const uint32_t mb_feb_b_id = 8;
    const uint32_t mb_feb_max = 9;
    const uint32_t mb_feb_test_source = 10;
    const uint32_t mb_feb_test_sample = 11;
    const uint32_t mb_feb_test_frame = 12;
    const uint32_t mb_feb_test_channel = 13;
    const uint32_t mb_feb_test_ph = 14;
    const uint32_t mb_feb_test_base = 15;
    const uint32_t mb_feb_test_ram_data = 16;
    const uint32_t mb_feb_a_test = 17;
    const uint32_t mb_feb_b_test = 18;
    const uint32_t mb_feb_rd_status = 20;
    const uint32_t mb_feb_a_rdhed = 21;
    const uint32_t mb_feb_a_rdbuf = 22;
    const uint32_t mb_feb_b_rdhed = 23;
    const uint32_t mb_feb_b_rdbuf = 24;
    const uint32_t mb_feb_read_probe = 30;
    const uint32_t mb_feb_dram_reset = 31;
    const uint32_t dma_buffer_size = 10000000;

};

} // hw_config

#endif //CONFIGURE_HARDWARE_H
