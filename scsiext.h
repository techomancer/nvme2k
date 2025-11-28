//
// SCSI Extension Header for NVMe Driver
// Contains SCSI/ATA translation structures and constants
//

#ifndef _SCSIEXT_H_
#define _SCSIEXT_H_

//
// SCSI Command Operation Codes
//
#define SCSIOP_ATA_PASSTHROUGH16        0x85
#define SCSIOP_ATA_PASSTHROUGH12        0xA1
#define SCSIOP_UNMAP                    0x42  // UNMAP command

//
// SCSI UNMAP Constants
//
#define UNMAP_BLOCK_DESCRIPTOR_SIZE     16    // Size of each UNMAP block descriptor (bytes)
#define UNMAP_LIST_HEADER_SIZE          8     // Size of UNMAP parameter list header (bytes)
#define UNMAP_MAX_DESCRIPTORS           256   // Maximum descriptors per UNMAP command (typical limit)

//
// SCSI Log Sense Page Codes (commonly used)
//
#define SCSI_LOG_PAGE_SUPPORTED_PAGES   0x00
#define SCSI_LOG_PAGE_WRITE_ERROR       0x02
#define SCSI_LOG_PAGE_READ_ERROR        0x03
#define SCSI_LOG_PAGE_VERIFY_ERROR      0x05
#define SCSI_LOG_PAGE_TEMPERATURE       0x0D
#define SCSI_LOG_PAGE_START_STOP        0x0E
#define SCSI_LOG_PAGE_SELF_TEST         0x10
#define SCSI_LOG_PAGE_INFORMATIONAL     0x2F

// These are missing from NT4 DDK
#ifndef MODE_PAGE_POWER_CONDITION
#define MODE_PAGE_POWER_CONDITION       0x1A
#endif

#ifndef MODE_PAGE_FAULT_REPORTING
#define MODE_PAGE_FAULT_REPORTING       0x1C
#endif

#ifndef IOCTL_SCSI_FREE_DUMP_POINTERS
#define IOCTL_SCSI_FREE_DUMP_POINTERS   CTL_CODE(IOCTL_SCSI_BASE, 0x0409, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_IDE_PASS_THROUGH
#define IOCTL_IDE_PASS_THROUGH          CTL_CODE(IOCTL_SCSI_BASE, 0x040a, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif

#ifndef IOCTL_SCSI_MINIPORT_SMART_VERSION
 
#define _FILE_DEVICE_SCSI 0x001B0000

#define IOCTL_SCSI_MINIPORT_SMART_VERSION               (_FILE_DEVICE_SCSI | 0x0500)
#define IOCTL_SCSI_MINIPORT_IDENTIFY                    (_FILE_DEVICE_SCSI | 0x0501)
#define IOCTL_SCSI_MINIPORT_READ_SMART_ATTRIBS          (_FILE_DEVICE_SCSI | 0x0502)
#define IOCTL_SCSI_MINIPORT_READ_SMART_THRESHOLDS       (_FILE_DEVICE_SCSI | 0x0503)
#define IOCTL_SCSI_MINIPORT_ENABLE_SMART                (_FILE_DEVICE_SCSI | 0x0504)
#define IOCTL_SCSI_MINIPORT_DISABLE_SMART               (_FILE_DEVICE_SCSI | 0x0505)
#define IOCTL_SCSI_MINIPORT_RETURN_STATUS               (_FILE_DEVICE_SCSI | 0x0506)
#define IOCTL_SCSI_MINIPORT_ENABLE_DISABLE_AUTOSAVE     (_FILE_DEVICE_SCSI | 0x0507)
#define IOCTL_SCSI_MINIPORT_SAVE_ATTRIBUTE_VALUES       (_FILE_DEVICE_SCSI | 0x0508)
#define IOCTL_SCSI_MINIPORT_EXECUTE_OFFLINE_DIAGS       (_FILE_DEVICE_SCSI | 0x0509)
#define IOCTL_SCSI_MINIPORT_ENABLE_DISABLE_AUTO_OFFLINE (_FILE_DEVICE_SCSI | 0x050A)
#define IOCTL_SCSI_MINIPORT_READ_SMART_LOG              (_FILE_DEVICE_SCSI | 0x050B)
#define IOCTL_SCSI_MINIPORT_WRITE_SMART_LOG             (_FILE_DEVICE_SCSI | 0x050C)
#endif

