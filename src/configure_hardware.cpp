//
// Created by Jon Sensenig on 1/29/25.
//

#include "configure_hardware.h"
#include <iostream>

namespace hw_config {

    bool ConfigureHardware::Configure(Config& config) {
        std::cout << "ConfigureHardware::Configure" << std::endl;

        // Configure XMIT hardware
        XMITConfigure(config);

        return false;
    }


    bool ConfigureHardware::XMITConfigure(Config &config) {

    }

} // hw_config
