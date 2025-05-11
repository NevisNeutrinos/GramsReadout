//
// Created by Jon Sensenig on 5/4/25.
//

#ifndef STATUS_H
#define STATUS_H

#include "pcie_interface.h"
#include "../src/data/data_handler.h"
#include <vector>
#include <cstdint>
#include <atomic>

namespace status {

class Status {
public:

    Status() = default;
    ~Status() = default;

    bool GetMinimalStatus(uint32_t board_number, pcie_int::PCIeInterface *pcie_interface, bool is_fem);
    int32_t GetBoardStatus(int32_t board_number, pcie_int::PCIeInterface *pcie_interface);
    std::vector<int32_t> ReadStatus(const std::vector<int>& boards, pcie_int::PCIeInterface *pcie_interface, bool minimal_status);
    void SetDataHandlerStatus(data_handler::DataHandler *data_handler);
    std::string JsonHandlerStatus(data_handler::DataHandler *data_handler);
    std::vector<int32_t> GetDataHandlerStatus() { return data_handler_status_vec_; }
    void SetPrintStatus(const bool print) { print_status_ = print; }

private:

    std::vector<uint32_t> GetFemStatus(uint32_t board_number, pcie_int::PCIeInterface *pcie_interface);
    bool CheckFemStatus(std::vector<uint32_t>& status_vec, uint32_t board_num);
    bool CheckXmitStatus(std::vector<uint32_t>& status_vec);

    bool print_status_;
    uint32_t fem_status_chip_ = 3;
    uint32_t fem_status_num_word_ = 1;
    std::vector<int32_t> data_handler_status_vec_;

};

} // status

#endif //STATUS_H