//
// READ DEFECT DATA (10) CDB structure
// Per SBC-3 section 5.7
//
typedef struct _CDB_READ_DEFECT_DATA {
    UCHAR OperationCode;        // Byte 0: 0x37
    UCHAR Reserved1;            // Byte 1: Reserved (bits 7-5), LUN (bits 2-0)
    UCHAR FormatByte;           // Byte 2: bit 4=Req_plist, bit 3=Req_glist, bits 2-0=format
    UCHAR Reserved3[4];         // Bytes 3-6: Reserved
    UCHAR AllocationLength[2];  // Bytes 7-8: Allocation length (big-endian)
    UCHAR Control;              // Byte 9: Control byte
                                //   Bit 7-6: Vendor specific
                                //   Bit 5-4: Reserved
                                //   Bit 3: NACA (Normal Auto Contingent Allegiance)
                                //   Bit 2: Reserved
                                //   Bit 1-0: Reserved/Link (obsolete)
} CDB_READ_DEFECT_DATA, *PCDB_READ_DEFECT_DATA;

//
// SCSI UNMAP CDB structure
// Per SBC-3 section 5.30
//
typedef struct _CDB_UNMAP {
    UCHAR OperationCode;        // Byte 0: 0x42 (SCSIOP_UNMAP)
    UCHAR Anchor : 1;           // Byte 1, Bit 0: Anchor (hint that LBAs won't be written soon)
    UCHAR Reserved1 : 7;        // Byte 1, Bits 7-1: Reserved
    UCHAR Reserved2[4];         // Bytes 2-5: Reserved
    UCHAR GroupNumber : 5;      // Byte 6, Bits 4-0: Group Number
    UCHAR Reserved3 : 3;        // Byte 6, Bits 7-5: Reserved
    UCHAR AllocationLength[2];  // Bytes 7-8: Parameter list length (big-endian)
    UCHAR Control;              // Byte 9: Control byte
} CDB_UNMAP, *PCDB_UNMAP;

//
// SCSI UNMAP Parameter List Header
// Per SBC-3 section 5.30.1
//
#pragma pack(push, 1)
typedef struct _UNMAP_LIST_HEADER {
    UCHAR DataLength[2];        // Bytes 0-1: UNMAP data length (big-endian, excludes these 2 bytes)
    UCHAR BlockDescrDataLength[2]; // Bytes 2-3: UNMAP block descriptor data length (big-endian)
    UCHAR Reserved[4];          // Bytes 4-7: Reserved
} UNMAP_LIST_HEADER, *PUNMAP_LIST_HEADER;

//
// SCSI UNMAP Block Descriptor
// Per SBC-3 section 5.30.1
// Each descriptor specifies a contiguous range of LBAs to unmap
//
typedef struct _UNMAP_BLOCK_DESCRIPTOR {
    UCHAR StartingLba[8];       // Bytes 0-7: Starting LBA (big-endian, 64-bit)
    UCHAR LbaCount[4];          // Bytes 8-11: Number of logical blocks (big-endian, 32-bit)
    UCHAR Reserved[4];          // Bytes 12-15: Reserved
} UNMAP_BLOCK_DESCRIPTOR, *PUNMAP_BLOCK_DESCRIPTOR;

#pragma pack(pop)

//
// ATA Commands for SMART (used in IOCTL translation)
//
#define ATA_SMART_CMD                   0xB0
// smart actions for ATA_SMART_CMD
#define ATA_SMART_READ_DATA             0xD0
#define ATA_SMART_READ_THRESHOLDS       0xD1
#define ATA_SMART_ENABLE                0xD8 // This is "SMART ENABLE OPERATIONS"
#define ATA_SMART_DISABLE               0xD9
#define ATA_SMART_RETURN_STATUS         0xDA
#define ATA_SMART_AUTOSAVE              0xD2
#define ATA_SMART_READ_LOG              0xD5
#define ATA_SMART_WRITE_LOG             0xD6
#define ATA_SMART_ENABLE_OPERATIONS     0x4F
// other ATA commands
#define ATA_IDENTIFY_DEVICE             0xEC
#define ATA_SMART_READ_LOG_DMA_EXT      0x57 // SMART log, bit 0
#define ATA_SMART_READ_LOG_EXT          0x58
#define ATA_SMART_WRITE_LOG_EXT         0x5B

