# NVMe2K - NVMe Driver for Windows 2000

A NVMe (Non-Volatile Memory Express) storage controller driver for Windows 2000, targeting both x86 and Alpha AXP platforms.

## Overview

NVMe2K is a SCSI miniport driver that provides NVMe device support for Windows 2000. It uses the ScsiPort framework to integrate NVMe solid-state drives with the Windows 2000 storage stack.

### Why?

It seemed like a good idea at first.

## Features

- **NVMe 1.0 specification support**
  - Single I/O queue pair (scalable architecture)
  - Admin queue for device management
  - PRP (Physical Region Page) based data transfers
  - Up to 2MB transfer sizes via PRP lists

- **SCSI Translation Layer**
  - Translates SCSI commands to NVMe commands
  - Tagged command queuing support
  - Ordered queue tag support with automatic flush
  - READ/WRITE/FLUSH/INQUIRY/READ_CAPACITY commands

- **Advanced Features**
  - Proper alignment for Alpha
  - Non-tagged request serialization
  - Queue depth management and statistics

- **Multi-Platform Support**
  - x86 (Pentium and later)
  - Alpha AXP (DEC Alpha workstations/servers)

## Architecture

```
Application Layer
       ↓
  SCSI Disk Driver
       ↓
   ScsiPort.sys
       ↓
   nvme2k.sys ← This driver
       ↓
  NVMe Controller (PCI)
```

### Key Components

- **nvme2k.c** - Main driver logic
- **nvme2k_nvme.c** - NVMe controller logic
- **nvme2k_scsi.c** - SCSI command handling
- **nvme2k_cpl.c** - NVMe completion handling
- **nvme2k.h** - Data structures, constants, NVMe register definitions
- **nvme2k.inf** - Multi-platform installation file

## Building

### Requirements

- **Windows 2000 DDK**
  - Final version (5.00.2195.1) for x86
  - RC1 version for Alpha AXP (does anyone have RC2 besides hoarders at betanews?)

- **Visual C++ 6.0**
  - x86 compiler for x86 builds
  - Alpha AXP compiler for Alpha builds

### Build Instructions

#### For x86:

```cmd
REM Set up build environment
cd C:\NTDDK\bin
setenv.bat C:\NTDDK free          REM or 'checked' for debug build

REM Navigate to driver directory
cd <path-to-nvme2k>

REM Build
build -cZ
```

Output: `obj\i386\nvme2k.sys`

#### For Alpha AXP:

```cmd
REM Set up build environment for Alpha
cd C:\NTDDK\bin
setenv.bat C:\NTDDK free    REM or 'checked' for debug build

REM Navigate to driver directory
cd <path-to-nvme2k>

REM Build
build -cZ
```

Output: `obj\alpha\nvme2k.sys`

### Build Options

Edit `nvme2k.h` to configure:

```c
#define NVME2K_DBG                    // Enable debug logging
// #define NVME2K_DBG_CMD             // Extra verbose command logging

```

## Installation

### Creating Installation Media

Create the following directory structure:

```
DriverDisk\
├── nvme2k.inf
├── i386\
│   └── nvme2k.sys    (x86 binary)
└── alpha\
    └── nvme2k.sys    (Alpha AXP binary)
```

### Installing

1. Copy driver files to installation media
2. Boot Windows 2000
3. Use Device Manager or Add Hardware Wizard
4. Point to the driver location when prompted
5. Select "NVMe Storage Controller (Windows 2000)"

**Note:** The driver requires NVMe devices to be visible on the PCI bus with class code `01-08-02` (Mass Storage - Non-Volatile Memory - NVMe).

## Configuration

The driver supports registry-based configuration via the INF file:

- **MaximumSGList** (Default: 255) - Maximum scatter-gather list entries
- **NumberOfRequests** (Default: 32) - Queue depth
- **MaxQueueDepth** (Default: 32) - Tagged command queue depth

## Debugging

Enable checked (debug) builds and use WinDbg with the Windows 2000 kernel debugger:

```
!scsiport.miniport <DeviceExtension>
!devobj <DeviceObject>
```

Debug messages are output via `ScsiDebugPrint()` and visible in checked builds.

## Known Limitations

- Single I/O queue pair (no multi-queue support)
- No MSI/MSI-X interrupt support (uses legacy INTx)
- Maximum 10 concurrent large transfers (PRP list pool limitation)
- No namespace management (assumes namespace 1)
- No power management features
- Tested primarily in virtualized environments and Windows 2000 RC2 on Alpha

## Technical Notes

### Synchronization Model

1. **NonTaggedInFlight** - Ensures only one non-tagged request at a time

All locks can be disabled via `#define` for performance testing.

### Memory Allocation

- **Uncached Extension** - 64KB for queues and PRP lists (DMA-accessible)
- **Admin Queue** - 4KB submission + 4KB completion (power-of-2 sized)
- **I/O Queue** - 4KB submission + 4KB completion (power-of-2 sized)
- **PRP List Pool** - 40KB (10 pages) for scatter-gather

### Command ID Encoding

```
Bit 15: Non-tagged flag (1 = non-tagged, 0 = tagged)
Bit 14: Ordered flush flag (for ORDERED queue tags)
Bits 0-13: QueueTag (tagged) or sequence number (non-tagged)
For admin queue command get log page, PRP slot is added to base CID
so we dont leak them if SRB is not available. For some reason SCSIPORT doesn't
return SRB when we ask it for SMART commands.
```

## License

This project is licensed under the **3-Clause BSD License**. See the [LICENSE](LICENSE) file for the full license text.

**USE AT YOUR OWN RISK.** This driver is experimental and may cause data loss, system instability, or hardware damage. Not recommended for production use.

### Summary

- ✅ Free to use, modify, and distribute
- ✅ Commercial use permitted
- ✅ Modification and redistribution allowed
- ⚠️ No warranty provided
- ⚠️ Use at your own risk

## Contributing

Feel free to submit issues or pull requests. Areas of interest:

- Multi-queue support
- MSI-X interrupt support (is it even possible?)
- Power management
- Additional SCSI command translations
- Performance optimizations

## Acknowledgments

- NVMe specification authors
- Windows 2000 DDK documentation
- Alpha AXP architecture documentation
- Everyone who thought this was a terrible idea (you were right)

---

**Disclaimer:** This is an unofficial, community-developed driver. Not affiliated with any company or the NVMe standards body.
