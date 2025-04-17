# GramsReadout

Repository for the Nevis readout electronics, both light and charge.
The code presupposes the PCIe driver has already been generated
using the WinDriver software such that there is a library containing
an API. This code is being developed for a driver generated using 
WinDriver 16.03 and has not been tested with older API versions.


To clone the repository,
```
git clone https://github.com/NevisNeutrinos/GramsReadout.git
```

and include the required networking and logging repositories with,
```
git submodule update --init 
```

Finally, to build the code create the build directory and cd into it,
```bash
mkdir build
cmake -B build/
cd build
```
Then build and install the code with the following commands.
```bash
make -j 8
sudo make install
```

Details on running the daemon for the project can be found in the
[daemon](daemon/README.md) directory.