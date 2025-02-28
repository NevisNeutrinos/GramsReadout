//
// Created by Jon Sensenig on 2/26/25.
//

#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H

#include <atomic>

namespace data_handler {

class DataHandler {
public:

  DataHandler() = default;
  ~DataHandler() = default;

  void SetRun(bool set_running);

private:

  std::atomic_bool is_running_;
  void CollectData();

};

} // data_handler

#endif //DATA_HANDLER_H