// SCSI ops not defined in win2k scsi.h
#define SCSIOP_RESERVE6                 0x16
#define SCSIOP_RELEASE6                 0x17
#define SCSIOP_RESERVE10                0x56
#define SCSIOP_RELEASE10                0x57
#ifndef SCSIOP_READ_DEFECT_DATA10
#define SCSIOP_READ_DEFECT_DATA10       0x37
#endif

//
// SMART signature values (cylinder registers)
//
#define SMART_CYL_LOW                   0x4F
#define SMART_CYL_HI                    0xC2

//
// Windows 2000 ATA Pass-through structures for SMART support
// (normally defined in ntdddisk.h/winioctl.h)
//

#ifndef CAP_ATA_ID_CMD
// these are not there on nt4?
typedef struct _GETVERSIONINPARAMS {
        UCHAR    bVersion;               // Binary driver version.
        UCHAR    bRevision;              // Binary driver revision.
        UCHAR    bReserved;              // Not used.
        UCHAR    bIDEDeviceMap;          // Bit map of IDE devices.
        ULONG   fCapabilities;          // Bit mask of driver capabilities.
        ULONG   dwReserved[4];          // For future use.
} GETVERSIONINPARAMS, *PGETVERSIONINPARAMS, *LPGETVERSIONINPARAMS;

//
// Bits returned in the fCapabilities member of GETVERSIONINPARAMS
//

#define CAP_ATA_ID_CMD          1       // ATA ID command supported
#define CAP_ATAPI_ID_CMD        2       // ATAPI ID command supported
#define CAP_SMART_CMD           4       // SMART commannds supported


// ATA/IDE register structure
typedef struct _IDEREGS {
    UCHAR bFeaturesReg;       // Feature register (SMART subcommand)
    UCHAR bSectorCountReg;    // Sector count
    UCHAR bSectorNumberReg;   // Sector number (LBA low)
    UCHAR bCylLowReg;         // Cylinder low (0x4F for SMART)
    UCHAR bCylHighReg;        // Cylinder high (0xC2 for SMART)
    UCHAR bDriveHeadReg;      // Drive/head
    UCHAR bCommandReg;        // Command register (0xB0 for SMART)
    UCHAR bReserved;
} IDEREGS, *PIDEREGS, *LPIDEREGS;

// Input parameters for IOCTL_SCSI_MINIPORT
typedef struct _SENDCMDINPARAMS {
    ULONG cBufferSize;        // Buffer size in bytes
    IDEREGS irDriveRegs;      // IDE register values
    UCHAR bDriveNumber;       // Physical drive number
    UCHAR bReserved[3];
#if 0
// introduced after XP
    ULONG dwReserved[4];
#endif
    UCHAR bBuffer[1];         // Variable length buffer for input data
} SENDCMDINPARAMS, *PSENDCMDINPARAMS, *LPSENDCMDINPARAMS;

// Driver status
typedef struct _DRIVERSTATUS {
    UCHAR bDriverError;       // Error code from driver
    UCHAR bIDEError;          // Error register from IDE controller
    UCHAR bReserved[2];
    ULONG dwReserved[2];
} DRIVERSTATUS, *PDRIVERSTATUS, *LPDRIVERSTATUS;

// Output parameters for IOCTL_SCSI_MINIPORT
typedef struct _SENDCMDOUTPARAMS {
    ULONG cBufferSize;            // Buffer size in bytes
    DRIVERSTATUS DriverStatus;    // Driver status
    UCHAR bBuffer[1];             // Variable length buffer (512 bytes for SMART data)
} SENDCMDOUTPARAMS, *PSENDCMDOUTPARAMS, *LPSENDCMDOUTPARAMS;
#endif /* CAP_ATA_ID_CMD */

