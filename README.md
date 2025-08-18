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

## Checking Your Environment
To check the environment is set up correctly, before running using the 
`env_check.sh` script. If everything is set up correctly your output 
should look something similar to this, of course the absolute path 
will be of your machine,
```bash
--- Checking Environment Variables ---
✔ Success: Environment variable 'TPC_DAQ_BASEDIR' is set to: /home/pgrams/GramsReadout
✔ Success: Environment variable 'WD_BASEDIR' is set to: /home/pgrams/WD1630LNX86_64
✔ Success: Environment variable 'WD_KERNEL_MODULE_NAME' is set to: KP_PGRAMS
✔ Success: Environment variable 'DATA_BASE_DIR' is set to: /home/pgrams/data

--- Checking Directories under '/home/pgrams/data' ---
✔ Success: Directory '/home/pgrams/data/readout_data' exists.
✔ Success: Directory '/home/pgrams/data/sabertooth_pps' exists.
✔ Success: Directory '/home/pgrams/data/trigger_data' exists.
✔ Success: Directory '/home/pgrams/data/logs' exists.
```