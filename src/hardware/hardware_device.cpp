//
// Created by Jon Sensenig on 3/1/25.
//
#include "hardware_device.h"
#include <iostream>
#include <unistd.h>

HardwareDevice::HardwareDevice() {
    logger_ = quill::Frontend::create_or_get_logger("readout_logger");
}

bool HardwareDevice::LoadFirmware(int module, int chip, std::string &fw_file,
                                    pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) {

    static uint32_t i, ik;
    static int count, counta, nword;
    static int ij, nsend;
    static int ichip_c, dummy1;
    unsigned char charchannel;
    timespec tim ={0, 100}, tim2 = {0, 100};

    FILE *inpf = fopen(fw_file.c_str(), "r");

    usleep(2000); // wait for a while (1ms->2ms JLS)
    count = 0;
    counta = 0;
    nsend = 500;
    ichip_c = 7; // set ichip_c to stay away from any other command in the
    nword = 1;
    dummy1 = 0;

    while (fread(&charchannel, sizeof(char), 1, inpf) == 1)
    {
        buffers.carray[count] = charchannel;
        count++;
        counta++;
        if ((count % (nsend * 2)) == 0)
        {
            buffers.buf_send[0] = (module << 11) + (ichip_c << 8) + (buffers.carray[0] << 16);
            pcie_int::PcieBuffers::send_array[0] = buffers.buf_send[0];
            if (dummy1 <= 5) {
                LOG_INFO(logger_, "counta = {}, first word = 0x{:X} 0x{:X} 0x{:X} 0x{:X} 0x{:X}\n", counta,
                    buffers.buf_send[0], buffers.carray[0], buffers.carray[1], buffers.carray[2], buffers.carray[3]);
            }
            for (ij = 0; ij < nsend; ij++)
            {
                if (ij == (nsend - 1)) {
                    buffers.buf_send[ij + 1] = buffers.carray[2 * ij + 1] + (0x0 << 16);
                } else {
                    buffers.buf_send[ij + 1] = buffers.carray[2 * ij + 1] + (buffers.carray[2 * ij + 2] << 16);
                }
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
    LOG_INFO(logger_, "Finished reading bitfile...\n");
    if (feof(inpf))
    {
        LOG_INFO(logger_, "EOF word count = {} {}\n", counta, count);
        buffers.buf_send[0] = (module << 11) + (ichip_c << 8) + (buffers.carray[0] << 16);
        if (count > 1)
        {
            if (((count - 1) % 2) == 0) {
                ik = (count - 1) / 2;
            } else {
                ik = (count - 1) / 2 + 1;
            }
            ik = ik + 2; // add one more for safety
            LOG_INFO(logger_, "Num Firmware words to send = {} \n", ik);
            for (ij = 0; ij < ik; ij++)
            {
                if (ij == (ik - 1)) {
                    buffers.buf_send[ij + 1] = buffers.carray[(2 * ij) + 1] + (((module << 11) + (chip << 8) + 0x0) << 16);
                } else {
                    buffers.buf_send[ij + 1] = buffers.carray[(2 * ij) + 1] + (buffers.carray[(2 * ij) + 2] << 16);
                }
                pcie_int::PcieBuffers::send_array[ij + 1] = buffers.buf_send[ij + 1];
            }
        } else {
            ik = 1;
        }
        for (ij = ik - 10; ij < ik + 1; ij++) {
            LOG_INFO(logger_, "Last firmware word {} [0x{:X}] \n", ij, buffers.buf_send[ij]);
        }
        nword = ik + 1;
        i = 1;
        i = pcie_interface->PCIeSendBuffer(1, i, nword, buffers.psend);
    }
    usleep(3000); // wait for 2ms to cover the packet time plus fpga init time (change to 3ms JLS)
    fclose(inpf);
    return true;
  }