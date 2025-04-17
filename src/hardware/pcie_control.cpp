//
// Created by Jon Sensenig on 2/24/25.
//

#include "pcie_control.h"
#include <iostream>

namespace pcie_control {

    PcieControl::PcieControl() {
        logger_ = quill::Frontend::create_or_get_logger("readout_logger");
    }

    bool PcieControl::Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {
        /* ^^^^^^^^^^^^^^^^^^^^^^^^^^ CONTROLLER-PCIE SETTUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

        // Initialize Tx for both transcievers
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, hw_consts::cs_init);
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, hw_consts::cs_init);

        // Initialize Rx for both transcievers
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::r1_cs_reg, hw_consts::cs_init);
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::r2_cs_reg, hw_consts::cs_init);

        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::tx_md_reg, 0xfff);

        buffers.psend = buffers.buf_send.data(); //&buf_send[0]; // RUN INITIALIZATION
        buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];

        buffers.buf_send[0] = 0x0; // INITIALIZE
        buffers.buf_send[1] = 0x0;

        uint32_t mode = 1;
        uint32_t num_words = 1;
        mode = pcie_interface->PCIeSendBuffer(kDev1, mode, num_words, buffers.psend);

        LOG_INFO(logger_, "1am i: {} \n", mode);
        LOG_INFO(logger_, "1am psend: 0x{:X} \n", *buffers.psend);

        return true;
    }

    std::vector<uint32_t> PcieControl::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool PcieControl::Reset(pcie_int::PCIeInterface *pcie_interface) {
        int i = 5;
        int j = 6;
        return i > j;
    }

} // pcie_control