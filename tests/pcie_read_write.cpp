//
// Created by Jon Sensenig on 2/22/25.
//

#include <iostream>
#include <vector>
#include <sstream>
#include "../lib/pcie_driver/pcie_interface.h"


// Gets user input safely.
int GetUserInput() {
    uint32_t choice;
    std::cin >> choice;

    // Handle invalid input (non-numeric)
    if (std::cin.fail()) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid input. Please enter a number.\n";
        return GetUserInput();  // Retry
    }

    return choice;
}

// Prints the current state and available options.
void PrintState() {
    std::cout << "Select a command:\n";
    std::cout << "  [0] Read\n";
    std::cout << "  [1] Write\n";
    std::cout << "  [-1] Exit\n";
    std::cout << "Enter choice: ";
}

std::vector<int32_t> GetUserInputList() {
    std::vector<int32_t> numbers;
    std::string line;
    std::cout << "Enter <AddrSpace> <AddrOffset> <Data> with spaces: ";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::getline(std::cin, line);

    std::stringstream ss(line);
    uint32_t num;
    while (ss >> std::hex >> num) {
        numbers.push_back(num);
    }
    if (numbers.size() != 3) std::cerr << "Invalid entry number, must be 3!. Size is: " << numbers.size() << "\n";
    return numbers;
}

// Runs the command-line interface for the state machine.
void Run(pcie_int::PCIeInterface& pcie_interface) {

    while (true) {
        PrintState();
        int input = GetUserInput();

        if (input == -1) {
            std::cout << "Exiting...\n";
            break;
        }

        std::vector<int32_t> reg_input = GetUserInputList();

        switch (input) {
            case 0:
                unsigned long long data;
                pcie_interface.ReadReg64(1, reg_input.at(0), reg_input.at(1), &data);
                std::cout << std::hex;
                std::cout << "Reading from Addr Space: " << reg_input.at(0) << " Offset: " << reg_input.at(1)
                << " Data upper/lower16b: " << (data >> 32) << " / " << (data & 0xFFFF) << std::endl;
                std::cout << std::dec;
                break;
            case 1:
                pcie_interface.WriteReg32(1, reg_input.at(0), reg_input.at(1), reg_input.at(2));
                std::cout << std::hex;
                std::cout << "Writing to Addr Space: " << reg_input.at(0) << " Offset: " << reg_input.at(1)
                << " Data: " << reg_input.at(2) << std::endl;
                std::cout << std::dec;
                break;
            default:
                std::cout << "Invalid input.\n";
        }

    }
}

int main() {

    pcie_int::PCIeInterface pcie_interface;

    uint32_t dev1 = 5;
    uint32_t dev2 = 4;
    if (!pcie_interface.InitPCIeDevices(dev1, dev2)) {
        std::cerr << "PCIe device initialization failed!" << std::endl;
        return 1;
    }

    pcie_interface.PCIeDeviceConfigure();
    Run(pcie_interface);

    return 0;
}