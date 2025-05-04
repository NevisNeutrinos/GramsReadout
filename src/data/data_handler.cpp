//
// Created by Jon Sensenig on 2/24/25.
//

#include "data_handler.h"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <chrono>

#include "quill/LogMacros.h"

namespace data_handler {

    // FIXME, make queue size configurable
    DataHandler::DataHandler() : num_events_(0), data_queue_(400), trigger_queue_(100),
    stop_write_(false), metrics_(data_monitor::DataMonitor::GetInstance()) {
        logger_ = quill::Frontend::create_or_get_logger("readout_logger");
    }

    DataHandler::~DataHandler() {
        std::cout << "Running DataHandler Destructor" << std::endl;
        metrics_.reset();
    }

    bool DataHandler::SetRecvBuffer(pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2, bool is_data) {

        static uint32_t data_32_;
        uint32_t dev_handle = is_data ? kDev2 : kDev1;

        // Lock DMA buffers, they should have been unlocked
        if (is_data) {
            bool init_buffer = pcie_interface->DmaContigBufferLock(1, DMABUFFSIZE, pbuf_rec1);
            if (!init_buffer) return false;
            init_buffer = pcie_interface->DmaContigBufferLock(2, DMABUFFSIZE, pbuf_rec2);
            if (!init_buffer) return false;
        } else { // FIXME config trig_dma_size
            pcie_interface->DmaContigBufferLock(3, 32, pbuf_rec1);
        }

        /* set tx mode register */
        data_32_ = 0x00002000;
        pcie_interface->WriteReg32(dev_handle, hw_consts::cs_bar, hw_consts::tx_md_reg, data_32_);

        /* write this will abort previous DMA */
        data_32_ = hw_consts::dma_abort;
        pcie_interface->WriteReg32(dev_handle, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data_32_);

        /* clear DMA register after the abort */
        data_32_ = 0;
        pcie_interface->WriteReg32(dev_handle, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data_32_);

        return true;
    }

    bool DataHandler::Configure(json &config) {

        trigger_module_ = config["crate"]["trig_slot"].get<int>();
        software_trigger_rate_ = config["trigger"]["software_trigger_rate_hz"].get<int>();
        num_events_ = config["data_handler"]["num_events"].get<size_t>();
        metrics_->DmaSize(DMABUFFSIZE);
        const size_t subrun = config["data_handler"]["subrun"].get<size_t>();
        std::string trig_src = config["trigger"]["trigger_source"].get<std::string>();
        ext_trig_ = trig_src == "ext" ? 1 : 0;
        software_trig_ = trig_src == "software" ? 1 : 0;
        LOG_INFO(logger_, "Trigger source software [{}] external [{}] \n", software_trig_, ext_trig_);

        LOG_INFO(logger_, "\t [{}] DMA loops with [{}] 32b words \n", num_dma_loops_, DATABUFFSIZE / 4);

        file_count_ = 0;
        write_file_name_ = "/data/readout_data/pGRAMS_bin_" + std::to_string(subrun) + "_";
        LOG_INFO(logger_, "\n Writing files: {}", write_file_name_);

        return true;
    }

    bool DataHandler::SwitchWriteFile() {

        // Make sure we copy `fd_` so when we re-assign it later it doesn't affect the thread.
        // Start a thread and detach it so the file closes in the background at its leisure
        // while we continue to write data to the newly opened file.
        int fd_copy = fd_;
        std::thread close_thread([&fd_copy, this]() {
            LOG_INFO(logger_, "Closing data file {} \n", fd_copy);
            if(close(fd_copy) == -1) {
                LOG_ERROR(logger_, "Failed to close data file, with error:{} \n", std::string(strerror(errno)));
            }
        });
        close_thread.detach();

        file_count_ += 1;
        std::string name = write_file_name_  + std::to_string(file_count_) + ".dat";

        fd_ = open(name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ == -1) {
            LOG_ERROR(logger_, "Failed to open file {} with error {} aborting run! \n", name, std::string(strerror(errno)));
            return false;
        }

        LOG_INFO(logger_, "Switching to file: {} fd={}\n", name, fd_);
        metrics_->NumFiles(file_count_);
        return true;
    }

