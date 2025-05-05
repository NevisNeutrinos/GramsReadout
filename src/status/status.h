//
// Created by Jon Sensenig on 5/4/25.
//

#ifndef STATUS_H
#define STATUS_H

#include "pcie_interface.h"
#include "quill/Logger.h"
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
    std::vector<int32_t> GetDataHandlerStatus();

    // Data Handler metrics
    void IsRunning(const bool running) { is_running_.store(running); }
    void DmaSize(const size_t dma_size) { dma_size_.store(dma_size); }
    void NumEvents(const size_t num_evts) { num_events_.store(num_evts); }
    void EventDiff(const size_t event_diff) { event_diff_.store(event_diff); }
    void DmaLoops(const size_t num_dma_loops) { num_dma_loops_.store(num_dma_loops); }
    void NumFiles(const size_t num_files) { num_files_.store(num_files); }
    void NumAdcWords(const size_t num_adc_words) { adc_words_per_event_.store(num_adc_words); }
    void EventSize(const size_t num_words) { words_per_event_.store(num_words); }
    void BytesReceived(const size_t num_bytes) { bytes_received_.store(num_bytes); }
    void ControllerState(const int state) { state_.store(state); }

private:

    std::vector<uint32_t> GetFemStatus(uint32_t board_number, pcie_int::PCIeInterface *pcie_interface);
    bool CheckFemStatus(std::vector<uint32_t>& status_vec, uint32_t board_num);
    bool CheckXmitStatus(std::vector<uint32_t>& status_vec);

    uint32_t fem_status_chip_ = 3;
    uint32_t fem_status_num_word_ = 1;

    /*
     * Data Handler metrics
     */

    // State
    std::atomic_bool is_running_;
    std::atomic<int> state_;

    // Configuration
    std::atomic<size_t> dma_size_;

    // Counters
    std::atomic<size_t> num_events_;
    std::atomic<size_t> event_diff_;
    std::atomic<size_t> num_files_;
    std::atomic<size_t> num_dma_loops_;
    std::atomic<size_t> adc_words_per_event_;
    std::atomic<size_t> bytes_received_;

    std::atomic<size_t> words_per_event_;
};

} // status

#endif //STATUS_H
