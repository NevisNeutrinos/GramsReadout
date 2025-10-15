This is the daemon that runs in the background. It acts as a 
client trying to connect to the server port at the specified
server address. For the pGRAMS flight the server will be the
HUB computer.

The daemon is built as part of the larger `GramsReadout` project.
Building the project will install the `daq_orchestrator` to 
`/usr/local/bin`. After the installation `daq_orchestrator` can be
run from any directory simply by typing this, in the command line. 
```bash
sudo daq_orchestrator
```

To run the `daq_orchestrator` as a service, the `daq_orchestrator.service`
file must be copied to `/etc/systemd/system`. The service can then be
manually started by running,
```bash
sudo systemctl start daq_orchestrator
```

and the status checked with, 
```bash
sudo systemctl status daq_orchestrator
```

where you should see a similar output with the service
listed as `active (runnning)` and the tail of the logger output.
```bash
sabertooth@sabertooth-01:~$ sudo systemctl status daq_orchestrator
● daq_orchestrator.service - DAQ Orchestrator Service
     Loaded: loaded (/etc/systemd/system/daq_orchestrator.service; disabled; vendor preset: enabled)
     Active: active (running) since Thu 2025-04-17 08:16:53 EDT; 4s ago
   Main PID: 1673358 (daq_orchestrato)
      Tasks: 4 (limit: 38125)
     Memory: 1.2M
     CGroup: /system.slice/daq_orchestrator.service
             └─1673358 /usr/local/bin/daq_orchestrator

Apr 17 08:16:53 sabertooth-01 systemd[1]: Started DAQ Orchestrator Service.
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: Starting Client on Address [127.0.0.1] Port [1740]
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: Connection failed: Connection refused
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: 08:16:53.038591526 [1673358] daq_orchestrator.cpp:117     LOG_INFO      readout_logger Logging initialized successfully. Outputting to stdout.
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: 08:16:53.038913022 [1673358] daq_orchestrator.cpp:224     LOG_INFO      readout_logger Controller service starting up..
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: 08:16:53.038913064 [1673358] daq_orchestrator.cpp:225     LOG_INFO      readout_logger Target Controller IP: 127.0.0.1, Port: 1738
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: 08:16:53.038967351 [1673358] daq_orchestrator.cpp:254     LOG_INFO      readout_logger Starting control connection
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: 08:16:53.039011860 [1673360] daq_orchestrator.cpp:236     LOG_INFO      readout_logger ASIO io_context thread started...
Apr 17 08:16:53 sabertooth-01 daq_orchestrator[1673358]: 08:16:53.039191856 [1673358] daq_orchestrator.cpp:269     LOG_INFO      readout_logger Service running. Waiting for termination signal...
Apr 17 08:16:56 sabertooth-01 daq_orchestrator[1673358]: Connection timed out.
```

To view the updating log you can run the following command,
```
sudo journalctl -u daq_orchestrator.service -f
```
