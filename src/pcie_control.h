//
// Created by Jon Sensenig on 1/23/25.
//

#ifndef PCIECONTROL_H
#define PCIECONTROL_H

#include "pcie_interface.h"
#include "quill/Logger.h"

namespace pcie_ctrl {

class PCIeControl {
    public:

    PCIeControl();
    ~PCIeControl();

    // Do not allow copying or copy assignments
    PCIeControl(const PCIeControl &) = delete;
    PCIeControl &operator=(const PCIeControl &) = delete;

    // Initialize class
    bool Initialize();

private:

    quill::Logger* logger_;

    pcie_int::PCIeInterface *pcie_interface;

    // PCIe device bus and IDs
    int pcie_bus_;
    int device_id_0_;
    int device_id_1_;

};

} // pcie_ctrl

#endif //PCIECONTROL_H