// NVMe SMART/Health Information Log (Log Page 0x02)
// Note: This structure matches the NVMe spec byte layout exactly
// Use byte arrays for unaligned fields to avoid Alpha alignment issues
#pragma pack(push, 1)
typedef struct _NVME_SMART_INFO {
    UCHAR CriticalWarning;           // Byte 0: Critical warning flags
    UCHAR Temperature[2];            // Bytes 1-2: Composite temperature (Kelvin, little-endian)
    UCHAR AvailableSpare;            // Byte 3: Available spare (%)
    UCHAR AvailableSpareThreshold;   // Byte 4: Available spare threshold (%)
    UCHAR PercentageUsed;            // Byte 5: Percentage used (%)
    UCHAR Reserved1[26];             // Bytes 6-31: Reserved
    UCHAR DataUnitsRead[16];         // Bytes 32-47: Data units read (128-bit, little-endian)
    UCHAR DataUnitsWritten[16];      // Bytes 48-63: Data units written (128-bit)
    UCHAR HostReadCommands[16];      // Bytes 64-79: Host read commands (128-bit)
    UCHAR HostWriteCommands[16];     // Bytes 80-95: Host write commands (128-bit)
    UCHAR ControllerBusyTime[16];    // Bytes 96-111: Controller busy time (128-bit)
    UCHAR PowerCycles[16];           // Bytes 112-127: Power cycles (128-bit)
    UCHAR PowerOnHours[16];          // Bytes 128-143: Power on hours (128-bit)
    UCHAR UnsafeShutdowns[16];       // Bytes 144-159: Unsafe shutdowns (128-bit)
    UCHAR MediaErrors[16];           // Bytes 160-175: Media errors (128-bit)
    UCHAR NumErrorLogEntries[16];    // Bytes 176-191: Number of error log entries (128-bit)
    UCHAR WarningTempTime[4];        // Bytes 192-195: Warning composite temp time
    UCHAR CriticalTempTime[4];       // Bytes 196-199: Critical composite temp time
    UCHAR TempSensor[16];            // Bytes 200-215: Temperature sensors 1-8 (8 x 16-bit)
    UCHAR Reserved2[296];            // Bytes 216-511: Reserved
} NVME_SMART_INFO, *PNVME_SMART_INFO;
#pragma pack(pop)

// ATA SMART Attribute (12 bytes each)
// Use byte arrays for potentially unaligned fields (Alpha compatibility)
#pragma pack(push, 1)
typedef struct _ATA_SMART_ATTRIBUTE {
    UCHAR Id;                        // Attribute ID
    UCHAR Flags[2];                  // Status flags (little-endian USHORT)
    UCHAR CurrentValue;              // Current normalized value (100 = new, 1 = worn)
    UCHAR WorstValue;                // Worst value seen
    UCHAR RawValue[6];               // Raw value (vendor specific format)
    UCHAR Reserved;
} ATA_SMART_ATTRIBUTE, *PATA_SMART_ATTRIBUTE;

// ATA SMART Data Structure (512 bytes)
typedef struct _ATA_SMART_DATA {
    UCHAR Version[2];                // Version number (little-endian USHORT)
    ATA_SMART_ATTRIBUTE Attributes[30]; // 30 attributes * 12 bytes = 360 bytes
    UCHAR OfflineDataCollectionStatus;
    UCHAR SelfTestExecutionStatus;
    UCHAR TotalTimeToCompleteOfflineDataCollection[2]; // little-endian USHORT
    UCHAR VendorSpecific1;
    UCHAR OfflineDataCollectionCapability;
    UCHAR SmartCapability[2];        // little-endian USHORT
    UCHAR ErrorLoggingCapability;
    UCHAR VendorSpecific2;
    UCHAR ShortSelfTestPollingTime;
    UCHAR ExtendedSelfTestPollingTime;
    UCHAR ConveyanceSelfTestPollingTime;
    UCHAR ExtendedSelfTestPollingTimeWord[2]; // little-endian USHORT
    UCHAR Reserved1[9];
    UCHAR VendorSpecific3[125];
    UCHAR Checksum;                  // Checksum of bytes 0-511 (sum = 0)
} ATA_SMART_DATA, *PATA_SMART_DATA;

