//
// Created by Jon Sensenig on 2/24/25.
//

#ifndef PCIE_CONFIG_H
#define PCIE_CONFIG_H

#include "hardware_device.h"
#include <vector>

namespace pcie_control {

class PcieControl : HardwareDevice {
public:

  PcieControl() = default;
  ~PcieControl() override = default;

  bool Configure(pcie_int::PCIeInterface *pcie_interface, pcie_int::PcieBuffers& buffers) override;
  std::vector<uint32_t> GetStatus() override;
  bool CloseDevice() override;

private:



};

} // pcie_control

#endif //PCIE_CONFIG_H
