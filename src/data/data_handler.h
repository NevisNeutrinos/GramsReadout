//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H

#include "pcie_control.h"
#include <atomic>
#include "stdio.h"
#include "json.hpp"
#include "../../../folly/include/folly/ProducerConsumerQueue.h"


namespace data_handler {

class DataHandler {
public:

    DataHandler();
    ~DataHandler() = default;

    bool Configure(json &config);
    void CollectData(pcie_int::PCIeInterface *pcie_interface);
    std::vector<uint32_t> GetStatus();
    bool CloseDevice();

    //std::atomic_bool is_running_;
    void SetRun(bool set_running) { is_running_.store(set_running); };

private:

    void DataWrite();
    void DMARead(pcie_int::PCIeInterface *pcie_interface);

    bool SetRecvBuffer(pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2);

    bool SwitchWriteFile();

    bool isEventStart(const uint32_t *word) { return (*word & 0xFFFFFFFF) == 0xFFFFFFFF; }
    bool isEventEnd(const uint32_t *word) { return (*word & 0xFFFFFFFF) == 0xE0000000; }

    typedef folly::ProducerConsumerQueue<uint32_t> Queue;
    Queue data_queue_;

    static constexpr uint32_t kDev1 = pcie_int::PCIeInterface::kDev1;
    static constexpr uint32_t kDev2 = pcie_int::PCIeInterface::kDev2;

    uint32_t dma_buf_size_ = 100000;
    size_t num_events_;
    size_t num_dma_loops_ = 1;
    size_t num_recv_bytes_{};
    size_t event_count_ = 0;
    size_t event_start_count_ = 0;
    size_t event_end_count_ = 0;
    bool event_start_;
    bool event_end_;

    std::string write_file_name_;
    size_t file_count_;

    std::atomic_bool is_running_;

    pcie_int::DMABufferHandle  pbuf_rec1_{};
    pcie_int::DMABufferHandle pbuf_rec2_{};

    FILE *file_ptr_;

    quill::Logger* logger_;

};

} // data_handler

#endif //DATA_HANDLER_H
