#include <iostream>
#include "src/controller.h"


// Gets user input safely.
int GetUserInput() {
    int choice;
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
void PrintState(const std::string& state) {
    std::cout << "\nCurrent state: " << state << "\n";
    std::cout << "Select a command:\n";
    std::cout << "  [0] Reset\n";
    std::cout << "  [1] Configure\n";
    std::cout << "  [2] Start\n";
    std::cout << "  [3] Stop\n";
    std::cout << "  [-1] Exit\n";
    std::cout << "Enter choice: ";
}

// Runs the command-line interface for the state machine.
void Run(controller::Controller& ctrl) {
    while (true) {
        PrintState(ctrl.GetStateName());
        int input = GetUserInput();

        if (input == -1) {
            std::cout << "Exiting...\n";
            break;
        }

        auto new_state = static_cast<controller::Transitions>(input);
        if (ctrl.HandleCommand(new_state)) {
            std::cout << "State changed to: " << ctrl.GetStateName() << "\n";
        } else {
            std::cout << "Invalid state transition!\n";
        }
    }
}

int main() {

    controller::Controller controller;
    controller.Init();
    Run(controller);

    return 0;
}