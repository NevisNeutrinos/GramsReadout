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
    *  Abstract method which will be the common interface to configure the respective hardware.
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
    *  Abstract method to get status of the respective hardware.
    *
    *   @param [in] query:  The status to be queried.
    *
    * @return  std::vector<uint32_t>, vector of status words returned by the hardware
    */
    virtual std::vector<uint32_t> GetStatus() = 0;

    HardwareDevice();
    virtual ~HardwareDevice() = default;

    /**
    *  Concrete method to read bitfile and load into FPGA.
    *
    *   @param [in] config:  The configuration passed from the top level.
    *   @param config
    *   @param [in] pcie_interface:  The PCIe interface library.
    *   @param buffers
    *
    *   @return  Returns true on successful configuration, false on failure.
    */
    bool LoadFirmware(int module, int chip, std::string &fw_file,
                                    pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers &buffers);

    /**
    *  Members to access the two PCIe devices.
    */
    static constexpr uint32_t kDev1 = pcie_int::PCIeInterface::kDev1;
    static constexpr uint32_t kDev2 = pcie_int::PCIeInterface::kDev2;

private:

    quill::Logger* logger_;

};

#endif //HARDWARE_DEVICE_H
