//
// Created by Jon Sensenig on 1/29/25.
//

#ifndef CONFIGURE_HARDWARE_H
#define CONFIGURE_HARDWARE_H

#include <vector>
#include <string>
#include <memory>
#include <unistd.h>
#include "../lib/pcie_driver/pcie_interface.h"
// #include "pcie_control.h"

namespace hw_config {

class ConfigureHardware {
public:
    ConfigureHardware();
    ~ConfigureHardware() = default;

    // FIXME placeholder for config object
    typedef std::vector<std::string> Config;
    typedef std::vector<std::string> Status;

    bool Configure(Config& config, pcie_int::PCIeInterface *pcie_interface);
    void ReadStatus(Status& status, pcie_int::PCIeInterface *pcie_interface);

protected:

    bool PCIeDeviceConfigure(pcie_int::PCIeInterface *pcie_interface);
    bool FullConfigure(pcie_int::PCIeInterface *pcie_interface);

    bool XMITLoadFPGA(Config& config, pcie_int::PCIeInterface *pcie_interface);
    bool XMITConfigure(Config& config, pcie_int::PCIeInterface *pcie_interface);
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

    static constexpr uint32_t kDev1 = pcie_int::PCIeInterface::kDev1;
    static constexpr uint32_t kDev2 = pcie_int::PCIeInterface::kDev2;

    static constexpr std::size_t ARR_SIZE = 40000;
    std::array<uint32_t, ARR_SIZE> buffer_send_{};
    std::array<uint32_t, ARR_SIZE> send_array_{};
    std::array<uint32_t, 100000> read_array_{};
    std::array<unsigned char, ARR_SIZE> char_array_{};

    std::array<uint32_t, 40000> buf_send{};
    std::array<unsigned char, 40000> carray{};
    static std::array<uint32_t, 40000> send_array;
    static std::array<uint32_t, 10000000> read_array;