//
// ATA IDENTIFY DEVICE Data Structure (512 bytes)
// This is the response to the ATA IDENTIFY DEVICE command (0xEC)
// We'll emulate an LBA-capable IDE drive
//
typedef struct _ATA_IDENTIFY_DEVICE_STRUCT {
    UCHAR GeneralConfiguration[2];       // Word 0: General configuration (little-endian)
    UCHAR NumCylinders[2];               // Word 1: Number of logical cylinders
    UCHAR Reserved1[2];                  // Word 2: Specific configuration
    UCHAR NumHeads[2];                   // Word 3: Number of logical heads
    UCHAR Reserved2[4];                  // Word 4-5: Retired
    UCHAR NumSectorsPerTrack[2];         // Word 6: Number of logical sectors per track
    UCHAR Reserved3[6];                  // Word 7-9: Reserved
    UCHAR SerialNumber[20];              // Word 10-19: Serial number (ASCII, padded with spaces)
    UCHAR Reserved4[6];                  // Word 20-22: Retired
    UCHAR FirmwareRevision[8];           // Word 23-26: Firmware revision (ASCII, padded with spaces)
    UCHAR ModelNumber[40];               // Word 27-46: Model number (ASCII, padded with spaces)
    UCHAR MaximumBlockTransfer[2];       // Word 47: Maximum sectors per interrupt
    UCHAR Reserved5[2];                  // Word 48: Trusted Computing
    UCHAR Capabilities[4];               // Word 49-50: Capabilities (LBA, DMA, etc.)
    UCHAR Reserved6[4];                  // Word 51-52: Obsolete
    UCHAR ValidFields[2];                // Word 53: Valid translation fields
    UCHAR CurrentCylinders[2];           // Word 54: Current logical cylinders
    UCHAR CurrentHeads[2];               // Word 55: Current logical heads
    UCHAR CurrentSectorsPerTrack[2];     // Word 56: Current logical sectors per track
    UCHAR CurrentCapacityLow[2];         // Word 57: Current capacity low word
    UCHAR CurrentCapacityHigh[2];        // Word 58: Current capacity high word
    UCHAR MultipleSectorSetting[2];      // Word 59: Multiple sector setting
    UCHAR TotalAddressableSectors[4];    // Word 60-61: Total user addressable sectors (LBA-28)
    UCHAR Reserved7[2];                  // Word 62: Obsolete
    UCHAR MultiwordDmaMode[2];           // Word 63: Multiword DMA modes
    UCHAR PioModesSupported[2];          // Word 64: PIO modes supported
    UCHAR MinMdmaCycleTime[2];           // Word 65: Minimum MDMA transfer cycle time
    UCHAR RecommendedMdmaCycleTime[2];   // Word 66: Recommended MDMA transfer cycle time
    UCHAR MinPioCycleTime[2];            // Word 67: Minimum PIO transfer cycle time
    UCHAR MinPioCycleTimeIordy[2];       // Word 68: Minimum PIO cycle time with IORDY
    UCHAR Reserved8[12];                 // Word 69-74: Reserved
    UCHAR QueueDepth[2];                 // Word 75: Queue depth
    UCHAR Reserved9[8];                  // Word 76-79: Reserved for SATA
    UCHAR MajorVersion[2];               // Word 80: Major version number
    UCHAR MinorVersion[2];               // Word 81: Minor version number
    UCHAR CommandSetSupported1[2];       // Word 82: Command sets supported
    UCHAR CommandSetSupported2[2];       // Word 83: Command sets supported
    UCHAR CommandSetSupportedExt[2];     // Word 84: Command sets supported extended
    UCHAR CommandSetEnabled1[2];         // Word 85: Command sets enabled
    UCHAR CommandSetEnabled2[2];         // Word 86: Command sets enabled
    UCHAR CommandSetDefault[2];          // Word 87: Command set default
    UCHAR UltraDmaMode[2];               // Word 88: Ultra DMA modes
    UCHAR TimeForSecurityErase[2];       // Word 89: Time required for security erase
    UCHAR TimeForEnhancedErase[2];       // Word 90: Time required for enhanced erase
    UCHAR CurrentPowerManagement[2];     // Word 91: Current advanced power management
    UCHAR MasterPasswordRevision[2];     // Word 92: Master password revision
    UCHAR HardwareResetResult[2];        // Word 93: Hardware reset result
    UCHAR Reserved10[12];                // Word 94-99: Reserved
    UCHAR TotalAddressableSectors48[8];  // Word 100-103: Total addressable sectors (LBA-48)
    UCHAR Reserved11a[8];                // Word 104-107: Reserved
    UCHAR WorldWideName[8];              // Word 108-111: World Wide Name (WWN) - 64-bit identifier
    UCHAR Reserved11b[28];               // Word 112-125: Reserved
    UCHAR RemovableMediaStatus[2];       // Word 126: Removable media status notification
    UCHAR SecurityStatus[2];             // Word 127: Security status
    UCHAR VendorSpecific[62];            // Word 128-158: Vendor specific
    UCHAR Reserved12[116];               // Word 159-216: Reserved
    UCHAR NominalMediaRotationRate[2];   // Word 217: Nominal Media Rotation Rate (1=SSD, 0x0401-0xFFFE=RPM)
    UCHAR Reserved13[76];                // Word 218-255: Reserved
} ATA_IDENTIFY_DEVICE_STRUCT, *PATA_IDENTIFY_DEVICE_STRUCT;

