//
// Created by Jon Sensenig on 2/24/25.
//

#include "data_handler.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include "trigger_control.h"

#include "quill/LogMacros.h"

namespace data_handler {

    // int DataHandler::file_ptr_ = 0x0;

    DataHandler::DataHandler() : num_events_(0) {
        logger_ = quill::Frontend::create_or_get_logger("root",
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));
    }

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

        dma_buf_size_ = config["data_handler"]["dma_buffer_size"].get<int>();
        num_events_ = config["data_handler"]["num_events"].get<size_t>();
        const size_t subrun = config["data_handler"]["subrun"].get<size_t>();

        LOG_INFO(logger_, "\t [{}] DMA loops with [{}] words \n", num_dma_loops_, dma_buf_size_ / 4);

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
        LOG_INFO(logger_, "Closed file after writing {}B to file {} \n", num_recv_bytes_, name);

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
        return true;
    }

    void DataHandler::CollectData(pcie_int::PCIeInterface *pcie_interface) {

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
        static size_t n_write;

        pcie_int::DMABufferHandle  pbuf_rec1;
        pcie_int::DMABufferHandle pbuf_rec2;

         /*TPC DMA*/
        SetRecvBuffer(pcie_interface, &pbuf_rec1, &pbuf_rec2);
        usleep(500000);

        num_recv_bytes_ = 0; // keeps track of total number of words received
        // Set IO buffer to size of event + 30%
        std::array<char, 300000> io_buffer{};
        if (setvbuf(file_ptr_, io_buffer.data(), _IOFBF, dma_buf_size_) != 0) {
            LOG_ERROR(logger_, "Failed to initialize IO buffer... \n");
        }

        while(is_running_.load() && event_count_ < num_events_) {
            // if (event_count_ % 10 == 0) LOG_INFO(logger_, " \n ===================> Event No. [{}] \n", event_count_);
            if (event_count_ % 50 == 0) printf(" \n ===================> Event No. %lu \n", event_count_);
            for (iv = 0; iv < (num_dma_loops_ + 1); iv++) { // note: ndma_loop=1
                static uint32_t dma_num = (iv % 2) == 0 ? 1 : 2;
                buffp_rec32 = (iv % 2) == 0 ? static_cast<uint32_t *>(pbuf_rec1) : static_cast<uint32_t *>(pbuf_rec2);

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
                if (iv == 0) trig_ctrl::TriggerControl::SendStartTrigger(pcie_interface, itrig_c, itrig_ext);

                // extra wait just to be certain --- WK
                // usleep(200);
                /***    check to see if DMA is done or not **/
                // Can get stuck waiting for the DMA to finish so add a way to break from it
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
                    static size_t num_read = (nwrite_byte - (u64Data & 0xffff)) / 4;

                    LOG_INFO(logger_, "Received {} bytes, writing to file.. \n", num_read);
                    num_recv_bytes_ = num_recv_bytes_ + num_read;
                    memcpy(pcie_int::PcieBuffers::read_array.data(), buffp_rec32, num_read * 4);
                    n_write = fwrite(pcie_int::PcieBuffers::read_array.data(), 1, (num_read * 4), file_ptr_);

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

                static size_t nwrite = nwrite_byte / 4;
                memcpy(pcie_int::PcieBuffers::read_array.data(), buffp_rec32, nwrite * 4);
                n_write = fwrite(pcie_int::PcieBuffers::read_array.data(), 1, (nwrite * 4), file_ptr_);
                num_recv_bytes_ += nwrite;
            } // end dma loop
            if ((event_count_ > 0) && (event_count_% 10000 == 0)) SwitchWriteFile();
            event_count_ += 1;
        } // end loop over events

        LOG_INFO(logger_, "Stopping triggers..\n");
        trig_ctrl::TriggerControl::SendStopTrigger(pcie_interface, itrig_c, itrig_ext);

        LOG_INFO(logger_, "Freeing DMA buffers and closing file..\n");
        if (!pcie_interface->FreeDmaContigBuffers()) {
            LOG_ERROR(logger_, "Failed freeing DMA buffers! \n");
        }

        // Flush the buffer to make sure all data is written to file
        fflush(file_ptr_);
        if(fclose(file_ptr_) == EOF) {
            LOG_ERROR(logger_, "Failed to close data file... \n");
        }
        LOG_INFO(logger_, "Closed file after writing {}B to file {} \n", num_recv_bytes_, write_file_name_);
        LOG_INFO(logger_, "Wrote {} events to {} files", event_count_, file_count_);
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