    uint32_t *p_send_{};
    uint32_t *p_recv_{};

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
    const uint32_t mb_feb_adc_reset = 33;
    const uint32_t mb_a_buf_status = 34;
    const uint32_t mb_b_buf_status = 35;
    const uint32_t mb_a_ham_status = 36;
    const uint32_t mb_b_ham_status = 37;
    const uint32_t mb_feb_a_maxwords = 40;
    const uint32_t mb_feb_b_maxwords = 41;
    const uint32_t mb_feb_hold_enable = 42;
    const uint32_t mb_pmt_adc_reset = 1;
    const uint32_t mb_pmt_spi_add = 2;
    const uint32_t mb_pmt_adc_data_load = 3;
    const uint32_t mb_xmit_conf_add = 0x2;
    const uint32_t mb_xmit_pass_add = 0x3;
    const uint32_t mb_xmit_modcount = 0x1;
    const uint32_t mb_xmit_enable_1 = 0x2;
    const uint32_t mb_xmit_enable_2 = 0x3;
    const uint32_t mb_xmit_test1 = 0x4;
    const uint32_t mb_xmit_test2 = 0x5;
    const uint32_t mb_xmit_testdata = 10;
    const uint32_t mb_xmit_rdstatus = 20;
    const uint32_t mb_xmit_rdcounters = 21;
    const uint32_t mb_xmit_link_reset = 22;
    const uint32_t mb_opt_dig_reset = 23;
    const uint32_t mb_xmit_dpa_fifo_reset = 24;
    const uint32_t mb_xmit_dpa_word_align = 25;
    const uint32_t mb_xmit_link_pll_reset = 26;
    const uint32_t mb_trig_run = 1;
    const uint32_t mb_trig_frame_size = 2;
    const uint32_t mb_trig_deadtime_size = 3;
    const uint32_t mb_trig_active_size = 4;
    const uint32_t mb_trig_delay1_size = 5;
    const uint32_t mb_trig_delay2_size = 6;
    const uint32_t mb_trig_calib_delay = 8;
    const uint32_t mb_trig_prescale0 = 10;
    const uint32_t mb_trig_prescale1 = 11;
    const uint32_t mb_trig_prescale2 = 12;
    const uint32_t mb_trig_prescale3 = 13;
    const uint32_t mb_trig_prescale4 = 14;
    const uint32_t mb_trig_prescale5 = 15;
    const uint32_t mb_trig_prescale6 = 16;
    const uint32_t mb_trig_prescale7 = 17;
    const uint32_t mb_trig_prescale8 = 18;
    const uint32_t mb_trig_mask0 = 20;
    const uint32_t mb_trig_mask1 = 21;
    const uint32_t mb_trig_mask2 = 22;
    const uint32_t mb_trig_mask3 = 23;
    const uint32_t mb_trig_mask4 = 24;
    const uint32_t mb_trig_mask5 = 25;
    const uint32_t mb_trig_mask6 = 26;
    const uint32_t mb_trig_mask7 = 27;
    const uint32_t mb_trig_mask8 = 28;
    const uint32_t mb_trig_rd_param = 30;
    const uint32_t mb_trig_pctrig = 31;
    const uint32_t mb_trig_rd_status = 32;
    const uint32_t mb_trig_reset = 33;
    const uint32_t mb_trig_calib = 34;
    const uint32_t mb_trig_rd_gps = 35;
    const uint32_t mb_trig_sel1 = 40;
    const uint32_t mb_trig_sel2 = 41;
    const uint32_t mb_trig_sel3 = 42;
    const uint32_t mb_trig_sel4 = 43;
    const uint32_t mb_trig_p1_delay = 50;
    const uint32_t mb_trig_p1_width = 51;
    const uint32_t mb_trig_p2_delay = 52;
    const uint32_t mb_trig_p2_width = 53;
    const uint32_t mb_trig_p3_delay = 54;
    const uint32_t mb_trig_p3_width = 55;
    const uint32_t mb_trig_pulse_delay = 58;
    const uint32_t mb_trig_pulse1 = 60;
    const uint32_t mb_trig_pulse2 = 61;
    const uint32_t mb_trig_pulse3 = 62;
    const uint32_t mb_shaper_pulsetime = 1;
    const uint32_t mb_shaper_dac = 2;
    const uint32_t mb_shaper_pattern = 3;
    const uint32_t mb_shaper_write = 4;
    const uint32_t mb_shaper_pulse = 5;
    const uint32_t mb_shaper_entrig = 6;
    const uint32_t mb_feb_pmt_gate_size = 47;
    const uint32_t mb_feb_pmt_beam_delay = 48;
    const uint32_t mb_feb_pmt_beam_size = 49;
    const uint32_t mb_feb_pmt_ch_set = 50;
    const uint32_t mb_feb_pmt_delay0 = 51;
    const uint32_t mb_feb_pmt_delay1 = 52;
    const uint32_t mb_feb_pmt_precount = 53;
    const uint32_t mb_feb_pmt_thresh0 = 54;
    const uint32_t mb_feb_pmt_thresh1 = 55;
    const uint32_t mb_feb_pmt_thresh2 = 56;
    const uint32_t mb_feb_pmt_thresh3 = 57;
    const uint32_t mb_feb_pmt_width = 58;
    const uint32_t mb_feb_pmt_deadtime = 59;
    const uint32_t mb_feb_pmt_window = 60;
    const uint32_t mb_feb_pmt_words = 61;
    const uint32_t mb_feb_pmt_cos_mul = 62;
    const uint32_t mb_feb_pmt_cos_thres = 63;
    const uint32_t mb_feb_pmt_mich_mul = 64;
    const uint32_t mb_feb_pmt_mich_thres = 65;
    const uint32_t mb_feb_pmt_beam_mul = 66;
    const uint32_t mb_feb_pmt_beam_thres = 67;
    const uint32_t mb_feb_pmt_en_top = 68;
    const uint32_t mb_feb_pmt_en_upper = 69;
    const uint32_t mb_feb_pmt_en_lower = 70;
    const uint32_t mb_feb_pmt_blocksize = 71;
    const uint32_t mb_feb_pmt_test = 80;
    const uint32_t mb_feb_pmt_clear = 81;
    const uint32_t mb_feb_pmt_test_data = 82;
    const uint32_t mb_feb_pmt_pulse = 83;
    const uint32_t mb_feb_pmt_rxreset = 84;
    const uint32_t mb_feb_pmt_align_pulse = 85;
    const uint32_t mb_feb_pmt_rd_counters = 86;
    const uint32_t dma_buffer_size = 10000000;

};

} // hw_config

#endif //CONFIGURE_HARDWARE_H