    void DataHandler::CollectData(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers *buffers) {

        auto write_thread = std::thread(&DataHandler::DataWrite, this);
        // auto read_thread = std::thread(&DataHandler::ReadoutDMARead, this, pcie_interface);
        auto read_thread = std::thread(&DataHandler::ReadoutViaController, this, pcie_interface, buffers);

        auto trigger_thread = std::thread(&trig_ctrl::TriggerControl::SendSoftwareTrigger, &trigger_,
            pcie_interface, software_trigger_rate_, trigger_module_);

        // FIXME Combines both Data and Trigger DMA readout, works for a little then hangs
        // auto read_thread = std::thread(&DataHandler::TestReadoutDMARead, this, pcie_interface);
        // FIXME Trigger DMA readout, causes both the trigger and data DMA to hang even though they're in
        // separate threads with separate DMAs
        //auto trigger_thread = std::thread(&DataHandler::TriggerDMARead, this, pcie_interface);
        LOG_INFO(logger_, "Started read and write threads... \n");

        // Shut down the write thread first so we're not trying to access buffer ptrs
        // after they have been deleted in the read thread
        write_thread.join();
        LOG_INFO(logger_, "write thread joined... \n");

        read_thread.join();
        LOG_INFO(logger_, "read thread joined... \n");

        // The read/write threads will block until a run stop is set. Then we stop the trigger.
        trigger_.SetRun(false);
        trigger_thread.join();
        LOG_INFO(logger_, "trigger thread joined... \n");
        //trigger_thread.join();
        //LOG_INFO(logger_, "trigger thread joined... \n");
    }

    void DataHandler::DataWrite() {
        LOG_INFO(logger_, "Read thread start! \n");

        std::string name = write_file_name_  + std::to_string(file_count_) + ".dat";
        // 0644 user, group and others read/write permissions
        fd_ = open(name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ == -1) {
            LOG_ERROR(logger_, "Failed to open file {} with error {} aborting run! \n", name, std::string(strerror(errno)));
        }

        uint32_t word;
        std::array<uint32_t, DATABUFFSIZE> word_arr{};
        std::array<uint32_t, EVENTBUFFSIZE> word_arr_write{}; // event buffer, ~0.231MB/event
        size_t prev_event_count = 0;
        bool event_start = false;
        size_t num_words = 0;
        size_t event_words = 0;
        size_t event_chunk = 0;
        size_t event_start_count = 0;
        size_t event_end_count = 0;
        size_t num_recv_bytes = 0;
        size_t local_event_count = 0;
        event_count_.store(0);

        auto start = std::chrono::high_resolution_clock::now();

        while (!stop_write_.load()) {
            while (data_queue_.read(word_arr)) {
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start);
                if (elapsed.count() >= 4) {
                    metrics_->EventDiff(local_event_count - prev_event_count);
                    prev_event_count = local_event_count;
                    start = now;
                    metrics_->LoadMetrics();
                }
                for (size_t i = 0; i < DATABUFFSIZE; i++) {
                    word = word_arr[i];
                    if (num_words > word_arr_write.size()) {
                        LOG_WARNING(logger_, "Unexpectedly large event, dropping it! num_words=[{}] > event buffer size=[{}] \n",
                                    num_words, EVENTBUFFSIZE);
                        num_words = 0;
                        event_start = false;
                    }
                    if (isEventStart(word)) {
                        if (event_start) num_words = 0; // Previous evt didn't finish, drop partial evt data
                        event_start = true; event_start_count++;
                    }
                    else if (isEventEnd(word) && event_start) {
                        event_end_count++; event_start = false;
                        event_chunk++;
                        event_words = num_words + 1; // add one for event end word
                    }
                    word_arr_write[num_words] = word;
                    num_words++;
                    // num_recv_bytes += 4;
                    if (event_chunk == EVENTCHUNK) {
                        if ((local_event_count % 500) == 0) LOG_INFO(logger_, " **** Event: {} \n", local_event_count);
                        local_event_count += event_chunk;
                        event_count_.store(local_event_count);
                        const size_t write_bytes = write(fd_, word_arr_write.data(), num_words*sizeof(uint32_t));
                        if (write_bytes == -1) LOG_WARNING(logger_, "Failed write {} \n", std::string(strerror(errno)));
                        else num_recv_bytes += write_bytes;

                        if ((local_event_count > 0) && (local_event_count % 5000 == 0)) {
                            SwitchWriteFile();
                        }
                        metrics_->EventSize(num_words);
                        metrics_->BytesReceived(num_recv_bytes);
                        metrics_->NumEvents(local_event_count);
                        event_start = false; num_words = 0; event_words = 0; event_chunk = 0;
                    }
                } // word loop
            } // read buffer loop
        } // run loop

        // Write any remaining full events in the buffer to file before closing
        const size_t write_bytes = write(fd_, word_arr_write.data(), event_words*sizeof(uint32_t));
        if (write_bytes == -1) LOG_WARNING(logger_, "Failed write {} \n", std::string(strerror(errno)));
        else num_recv_bytes += write_bytes;

        LOG_INFO(logger_, "Ended data write and closing file..\n");
        // Make sure all data is flushed to file before closing
        if(fsync(fd_) == -1) {
            LOG_ERROR(logger_, "Failed to sync data file with error: {} \n", std::string(strerror(errno)));
        }
        if(close(fd_) == -1) {
            LOG_ERROR(logger_, "Failed to close data file with error: {} \n", std::string(strerror(errno)));
        }

