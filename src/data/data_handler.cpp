//
// Created by Jon Sensenig on 2/24/25.
//

#include "data_handler.h"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <pthread.h>
#include <sched.h>

#include "quill/LogMacros.h"

namespace data_handler {

    // FIXME, make queue size configurable
    DataHandler::DataHandler() :
        data_queue_(400),
        trigger_queue_(100),
        num_events_(0),
        num_dma_loops_(1),
        num_recv_bytes_(0),
        trig_data_ctr_(0),
        read_write_buff_overflow_(false),
        file_count_(0),
        stop_write_(false) {
        logger_ = quill::Frontend::create_or_get_logger("readout_logger");
    }

    DataHandler::~DataHandler() {
        std::cout << "Running DataHandler Destructor" << std::endl;
    }

    std::map<std::string, size_t> DataHandler::GetMetrics() {
        std::map<std::string, size_t> metrics;
        metrics["run_number"] = run_number_;
        metrics["num_events"] = event_count_.load();
        metrics["num_files"] = file_count_.load();
        metrics["num_dma_loops"] = dma_loop_count_.load();
        metrics["mega_bytes_received"] = num_recv_mB_.load();
        // metrics["event_diff"] = event_count_.load() - prev_event_count_; //redundant
        metrics["event_size_words"] = num_event_chunk_words_.load();
        metrics["num_rw_buffer_overflow"] = num_rw_buffer_overflow_.load();
        metrics["event_start_markers"] = event_start_markers_.load();
        metrics["event_end_markers"] = event_end_markers_.load();

        // Since we poll the metrics every dt we can find the average rate
        prev_event_count_ = event_count_.load();
        prev_dma_loop_count_ = dma_loop_count_.load();

        return metrics;
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

    void DataHandler::PinThread(std::thread& t, size_t core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        pthread_t handle = t.native_handle();
        // Set the thread to run on a specific core, the priority for the thread is set below
        int rc = pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            LOG_ERROR(logger_, "pthread_setaffinity_np failed for core {}\n", core_id);
        }

        // Be VERY careful with this number, range=(0,99) with 99 the highest priority.
        // Setting too high will make the CPU core unusable for other processes.
        int priority = 70;
        sched_param sch_params{};
        sch_params.sched_priority = priority;

        // This requires root permission but opening the PCIe card also does so we can assume
        // we are always root if we get to this function.
        int ret = pthread_setschedparam(handle, SCHED_FIFO, &sch_params);
        if (ret != 0) {
            LOG_ERROR(logger_, "Failed to set realtime priority: {} ", ret);
        }
    }

    uint32_t DataHandler::Configure(json &config) {
        try {
            trigger_module_ = config["crate"]["trig_slot"].get<int>();
            software_trigger_rate_ = config["trigger"]["software_trigger_rate_hz"].get<int>();
            num_events_ = config["data_handler"]["num_events"].get<size_t>();
            // const size_t subrun = config["data_handler"]["subrun"].get<size_t>();
            run_number_ = config["data_handler"]["subrun"].get<size_t>();
            std::string trig_src = config["trigger"]["trigger_source"].get<std::string>();
            ext_trig_ = trig_src == "external" || "light" ? 1 : 0;
            software_trig_ = trig_src == "software" ? 1 : 0;
            data_basedir_ = config["data_handler"]["data_basedir"].get<std::string>();
            file_count_.store(0);
            write_file_name_ = data_basedir_ + "/readout_data/pGRAMS_bin_" + std::to_string(run_number_) + "_";
            pps_sample_period_ = config["data_handler"]["pps_sample_period"].get<int>();
            read_core_id_ = config["data_handler"]["read_core_id"].get<size_t>();
            write_core_id_ = config["data_handler"]["write_core_id"].get<size_t>();
            LOG_INFO(logger_, "Trigger source software [{}] external [{}] \n", software_trig_, ext_trig_);
            LOG_DEBUG(logger_, "\t [{}] DMA loops with [{}] 32b words \n", num_dma_loops_, DATABUFFSIZE / 4);
            LOG_INFO(logger_, "\n Writing files: {}", write_file_name_);
        } catch (std::exception &e) {
            LOG_ERROR(logger_, "Exception while getting DataHandler config, with error {} \n", e.what());
            return TpcReadoutMonitor::ErrorBits::datahandler_get_config;
        }
        return 0x0;
    }

