//
// Created by Jon Sensenig on 3/18/25.
//

#include "data_monitor.h"
#include "json.hpp"

namespace data_monitor {

    using json = nlohmann::json;

    DataMonitor::DataMonitor(bool enable_metrics) : enable_metrics_(enable_metrics) {
        if (enable_metrics_) {
            context_ = std::make_unique<zmq::context_t>(1); // Single I/O thread context
            socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUSH); // PUSH socket type
            socket_->connect("tcp://localhost:5555"); // Connect to the given endpoint
        }
        ResetMetrics();
    }

    DataMonitor:: ~DataMonitor() {
        std::cout << "Destructing metrics.. " << std::endl;
        ResetMetrics();
        if (enable_metrics_) {
            constexpr int linger_value = 0;
            // Discard any lingering messages so we can close the socket
            socket_->setsockopt(ZMQ_LINGER, &linger_value, sizeof(linger_value));
            socket_->close();
            context_->close();
        }
        std::cout << "Closed metric socket.. " << std::endl;
    }

    template<typename T>
    void DataMonitor::SendMetrics(T &data, const size_t size) {
        zmq::message_t message(size);
        memcpy(message.data(), data.data(), size); // Copy counter to message data
        socket_->send(message, zmq::send_flags::none); // Send the message
        std::cout << "Sent metric: " << data << std::endl;
    }

    bool DataMonitor::LoadMetrics() {

        if (!enable_metrics_) return false;

        json metrics;
        metrics["is_running"] = is_running_.load();
        metrics["dma_buffer_size"] = dma_size_.load();
        metrics["num_events"] = num_events_.load();
        metrics["num_files"] = num_files_.load();
        metrics["num_dma_loops"] = num_dma_loops_.load();
        metrics["adc_words"] = adc_words_per_event_.load();
        metrics["bytes_received"] = bytes_received_.load();
        metrics["event_diff"] = event_diff_.load();

        std::string msg = metrics.dump();
        SendMetrics(msg, msg.size());

        return true;
    }

    void DataMonitor::ResetMetrics() {
        is_running_.store(false);
        // Configuration
        dma_size_.store(0);
        // Counters
        num_events_.store(0);
        num_files_.store(0);
        num_dma_loops_.store(0);
        adc_words_per_event_.store(0);
        words_per_event_.store(0);
        bytes_received_.store(0);
        event_diff_.store(0);
    }

} // data_monitor