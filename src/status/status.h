//
// Created by Jon Sensenig on 5/4/25.
//

#ifndef STATUS_H
#define STATUS_H

#include "pcie_interface.h"
#include <vector>
#include <cstdint>

namespace status {

class Status {
public:

    Status() = default;
    ~Status() = default;

    bool GetMinimalStatus(uint32_t board_number, pcie_int::PCIeInterface *pcie_interface, bool is_fem);
    int32_t GetBoardStatus(int32_t board_number, pcie_int::PCIeInterface *pcie_interface);

private:

    std::vector<uint32_t> GetFemStatus(uint32_t board_number, pcie_int::PCIeInterface *pcie_interface);
    bool CheckFemStatus(std::vector<uint32_t>& status_vec, uint32_t board_num);
    bool CheckXmitStatus(std::vector<uint32_t>& status_vec);

    uint32_t fem_status_chip_ = 3;
    uint32_t fem_status_num_word_ = 1;
};

} // status

#endif //STATUS_H