    bool DataHandler::SwitchWriteFile() {

        // Make sure we copy `fd_` so when we re-assign it later it doesn't affect the thread.
        // Start a thread and detach it so the file closes in the background at its leisure
        // while we continue to write data to the newly opened file.
        int fd_copy = fd_;
        std::thread close_thread([&fd_copy, this]() {
            LOG_DEBUG(logger_, "Closing data file {} \n", fd_copy);
            if(close(fd_copy) == -1) {
                LOG_ERROR(logger_, "Failed to close data file, with error:{} \n", std::string(strerror(errno)));
            }
        });
        close_thread.detach();

        file_count_ += 1;
        std::string name = write_file_name_  + std::to_string(file_count_.load()) + ".dat";

        fd_ = open(name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ == -1) {
            LOG_ERROR(logger_, "Failed to open file {} with error {} aborting run! \n", name, std::string(strerror(errno)));
            return false;
        }

        LOG_INFO(logger_, "Switched to file: {} fd={}\n", name, fd_);
        return true;
    }

    void DataHandler::CollectData(pcie_int::PCIeInterface *pcie_interface) {

        auto write_thread = std::thread(&DataHandler::DataWrite, this);
        //auto write_thread = std::thread(&DataHandler::FastDataWrite, this);
        auto read_thread = std::thread(&DataHandler::ReadoutDMARead, this, pcie_interface);

        // Pin these threads to the specified core
        // PinThread(read_thread, read_core_id_);
        // PinThread(write_thread, write_core_id_);

        // auto read_thread = std::thread(&DataHandler::ReadoutViaController, this, pcie_interface, buffers);
        auto trig_pps = std::thread(&DataHandler::PollTriggerPPS, this, pcie_interface);
        auto trigger_thread = std::thread(&DataHandler::TriggerDMARead, this, pcie_interface);

        // auto trigger_thread = std::thread(&trig_ctrl::TriggerControl::SendSoftwareTrigger, &trigger_,
        //     pcie_interface, software_trigger_rate_, trigger_module_);

        // FIXME Combines both Data and Trigger DMA readout, works for a little then hangs
        // auto read_thread = std::thread(&DataHandler::TestReadoutDMARead, this, pcie_interface);
        // FIXME Trigger DMA readout, causes both the trigger and data DMA to hang even though they're in
        // separate threads with separate DMAs
        //auto trigger_thread = std::thread(&DataHandler::TriggerDMARead, this, pcie_interface);
        LOG_INFO(logger_, "Started read and write threads... \n");

        // Shut down PPS polling thread
        trig_pps.join();

        // Shut down the write thread first so we're not trying to access buffer ptrs
        // after they have been deleted in the read thread
        write_thread.join();
        LOG_DEBUG(logger_, "write thread joined... \n");

        read_thread.join();
        LOG_DEBUG(logger_, "read thread joined... \n");

        // The read/write threads will block until a run stop is set. Then we stop the trigger.
        trigger_thread.join();
        LOG_DEBUG(logger_, "trigger thread joined... \n");
        LOG_INFO(logger_, "Read, Write and Trigger threads joined... \n");
    }

    void DataHandler::FastDataWrite() {
        LOG_INFO(logger_, "Fast Read thread start! \n");

        std::string name = write_file_name_  + std::to_string(file_count_.load()) + ".dat";
        // 0644 user, group and others read/write permissions
        fd_ = open(name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ == -1) {
            LOG_ERROR(logger_, "Failed to open file {} with error {} aborting run! \n", name, std::string(strerror(errno)));
        }

        std::array<uint32_t, DATABUFFSIZE> word_arr{};
        while (!stop_write_.load()) {
            while (data_queue_.read(word_arr)) {
                write(fd_, word_arr.data(), word_arr.size()*sizeof(uint32_t));
            } // read buffer loop
        } // run loop

        // Write any remaining full events in the buffer to file before closing
        while (!data_queue_.isEmpty()) {
            data_queue_.read(word_arr);
            write(fd_, word_arr.data(), word_arr.size()*sizeof(uint32_t));
        }

        LOG_INFO(logger_, "Ended data write and closing file..\n");
        // Make sure all data is flushed to file before closing
        if(fsync(fd_) == -1) {
            LOG_ERROR(logger_, "Failed to sync data file with error: {} \n", std::string(strerror(errno)));
        }
        if(close(fd_) == -1) {
            LOG_ERROR(logger_, "Failed to close data file with error: {} \n", std::string(strerror(errno)));
        }
    }

