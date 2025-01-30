# GramsReadout

Repository for the Nevis readout electronics, both light and charge.
The code presupposes the PCIe driver has already been generated
using the WinDriver software such that there is a library containing
an API. This code is being developed for a driver generated using 
WinDriver 16.03 and has not been tested with older API versions.


To clone the repository,
```
git clone 
```

and include the required logging repository with
```
git submodule update --init 
```

Finally, to build the code do,
```
mkdir build
cmake -B build/
cd build
make
```