//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H

#include "pcie_control.h"
#include "./../lib/monitoring/data_monitor.h"
#include <atomic>
#include <cstdio>
#include <map>
#include <trigger_control.h>

#include "json.hpp"
#include "../../lib/folly/ProducerConsumerQueue.h"


namespace data_handler {

class DataHandler {
public:

    explicit DataHandler();
    ~DataHandler() = default;

    bool Configure(json &config);
    void CollectData(pcie_int::PCIeInterface *pcie_interface);
    std::vector<uint32_t> GetStatus();
    bool CloseDevice();
    void SetRun(bool set_running) { is_running_.store(set_running); }

private:
    // The metrics class
    data_monitor::DataMonitor &metrics_;

    void DataWrite();
    void ReadoutDMARead(pcie_int::PCIeInterface *pcie_interface);
    void TestReadoutDMARead(pcie_int::PCIeInterface *pcie_interface);
    void TriggerDMARead(pcie_int::PCIeInterface *pcie_interface);
    bool WaitForDma(pcie_int::PCIeInterface *pcie_interface, uint32_t *data, uint32_t dev_num);
    void ClearDmaOnAbort(pcie_int::PCIeInterface *pcie_interface, unsigned long long *u64Data, uint32_t dev_num);
    uint32_t DmaLoop(pcie_int::PCIeInterface *pcie_interface, uint32_t dma_num, size_t loop, unsigned long long *u64Data, bool is_first_loop);
    bool SetRecvBuffer(pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2, bool is_data);

    bool SwitchWriteFile();

    static bool isEventStart(const uint32_t word) { return (word & 0xFFFFFFFF) == 0xFFFFFFFF; }
    static bool isEventEnd(const uint32_t word) { return (word & 0xFFFFFFFF) == 0xE0000000; }

    constexpr static std::size_t DATABUFFSIZE = 75000; //300000;
    constexpr static std::size_t DMABUFFSIZE = DATABUFFSIZE * sizeof(uint32_t);

    static constexpr uint32_t kDev1 = pcie_int::PCIeInterface::kDev1;
    static constexpr uint32_t kDev2 = pcie_int::PCIeInterface::kDev2;

    typedef folly::ProducerConsumerQueue<std::array<uint32_t, DATABUFFSIZE>> Queue;
    Queue data_queue_;
    typedef folly::ProducerConsumerQueue<std::array<uint32_t, 8>> TrigQueue;
    TrigQueue trigger_queue_;

    int ext_trig_;
    int software_trig_;
    int trigger_module_;
    size_t num_events_;
    size_t num_dma_loops_ = 1;
    size_t num_recv_bytes_{};
    size_t event_count_ = 0;
    size_t event_start_count_ = 0;
    size_t event_end_count_ = 0;
    bool event_start_;
    bool event_end_;
    bool test;

    // uint32_t data;
    // static unsigned long long u64Data;

    std::string write_file_name_;
    size_t file_count_;

    std::atomic_bool is_running_;
    std::atomic_bool stop_write_;

    pcie_int::DMABufferHandle  pbuf_rec1_{};
    pcie_int::DMABufferHandle pbuf_rec2_{};

    int fd_;
    quill::Logger* logger_;

};

} // data_handler

#endif //DATA_HANDLER_H
