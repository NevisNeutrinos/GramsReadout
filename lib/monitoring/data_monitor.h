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

    // This is a singleton so we only allow access to it via
    // a static reference, ensuring there can only be one
    // instance of the class
    static DataMonitor& GetInstance() {
        static DataMonitor instance;
        return instance;
    }

    bool LoadMetrics();
    void ResetMetrics();
    template<typename T>
    void SendMetrics(T &data, size_t size);

    size_t id = reinterpret_cast<size_t>(this);
    inline void PrintInst() const { std::cout << "this METRICS: " << id << std::endl; }
    inline void IsRunning(const bool running) { is_running_.store(running); }
    inline void DmaSize(const size_t dma_size) { dma_size_.store(dma_size); }
    inline void NumEvents(const size_t num_evts) { num_events_.store(num_evts); }
    inline void EventDiff(const size_t event_diff) { event_diff_.store(event_diff); }
    inline void DmaLoops(const size_t num_dma_loops) { num_dma_loops_.store(num_dma_loops); }
    inline void NumFiles(const size_t num_files) { num_files_.store(num_files); }
    inline void NumAdcWords(const size_t num_adc_words) { adc_words_per_event_.store(num_adc_words); }
    inline void EventSize(const size_t num_words) { words_per_event_.store(num_words); }
    inline void BytesReceived(const size_t num_bytes) { bytes_received_.store(num_bytes); }
    inline void ControllerState(const int state) { state_.store(state); }


    void EnableMonitoring(bool enable_metrics) ;

 private:

    explicit DataMonitor();
    ~DataMonitor();

    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;

    bool enable_metrics_;

   // Metric variables

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

    std::map<std::string, size_t> metric_map_;

};

} // data_monitor

#endif //DATA_MONITOR_H
