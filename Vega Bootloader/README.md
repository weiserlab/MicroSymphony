# Vega Bootloader

The **Vega Bootloader** is a custom bootloader designed as part of the **MicroSymphony** platform to enable broadcast-based firmware upgrades for multiple microcontrollers using a single programmer. This bootloader can be loaded onto MSP430-class microcontrollers, with code provided for MSP430FR5949/5969/5959. 

The bootloader is built on [MSP430FRBoot](https://www.ti.com/lit/an/slaa721e/slaa721e.pdf), TI's bootloader framework.

## Overview

In this system:
- **Target**: The microcontroller(s) that receive the bootloader and will be programmed over the multi-drop network
- **Host**: An external microcontroller (MSP430FR5969 LaunchPad) that serves as the programmer, flashing firmware to all connected targets simultaneously

<a href="../misc/bootloader_flow.svg">
  <img src="../misc/bootloader_flow.svg" alt="VEGA Bootloader Flow" >
</a>


## Features

- **GPIO-based boot entry**: Boot pin based entry mechanism into boot mode on reset
- **Unique addressing**: Each microcontroller has an address in format `xxxyyyy` where:
  - `xxx` = board/platform number (one-hot encoded)
  - `yyyy` = microcontroller number (one-hot encoded)
- **Multi-drop UART protocol**: Enables broadcasting to multiple targets
- **Simultaneous flashing**: Flash multiple microcontrollers at once (tested up to 12 devices)
- **Built on MSP430FRBoot**: Leverages TI's proven bootloader framework

## Prerequisites

- Code Composer Studio (CCS) IDE
- MSP430FR5969 LaunchPad (for Host)
- MSP430FR5949/5969/5959 targets
- Python 3.6+ with `pyserial` library

## Repository Structure

```
.
├── Host-parallel/              # Host MSP430FR5969 project (CCS project)
├── MSPFR5949/
│   ├── App1_5949/             # Example application for MSP430FR5949 (CCS project)
│   └── Slave-parallel_5949/   # Bootloader for MSP430FR5949 (CCS project)
├── send2MSP-parallel.py       # Python script to send app image to host
└── README.md                  # This file
```

## Hardware Connections

### Host MSP (MSP430FR5969 LaunchPad)
- **P2.6** → RX (UART receive)
- **P2.5** → TX (UART transmit)
- **P3.0** → RST (reset line to all targets)
- **P1.3** → Boot pin (connects to all targets)

### Target MSP (MSP430FR5949/5969/5959)
- **P2.6** → RX (UART receive)
- **P2.5** → TX (UART transmit)
- **RST** → RST (shared reset line from host)
- **P1.3** → Boot pin (from host's P1.3)

All devices (host and targets) must share a common ground connection.

## Getting Started

<a href="../misc/flashing_flow.svg">
  <img src="../misc/flashing_flow.svg" alt="VEGA Bootloader Flow" >
</a>

### Step 1: Flash the Bootloader to Target Devices

1. **Configure the bootloader** for each target:
   - Open the appropriate `Slave-parallel_xxxx` project for your target MCU in CCS
   - Edit `TI_MSPBoot_Config.h` to set:
     - `SLAVE_ADDRESS_MASK`: Unique address for this target (e.g., `BIT0`, `BIT1`, `BIT2`, `BIT3`)
     - `BOARD_ADDRESS_MASK`: Board/platform number (e.g., `BIT4`, `BIT5`, `BIT6`)
     - `HW_ENTRY_CONDITION`: Boot pin configuration (default: `P1IN & BIT3`)
   
   **Important**: Ensure no two targets have the same address combination! If addresses conflict, multiple devices may respond simultaneously, causing communication errors and flash failures.

2. **Build and flash** the bootloader project to each target microcontroller using CCS or a programmer

3. **Repeat** for all target devices, changing the address for each

### Step 2: Prepare Your Application

1. **Merge your application code** into the appropriate `App1_xxxx` project for your target MCU

2. **Configure the linker file**:
   - The App project should use the `lnk_App.cmd` linker file
   - This ensures the application is placed in the correct memory region (not overwriting the bootloader)

3. **Build the application** in CCS:
   - Build the project to generate the output files
   - Locate the generated `.txt` file (TI-TXT format) in the output directory

4. **Note the TI-TXT file location** - you'll need this for the Python script

### Step 3: Flash the Host Programmer

1. **Open** the `Host-parallel` project in CCS

2. **Verify memory addresses** in `main.c` match your target device (see [Linker File Generation](#linker-file-generation) if adapting to other MSP variants)

3. **Build and flash** the Host project to the MSP430FR5969 LaunchPad

### Step 4: Program Targets via Host

1. **Connect hardware**:
   - Connect the Host MSP430FR5969 LaunchPad to your PC via USB
   - Ensure all target devices are connected to the host as described in [Hardware Connections](#hardware-connections)

2. **Configure the Python script**:
   - Open `send2MSP-parallel.py`
   - Set `port_path` to match your serial port:
     - Linux: `/dev/ttyACM0`, `/dev/ttyACM1`, or `/dev/ttyUSB0`
     - Windows: `COM3`, `COM4`, etc.
     - macOS: `/dev/cu.usbmodem*` (e.g., `/dev/cu.usbmodem14201`)
   
   ```python
   port_path = "/dev/ttyACM0"  # Change to your port
   ```

3. **Run the flashing script**:
   ```bash
   python send2MSP-parallel.py
   ```

4. The script will:
   - Parse the TI-TXT file
   - Frame the data into packets
   - Send the firmware image to the host
   - The host will broadcast to all connected targets
   - Targets matching the configured addresses will be programmed


## Troubleshooting

- **Targets not responding**: Verify all addresses are unique and connections are correct
- **Flash verification fails**: Ensure the application doesn't overlap with bootloader memory
- **Serial port errors**: Check port permissions and that the correct port is specified
- **CRC mismatch**: Regenerate the TI-TXT file and ensure no corruption during transfer

## References

- [MSP430FRBoot Application Note (SLAA721E)](https://www.ti.com/lit/an/slaa721e/slaa721e.pdf)
- [MSP430FR5xx and MSP430FR6xx Family User's Guide](https://www.ti.com/lit/ug/slau367p/slau367p.pdf)
- Code Composer Studio: [https://www.ti.com/tool/CCSTUDIO](https://www.ti.com/tool/CCSTUDIO)


See individual source files for license information. This project builds upon TI's MSP430FRBoot framework.

This is part of the MicroSymphony platform research project. For questions or contributions, please open an issue.

