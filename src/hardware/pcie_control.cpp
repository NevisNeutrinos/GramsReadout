//
// Created by Jon Sensenig on 2/24/25.
//

#include "pcie_control.h"
#include <iostream>

namespace pcie_control {


bool PcieControl::Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {
    /* ^^^^^^^^^^^^^^^^^^^^^^^^^^ CONTROLLER-PCIE SETTUP  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
    static uint32_t dwAddrSpace;
    static uint32_t u32Data;
    static uint32_t dwOffset;
    // static uint32_t i, k;
    static uint32_t mode, num_words;
    static long imod, ichip;
    // uint32_t *psend, *precv;

    dwAddrSpace = 2;
    u32Data = hw_consts::cs_init; //0x20000000; // initial transmitter, no hold
    dwOffset = hw_consts::t1_cs_reg; //0x18;
    pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);
    dwAddrSpace = 2;

    u32Data = hw_consts::cs_init; //0x20000000; // initial transmitter, no hold
    dwOffset = hw_consts::t2_cs_reg; //0x20;
    pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

    dwAddrSpace = 2;
    u32Data = hw_consts::cs_init; //0x20000000; // initial receiver
    dwOffset = hw_consts::r1_cs_reg; //0x1c;
    pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

    dwAddrSpace = 2;
    u32Data = hw_consts::cs_init; //0x20000000; // initial receiver
    dwOffset = hw_consts::r2_cs_reg; //0x24;
    pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

    dwAddrSpace = 2;
    u32Data = 0xfff; // set mode off with 0xfff...
    dwOffset = hw_consts::tx_md_reg; //0x28;
    pcie_interface->WriteReg32(kDev2, dwAddrSpace, dwOffset, u32Data);

    buffers.psend = buffers.buf_send.data(); //&buf_send[0]; // RUN INITIALIZATION
    buffers.precv = pcie_int::PcieBuffers::read_array.data(); //&read_array[0];
    imod = 0; // controller module

    // pcie_interface->buf_send_[0] = 0x0; // INITIALIZE
    // pcie_interface->buf_send_[1] = 0x0;
    buffers.buf_send[0] = 0x0; // INITIALIZE
    buffers.buf_send[1] = 0x0;
    mode = 1;
    num_words = 1;
    mode = pcie_interface->PCIeSendBuffer(kDev1, mode, num_words, buffers.psend);

    std::cout << "1am i: " << mode << std::endl;
    std::cout << "1am psend: " << buffers.psend << std::endl;

    return true;
}

std::vector<uint32_t> PcieControl::GetStatus() {
    std::vector<uint32_t> status;
    status.push_back(1);
    return status;
}

bool PcieControl::CloseDevice() {
    int i = 5;
    int j = 6;
    return i > j;
}

} // pcie_control