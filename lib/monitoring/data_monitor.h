//
// Created by Jon Sensenig on 3/18/25.
//

#ifndef DATA_MONITOR_H
#define DATA_MONITOR_H

#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <iostream>
#include <memory>
#include "zmq.h"
#include <zmq.hpp>
#include <map>


namespace data_monitor {

class DataMonitor {
public:

    explicit DataMonitor(bool enable_metrics);
    ~DataMonitor();

    bool LoadMetrics();
    void ResetMetrics();
    template<typename T>
    void SendMetrics(T &data, size_t size);

    size_t id = reinterpret_cast<size_t>(this);
    inline void PrintInst() { std::cout << "this METRICS: " << id << std::endl; }
    inline void IsRunning(bool running) { is_running_.store(running); }
    inline void DmaSize(size_t dma_size) { dma_size_.store(dma_size); }
    inline void NumEvents(size_t num_evts) { num_events_.store(num_evts); }
    inline void EventDiff(size_t event_diff) { event_diff_.store(event_diff); }
    inline void DmaLoops(size_t num_dma_loops) { num_dma_loops_.store(num_dma_loops); }
    inline void NumFiles(size_t num_files) { num_files_.store(num_files); }
    inline void NumAdcWords(size_t num_adc_words) { adc_words_per_event_.store(num_adc_words); }
    inline void EventSize(size_t num_words) { words_per_event_.store(num_words); }
    inline void BytesReceived(size_t num_bytes) { bytes_received_.store(num_bytes); }

 private:

    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;

    bool enable_metrics_;

   // Metric variables

    // State
    std::atomic_bool is_running_;

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

    std::map<std::string, size_t> metric_map_;

};

} // data_monitor

#endif //DATA_MONITOR_H
