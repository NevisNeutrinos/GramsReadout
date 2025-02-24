//
// Created by sabertooth on 2/24/25.
//

#include "dma_control.h"

namespace dma_control {
    bool DmaControl::Configure(pcie_int::PCIeInterface *pcie_interface) {
        return true;
    }

    std::vector<uint32_t> DmaControl::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool DmaControl::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }
} // dma_control