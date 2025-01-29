The DriverWizard has created:
* A sample diagnostics application for testing the communication with
  your hardware from the user mode.
* Library files, which include API for controlling your hardware.

  /path/to/file/gramsreadout_lib.h
         A library for controlling your hardware through WinDriver.

  /path/to/file/gramsreadout_lib.c
         Contains the implementation of the functions used for
         accessing each of the resources defined in the Wizard.

  /path/to/file/pcie_interface.h/c
        Wrapper around the Windriver library to provide an
        API to the control code.

  /path/to/file/linux/makefile
         Linux makefile.

Compiling this project:
  For Linux, run  - "make -C linux"

