//
// Created by Jon Sensenig on 2/24/25.
//

#include "data_handler.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <chrono>

#include "quill/LogMacros.h"

namespace data_handler {

    // FIXME, make queue size configurable
    DataHandler::DataHandler(bool enable_metrics) : num_events_(0), data_queue_(100),
    stop_write_(false), metrics_(data_monitor::DataMonitor(enable_metrics)) {
        logger_ = quill::Frontend::create_or_get_logger("root",
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));
    }

    // DataHandler::~DataHandler() {
    //     LOG_INFO(logger_, "Destructing data handler.. \n");
    // }

    bool DataHandler::SetRecvBuffer(pcie_int::PCIeInterface *pcie_interface,
        pcie_int::DMABufferHandle *pbuf_rec1, pcie_int::DMABufferHandle *pbuf_rec2) {

        static uint32_t data_32_;

        // first dma
        LOG_INFO(logger_, "\n First DMA .. \n");
        LOG_INFO(logger_, "Buffer 1 & 2 allocation size: {} \n", dma_buf_size_);

        pcie_interface->DmaContigBufferLock(1, dma_buf_size_, pbuf_rec1);
        pcie_interface->DmaContigBufferLock(2, dma_buf_size_, pbuf_rec2);

        /* set tx mode register */
        data_32_ = 0x00002000;
        pcie_interface->WriteReg32(pcie_int::PCIeInterface::kDev2, hw_consts::cs_bar, hw_consts::tx_md_reg, data_32_);

        /* write this will abort previous DMA */
        data_32_ = hw_consts::dma_abort;
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data_32_);

        /* clear DMA register after the abort */
        data_32_ = 0;
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data_32_);

        return true;
    }

    bool DataHandler::Configure(json &config) {

        trigger_module_ = config["crate"]["trig_slot"].get<int>();
        dma_buf_size_ = config["data_handler"]["dma_buffer_size"].get<int>();
        num_events_ = config["data_handler"]["num_events"].get<size_t>();
        metrics_.DmaSize(dma_buf_size_);
        const size_t subrun = config["data_handler"]["subrun"].get<size_t>();
        std::string trig_src = config["trigger"]["trigger_source"].get<std::string>();
        ext_trig_ = trig_src == "ext" ? 1 : 0;
        software_trig_ = trig_src == "software" ? 1 : 0;
        LOG_INFO(logger_, "Trigger source software [{}] external [{}] \n", software_trig_, ext_trig_);

        LOG_INFO(logger_, "\t [{}] DMA loops with [{}] 32b words \n", num_dma_loops_, dma_buf_size_ / 4);

        file_count_ = 0;
        write_file_name_ = "/data/readout_data/pGRAMS_bin_" + std::to_string(subrun) + "_";
        std::string name = write_file_name_  + std::to_string(file_count_) + ".dat";
        file_ptr_ = fopen(name.c_str(), "wb");
        if (!file_ptr_) {
            LOG_ERROR(logger_, "Failed to open file {} aborting run! \n", name);
            return false;
        }

        LOG_INFO(logger_, "\n Output file: {}", name);

        LOG_INFO(logger_, "Collected {} events... \n", event_count_);
        // LOG_INFO(logger_, "Closed file after writing {}B to file {} \n", num_recv_bytes_, name);

        return true;
    }

    bool DataHandler::SwitchWriteFile() {

        fflush(file_ptr_);
        if(fclose(file_ptr_) == EOF) {
            LOG_ERROR(logger_, "Failed to close data file... \n");
            return false;
        }

        file_count_ += 1;
        std::string name = write_file_name_  + std::to_string(file_count_) + ".dat";
        LOG_INFO(logger_, "Switching to file: {} \n", name);

        file_ptr_ = fopen(name.c_str(), "wb");
        if (!file_ptr_) {
            LOG_ERROR(logger_, "Failed to open file {} aborting run! \n", name);
            return false;
        }
        metrics_.NumFiles(file_count_);
        return true;
    }

    void DataHandler::CollectData(pcie_int::PCIeInterface *pcie_interface) {

        auto write_thread = std::thread(&DataHandler::DataWrite, this);
        auto read_thread = std::thread(&DataHandler::DMARead, this, pcie_interface);
        LOG_INFO(logger_, "Started read and write threads... \n");

        // Shut down the write thread first so we're not trying to access buffer ptrs
        // after they have been deleted in the read thread
        write_thread.join();
        LOG_INFO(logger_, "write thread joined... \n");

        read_thread.join();
        LOG_INFO(logger_, "read thread joined... \n");

    }

    void DataHandler::DataWrite() {
        LOG_INFO(logger_, "Read thread start! sizeof(uint32_t)={} \n", sizeof(uint32_t));

        static uint32_t word;
        std::array<uint32_t, 300000> word_arr{};
        std::array<uint32_t, 500000> word_arr_write{};
        size_t prev_event_count = 0;
        event_start_ = false;
        event_end_ = false;
        size_t num_words = 0;
        event_count_ = 0;
        event_start_count_ = 0;
        event_end_count_ = 0;
        num_recv_bytes_ = 0;
        auto start = std::chrono::high_resolution_clock::now();

        while (!stop_write_.load()) {
            while (data_queue_.read(word_arr)) {
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start);
                if (elapsed.count() >= 4) {
                    metrics_.EventDiff(event_count_ - prev_event_count);
                    prev_event_count = event_count_;
                    start = now;
                    metrics_.LoadMetrics();
                }
                for (size_t i = 0; i < (dma_buf_size_/sizeof(uint32_t)); i++) {
                    word = word_arr[i];
                    if (num_words > word_arr_write.size()) {
                        LOG_WARNING(logger_, "Unexpectedly large event, dropping it! num_words=[{}] > buffer size=[{}] \n",
                                    num_words, dma_buf_size_/2);
                        num_words = 0;
                        event_start_ = false;
                    }
                    if (isEventStart(word)) {
                        if (event_start_) num_words = 0; // Previous evt didn't finish, drop partial evt data
                        event_start_ = true; event_start_count_++;
                    }
                    else if (isEventEnd(word) && event_start_) {
                        event_end_ = true; event_end_count_++;
                    }
                    word_arr_write[num_words] = word;
                    num_words++;
                    num_recv_bytes_ += 4;
                    if (event_start_ & event_end_) {
                        if ((event_count_ % 500) == 0) LOG_INFO(logger_, " **** Event: {}", event_count_);
                        event_count_++;
                        fwrite(word_arr_write.data(), 1, (num_words*4), file_ptr_);
                        if ((event_count_ > 0) && (event_count_ % 1000 == 0)) {
                            SwitchWriteFile();
                        }
                        metrics_.EventSize(num_words);
                        metrics_.BytesReceived(num_recv_bytes_);
                        metrics_.NumEvents(event_count_);
                        event_start_ = false; event_end_ = false; num_words = 0;
                    }
                } // word loop
            } // read buffer loop
        } // run loop

        // Flush the buffer to make sure all data is written to file
        fflush(file_ptr_);
        if(fclose(file_ptr_) == EOF) {
            LOG_ERROR(logger_, "Failed to close data file... \n");
        }

        LOG_INFO(logger_, "Closed file after writing {}B to file {} \n", num_recv_bytes_, write_file_name_);
        LOG_INFO(logger_, "Wrote {} events to {} files", event_count_, file_count_);
        LOG_INFO(logger_, "Counted [{}] start events & [{}] end events \n", event_start_count_, event_end_count_);
    }

    uint32_t DataHandler::DmaLoop(pcie_int::PCIeInterface *pcie_interface, uint32_t dma_num, size_t loop, bool is_first_loop) {
        bool idebug = false;
        uint32_t data;
        static unsigned long long u64Data;

        /* sync CPU cache */
        pcie_interface->DmaSyncCpu(dma_num);

        uint32_t nwrite_byte = dma_buf_size_;
        if (idebug) LOG_INFO(logger_, "DMA loop {} with DMA length {}B \n", loop, nwrite_byte);

        /** initialize and start the receivers ***/
        for (size_t rcvr = 1; rcvr < 3; rcvr++) {
            static uint32_t r_cs_reg = rcvr == 1 ? hw_consts::r1_cs_reg : hw_consts::r2_cs_reg;
            if (is_first_loop) {
                pcie_interface->WriteReg32(kDev2,  hw_consts::cs_bar, r_cs_reg, hw_consts::cs_init);
            }
            data = hw_consts::cs_start + nwrite_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
            pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, r_cs_reg, data);
        }

        /** set up DMA for both transceiver together **/
        data = pcie_interface->GetBufferPageAddrLower(dma_num);
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_low_reg, data);

        data = pcie_interface->GetBufferPageAddrUpper(dma_num);
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_high_reg, data);

        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, nwrite_byte);

        /* write this will start DMA */
        static uint32_t is = pcie_interface->GetBufferPageAddrUpper(dma_num);
        data = is == 0 ? hw_consts::dma_tr12 + hw_consts::dma_3dw_rec : hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;

        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
        if (idebug) LOG_INFO(logger_, "DMA set up done, byte count = {} \n", nwrite_byte);

        // send trigger
        if (loop == 0) trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);

        u64Data = 0;
        if (!WaitForDma(pcie_interface, &data)) {
            LOG_WARNING(logger_, " loop [{}] DMA is not finished, aborting...  \n", loop);
            pcie_interface->ReadReg64(kDev2,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
            pcie_interface->DmaSyncIo(dma_num);
            static size_t num_read = (nwrite_byte - (u64Data & 0xffff)) / sizeof(uint32_t);

            LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);

            // std::memcpy(word_arr.data(), buffp_rec32, num_read);
            // data_queue_.write(word_arr);
            // if (data_queue_.isFull()) {
            //     num_buffer_full++;
            //     LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
            // }
            // // If DMA did not finish, clear DMA and abort loop!
            // ClearDmaOnAbort(pcie_interface);
            // break;
            return num_read;
        }

        /* synch DMA i/O cache **/
        pcie_interface->DmaSyncIo(dma_num);

        if (idebug) {
            u64Data = 0;
            pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
            LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

            u64Data = 0;
            pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
            LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));
        }
        return dma_buf_size_ / sizeof(uint32_t);

    }

    void DataHandler::DMARead(pcie_int::PCIeInterface *pcie_interface) {

        metrics_.IsRunning(is_running_);
        bool idebug = false;
        bool is_first_event = true;
        static uint32_t iv, r_cs_reg;
        static uint32_t nwrite_byte;
        static uint32_t is;
        static int idone;
        static int itrig_c = 0;
        static int itrig_ext = 1;

        uint32_t data;
        static unsigned long long u64Data;
        static uint32_t *buffp_rec32;
        static std::array<uint32_t, 300000> word_arr{};
        size_t dma_loops = 0;
        static size_t nwrite = 0;
        size_t num_buffer_full = 0;

        pcie_int::DMABufferHandle  pbuf_rec1;
        pcie_int::DMABufferHandle pbuf_rec2;

         /*TPC DMA*/
        SetRecvBuffer(pcie_interface, &pbuf_rec1, &pbuf_rec2);
        usleep(500000);

        while(is_running_.load() && dma_loops < num_events_) {
            if (dma_loops % 500 == 0) LOG_INFO(logger_, "=======> DMA Loop [{}] \n", dma_loops);
            for (iv = 0; iv < 2; iv++) { // note: ndma_loop=1
                static uint32_t dma_num = (iv % 2) == 0 ? 1 : 2;
                buffp_rec32 = dma_num == 1 ? static_cast<uint32_t *>(pbuf_rec1) : static_cast<uint32_t *>(pbuf_rec2);
// FIXME Previous /////
                /* sync CPU cache */
                pcie_interface->DmaSyncCpu(dma_num);

                nwrite_byte = dma_buf_size_;
                if (idebug) LOG_INFO(logger_, "DMA loop {} with DMA length {}B \n", iv, nwrite_byte);

                /** initialize and start the receivers ***/
                for (size_t rcvr = 1; rcvr < 3; rcvr++) {
                    r_cs_reg = rcvr == 1 ? hw_consts::r1_cs_reg : hw_consts::r2_cs_reg;
                    if (is_first_event) {
                        pcie_interface->WriteReg32(kDev2,  hw_consts::cs_bar, r_cs_reg, hw_consts::cs_init);
                    }
                    data = hw_consts::cs_start + nwrite_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
                    pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, r_cs_reg, data);
                }

                is_first_event = false;

                /** set up DMA for both transceiver together **/
                data = pcie_interface->GetBufferPageAddrLower(dma_num);
                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_low_reg, data);

                data = pcie_interface->GetBufferPageAddrUpper(dma_num);
                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_high_reg, data);

                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, nwrite_byte);

                /* write this will start DMA */
                is = pcie_interface->GetBufferPageAddrUpper(dma_num);
                data = is == 0 ? hw_consts::dma_tr12 + hw_consts::dma_3dw_rec : hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;

                pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
                if (idebug) LOG_INFO(logger_, "DMA set up done, byte count = {} \n", nwrite_byte);

                // send trigger
                if (iv == 0) trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, itrig_c, itrig_ext, 11);

                // extra wait just to be certain --- WK
                // usleep(200);
                /***    check to see if DMA is done or not **/
                // Can get stuck waiting for the DMA to finish, use is_running flag check to break from it
                idone = 0;
                for (is = 0; is < 6000000000; is++) {
                    u64Data = 0;
                    pcie_interface->ReadReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, &data);
                    if ((data & hw_consts::dma_in_progress) == 0) {
                        idone = 1;
                        break;
                        if (idebug) LOG_INFO(logger_, "Receive DMA complete...  (iter={}) \n", is);
                    }
                    if ((is > 0) && ((is % 10000000) == 0)) std::cout << "Wait iter: " << is << "\n";
                    if (!is_running_.load()) break;
                }
                if (idone == 0) {
                    LOG_WARNING(logger_, " loop [{}] DMA is not finished, aborting...  \n", iv);
                    pcie_interface->ReadReg64(kDev2,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
                    pcie_interface->DmaSyncIo(dma_num);
                    static size_t num_read = (nwrite_byte - (u64Data & 0xffff));

                    LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);

                    std::memcpy(word_arr.data(), buffp_rec32, num_read);
                    data_queue_.write(word_arr);
                    if (data_queue_.isFull()) {
                        num_buffer_full++;
                        LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                    }

                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 2 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

                    /* write this will abort previous DMA */
                    data = hw_consts::dma_abort;
                    pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data);

                    /* clear DMA register after the abort */
                    data = 0;
                    pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, data);

                    // If DMA did not finish, abort loop!
                    break;
                }

                /* synch DMA i/O cache **/
                pcie_interface->DmaSyncIo(dma_num);

                if (idebug) {
                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                    LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));
                }