#pragma pack(pop)

//
// SCSI Log Page Structures
//
#pragma pack(push, 1)

// SCSI Log Page Header (4 bytes)
typedef struct _SCSI_LOG_PAGE_HEADER {
    UCHAR PageCode;              // Bits 5:0 = Page Code, Bits 7:6 = Page Control
    UCHAR Reserved;
    UCHAR PageLength[2];         // Big-endian: Total length of parameters (not including header)
} SCSI_LOG_PAGE_HEADER, *PSCSI_LOG_PAGE_HEADER;

// SCSI Log Parameter Header (4 bytes)
typedef struct _SCSI_LOG_PARAMETER {
    UCHAR ParameterCode[2];      // Big-endian: Parameter code
    UCHAR ControlByte;           // Bits: DU, DS, TSD, ETC, TMC, LBIN, LP
    UCHAR ParameterLength;       // Length of parameter value (bytes following this header)
} SCSI_LOG_PARAMETER, *PSCSI_LOG_PARAMETER;

//
// SAT (SCSI/ATA Translation) ATA PASS-THROUGH CDB structures
//
typedef struct _SAT_PASSTHROUGH_16 {
    UCHAR OperationCode;     // 0x85
    UCHAR Protocol : 4;      // Transfer protocol (4=PIO Data-In for SMART)
    UCHAR Multiple : 3;      // Multiple count
    UCHAR Extend : 1;        // 1=48-bit command
    UCHAR Offline : 2;       // Offline control
    UCHAR CkCond : 1;        // Check condition
    UCHAR TType : 1;         // Transfer type
    UCHAR TDir : 1;          // Transfer direction (1=device to host)
    UCHAR ByteBlock : 1;     // Byte/block
    UCHAR TLength : 2;       // Transfer length
    UCHAR Features15_8;      // Features register (15:8)
    UCHAR Features7_0;       // Features register (7:0)
    UCHAR SectorCount15_8;   // Sector count (15:8)
    UCHAR SectorCount7_0;    // Sector count (7:0)
    UCHAR LbaLow15_8;        // LBA Low (15:8)
    UCHAR LbaLow7_0;         // LBA Low (7:0)
    UCHAR LbaMid15_8;        // LBA Mid (15:8)
    UCHAR LbaMid7_0;         // LBA Mid (7:0)
    UCHAR LbaHigh15_8;       // LBA High (15:8)
    UCHAR LbaHigh7_0;        // LBA High (7:0)
    UCHAR Device;            // Device register
    UCHAR Command;           // ATA Command
    UCHAR Control;           // Control
} SAT_PASSTHROUGH_16, *PSAT_PASSTHROUGH_16;

