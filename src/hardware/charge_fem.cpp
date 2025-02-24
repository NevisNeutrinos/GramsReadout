//
// Created by sabertooth on 2/24/25.
//

#include "charge_fem.h"

namespace charge_fem {

    bool ChargeFem::Configure(pcie_int::PCIeInterface *pcie_interface) {
        return true;
    }

    std::vector<uint32_t> ChargeFem::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool ChargeFem::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }
} // charge_fem