    void DataHandler::DataWrite() {
        LOG_INFO(logger_, "Read thread start! \n");

        std::string name = write_file_name_  + std::to_string(file_count_.load()) + ".dat";
        // 0644 user, group and others read/write permissions
        fd_ = open(name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ == -1) {
            LOG_ERROR(logger_, "Failed to open file {} with error {} aborting run! \n", name, std::string(strerror(errno)));
        }

        uint32_t word;
        std::array<uint32_t, DATABUFFSIZE> word_arr{};
        std::array<uint32_t, EVENTBUFFSIZE> word_arr_write{}; // event buffer, ~0.231MB/event
        bool event_start = false;
        size_t num_words = 0;
        size_t event_words = 0;
        size_t event_chunk = 0;
        size_t event_start_count = 0;
        size_t event_end_count = 0;
        size_t num_recv_bytes = 0;
        size_t local_event_count = 0;
        event_count_.store(0);
        event_start_markers_.store(0);
        event_end_markers_.store(0);

        while (!stop_write_.load()) {
            while (data_queue_.read(word_arr)) {
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
                        event_start_markers_++;
                    }
                    else if (isEventEnd(word) && event_start) {
                        event_end_count++; event_start = false;
                        event_end_markers_++;
                        event_chunk++;
                        local_event_count++;
                        event_count_.store(local_event_count);
                        event_words = num_words + 1; // add one for event end word
                    }
                    word_arr_write[num_words] = word;
                    num_words++;
                    // num_recv_bytes += 4;
                    if (event_chunk == EVENTCHUNK) {
                        if ((local_event_count % 500) == 0) LOG_INFO(logger_, " **** Event: {} \n", local_event_count);
                        const int write_bytes = write(fd_, word_arr_write.data(), num_words*sizeof(uint32_t));
                        if (write_bytes == -1) LOG_WARNING(logger_, "Failed write {} \n", std::string(strerror(errno)));
                        else num_recv_bytes += static_cast<size_t>(write_bytes);

                        if ((local_event_count > 0) && (local_event_count % 5000 == 0)) {
                            SwitchWriteFile();
                        }
                        num_recv_mB_.store(num_recv_bytes / 1000000);
                        num_event_chunk_words_.store(num_words / EVENTCHUNK);
                        event_start = false; num_words = 0; event_words = 0; event_chunk = 0;
                    }
                } // word loop
            } // read buffer loop
        } // run loop

        // Write any remaining full events in the buffer to file before closing
        const int write_bytes = write(fd_, word_arr_write.data(), event_words*sizeof(uint32_t));
        if (write_bytes == -1) LOG_WARNING(logger_, "Failed write {} \n", std::string(strerror(errno)));
        else num_recv_bytes += static_cast<size_t>(write_bytes);

        LOG_INFO(logger_, "Ended data write and closing file..\n");
        // Make sure all data is flushed to file before closing
        if(fsync(fd_) == -1) {
            LOG_ERROR(logger_, "Failed to sync data file with error: {} \n", std::string(strerror(errno)));
        }
        if(close(fd_) == -1) {
            LOG_ERROR(logger_, "Failed to close data file with error: {} \n", std::string(strerror(errno)));
        }

        LOG_INFO(logger_, "Closed file after writing {}B to file {} \n", num_recv_bytes, write_file_name_);
        LOG_INFO(logger_, "Wrote {} events to {} files \n", event_count_.load(), file_count_.load());
        LOG_INFO(logger_, "Counted [{}] start events & [{}] end events \n", event_start_count, event_end_count);
    }

    void DataHandler::ReadoutDMARead(pcie_int::PCIeInterface *pcie_interface) {

        bool idebug = false;
        bool is_first_event = true;
        static uint32_t iv, r_cs_reg;
        static uint32_t num_dma_byte = DMABUFFSIZE;
        static uint32_t is;

        uint32_t data;
        static unsigned long long u64Data;
        static uint32_t *buffp_rec32;
        static std::array<uint32_t, DATABUFFSIZE> word_arr{};

        pcie_int::DMABufferHandle  pbuf_rec1;
        pcie_int::DMABufferHandle pbuf_rec2;

        // Init the metric counters
        dma_loop_count_.store(0);

         /*TPC DMA*/
        LOG_DEBUG(logger_, "Buffer 1 & 2 allocation size: {} \n", DMABUFFSIZE);
        SetRecvBuffer(pcie_interface, &pbuf_rec1, &pbuf_rec2, true);
        usleep(500000);

        std::thread trigger_thread;
        if (software_trig_ == 1) {
            LOG_INFO(logger_, "Starting software trigger thread...\n");
            trigger_thread = std::thread(&trig_ctrl::TriggerControl::SendSoftwareTrigger, &trigger_,
                                            pcie_interface, software_trigger_rate_, trigger_module_);
        }

        while(is_running_.load() && event_count_.load() < num_events_) {
            if (dma_loop_count_.load() % 500 == 0) LOG_INFO(logger_, "=======> DMA Loop [{}] \n", dma_loop_count_.load());
            for (iv = 0; iv < 2; iv++) { // note: ndma_loop=1
                static uint32_t dma_num = (iv % 2) == 0 ? 1 : 2;
                buffp_rec32 = dma_num == 1 ? static_cast<uint32_t *>(pbuf_rec1) : static_cast<uint32_t *>(pbuf_rec2);
                // sync CPU cache
                pcie_interface->DmaSyncCpu(dma_num);

                if (idebug) LOG_DEBUG(logger_, "DMA loop {} with DMA length {}B \n", iv, DMABUFFSIZE);

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
                if (idebug) LOG_DEBUG(logger_, "DMA set up done, byte count = {} \n", DMABUFFSIZE);

                // send trigger
                if (iv == 0) {
                    trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);
                } else if (software_trig_) {
                    trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);
                }

                if (!WaitForDma(pcie_interface, &data, kDev2)) {
                    LOG_WARNING(logger_, " loop [{}] DMA is not finished, aborting...  \n", iv);
                    pcie_interface->ReadReg64(kDev2,  hw_consts::cs_bar, hw_consts::cs_dma_by_cnt, &u64Data);
                    pcie_interface->DmaSyncIo(dma_num);
                    static size_t num_read = (num_dma_byte - (u64Data & 0xffff));

                    LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);

                    std::memcpy(word_arr.data(), buffp_rec32, num_read);
                    data_queue_.write(word_arr);
                    if (data_queue_.isFull()) {
                        read_write_buff_overflow_.store(true);
                        num_rw_buffer_overflow_++;
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
                    LOG_DEBUG(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));

                    u64Data = 0;
                    pcie_interface->ReadReg64(kDev2, hw_consts::cs_bar, hw_consts::t2_cs_reg, &u64Data);
                    LOG_DEBUG(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X}", (u64Data >> 32), (u64Data & 0xffff));
                }
                std::memcpy(word_arr.data(), buffp_rec32, DMABUFFSIZE);
                data_queue_.write(word_arr);
                if (data_queue_.isFull()) {
                    read_write_buff_overflow_.store(true);
                    num_rw_buffer_overflow_++;
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }
            } // end dma loop
            dma_loop_count_++;
        } // end loop over events

        LOG_DEBUG(logger_, "Stopping triggers..\n");
        if (software_trig_) {
            trigger_.StopTrigger(true);
            if (trigger_thread.joinable()) trigger_thread.join();
        }
        trig_ctrl::TriggerControl::SendStopTrigger(pcie_interface, software_trig_, ext_trig_, trigger_module_);

        // If event count triggered the end of run, set stop write flag so write thread completes
        LOG_DEBUG(logger_, "Stopping run and freeing pointer \n");
        stop_write_.store(true);

        if (num_rw_buffer_overflow_.load() > 0) LOG_WARNING(logger_, "Buffer full: [{}]\n", num_rw_buffer_overflow_.load());

        // Since the two PCIe buffer handles are scoped to this function, free the buffer before
        // they go out of scope
        LOG_DEBUG(logger_, "Freeing DMA buffers and closing file..\n");
        if (!pcie_interface->FreeDmaContigBuffers()) {
            LOG_ERROR(logger_, "Failed freeing DMA buffers! \n");
        }

        LOG_INFO(logger_, "Finished read with {} DMA loops \n", dma_loop_count_.load());
    }

    void DataHandler::ReadoutViaController(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers *buffers) {

        std::array<uint32_t, DATABUFFSIZE> word_arr{};
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
                if (print) printf("receive data word = %x, %x, %x, %x, %x, %x\n",
                    pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1], pcie_int::PcieBuffers::read_array[2],
                    pcie_int::PcieBuffers::read_array[3], pcie_int::PcieBuffers::read_array[4], pcie_int::PcieBuffers::read_array[5]);
                // For some reason the first read is garbage so read once before
                // if (t < 5) continue;

                if (print) printf("receive data word = %x, %x, %x, %x, %x, %x\n",
                    pcie_int::PcieBuffers::read_array[0], pcie_int::PcieBuffers::read_array[1], pcie_int::PcieBuffers::read_array[2],
                    pcie_int::PcieBuffers::read_array[3], pcie_int::PcieBuffers::read_array[4], pcie_int::PcieBuffers::read_array[5]);
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

                uint32_t nread = ((pcie_int::PcieBuffers::read_array[1] >> 16) & 0xFFF) + ((pcie_int::PcieBuffers::read_array[1] & 0xFFF) << 12);

                nword = (nread + 1) / 2;                    // short words
                // i = pcie_rec(hDev,0,1,nword,iprint,py);     // init the receiver
                pcie_interface->PCIeRecvBuffer(kDev1, 0, 1, nword, 1, buffers->precv); // init the receiver

                // Read neutrino data through controller (slow control path)
                buffers->buf_send[0] = (fem_number << 11) + (hw_consts::mb_feb_pass_add << 8) + hw_consts::mb_feb_a_rdbuf + (0x0 << 16);
                pcie_interface->PCIeSendBuffer(kDev1, 1, 1, buffers->psend);

                // py = &read_array;
                // i = pcie_rec(hDev,0,2,nword,iprint,py);     // read out 2 32 bits words
                buffers->precv = pcie_int::PcieBuffers::read_array.data();
                pcie_interface->PCIeRecvBuffer(kDev1, 0, 2, nword, 1, buffers->precv);

                // Save the rest of the event words
                std::memcpy(word_arr.data(), pcie_int::PcieBuffers::read_array.data(), nword*sizeof(pcie_int::PcieBuffers::read_array[0]));
                data_queue_.write(word_arr);
                if (data_queue_.isFull()) {
                    LOG_ERROR(logger_, "Data read/write queue is full! This is unexpected! \n");
                }
            } // trig loop

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

     // Start very simple, just read and print data
    void DataHandler::TriggerDMARead(pcie_int::PCIeInterface *pcie_interface) {
        bool debug = false;
        unsigned long long trig_data_ctr;
        trig_data_ctr_ = 0xFFFFFF;

        LOG_INFO(logger_, "Opening Trigger data file");
        std::ofstream trigger_file;
        // std::string trigger_file_name = "trigger_data_run" + std::to_string(run_number_) + ".csv" ;
        std::string trigger_file_name = data_basedir_ + "/trigger_data/trigger_data_run" + std::to_string(run_number_) + ".csv";
        trigger_file.open(trigger_file_name);
        if (!trigger_file.is_open()) {
         LOG_WARNING(logger_, "Trigger file failed to open, only printing!");
        }

        // 1. Start with initializing 2nd fiber on the PCIe card
        pcie_interface->WriteReg32(kDev1,  hw_consts::cs_bar, hw_consts::tx_mode_reg, 0xf0000008);
        pcie_interface->WriteReg32(kDev1,  hw_consts::cs_bar, hw_consts::r2_cs_reg, hw_consts::cs_init);
        // Loading in the amount of data to expect, will decrement by 16B for every read, should last for ~1M events
        pcie_interface->WriteReg32(kDev1,  hw_consts::cs_bar, hw_consts::r2_cs_reg, hw_consts::cs_start+0xffffff);

        LOG_INFO(logger_, "Starting Trigger Read \n");

        // while(is_running_.load()) {
        while(is_running_.load()) {
         std::this_thread::sleep_for(std::chrono::milliseconds(2000));
         pcie_interface->ReadReg64(kDev1, hw_consts::cs_bar, hw_consts::t2_cs_reg, &trig_data_ctr);
         trig_data_ctr = (trig_data_ctr>>32) & 0xffffff;
         if( (trig_data_ctr_ - trig_data_ctr) < 16 ) {
             if (!is_running_.load()) break;
             continue;
             }

         if(trig_data_ctr < 1) {
             LOG_ERROR(logger_, "Got 0 trigger data! \n");
             }

         unsigned long long trig_data0;
         unsigned long long trig_data1;
         pcie_interface->ReadReg64(kDev1, hw_consts::t2_tr_bar, 0x0, &trig_data0);
         trig_data1 = trig_data0;
         pcie_interface->ReadReg64(kDev1, hw_consts::t2_tr_bar, 0x0, &trig_data1);

         uint64_t _trig_ctr = (trig_data0 >> 40);

         uint64_t _trig_frame = ( ( ( (trig_data0 >> 32) & 0xff ) << 16 ) + ( (trig_data0 >> 16) & 0xffff ) );
         uint64_t _trig_sample = ( (trig_data0 >> 4) & 0xfff );
         uint64_t _trig_sample_remain_16MHz = ( (trig_data0 >> 1) & 0x7 );
         uint64_t _trig_sample_remain_64MHz = ( (trig_data1 >> 15) & 0x3 );

         if (debug) {
             std::ostringstream msg;
             msg << "\033[1;33;40m[ NEW TRIGGER ]\033[00m\n"
             << " "
             << " TrigBytes: " << trig_data_ctr
             << " Trig: "   << _trig_ctr
             << " Frame: "  << _trig_frame
             << " Sample: " << _trig_sample
             << " Remine (16MHz): " << _trig_sample_remain_16MHz
             << " Remine (64MHz): " << _trig_sample_remain_64MHz
             << "\n"
             << " "
             << " Bits: \033[1;35;40m";
             if((trig_data1>>8) & 0x1) msg << " PC";
             if((trig_data1>>9) & 0x1) msg << " EXT";
             if((trig_data1>>12) & 0x1) msg << " Gate1";
             if((trig_data1>>11) & 0x1) msg << " Gate2";
             if((trig_data1>>10) & 0x1) msg << " Active";
             if((trig_data1>>13) & 0x1) msg << " Veto";
             if((trig_data1>>14) & 0x1) msg << " Calib";
             msg << "\033[00m\n";
             std::cout << msg.str() << std::endl;
             }

         trigger_file << _trig_ctr << ", " << _trig_frame << ", " << _trig_sample << ", "
                      << _trig_sample_remain_16MHz << ", " << _trig_sample_remain_64MHz << ", "
                      << trig_data_ctr << std::endl;


         trig_data_ctr_ -= 16;
        }
        trigger_file.close();
        LOG_INFO(logger_, "Ended Trigger Read {} \n", trig_data_ctr_);
    }

    void DataHandler::ClearDmaOnAbort(pcie_int::PCIeInterface *pcie_interface, unsigned long long *u64Data, uint32_t dev_num) {
        pcie_interface->ReadReg64(dev_num, hw_consts::cs_bar, hw_consts::t1_cs_reg, u64Data);
        LOG_DEBUG(logger_, " Status word for channel 1 after read = 0x{:X}, 0x{:X} \n", (*u64Data >> 32), (*u64Data & 0xffff));

        *u64Data = 0;
        pcie_interface->ReadReg64(dev_num, hw_consts::cs_bar, hw_consts::t2_cs_reg, u64Data);
        LOG_DEBUG(logger_, " Status word for channel 2 after read = 0x{:X}, 0x{:X} \n", (*u64Data >> 32), (*u64Data & 0xffff));

        /* write this will abort previous DMA */
        pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, hw_consts::dma_abort);

        /* clear DMA register after the abort */
        pcie_interface->WriteReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_msi_abort, 0);
    }

    bool DataHandler::WaitForDma(pcie_int::PCIeInterface *pcie_interface, uint32_t *data, uint32_t dev_num) {
        // Poll DMA until finished
        // Can get stuck waiting for the DMA to finish, use is_running flag check to break from it
        // for (size_t is = 0; is < 6000000000; is++) {
        for (size_t is = 0; is < 10000000; is++) {
            pcie_interface->ReadReg32(dev_num, hw_consts::cs_bar, hw_consts::cs_dma_cntrl, data);
            if ((*data & hw_consts::dma_in_progress) == 0) {
                return true;
            }
            if ((is > 0) && ((is % 10000000) == 0)) std::cout << "Wait iter: " << is << "\n";
            if (!is_running_.load()) break;
        }
        return false;
    }

    void DataHandler::PollTriggerPPS(pcie_int::PCIeInterface *pcie_interface) {
        std::array<uint32_t, 2> send_array{};
        std::array<uint32_t, 2> read_array{};
        uint32_t *psend = send_array.data();
        uint32_t *precv = read_array.data();

        size_t num_status_words = 2;
        uint32_t chip_num = 3;

        LOG_INFO(logger_, "Opening PPS data file");
        std::ofstream pps_file;
        std::string pps_file_name = data_basedir_ + "/pps_data/pps_data_run" + std::to_string(run_number_) + ".csv" ;
        pps_file.open(pps_file_name);
        if (!pps_file.is_open()) {
            LOG_WARNING(logger_, "PPS file failed to open, only printing!");
        }

        while (is_running_.load()) {
            // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            std::this_thread::sleep_for(std::chrono::milliseconds(pps_sample_period_));
            // init the receiver
            pcie_interface->PCIeRecvBuffer(1, 0, 1, num_status_words, 0, precv);
            // read out status =32 or read PPS register =35
            send_array[0] = (trigger_module_ << 11) + (chip_num << 8) + 35 + (0x0 << 16);
            pcie_interface->PCIeSendBuffer(1, 0, 1, psend);
            pcie_interface->PCIeRecvBuffer(1, 0, 2, num_status_words, 0, precv);

            uint32_t pps_frame = read_array.at(0) & 0xFFFFFF;
            uint32_t pps_sample = read_array.at(1) & 0xFFF;
            uint32_t pps_div = ( ( read_array.at(1) & 0x70000) >> 16 );
            LOG_DEBUG(logger_, "PPS - Frame: {} Sample: {} Div: {}", pps_frame, pps_sample, pps_div);

            if (pps_frame > 0) {
                // Get the current time point from the system clock
                auto now = std::chrono::system_clock::now();
                long long seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                pps_file << seconds << ", " << pps_frame << ", " << pps_sample << ", " << pps_div << std::endl;
                LOG_DEBUG(logger_, "Writing PPS data to file");
            }
        }

    pps_file.close();
    LOG_INFO(logger_, "Closed PPS data file");
    }

    std::vector<uint32_t> DataHandler::GetStatus() {
        std::vector<uint32_t> status;
        status.push_back(1);
        return status;
    }

    bool DataHandler::Reset(size_t run_number) {

        // Reset some counting variables
        file_count_.store(0);
        event_count_.store(0);
        num_dma_loops_ = 0;
        num_recv_bytes_ = 0;

        // Reset the stop write flag so we can restart
        stop_write_.store(false);

        // Double check the buffer is emptied
        while (!data_queue_.isEmpty()) data_queue_.popFront();

        run_number_ = run_number;
        write_file_name_ = data_basedir_ + "/readout_data/pGRAMS_bin_" + std::to_string(run_number_) + "_";
        LOG_INFO(logger_, "Reset data handler..");

        return true;
    }
} // data_handler