typedef struct _SAT_PASSTHROUGH_12 {
    UCHAR OperationCode;     // 0xA1
    UCHAR Protocol : 4;      // Transfer protocol (4=PIO Data-In for SMART)
    UCHAR Multiple : 3;      // Multiple count
    UCHAR Extend : 1;        // 1=48-bit command
    UCHAR Offline : 2;       // Offline control
    UCHAR CkCond : 1;        // Check condition
    UCHAR TType : 1;         // Transfer type
    UCHAR TDir : 1;          // Transfer direction (1=device to host)
    UCHAR ByteBlock : 1;     // Byte/block
    UCHAR TLength : 2;       // Transfer length
    UCHAR Features;          // Features register (7:0)
    UCHAR SectorCount;       // Sector count (7:0)
    UCHAR LbaLow;            // LBA Low (7:0)
    UCHAR LbaMid;            // LBA Mid (7:0)
    UCHAR LbaHigh;           // LBA High (7:0)
    UCHAR Device;            // Device register
    UCHAR Command;           // ATA Command
    UCHAR Reserved;          // Reserved
    UCHAR Control;           // Control
} SAT_PASSTHROUGH_12, *PSAT_PASSTHROUGH_12;

// SAT Protocol values
#define SAT_PROTOCOL_HARD_RESET         0
#define SAT_PROTOCOL_SRST               1
#define SAT_PROTOCOL_NON_DATA           3
#define SAT_PROTOCOL_PIO_DATA_IN        4
#define SAT_PROTOCOL_PIO_DATA_OUT       5
#define SAT_PROTOCOL_DMA                6
#define SAT_PROTOCOL_DMA_QUEUED         7
#define SAT_PROTOCOL_DEVICE_DIAGNOSTIC  8
#define SAT_PROTOCOL_DEVICE_RESET       9
#define SAT_PROTOCOL_UDMA_DATA_IN       10
#define SAT_PROTOCOL_UDMA_DATA_OUT      11
#define SAT_PROTOCOL_FPDMA              12
#define SAT_PROTOCOL_RETURN_RESPONSE    15

#pragma pack(pop)

// Common ATA SMART Attribute IDs
#define ATA_SMART_ATTR_READ_ERROR_RATE          1
#define ATA_SMART_ATTR_THROUGHPUT_PERFORMANCE   2
#define ATA_SMART_ATTR_SPIN_UP_TIME             3
#define ATA_SMART_ATTR_START_STOP_COUNT         4
#define ATA_SMART_ATTR_REALLOCATED_SECTOR_COUNT 5
#define ATA_SMART_ATTR_SEEK_ERROR_RATE          7
#define ATA_SMART_ATTR_SEEK_TIME_PERFORMANCE    8
#define ATA_SMART_ATTR_POWER_ON_HOURS           9
#define ATA_SMART_ATTR_SPIN_RETRY_COUNT         10
#define ATA_SMART_ATTR_RECALIBRATION_RETRIES    11
#define ATA_SMART_ATTR_POWER_CYCLE_COUNT        12
#define ATA_SMART_ATTR_AIRFLOW_TEMPERATURE      190
#define ATA_SMART_ATTR_TEMPERATURE              194
#define ATA_SMART_ATTR_REALLOCATED_EVENT_COUNT  196
#define ATA_SMART_ATTR_CURRENT_PENDING_SECTORS  197
#define ATA_SMART_ATTR_OFFLINE_UNCORRECTABLE    198
#define ATA_SMART_ATTR_UDMA_CRC_ERROR_COUNT     199
#define ATA_SMART_ATTR_WEAR_LEVELING_COUNT      173
#define ATA_SMART_ATTR_PROGRAM_FAIL_COUNT       181
#define ATA_SMART_ATTR_ERASE_FAIL_COUNT         182
#define ATA_SMART_ATTR_REPORTED_UNCORRECTABLE   187
#define ATA_SMART_ATTR_COMMAND_TIMEOUT          188
#define ATA_SMART_ATTR_HIGH_FLY_WRITES          189
#define ATA_SMART_ATTR_TOTAL_LBA_WRITTEN        241
#define ATA_SMART_ATTR_TOTAL_LBA_READ           242

#endif // _SCSIEXT_H_
