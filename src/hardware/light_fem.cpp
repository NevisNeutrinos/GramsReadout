//
// Created by sabertooth on 2/24/25.
//

#include "light_fem.h"

namespace light_fem {

    bool LightFem::Configure(pcie_int::PCIeInterface *pcie_interface) {
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