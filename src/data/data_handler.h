//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H

#include "hardware_device.h"
#include <atomic>
#include <fcntl.h>

namespace data_handler {

class DataHandler : public HardwareDevice {
public:

    DataHandler();
    ~DataHandler() override = default;

    bool Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) override;
    std::vector<uint32_t> GetStatus() override;
    bool CloseDevice() override;

    void SetRun(bool set_running);

private:

    bool SetRecvBuffer(json &config, pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2);
	bool CollectData(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers);

    uint32_t dma_buf_size_ = 100000;
    size_t num_events_;
    size_t num_dma_loops_ = 1;
    size_t num_recv_bytes_;
    size_t event_count_ = 0;

    pcie_int::DMABufferHandle  pbuf_rec1_;
    pcie_int::DMABufferHandle pbuf_rec2_;

//    std::unique_ptr<FILE> data_file_;
    static int file_ptr_;

    quill::Logger* logger_;

	std::atomic_bool is_running_;

};

} // data_handler

#endif //DATA_HANDLER_H
