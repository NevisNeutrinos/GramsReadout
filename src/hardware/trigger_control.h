//
// Created by Jon Sensenig on 2/25/25.
//

#ifndef TRIGGER_CONTROL_H
#define TRIGGER_CONTROL_H

#include "hardware_device.h"
#include <atomic>

namespace trig_ctrl {

class TriggerControl : HardwareDevice {
public:

    TriggerControl();
    ~TriggerControl() override = default;

    bool Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers& buffers) override;
    std::vector<uint32_t> GetStatus() override;
    void SetRun(const bool set_running) { is_running_.store(set_running); }
    bool Reset(pcie_int::PCIeInterface *pcie_interface) override;


    static void SendStartTrigger(pcie_int::PCIeInterface *pcie_interface, int itrig_c, int itrig_ext, int trigger_module);
    static void SendStopTrigger(pcie_int::PCIeInterface *pcie_interface, int itrig_c, int itrig_ext, int trigger_module);
    void SendSoftwareTrigger(pcie_int::PCIeInterface *pcie_interface, int software_trigger_rate, int trigger_module);

private:

    quill::Logger* logger_;
    std::atomic_bool is_running_ = false;
    std::vector<uint32_t> prescale_vec_;
    int software_trigger_rate_{};
    int trigger_module_;
    int ext_trig_;
    int light_trig_;
    int software_trig_;

};

} // trig_ctrl

#endif //TRIGGER_CONTROL_H
