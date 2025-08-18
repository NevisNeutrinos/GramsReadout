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
    ~DataHandler();

    bool Configure(json &config);
    void CollectData(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers *buffers);
    std::vector<uint32_t> GetStatus();
    bool Reset(pcie_int::PCIeInterface *pcie_interface, size_t run_number);
    void SetRun(bool set_running) { is_running_.store(set_running); }
    std::map<std::string, size_t> GetMetrics();

private:
    trig_ctrl::TriggerControl trigger_{};
    // The metrics class
    // data_monitor::DataMonitor &metrics_;
    std::shared_ptr<data_monitor::DataMonitor> metrics_;

    void DataWrite();
    void ReadoutDMARead(pcie_int::PCIeInterface *pcie_interface);
    void ReadoutViaController(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers *buffers);
    void TestReadoutDMARead(pcie_int::PCIeInterface *pcie_interface);
    void TriggerDMARead(pcie_int::PCIeInterface *pcie_interface);
    bool WaitForDma(pcie_int::PCIeInterface *pcie_interface, uint32_t *data, uint32_t dev_num);
    void ClearDmaOnAbort(pcie_int::PCIeInterface *pcie_interface, unsigned long long *u64Data, uint32_t dev_num);
    uint32_t DmaLoop(pcie_int::PCIeInterface *pcie_interface, uint32_t dma_num, size_t loop, unsigned long long *u64Data, bool is_first_loop);
    bool SetRecvBuffer(pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2, bool is_data);
    bool SwitchWriteFile();
    void PollTriggerPPS(pcie_int::PCIeInterface *pcie_interface);

    static bool isEventStart(const uint32_t word) { return (word & 0xFFFFFFFF) == 0xFFFFFFFF; }
    static bool isEventEnd(const uint32_t word) { return (word & 0xFFFFFFFF) == 0xE0000000; }

    /*
     * The number of events to collect before writing to disk. Since small writes are inefficient
     * we collect EVENTCHUNK number of events and then write them to disk in one go. Ideally this
     * should be sized such that (EVENTCHUNK * EVENTSIZE) is 1-10MB
     */
    constexpr static size_t EVENTCHUNK = 10;

    /*
    * This is not configurable since we are sizing std::arrays which have to be known at compile time.
    * Additionally, this should be known/tuned and then not touched during data collection.
    *
    * EVENTSIZE: The approximate size of an event in bytes. This does not have to be exact but should
    * be within 50kB of the expected size. If it is too small relative to the real event size it could
    * cause less efficient DMA reading
    *
    * DMABUFFSIZE: This sets the size of the 2 contiguous memory DMA buffers. It is the expected event
    * size plus 30% so we are overestimating a little for reading efficiency reasons.
    *
    * DATABUFFSIZE: This is the width of the buffer between the read and write threads. The DMA buffer
    * is memory copied into it so it *must* be at least the same size as the DMA buffer size, otherwise
    * DATA WILL BE LOST!! Hence, this is set to the DMA buffer size _plus_ 8B, just in case the division
    * rounds down. We divide by sizeof(uint32_t), 4B, since the buffer is in 32-bit words, while the DMA
    * buffer is in bytes.
    *
    * EVENTBUFFSIZE: This is the event buffer in the write thread. It is used to chunk the events so the
    * writes are larger and therefore more efficient (see `EVENTCHUNK` above).
    */
    // Full setup 1 charge FEM 100k and 3 charge FEM 231k
    constexpr static size_t EVENTSIZE = 231000; // in bytes
    constexpr static size_t DMABUFFSIZE = static_cast<size_t>(1.3 * EVENTSIZE);
    constexpr static size_t DATABUFFSIZE = (DMABUFFSIZE / sizeof(uint32_t)); //+ 2 * sizeof(uint32_t);
    constexpr static size_t EVENTBUFFSIZE = DATABUFFSIZE * EVENTCHUNK;

    static_assert((DMABUFFSIZE % sizeof(uint32_t)) == 0, "DataHandler DMABUFFSIZE must be a multiple of 4!");

    static constexpr uint32_t kDev1 = pcie_int::PCIeInterface::kDev1;
    static constexpr uint32_t kDev2 = pcie_int::PCIeInterface::kDev2;

    typedef folly::ProducerConsumerQueue<std::array<uint32_t, DATABUFFSIZE>> Queue;
    Queue data_queue_;
    typedef folly::ProducerConsumerQueue<std::array<uint32_t, 8>> TrigQueue;
    TrigQueue trigger_queue_;

    int ext_trig_;
    int software_trig_;
    int software_trigger_rate_{};
    int trigger_module_;
    size_t num_events_;
    size_t num_dma_loops_ = 1;
    size_t num_recv_bytes_{};
    int pps_sample_period_;
    size_t run_number_;
    unsigned long long trig_data_ctr_;

    // Metric counters
    std::atomic<size_t> event_count_ = 0;
    std::atomic<size_t> dma_loop_count_ = 0;
    std::atomic<size_t> num_recv_mB_ = 0;
    std::atomic<size_t> num_event_chunk_words_ = 0;
    std::atomic<size_t> event_diff_ = 0;

    // uint32_t data;
    // static unsigned long long u64Data;

    std::string data_basedir_;
    std::string write_file_name_;
    std::atomic<size_t> file_count_;

    std::atomic_bool is_running_;
    std::atomic_bool stop_write_;

    pcie_int::DMABufferHandle  pbuf_rec1_{};
    pcie_int::DMABufferHandle pbuf_rec2_{};

    int fd_;
    quill::Logger* logger_;

};

} // data_handler

#endif //DATA_HANDLER_H
