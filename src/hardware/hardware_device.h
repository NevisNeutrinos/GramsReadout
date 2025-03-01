//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef HARDWARE_DEVICE_H
#define HARDWARE_DEVICE_H

#include <cstdint>
#include <vector>
#include "quill/LogMacros.h"
#include "quill/Frontend.h"
#include "quill/sinks/ConsoleSink.h"
#include "pcie_interface.h"
#include "hardware_constants.h"
#include "json.hpp"

using json = nlohmann::json;

class HardwareDevice {
  public:

    /**
    *  Abstract class which will be the common interface to configure the respective hardware.
    *
    *   @param [in] config:  The configuration passed from the top level.
    *   @param config
    *   @param [in] pcie_interface:  The PCIe interface library.
    *   @param buffers
    *
    * @return  Returns true on successful configuration, false on failure.
    */
    virtual bool Configure(json &config, pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers) = 0;

    /**
    *  Abstract class to get status of the respective hardware.
    *
    *   @param [in] query:  The status to be queried.
    *
    * @return  std::vector<uint32_t>, vector of status words returned by the hardware
    */
    virtual std::vector<uint32_t> GetStatus() = 0;

    /**
    *  Abstract class which shall implement all required shutdown procedures to
    *  safely close the hardware and handle any errors which might occur.
    *
    *
    *
    * @return  Returns true on successful shutdown, false on failure.
    */
    virtual bool CloseDevice() = 0;

    virtual ~HardwareDevice() = default;

    static constexpr uint32_t kDev1 = pcie_int::PCIeInterface::kDev1;
    static constexpr uint32_t kDev2 = pcie_int::PCIeInterface::kDev2;

};

#endif //HARDWARE_DEVICE_H