// FIXME Previous to here
//// FIXME Comment from here
                // /* sync CPU cache */
                // pcie_interface->DmaSyncCpu(dma_num);
                //
                // nwrite_byte = dma_buf_size_;
                // if (idebug) LOG_INFO(logger_, "DMA loop {} with DMA length {}B \n", iv, nwrite_byte);
                //
                // /** initialize and start the receivers ***/
                // for (size_t rcvr = 1; rcvr < 3; rcvr++) {
                //     r_cs_reg = rcvr == 1 ? hw_consts::r1_cs_reg : hw_consts::r2_cs_reg;
                //     if (is_first_event) {
                //         pcie_interface->WriteReg32(kDev2,  hw_consts::cs_bar, r_cs_reg, hw_consts::cs_init);
                //     }
                //     data = hw_consts::cs_start + nwrite_byte; /* 32 bits mode == 4 bytes per word *2 fibers **/
                //     pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, r_cs_reg, data);
                // }
                //
                // is_first_event = false;
                //
                // /** set up DMA for both transceiver together **/
                // data = pcie_interface->GetBufferPageAddrLower(dma_num);
                // pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_low_reg, data);
                //
                // data = pcie_interface->GetBufferPageAddrUpper(dma_num);
                // pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_add_high_reg, data);
                //
                // pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, nwrite_byte);
                //
                // /* write this will start DMA */
                // is = pcie_interface->GetBufferPageAddrUpper(dma_num);
                // data = is == 0 ? hw_consts::dma_tr12 + hw_consts::dma_3dw_rec : hw_consts::dma_tr12 + hw_consts::dma_4dw_rec;
                //
                // pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
                // if (idebug) LOG_INFO(logger_, "DMA set up done, byte count = {} \n", nwrite_byte);
                //
                // // send trigger
                // if (iv == 0) trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);
                //
                // u64Data = 0;
                // if (!WaitForDma(pcie_interface, &data)) {
                //     LOG_WARNING(logger_, " loop [{}] DMA is not finished, aborting...  \n", iv);
                //     pcie_interface->ReadReg64(kDev2,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
                //     pcie_interface->DmaSyncIo(dma_num);
                //     static size_t num_read = (nwrite_byte - (u64Data & 0xffff)) / 4;
                //
                //     LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);
                //
                //     std::memcpy(word_arr.data(), buffp_rec32, num_read);
                //     data_queue_.write(word_arr);
                //     if (data_queue_.isFull()) {
                //         num_buffer_full++;
                //         LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                //     }
                //     // If DMA did not finish, clear DMA and abort loop!
                //     ClearDmaOnAbort(pcie_interface);
                //     break;
                // }
                //
                // /* synch DMA i/O cache **/
                // pcie_interface->DmaSyncIo(dma_num);
                //
                // if (idebug) {
                //     u64Data = 0;
                //     pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
                //     LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));
                //
                //     u64Data = 0;
                //     pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                //     LOG_INFO(logger_, " Status word for channel 2 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));
                // }
