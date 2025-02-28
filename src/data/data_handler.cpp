//
// Created by Jon Sensenig on 2/26/25.
//

#include "data_handler.h"

namespace data_handler {

    void DataHandler::SetRun(bool set_running) {
        is_running_ = set_running;
    }

    void DataHandler::CollectData() {
        while (is_running_.load()) {

        }
    }

} // data_handler