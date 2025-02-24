//
// Created by sabertooth on 2/24/25.
//

#include "xmit_control.h"

namespace xmit_control {

    bool XmitControl::Configure(pcie_int::PCIeInterface *pcie_interface) {
      return true;
    }

    std::vector<uint32_t> XmitControl::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool XmitControl::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }

} // xmit_control