//// FIXME Comment to here
                // FIXME /////
                // size_t num_read = DmaLoop(pcie_interface, dma_num, iv, is_first_event);
                // is_first_event = false;
                // FIXME ///
                // nwrite = nwrite_byte / 4;//sizeof(uint32_t);

                std::memcpy(word_arr.data(), buffp_rec32, nwrite_byte);
                data_queue_.write(word_arr);
                if (data_queue_.isFull()) {
                    num_buffer_full++;
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }
                // FIXME /////
                // if (num_read != (dma_buf_size_ / sizeof(uint32_t))) {
                //     LOG_WARNING(logger_, "Did not read full DMA buffer! \n");
                //     ClearDmaOnAbort(pcie_interface);
                //     break;
                // }
                // FIXME /////
            } // end dma loop
            dma_loops += 1;
            metrics_.DmaLoops(dma_loops);
        } // end loop over events

        LOG_INFO(logger_, "Stopping triggers..\n");
        trig_ctrl::TriggerControl::SendStopTrigger(pcie_interface, software_trig_, ext_trig_, 11);

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

    void DataHandler::ClearDmaOnAbort(pcie_int::PCIeInterface *pcie_interface) {
        static unsigned long long u64Data = 0;
        pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t1_cs_reg, &u64Data);
        LOG_INFO(logger_, " Status word for channel 1 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

        u64Data = 0;
        pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
        LOG_INFO(logger_, " Status word for channel 2 after read = {}, {}", (u64Data >> 32), (u64Data & 0xffff));

        /* write this will abort previous DMA */
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, hw_consts::dma_abort);

        /* clear DMA register after the abort */
        pcie_interface->WriteReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, 0);
    }

    bool DataHandler::WaitForDma(pcie_int::PCIeInterface *pcie_interface, uint32_t *data) {
        /***    check to see if DMA is done or not **/
        // Can get stuck waiting for the DMA to finish, use is_running flag check to break from it
        for (size_t is = 0; is < 6000000000; is++) {
            pcie_interface->ReadReg32(kDev2, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
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

    bool DataHandler::CloseDevice() {
        int i = 5;
        int j = 6;
        return i > j;
    }
} // data_handler
