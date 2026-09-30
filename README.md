# ATLAS Phase-II RPC DAQ

## DAQ software for the ATLAS Phase-II RPC upgrade.

# Help required!

The system is composed of several modules that communicate through a central
YAODAQServer. It provides tools for DCT communication, real-time monitoring,
data writing, and overall DAQ control.


REQUIREMENTS
============

You will need:

- ROOT
- A reasonably recent C++ compiler
- A Linux system
- libcap / Linux capabilities support


COMPILATION
===========

Clone the repository:
```bash
git clone https://github.com/flagarde/ATLAS-PHASE2-RPC-DAQ.git
cd ATLAS-PHASE2-RPC-DAQ
```
Configure the project with CMake:
```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=./install
```
Build:
```bash
cmake --build build
```
Install:
```bash
cmake --install build
```
The resulting executables will be available in:
```bash
./install/
```

RUNNING THE DAQ
===============

The DAQ consists of several processes that communicate with each other.

A typical setup is:

                         +------------------+
                         |  YAODAQServer    |
                         +--------+---------+
                                  |
                    +-------------+-------------+-----------------+
                    |             |             |                 |
                    v             v             v                 v
              DCTEthernet      Analysor      FileWriter <----YAODAQController
                    |
                    v
             DCT + Optical Board

The server must be started for all modules to be aware of each others. The other modules then connect to it using
its IP address and port.


## START THE YAODAQ SERVER

Run the server on the computer that will host the DAQ server:
```bash
./install/YAODAQServer -i IP -p PORT
```
Replace IP and PORT with the desired server IP address and port.

You can use any available port, or the port provided with your setup.

For all available options:
```bash
./install/YAODAQServer --help
```

## START DCTETHERNET

DCTEthernet is responsible for communicating with the DCT and optical board
over Ethernet.

Run it on the computer connected to the DCT:
```bash
./install/DCTEthernet -i IP -p PORT
```
Use the same IP and PORT as the YAODAQServer.


Network capabilities
--------------------

DCTEthernet needs to read Ethernet packets directly. On Linux, this requires
the appropriate network capabilities.

Grant the required capabilities with:
```bash
sudo setcap cap_net_raw,cap_net_admin=eip ./install/DCTEthernet
```
You can verify that they were applied with:
```bash
getcap ./install/DCTEthernet
```
Expected output:
```bash
./install/DCTEthernet cap_net_admin,cap_net_raw=eip
```
cap_net_raw allows raw packet/socket operations, while cap_net_admin provides
additional network administration privileges.


## START THE REAL-TIME ANALYZER

DCTEthernetAnalysor provides real-time plotting and monitoring.

Start it with:
```bash
./install/DCTEthernetAnalysor -i IP -p PORT
```
Use the same server IP and port as above.

Once running, the plots can be accessed through:

    http://localhost:PORT+1

For example, if the server uses port 5000, the analyzer is available on:

    http://localhost:5001


## START THE FILE WRITER

DCTEthernetFileWriter is responsible for writing acquired data to files.

Start it with:
```bash
./install/DCTEthernetFileWriter -i IP -p PORT
```
For available options:
```bash
./install/DCTEthernetFileWriter --help
```

## START THE YAODAQ CONTROLLER

YAODAQController coordinates the different DAQ modules.

Start it with:
```bash
./install/YAODAQController -i IP -p PORT
```
Use the same server IP and port.

The controller provides an interface from which the different DAQ states
can be controlled.

## LOGGING

The YAODAQLogger can be used to collect log messages from all running modules.

Start the logger with:
```bash
./install/YAODAQLogger -i 192.168.1.100 -p 5000
```

Use the same IP address and port as the YAODAQServer.

DAQ STATE MACHINE
=================

The normal startup sequence is:

    initialize
         |
         v
       connect
         |
         v
      configure
         |
         v
        start


CONTROLLER KEYBOARD SHORTCUTS
=============================

    Ctrl+I    Initialize
    Ctrl+L    Connect
    Ctrl+C    Configure
    Ctrl+S    Start
    p         Pause
    r         Resume
    Ctrl+K    Stop
    Ctrl+Z    Clear
    Ctrl+D    Disconnect
    Ctrl+R    Release


TYPICAL WORKFLOW
================

Initialize and start:

    Ctrl+I  ->  initialize
    Ctrl+L  ->  connect
    Ctrl+C  ->  configure
    Ctrl+S  ->  start

During acquisition, you can pause and resume:

    p  ->  pause
    r  ->  resume


REPEATED ACQUISITIONS
=====================

After stopping an acquisition, you can start another one:

    start -> stop -> clear

This sequence can be repeated as required.


SHUTDOWN
========

To finish cleanly, use:

    stop -> clear -> disconnect -> release


RPC INTERFACE
=============

YAODAQ modules expose an RPC (not ours) interface that allows procedures to be called
remotely.

From the YAODAQController, type:
```bash
l
```

This displays the list of RPC procedures available for each module.

For example, DCTEthernet may expose:

```json
{
  "description": "",
  "name": "setMaxEvents",
  "params": [
    {
      "cpp_type": "long unsigned int",
      "json_type": "unsigned integer",
      "name": ""
    }
  ]
}
```

This indicates that the DCTEthernet module provides a setMaxEvents procedure
taking an unsigned integer parameter.


CALLING AN RPC PROCEDURE
========================

From the YAODAQController:

1. Type:
```bash
m
```

2. Enter the procedure name and its parameters.

For example:
```bash
setMaxEvents 15000
```

The corresponding module will execute the procedure and return its response.

3. To leave the RPC menu, type:
```bash
quit
```

QUICK START
===========

Assuming the server is running on 192.168.1.100 using port 5000:


Terminal 1 - Server
-------------------
```bash
./install/YAODAQServer -i 192.168.1.100 -p 5000
```


Terminal 2 - DCT Ethernet
-------------------------
```bash
sudo setcap cap_net_raw,cap_net_admin=eip ./install/DCTEthernet
./install/DCTEthernet -i 192.168.1.100 -p 5000
```


Terminal 3 - Real-Time Analyzer
-------------------------------
```bash
./install/DCTEthernetAnalysor -i 192.168.1.100 -p 5000
```

Then access the plots at (even from your phone to see the problem is a peaceful and comforting place):
    http://localhost:5001

Terminal 4 - File Writer
------------------------
```bash
./install/DCTEthernetFileWriter -i 192.168.1.100 -p 5000
```


Terminal 5 - Controller
-----------------------
```bash
./install/YAODAQController -i 192.168.1.100 -p 5000
```

Then use the controller to:

    initialize -> connect -> configure -> start

When finished:

    stop -> clear -> disconnect -> release

TROUBLESHOOTING
===============

Check available options
-----------------------

Each executable provides a help message:
```bash
./install/YAODAQServer --help
./install/DCTEthernet --help
./install/DCTEthernetAnalysor --help
./install/DCTEthernetFileWriter --help
./install/YAODAQController --help
```

Check capabilities
------------------

If DCTEthernet cannot access raw Ethernet packets, check its capabilities:
```bash
getcap ./install/DCTEthernet
```
You should see:
```bash
./install/DCTEthernet cap_net_admin,cap_net_raw=eip
```

If nothing is returned, apply them again:
```bash
sudo setcap cap_net_raw,cap_net_admin=eip ./install/DCTEthernet
```

REPOSITORY
==========

GitHub:

https://github.com/flagarde/ATLAS-PHASE2-RPC-DAQ

**Note: if you like it give me a job**