        LOG_INFO(logger_, "Closed file after writing {}B to file {} \n", num_recv_bytes, write_file_name_);
        LOG_INFO(logger_, "Wrote {} events to {} files \n", event_count_.load(), file_count_);
        LOG_INFO(logger_, "Counted [{}] start events & [{}] end events \n", event_start_count, event_end_count);
    }

    void DataHandler::ReadoutDMARead(pcie_int::PCIeInterface *pcie_interface) {

        metrics_->IsRunning(is_running_);
        bool idebug = false;
        bool is_first_event = true;
        static uint32_t iv, r_cs_reg;
        static uint32_t num_dma_byte = DMABUFFSIZE;
        static uint32_t is;
        static int itrig_c = 0;
        static int itrig_ext = 1;

        uint32_t data;
        static unsigned long long u64Data;
        static uint32_t *buffp_rec32;
        static std::array<uint32_t, DATABUFFSIZE> word_arr{};
        size_t dma_loops = 0;
        size_t num_buffer_full = 0;

        pcie_int::DMABufferHandle  pbuf_rec1;
        pcie_int::DMABufferHandle pbuf_rec2;

         /*TPC DMA*/
        LOG_INFO(logger_, "Buffer 1 & 2 allocation size: {} \n", DMABUFFSIZE);
        SetRecvBuffer(pcie_interface, &pbuf_rec1, &pbuf_rec2, true);
        usleep(500000);

        while(is_running_.load() && event_count_.load() < num_events_) {
            if (dma_loops % 500 == 0) LOG_INFO(logger_, "=======> DMA Loop [{}] \n", dma_loops);
            for (iv = 0; iv < 2; iv++) { // note: ndma_loop=1
                static uint32_t dma_num = (iv % 2) == 0 ? 1 : 2;
                buffp_rec32 = dma_num == 1 ? static_cast<uint32_t *>(pbuf_rec1) : static_cast<uint32_t *>(pbuf_rec2);
                // sync CPU cache
                pcie_interface->DmaSyncCpu(dma_num);

                // nwrite_byte = DMABUFFSIZE;
                if (idebug) LOG_INFO(logger_, "DMA loop {} with DMA length {}B \n", iv, DMABUFFSIZE);

                /** initialize and start the receivers ***/
                for (size_t rcvr = 1; rcvr < 3; rcvr++) {
                    r_cs_reg = rcvr == 1 ? hw_consts::r1_cs_reg : hw_consts::r2_cs_reg;
                    if (is_first_event) {
                        pcie_interface->WriteReg32(kDev2,  hw_consts::cs_bar, r_cs_reg, hw_consts::cs_init);
                    }
                    data = hw_consts::cs_start + num_dma_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
                    pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, r_cs_reg, data);
                }

                is_first_event = false;

                /** set up DMA for both transceiver together **/
                data = pcie_interface->GetBufferPageAddrLower(dma_num);
                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_low_reg, data);

                data = pcie_interface->GetBufferPageAddrUpper(dma_num);
                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_high_reg, data);

                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, num_dma_byte);

                /* write this will start DMA */
                is = pcie_interface->GetBufferPageAddrUpper(dma_num);
                data = is == 0 ? hw_consts::dma_tr12 + hw_consts::dma_3dw_rec : hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;

                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
                if (idebug) LOG_INFO(logger_, "DMA set up done, byte count = {} \n", DMABUFFSIZE);

                // send trigger
                if (iv == 0) trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, software_trig_, itrig_ext, trigger_module_);

                if (!WaitForDma(pcie_interface, &data, kDev2)) {
                    LOG_WARNING(logger_, " loop [{}] DMA is not finished, aborting...  \n", iv);
                    pcie_interface->ReadReg64(kDev2,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
                    pcie_interface->DmaSyncIo(dma_num);
                    static size_t num_read = (num_dma_byte - (u64Data & 0xffff));

                    LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);

                    std::memcpy(word_arr.data(), buffp_rec32, num_read);
                    data_queue_.write(word_arr);
                    if (data_queue_.isFull()) {
                        num_buffer_full++;
                        LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                    }
                    ClearDmaOnAbort(pcie_interface, &u64Data, kDev2);
                    // If DMA did not finish, abort loop!
                    break;
                }
                // sync DMA I/O cache
                pcie_interface->DmaSyncIo(dma_num);

                if (idebug) {
                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));

                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));
                }
                std::memcpy(word_arr.data(), buffp_rec32, DMABUFFSIZE);
                data_queue_.write(word_arr);
                if (data_queue_.isFull()) {
                    num_buffer_full++;
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }
            } // end dma loop
            dma_loops += 1;
            metrics_->DmaLoops(dma_loops);
        } // end loop over events

        LOG_INFO(logger_, "Stopping triggers..\n");
        trig_ctrl::TriggerControl::SendStopTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);

        // If event count triggered the end of run, set stop write flag so write thread completes
        LOG_INFO(logger_, "Stopping run and freeing pointer \n");
        stop_write_.store(true);

        if (num_buffer_full > 0) LOG_WARNING(logger_, "Buffer full: [{}]\n", num_buffer_full);

        // Since the two PCIe buffer handles are scoped to this function, free the buffer before
        // they go out of scope
        LOG_INFO(logger_, "Freeing DMA buffers and closing file..\n");
        if (!pcie_interface->FreeDmaContigBuffers()) {
            LOG_ERROR(logger_, "Failed freeing DMA buffers! \n");
        }

        LOG_INFO(logger_, "Ran {} DMA loops \n", dma_loops);
     }

    void DataHandler::TestReadoutDMARead(pcie_int::PCIeInterface *pcie_interface) {
        // This is a test function for running the data and trigger DMAs together
        // currently does not work.
        metrics_->IsRunning(is_running_);
        bool idebug = true;
        bool is_first_event = true;
        static uint32_t iv, r_cs_reg;
        static uint32_t nwrite_byte;
        static uint32_t is;
        static int itrig_c = 0;
        static int itrig_ext = 1;

        uint32_t data;
        static unsigned long long u64Data;
        static uint32_t *buffp_rec32;
        static std::array<uint32_t, DATABUFFSIZE> word_arr{};
        static std::array<uint32_t, 8> trig_word_arr{};
        size_t dma_loops = 0;
        size_t num_buffer_full = 0;
        uint32_t dev_num;

        pcie_int::DMABufferHandle  pbuf_rec1;
        pcie_int::DMABufferHandle pbuf_rec2;
        pcie_int::DMABufferHandle pbuf_rec_trig;

         /*TPC DMA*/
        LOG_INFO(logger_, "Buffer 1 & 2 & Trig allocation size: {} \n", DATABUFFSIZE);
        SetRecvBuffer(pcie_interface, &pbuf_rec1, &pbuf_rec2, true);
        SetRecvBuffer(pcie_interface, &pbuf_rec_trig, nullptr, false);
        usleep(500000);

        while(is_running_.load() && dma_loops < num_events_) {
            if (dma_loops % 500 == 0) LOG_INFO(logger_, "=======> DMA Loop [{}] \n", dma_loops);
            for (iv = 0; iv < 3; iv++) { // note: ndma_loop=1
                static uint32_t dma_num = iv + 1; //(iv % 2) == 0 ? 1 : 2;
                dev_num = dma_num == 3 ? kDev1 : kDev2;
                if (dma_num == 1) buffp_rec32 = static_cast<uint32_t *>(pbuf_rec1);
                if (dma_num == 2) buffp_rec32 = static_cast<uint32_t *>(pbuf_rec2);
                if (dma_num == 3) buffp_rec32 = static_cast<uint32_t *>(pbuf_rec_trig);
                // buffp_rec32 = dma_num == 1 ? static_cast<uint32_t *>(pbuf_rec1) : static_cast<uint32_t *>(pbuf_rec2);
                // sync CPU cache
                pcie_interface->DmaSyncCpu(dma_num);

                nwrite_byte = DATABUFFSIZE;
                if (idebug) LOG_INFO(logger_, "DMA loop {} with DMA length {}B \n", iv, nwrite_byte);

                /** initialize and start the receivers ***/
                for (size_t rcvr = 1; rcvr < 4; rcvr++) {
                    r_cs_reg = rcvr == 1 ? hw_consts::r1_cs_reg : hw_consts::r2_cs_reg;
                    if (is_first_event) {
                        pcie_interface->WriteReg32(dev_num,  hw_consts::cs_bar, r_cs_reg, hw_consts::cs_init);
                    }
                    data = hw_consts::cs_start + nwrite_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
                    pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, r_cs_reg, data);
                }

                is_first_event = false;

                /** set up DMA for both transceiver together **/
                data = pcie_interface->GetBufferPageAddrLower(dma_num);
                pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_add_low_reg, data);

                data = pcie_interface->GetBufferPageAddrUpper(dma_num);
                pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_add_high_reg, data);

                pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, nwrite_byte);

                /* write this will start DMA */
                is = pcie_interface->GetBufferPageAddrUpper(dma_num);
                data = is == 0 ? hw_consts::dma_tr12 + hw_consts::dma_3dw_rec : hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;

                pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
                if (idebug) LOG_INFO(logger_, "DMA set up done, byte count = {} \n", nwrite_byte);

                // send trigger
                if (iv == 0) trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, itrig_c, itrig_ext, trigger_module_);

                if (!WaitForDma(pcie_interface, &data, dev_num)) {
                    LOG_WARNING(logger_, " loop [{}] DMA is not finished, aborting...  \n", iv);
                    pcie_interface->ReadReg64(dev_num,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
                    pcie_interface->DmaSyncIo(dma_num);
                    static size_t num_read = (nwrite_byte - (u64Data & 0xffff));

                    LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);

                    std::memcpy(word_arr.data(), buffp_rec32, num_read);
                    data_queue_.write(word_arr);
                    if (data_queue_.isFull()) {
                        num_buffer_full++;
                        LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                    }
                    ClearDmaOnAbort(pcie_interface, &u64Data, dev_num);
                    // If DMA did not finish, abort loop!
                    break;
                }
                // sync DMA I/O cache
                pcie_interface->DmaSyncIo(dma_num);

                if (idebug) {
                    u64Data = 0;
                    pcie_interface->ReadReg64(dev_num, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));

                    u64Data = 0;
                    pcie_interface->ReadReg64(dev_num, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));
                }
                std::memcpy(trig_word_arr.data(), buffp_rec32, 8*4);
                // if (dma_num == 3) for (const auto &el : trig_word_arr ) std::cout << el << "  \n";
                if (dma_num == 3) std::cout << trig_word_arr[0] << "  " << trig_word_arr[1] << " " << trig_word_arr[4] << "  \n";
                std::memcpy(word_arr.data(), buffp_rec32, DATABUFFSIZE);
                data_queue_.write(word_arr);
                if (data_queue_.isFull()) {
                    num_buffer_full++;
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }
            } // end dma loop
            dma_loops += 1;
            metrics_->DmaLoops(dma_loops);
        } // end loop over events

        LOG_INFO(logger_, "Stopping triggers..\n");
        trig_ctrl::TriggerControl::SendStopTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);

        // If event count triggered the end of run, set stop write flag so write thread completes
        LOG_INFO(logger_, "Stopping run and freeing pointer \n");
        stop_write_.store(true);

        if (num_buffer_full > 0) LOG_WARNING(logger_, "Buffer full: [{}]\n", num_buffer_full);

        LOG_INFO(logger_, "Flushing data buffer \n");
        while (!data_queue_.isEmpty()) data_queue_.popFront();

        LOG_INFO(logger_, "Freeing DMA buffers and closing file..\n");
        if (!pcie_interface->FreeDmaContigBuffers()) {
            LOG_ERROR(logger_, "Failed freeing DMA buffers! \n");
        }

        LOG_INFO(logger_, "Ran {} DMA loops \n", dma_loops);
     }

    void DataHandler::ReadoutViaController(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers *buffers) {

        // std::array<uint32_t, 100000> buf_send;
        // std::array<uint32_t, 100000> read_array{};
        std::array<uint32_t, DATABUFFSIZE> word_arr{};
        // uint32_t *psend{};
        // uint32_t *precv{};
        // psend = buf_send.data();
        // precv = read_array.data();

        buffers->psend = buffers->buf_send.data();

        size_t num_controller_triggers = 1;
        int nword = 0;
        uint32_t fem_number = 13;
        bool print = true;

        //kaleko 013013
        //set calibration delay.  number has to be smaller than frame size
        //the trigger will be fired that fixed delay after the frame sync
        //set up calibration delay to 0x10
        buffers->buf_send[0] = (trigger_module_ << 11) + (hw_consts::mb_trig_calib_delay) + ((0x10) << 16);
        //change 0x10 to 0x11, 0x12, 0x13 SHOULD shift the pulse.  currently broken.  ask chi
        pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

        uint32_t ichip=3;
        buffers->buf_send[0] = (fem_number << 11) + (ichip << 8) + 10 + (0x1 << 16);    // enable a test n // test point mode
        pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

        //Set trigger run
        buffers->buf_send[0] = (trigger_module_ << 11) + (hw_consts::mb_trig_run) + ((0x1) << 16); //set up run
        pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

        printf("Just set up the trigger run ...\n");
        usleep(5000); //wait for 5 ms

        while(is_running_.load() && event_count_.load() < num_events_) {
            for (size_t t = 0; t < num_controller_triggers; t++) {//jcrespo: multiple triggers
                //kaleko 013013 changing mb_trig_pctrig to mb_trig_calib
                buffers->buf_send[0] = (trigger_module_ << 11) + hw_consts::mb_trig_calib + ((0x0) << 16);
                pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);
                usleep(10000);
            } //jcrespo: end of multiple triggers

            // FIXME set module number again to enable the FEB module read back
            // set user-defined module number to appear in output data header
            uint32_t ichip=3;
            buffers->buf_send[0] = (fem_number << 11) + (ichip << 8) + hw_consts::mb_feb_mod_number + (fem_number << 16);
            pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

            usleep(5000); // wait for 5 ms

            for (size_t t = 0; t < num_controller_triggers; t++) {
                nword = 5;
                pcie_interface->PCIeRecvBuffer(kDev1, 0, 1, nword, 1, buffers->precv); // init the receiver

                // read a header // enable read for neutrino header buffer through slow readout
                ichip=3;
                buffers->buf_send[0] = (fem_number << 11) + (ichip << 8) + hw_consts::mb_feb_a_rdhed ;//+ (0x1 << 16);
                pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

                // py = &read_array;
                // read out 2 32 bits words
                for (size_t i = 0; i < 5; i++) pcie_int::PcieBuffers::read_array[i] = 0;
                buffers->precv = pcie_int::PcieBuffers::read_array.data();
                pcie_interface->PCIeRecvBuffer(kDev1, 0, 2, nword, 1, buffers->precv);
                if (print) printf("receive data word = %x, %x, %x, %x, %x, %x\n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1], pcie_int::PcieBuffers::read_array[2], pcie_int::PcieBuffers::read_array[3], pcie_int::PcieBuffers::read_array[4], pcie_int::PcieBuffers::read_array[5]);
                // For some reason the first read is garbage so read once before
                // if (t < 5) continue;

                if (print) printf("receive data word = %x, %x, %x, %x, %x, %x\n", pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1], pcie_int::PcieBuffers::read_array[2], pcie_int::PcieBuffers::read_array[3], pcie_int::PcieBuffers::read_array[4], pcie_int::PcieBuffers::read_array[5]);
                if (print) {
                    printf(" header word %x \n",(pcie_int::PcieBuffers::read_array[0] & 0xFFFF));
                    uint32_t k = (pcie_int::PcieBuffers::read_array[0] >> 16) & 0xFFF;
                    printf(" module adress %d, id number %d\n", (k & 0x1f), ((k>>5) & 0x7f));
                    printf(" number of data word to read %d\n", (((pcie_int::PcieBuffers::read_array[1]>>16) & 0xfff)+((pcie_int::PcieBuffers::read_array[1] &0xfff) <<12)));
                    printf(" event number %d\n", (((pcie_int::PcieBuffers::read_array[2]>>16) & 0xfff)+((pcie_int::PcieBuffers::read_array[2] &0xfff) <<12)));
                    printf(" frame number %d\n", (((pcie_int::PcieBuffers::read_array[3]>>16) & 0xfff)+((pcie_int::PcieBuffers::read_array[3] &0xfff) <<12)));
                    printf(" checksum %x\n", (((pcie_int::PcieBuffers::read_array[4]>>16) & 0xfff)+((pcie_int::PcieBuffers::read_array[4] &0xfff) <<12)));
                }

                // Save header words
                std::memcpy(word_arr.data(), pcie_int::PcieBuffers::read_array.data(), 6*sizeof(pcie_int::PcieBuffers::read_array[0]));
                data_queue_.write(word_arr);
                if (data_queue_.isFull()) {
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }
                // int nwrite_1 = write(outBinFile, read_array, 6*sizeof(read_array[0]));
                // printf("\n\n\n\n\t %d header bytes written to %s \n\n\n\n", nwrite_1, outBinFileName);

                uint32_t nread = ((pcie_int::PcieBuffers::read_array[1] >> 16) & 0xFFF) + ((pcie_int::PcieBuffers::read_array[1] & 0xFFF) << 12);

                nword = (nread + 1) / 2;                    // short words
                // i = pcie_rec(hDev,0,1,nword,iprint,py);     // init the receiver
                pcie_interface->PCIeRecvBuffer(kDev1, 0, 1, nword, 1, buffers->precv); // init the receiver

                // Read neutrino data through controller (slow control path)
                buffers->buf_send[0] = (fem_number << 11) + (hw_consts::mb_feb_pass_add << 8) + hw_consts::mb_feb_a_rdbuf + (0x0 << 16);
                pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

                // jcrespo verbose test: read all the words? To do: adjust timesize too
                // nword = 64*1024/2;

                // jcrespo: code reviewed and commented till here (Sep 13, 2016)

                // py = &read_array;
                // i = pcie_rec(hDev,0,2,nword,iprint,py);     // read out 2 32 bits words
                buffers->precv = pcie_int::PcieBuffers::read_array.data();
                pcie_interface->PCIeRecvBuffer(kDev1, 0, 2, nword, 1, buffers->precv);

                // if(iprint == 1) {
                //     for (i=0; i< nword; i++) {
                //         if((i%8) ==0) printf("%4d",i);
                //         printf(" %8x",read_array[i]);
                //         if(((i+1)%8) ==0 ) printf("\n");
                //     }
                // }

                // ik=0;

                //char outBinFileName[256];
                //sprintf(outBinFileName, "%s_output.dat", outDate);
                //FILE* outBinFile = creat(outBinFileName,0755);
                // int nwrite_2 = write(outBinFile, read_array, nword*sizeof(read_array[0]));
                // printf("\n\n\n\n\t %d bytes written to %s \n\n\n\n", nwrite_2, outBinFileName);

                // Save the rest of the event words
                std::memcpy(word_arr.data(), pcie_int::PcieBuffers::read_array.data(), nword*sizeof(pcie_int::PcieBuffers::read_array[0]));
                data_queue_.write(word_arr);
                if (data_queue_.isFull()) {
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }

                // Sort words
                // for (i=0; i< nword; i++) {
                //   read_array_s[ik] = read_array[i] &0xffff;
                //   read_array_s[ik+1] = ((read_array[i]>>16) & 0xffff);
                //   ik=ik+2;
                // }

                //
                //      printout formatted word
                //
                // if(iprint ==1) {
                //   iset = 0;
                //   for(i=0; i< 2*nword; i++) {
                //     if((read_array_s[i] & 0xf000) == 0x4000) {
                //       iset=1;
                //       ncount=0;
                //       printf(" channel %d\n",(read_array_s[i] & 0xfff));
                //     }
                //     else if ((read_array_s[i] & 0xf000) == 0x5000) printf(" channel end %d\n",(read_array_s[i] &0xfff));
                //     else if (iset ==1) {
                //       printf(" %4x",read_array_s[i]);
                //       ncount = ncount+1;
                //       if((ncount%8) == 0) printf("\n");
                //     }
                //     else {
                //       printf("%x",read_array_s[i]);
                //       ncount = ncount+1;
                //       if((ncount%8) == 0) printf("\n");
                //     }
                //   }
                // }

                // jcrespo verbose test
                // icheck = 1;

                // if(icheck ==1 ){
                //   if((2*nword) == (64*timesize*3)){
                //     for (i=0; i<64; i++){
                //       k=i*(timesize*3);
                //       ij= i*256;
                //       if(read_array_s[k] != (0x4000+i))
                //         printf(" first word error, event %d data received %x, data expected %x\n", is, read_array_s[k], (0x4000+i));
                //       for (ik=0; ik< ((3*timesize)-2); ik++) {
                //         if(read_array_s[k+1+ik] != send_array[ij+ik])
                //           printf(" data word error, event %d ch = %d, received %x, expected %x\n",is,i,read_array_s[k+1+ik], send_array[ij+ik]);
                //       }
                //       k=(i+1)*(timesize*3)-1;
                //       if(read_array_s[k] != (0x5000+i))
                //         printf(" last word error, event %d data received %x, data expected %x\n", is, read_array_s[k], (0x5000+i));
                //     }
                //   }
                //   else {
                //     printf(" event %d number word receive = %d, expected=  %d \n", is, (2*nword), (64*timesize*3));
                //   }
                // }

            } // trig loop
            //
            // if(icheck ==1) {
            //     k = is%1000;
            //     if(k ==0) printf("event %d\n",is);
            // }
            LOG_INFO(logger_, "Read trigger... \n");
            usleep(100000); // wait for 100 ms -> ~10Hz
        } // event while loop

        LOG_INFO(logger_, "Setting run off.. \n");
        uint32_t imod_place = 0;
        ichip = 1;
        buffers->buf_send[0] = (imod_place << 11) + (ichip << 8) + (hw_consts::mb_cntrl_set_run_off) + (0x0 << 16); //enable offline run off
        pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

        // If event count triggered the end of run, set stop write flag so write thread completes
        LOG_INFO(logger_, "Stopping run and freeing pointer \n");
        stop_write_.store(true);

        // close(outBinFile);

    }

        void DataHandler::TriggerDMARead(pcie_int::PCIeInterface *pcie_interface) {

        bool idebug = true;
        bool is_first_event = true;
        static uint32_t nwrite_byte;
        static uint32_t is;
        static int itrig_c = 0;
        static int itrig_ext = 1;

        uint32_t data;
        static unsigned long long u64Data;
        static uint32_t *buffp_rec32;
        static std::array<uint32_t, 8> word_arr{};
        size_t dma_loops = 0;
        size_t num_buffer_full = 0;
        static uint32_t dma_num = 3;

        pcie_int::DMABufferHandle  pbuf_rec1_trig;

         /*TPC DMA*/
        SetRecvBuffer(pcie_interface, &pbuf_rec1_trig, nullptr, false);
        buffp_rec32 = static_cast<uint32_t *>(pbuf_rec1_trig);
        usleep(500000);

        while(is_running_.load()) {
            if (dma_loops % 500 == 0) LOG_INFO(logger_, "=======> Trigger DMA Loop [{}] \n", dma_loops);

            // sync CPU cache
            pcie_interface->DmaSyncCpu(dma_num);

            nwrite_byte = 8;

            /** initialize and start the receivers ***/
            if (is_first_event) {
                pcie_interface->WriteReg32(kDev1,  hw_consts::cs_bar, hw_consts::r2_cs_reg, hw_consts::cs_init);
            }
            data = hw_consts::cs_start + nwrite_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
            pcie_interface->WriteReg32(kDev1, hw_consts::cs_bar, hw_consts::r2_cs_reg, data);

            is_first_event = false;

            /** set up DMA for both transceiver together **/
            data = pcie_interface->GetBufferPageAddrLower(dma_num);
            pcie_interface->WriteReg32(kDev1, hw_consts::cs_bar, hw_consts::cs_dma_add_low_reg, data);

            data = pcie_interface->GetBufferPageAddrUpper(dma_num);
            pcie_interface->WriteReg32(kDev1, hw_consts::cs_bar, hw_consts::cs_dma_add_high_reg, data);

            pcie_interface->WriteReg32(kDev1, hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, nwrite_byte);

            /* write this will start DMA */
            is = pcie_interface->GetBufferPageAddrUpper(dma_num);
            data = is == 0 ? hw_consts::dma_tr12 + hw_consts::dma_3dw_rec : hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;

            pcie_interface->WriteReg32(kDev1, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
            if (idebug) LOG_INFO(logger_, "DMA set up done, byte count = {} \n", nwrite_byte);

            // send trigger // FIXME is this needed??
            // trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, itrig_c, itrig_ext, trigger_module_);

            if (!WaitForDma(pcie_interface, &data, kDev1)) {
                LOG_WARNING(logger_, "DMA is not finished, aborting...  \n");
                pcie_interface->ReadReg64(kDev1,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
                pcie_interface->DmaSyncIo(dma_num);
                static size_t num_read = (nwrite_byte - (u64Data & 0xffff));

                LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);

                std::memcpy(word_arr.data(), buffp_rec32, num_read);
                trigger_queue_.write(word_arr);
                if (trigger_queue_.isFull()) {
                    num_buffer_full++;
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }
                // ClearDmaOnAbort(pcie_interface, &u64Data, kDev1);
                // pcie_interface->ReadReg64(dev_num, hw_consts::cs_bar, hw_consts::t1_cs_reg, u64Data);
                // LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (*u64Data >> 32), (*u64Data & 0xffff));

                u64Data = 0;
                pcie_interface->ReadReg64(kDev1, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                LOG_INFO(logger_, " Status word for channel 2 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));

                /* write this will abort previous DMA */
                pcie_interface->WriteReg32(kDev1, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, hw_consts::dma_abort);

                /* clear DMA register after the abort */
                pcie_interface->WriteReg32(kDev1, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, 0);

                // If DMA did not finish, abort loop!
                break;
            }
            // sync DMA I/O cache
            pcie_interface->DmaSyncIo(dma_num);

            if (idebug) {
                u64Data = 0;
                pcie_interface->ReadReg64(kDev1, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                LOG_INFO(logger_, " Status word for channel 2 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));
            }
            std::memcpy(word_arr.data(), buffp_rec32, 8);
            std::cout << std::hex;
            if (dma_loops % 50 == 0) for (const auto &el : word_arr ) std::cout << el << "  \n";
            std::cout << std::dec;
            trigger_queue_.write(word_arr);
            if (trigger_queue_.isFull()) {
                num_buffer_full++;
                LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
            }
            dma_loops++;
        } // end loop over events

        LOG_INFO(logger_, "Stopping triggers..\n");
        trig_ctrl::TriggerControl::SendStopTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);

        // If event count triggered the end of run, set stop write flag so write thread completes
        // LOG_INFO(logger_, "Stopping run and freeing pointer \n");
        // stop_write_.store(true);

        if (num_buffer_full > 0) LOG_WARNING(logger_, "Buffer full: [{}]\n", num_buffer_full);

        LOG_INFO(logger_, "Flushing trigger data buffer \n");
        while (!trigger_queue_.isEmpty()) trigger_queue_.popFront();

     }

    void DataHandler::ClearDmaOnAbort(pcie_int::PCIeInterface *pcie_interface, unsigned long long *u64Data, uint32_t dev_num) {
        pcie_interface->ReadReg64(dev_num, hw_consts::cs_bar, hw_consts::t1_cs_reg, u64Data);
        LOG_INFO(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X} \n", (*u64Data >> 32), (*u64Data & 0xffff));

        *u64Data = 0;
        pcie_interface->ReadReg64(dev_num, hw_consts::cs_bar, hw_consts::t2_cs_reg, u64Data);
        LOG_INFO(logger_, " Status word for channel 2 after read = 0x{:X}, 0x{:X} \n", (*u64Data >> 32), (*u64Data & 0xffff));

        /* write this will abort previous DMA */
        pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, hw_consts::dma_abort);

        /* clear DMA register after the abort */
        pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, 0);
    }

    bool DataHandler::WaitForDma(pcie_int::PCIeInterface *pcie_interface, uint32_t *data, uint32_t dev_num) {
        // Check to see if DMA is done or not
        // Can get stuck waiting for the DMA to finish, use is_running flag check to break from it
        for (size_t is = 0; is < 6000000000; is++) {
            pcie_interface->ReadReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
            if ((*data & hw_consts::dma_in_progress) == 0) {
                return true;
            }
            if ((is > 0) && ((is % 10000000) == 0)) std::cout << "Wait iter: " << is << "\n";
            if (!is_running_.load()) break;
        }
        return false;
    }

    std::vector<uint32_t> DataHandler::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool DataHandler::Reset(pcie_int::PCIeInterface *pcie_interface) {

        // Reset some counting variables
        file_count_ = 0;
        event_count_.store(0);
        num_dma_loops_ = 0;
        num_recv_bytes_ = 0;

        // Reset the stop write flag so we can restart
        stop_write_.store(false);

        // Double check the buffer is emptied
        while (!data_queue_.isEmpty()) data_queue_.popFront();

        // Reset the metrics
        metrics_->ResetMetrics();

        return true;
    }
} // data_handler
