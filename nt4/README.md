# Windows NT 4.0 Installation Files

This directory contains the installation files for the NVMe driver on Windows NT 4.0.

## Files

- **oemsetup.inf** - Main installation script for Windows NT 4.0
- **disk1** - Tag file that identifies the installation media
- **INSTALL.txt** - Detailed installation instructions
- **sample.inf** - DDK sample reference (for development reference only)

## Directory Structure for Distribution

When preparing installation media (floppy, CD, or network share), organize the files as follows:

```
installation_media/
├── oemsetup.inf          ← Main INF file
├── disk1                 ← Tag file (required)
├── i386/                 ← x86 architecture binaries
│   └── nvme2k.sys
└── alpha/                ← Alpha architecture binaries
    └── nvme2k.sys
```

## How Architecture Selection Works

Windows NT 4.0 automatically determines the correct architecture at installation time:

1. The NT Setup or SCSI Adapters control panel reads `oemsetup.inf`
2. NT sets the `$(!STF_PLATFORM)` variable to either "i386" or "Alpha" based on the system
3. The INF file uses `AddSectionKeyFileToCopyList` which automatically looks in the platform-specific subdirectory
4. The correct binary is copied from `i386\nvme2k.sys` or `alpha\nvme2k.sys` to `%SystemRoot%\System32\drivers\`

**You don't need to create separate INF files for each architecture** - this single `oemsetup.inf` handles both!

## Tag File (disk1)

The `disk1` file is a **tag file** that identifies the installation media. Windows NT Setup uses this to:
- Verify the correct disk/media is inserted
- Avoid repeatedly prompting for the same disk
- Track which media has been used during installation

The tag file should be a simple text file (the content doesn't matter much, but it should be present).

## Installation Methods

### Method 1: During NT 4.0 Setup (F6 Driver)
1. Press F6 when prompted during text-mode setup
2. Press 'S' to specify additional SCSI adapter
3. Insert media and specify the path to the root directory containing oemsetup.inf
4. Select "NVMe Storage Controller Driver for Windows NT 4.0"

### Method 2: Post-Install via SCSI Adapters Control Panel
1. Control Panel → SCSI Adapters → Drivers tab
2. Click "Add..." → "Have Disk..."
3. Specify path to directory containing oemsetup.inf
4. Select driver and install
5. Reboot

## Building for Multiple Architectures

To build the driver for both architectures:

### For i386:
```bash
# Use NT4 DDK with x86 environment
cd /path/to/nvme2k
build -cZ
# Copy nvme2k.sys to nt4/i386/
```

### For Alpha:
```bash
# Use NT4 DDK with Alpha environment
cd /path/to/nvme2k
build -cZ
# Copy nvme2k.sys to nt4/alpha/
```

## Registry Configuration

The INF automatically creates the following registry structure:

```
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvme2k
├── Type = 1 (SERVICE_KERNEL_DRIVER)
├── Start = 0 (SERVICE_BOOT_START)
├── ErrorControl = 1 (SERVICE_ERROR_NORMAL)
├── Group = "SCSI Miniport"
├── Tag = 33
├── ImagePath = "System32\drivers\nvme2k.sys"
├── Parameters\
│   └── BusType = 5 (PCI)
└── Enum\
    (populated by PnP manager)
```

## Notes

- **PCI Only**: This driver only supports PCI bus (NVMe is PCIe-based)
- **Boot Device**: Tagged as Tag=33 to load early in boot sequence
- **SCSI Miniport**: Uses NT4 SCSI miniport model (not StorPort, which didn't exist until Windows 2003)
- **HwAdapterState**: NT4 uses `HwAdapterState` for power management instead of the Windows 2000+ `HwAdapterControl`

## Testing

To verify the INF is properly formatted, you can use the NT4 DDK tool:
```
chkinf oemsetup.inf
```

## References

- Windows NT 4.0 DDK Documentation
- sample.inf - Based on the DDK SCSI miniport sample
- NT4 SCSI Miniport Driver Architecture Guide
