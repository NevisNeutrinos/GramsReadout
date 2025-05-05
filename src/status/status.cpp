//
// Created by Jon Sensenig on 5/4/25.
//

#include "status.h"
#include <array>
#include <iostream>
#include <thread>

namespace status {

    void Status::SetDataHandlerStatus(data_handler::DataHandler *data_handler) {

        auto metrics = data_handler->GetMetrics();

        data_handler_status_vec_.clear();
        size_t tmp_size = 0;
        size_t mask_32b = 0xFFFFFFFF;

        tmp_size = metrics["num_events"];
        data_handler_status_vec_.push_back((tmp_size >> 32) & mask_32b);
        data_handler_status_vec_.push_back(tmp_size & mask_32b);

        tmp_size = metrics["event_diff"];
        data_handler_status_vec_.push_back((tmp_size >> 32) & mask_32b);
        data_handler_status_vec_.push_back(tmp_size & mask_32b);

        tmp_size = metrics["num_dma_loops"];
        data_handler_status_vec_.push_back((tmp_size >> 32) & mask_32b);
        data_handler_status_vec_.push_back(tmp_size & mask_32b);

        tmp_size = metrics["mega_bytes_received"];
        data_handler_status_vec_.push_back((tmp_size >> 32) & mask_32b);
        data_handler_status_vec_.push_back(tmp_size & mask_32b);

        tmp_size = metrics["event_size_words"];
        data_handler_status_vec_.push_back((tmp_size >> 32) & mask_32b);
        data_handler_status_vec_.push_back(tmp_size & mask_32b);

        tmp_size = metrics["num_files"];
        data_handler_status_vec_.push_back((tmp_size >> 32) & mask_32b);
        data_handler_status_vec_.push_back(tmp_size & mask_32b);

    }

    std::vector<int32_t> Status::ReadStatus(const std::vector<int>& boards, pcie_int::PCIeInterface *pcie_interface, bool minimal_status) {

        // This contains the data collection stats and status
        auto status_vec = GetDataHandlerStatus();

        if (minimal_status) {
             int32_t status_res = 0;
             bool is_fem = false;
             for (size_t b = 0; b < boards.size(); b++) {
                 bool res = GetMinimalStatus(boards.at(b), pcie_interface, is_fem);
                 status_res += (res << b);
                 is_fem = true;
             }
            status_vec.push_back(status_res);
        } else {
            for (const int board : boards) {
                auto res = GetBoardStatus(board, pcie_interface);
                status_vec.push_back(res);
            }
        }
        return status_vec;
    }

    bool Status::GetMinimalStatus(uint32_t board_number, pcie_int::PCIeInterface *pcie_interface, bool is_fem) {

        // FEM & XMIT status all the same form: module_num + chip=3 + cmd=20
        auto status = GetFemStatus(board_number, pcie_interface);
        if (is_fem) {
            return CheckFemStatus(status, board_number);
        }
        return CheckXmitStatus(status);
    }

    int32_t Status::GetBoardStatus(int32_t board_number, pcie_int::PCIeInterface *pcie_interface) {
        auto status = GetFemStatus(board_number, pcie_interface);
        if (status.size() == 0) return 0;
        // We care about the 16b status word which is the upper 16b of the 32b word
        return (status.at(0) >> 16) & 0xFFFF;
    }

    std::vector<uint32_t> Status::GetFemStatus(uint32_t board_number, pcie_int::PCIeInterface *pcie_interface) {
        // For all FEMs + XMIT
        std::array<uint32_t, 2> send_array;
        std::array<uint32_t, 2> read_array;
        uint32_t *psend = send_array.data();
        uint32_t *precv = read_array.data();

        // init the receiver
        pcie_interface->PCIeRecvBuffer(1, 0, 1, fem_status_num_word_, 0, precv);
        // read out status
        send_array[0] = (board_number << 11) + (fem_status_chip_ << 8) + 20 + (0x0 << 16);
        pcie_interface->PCIeSendBuffer(1, 0, 1, psend);
        pcie_interface->PCIeRecvBuffer(1, 0, 2, fem_status_num_word_, 0, precv);

        std::vector<uint32_t> status_vec(fem_status_num_word_, 0);
        for(size_t i = 0; i < fem_status_num_word_; i++) {
            status_vec.at(i) = read_array.at(i);
            if (print_status_) std::cout << board_number << "- 0x" << std::hex << read_array.at(i) << std::dec << std::endl;
        }
        return status_vec;
    }

    bool Status::CheckFemStatus(std::vector<uint32_t>& status_vec, uint32_t board_num) {

        uint32_t read_word = status_vec.at(0);
        bool check_status = ((read_word & 0xFF) == 20) &&  // command return
                            (((read_word >> 11) & 0x1F) == board_num) && // module number
                            (((read_word >> 16) & 0x1) == 0x0) &&     // check bit 0
                            (((read_word >> 17) & 0x1) == 0x1) &&     // right ADC DPA locked
                            (((read_word >> 18) & 0x1) == 0x1) &&     // left ADC DPA locked
                            (((read_word >> 19) & 0x1) == 0x0) &&     // SN pre-buf err
                           // (((read_word >> 20) & 0x1) == 0x0) &&     // neutrino pre-buf err
                            (((read_word >> 21) & 0x1) == 0x1) &&     // PLL locked
                            (((read_word >> 22) & 0x1) == 0x1) &&     // SN memory ready
                            (((read_word >> 23) & 0x1) == 0x1) &&     // neutrino memory ready
                            (((read_word >> 24) & 0x1) == 0x1) &&     // ADC lock right
                            (((read_word >> 25) & 0x1) == 0x1) &&     // ADC lock left
                            (((read_word >> 26) & 0x1) == 0x1) &&     // ADC align right
                            (((read_word >> 27) & 0x1) == 0x1) &&     // ADC align left
                            (((read_word >> 28) & 0xF) == 0x7);       // check bits 15:12

        return check_status;
    }

    bool Status::CheckXmitStatus(std::vector<uint32_t>& status_vec) {

        uint32_t read_word = status_vec.at(0);
        bool check_status = (((read_word >> 16) & 0x1) == 0x1) &&     // PLL locked
                            (((read_word >> 17) & 0x1) == 0x1) &&     // Receiver lock
                            (((read_word >> 18) & 0x1) == 0x1) &&     // DPA lock
                            (((read_word >> 19) & 0x1) == 0x1) &&     // NU Optical lock
                            (((read_word >> 20) & 0x1) == 0x1) &&     // SN Optical lock
                            (((read_word >> 22) & 0x1) == 0x0) &&     // SN Busy
                            (((read_word >> 23) & 0x1) == 0x0) &&     // NU Busy
                            (((read_word >> 24) & 0x1) == 0x0) &&     // SN2 Optical
                            (((read_word >> 25) & 0x1) == 0x0) &&     // SN1 Optical
                            (((read_word >> 26) & 0x1) == 0x1) &&     // NU2 Optical
                            (((read_word >> 27) & 0x1) == 0x1) &&     // NU1 Optical
                            (((read_word >> 28) & 0x1) == 0x0) &&     // Timeout1
                            (((read_word >> 29) & 0x1) == 0x0) &&     // Timeout2
                            (((read_word >> 30) & 0x1) == 0x0) &&     // Align1
                            (((read_word >> 31) & 0xF) == 0x0);       // Align2

        return check_status;
    }

} // status