#ifndef __NVME_H
#define __NVME_H

//
// NVMe Specification Definitions
// Pure NVMe spec structures, registers, and constants
//

//
// NVMe Register Offsets
//
#define NVME_REG_CAP        0x0000  // Controller Capabilities (8 bytes)
#define NVME_REG_VS         0x0008  // Version (4 bytes)
#define NVME_REG_INTMS      0x000C  // Interrupt Mask Set
#define NVME_REG_INTMC      0x0010  // Interrupt Mask Clear
#define NVME_REG_CC         0x0014  // Controller Configuration
#define NVME_REG_CSTS       0x001C  // Controller Status
#define NVME_REG_AQA        0x0024  // Admin Queue Attributes
#define NVME_REG_ASQ        0x0028  // Admin Submission Queue Base Address (8 bytes)
#define NVME_REG_ACQ        0x0030  // Admin Completion Queue Base Address (8 bytes)

//
// Controller Capabilities Register bits
//
#define NVME_CAP_MQES_MASK  0x0000FFFF  // Maximum Queue Entries Supported (bits 15:0)

//
// Controller Configuration Register bits
//
#define NVME_CC_ENABLE      0x00000001
#define NVME_CC_CSS_NVM     0x00000000
#define NVME_CC_MPS_SHIFT   7
#define NVME_CC_AMS_RR      0x00000000
#define NVME_CC_SHN_NONE    0x00000000
#define NVME_CC_SHN_NORMAL  0x00004000  // Normal shutdown notification (bits 15:14 = 01b)
#define NVME_CC_SHN_ABRUPT  0x00008000  // Abrupt shutdown notification (bits 15:14 = 10b)
#define NVME_CC_SHN_MASK    0x0000C000  // Shutdown notification mask
#define NVME_CC_IOSQES      0x00060000  // I/O Submission Queue Entry Size (64 bytes = 6)
#define NVME_CC_IOCQES      0x00400000  // I/O Completion Queue Entry Size (16 bytes = 4)

//
// Controller Status Register bits
//
#define NVME_CSTS_RDY       0x00000001
#define NVME_CSTS_CFS       0x00000002  // Controller Fatal Status
#define NVME_CSTS_SHST_MASK 0x0000000C  // Shutdown Status mask (bits 3:2)
#define NVME_CSTS_SHST_NORMAL 0x00000000  // Normal operation (no shutdown)
#define NVME_CSTS_SHST_OCCURRING 0x00000004  // Shutdown processing occurring (bits 3:2 = 01b)
#define NVME_CSTS_SHST_COMPLETE 0x00000008  // Shutdown processing complete (bits 3:2 = 10b)

//
// NVMe Admin Command Opcodes
//
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_GET_LOG_PAGE 0x02
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_ABORT        0x08
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_GET_FEATURES 0x0A

//
// NVMe I/O Command Opcodes
//
#define NVME_CMD_FLUSH          0x00
#define NVME_CMD_WRITE          0x01
#define NVME_CMD_READ           0x02
#define NVME_CMD_COMPARE        0x05
#define NVME_CMD_ZERO           0x08
#define NVME_CMD_DSM            0x09  // Dataset Management (TRIM/UNMAP)
#define NVME_CMD_VERIFY         0x0C

//
// NVMe Identify CNS values
//
#define NVME_CNS_NAMESPACE    0x00
#define NVME_CNS_CONTROLLER   0x01

//
// NVMe Log Page Identifiers
//
#define NVME_LOG_PAGE_ERROR_INFO        0x01
#define NVME_LOG_PAGE_SMART_HEALTH      0x02
#define NVME_LOG_PAGE_FW_SLOT_INFO      0x03

//
// NVMe Status Codes (Status Code field, bits 7:1 of Status Word DW3[15:0])
// Per NVMe 1.0e specification, Figure 38
//

// Generic Command Status (Status Code Type = 0x0)
#define NVME_SC_SUCCESS                     0x00  // Successful Completion
#define NVME_SC_INVALID_OPCODE              0x01  // Invalid Command Opcode
#define NVME_SC_INVALID_FIELD               0x02  // Invalid Field in Command
#define NVME_SC_CMDID_CONFLICT              0x03  // Command ID Conflict
#define NVME_SC_DATA_XFER_ERROR             0x04  // Data Transfer Error
#define NVME_SC_POWER_LOSS                  0x05  // Commands Aborted due to Power Loss Notification
#define NVME_SC_INTERNAL                    0x06  // Internal Device Error
#define NVME_SC_ABORT_REQ                   0x07  // Command Abort Requested
#define NVME_SC_ABORT_QUEUE                 0x08  // Command Aborted due to SQ Deletion
#define NVME_SC_FUSED_FAIL                  0x09  // Command Aborted due to Failed Fused Command
#define NVME_SC_FUSED_MISSING               0x0A  // Command Aborted due to Missing Fused Command
#define NVME_SC_INVALID_NS                  0x0B  // Invalid Namespace or Format
#define NVME_SC_CMD_SEQ_ERROR               0x0C  // Command Sequence Error
#define NVME_SC_INVALID_SGL_SEG_DESC        0x0D  // Invalid SGL Segment Descriptor (NVMe 1.1+)
#define NVME_SC_INVALID_NUM_SGL_DESC        0x0E  // Invalid Number of SGL Descriptors (NVMe 1.1+)
#define NVME_SC_DATA_SGL_LEN_INVALID        0x0F  // Data SGL Length Invalid (NVMe 1.1+)
#define NVME_SC_METADATA_SGL_LEN_INVALID    0x10  // Metadata SGL Length Invalid (NVMe 1.1+)
#define NVME_SC_SGL_DESC_TYPE_INVALID       0x11  // SGL Descriptor Type Invalid (NVMe 1.1+)

// Command Specific Status (Status Code Type = 0x0)
#define NVME_SC_LBA_RANGE                   0x80  // LBA Out of Range
#define NVME_SC_CAP_EXCEEDED                0x81  // Capacity Exceeded
#define NVME_SC_NS_NOT_READY                0x82  // Namespace Not Ready
#define NVME_SC_RESERVATION_CONFLICT        0x83  // Reservation Conflict (NVMe 1.1+)

// Media Errors (Status Code Type = 0x2)
#define NVME_SC_WRITE_FAULT                 0x80  // Write Fault
#define NVME_SC_READ_ERROR                  0x81  // Unrecovered Read Error
#define NVME_SC_GUARD_CHECK                 0x82  // End-to-end Guard Check Error
#define NVME_SC_APPTAG_CHECK                0x83  // End-to-end Application Tag Check Error
#define NVME_SC_REFTAG_CHECK                0x84  // End-to-end Reference Tag Check Error
#define NVME_SC_COMPARE_FAILED              0x85  // Compare Failure
#define NVME_SC_ACCESS_DENIED               0x86  // Access Denied

//
// Queue Flags (for CDW11 in Create I/O Queue commands)
//
#define NVME_QUEUE_PHYS_CONTIG  0x0001  // Bit 0: PC (Physically Contiguous)
#define NVME_QUEUE_IRQ_ENABLED  0x0002  // Bit 1: IEN (Interrupts Enabled)

//
// Command Dword 0 fields
//
#define NVME_CMD_PRP            0x00
#define NVME_CMD_SGL            0x40

//
// Queue sizes and scatter-gather limits
//
#define NVME_SQ_ENTRY_SIZE      64      // Submission Queue Entry size
#define NVME_CQ_ENTRY_SIZE      16      // Completion Queue Entry size

//
// NVMe Command Dword 0 structure
//
typedef union _NVME_CDW0 {
    struct {
        UCHAR Opcode;           // Bits 7:0 - Command Opcode
        UCHAR Flags;            // Bits 15:8 - Fused Operation (bit 0-1), Reserved (2-5), PSDT (6-7)
        USHORT CommandId;       // Bits 31:16 - Command Identifier
    } Fields;
    ULONG AsUlong;
} NVME_CDW0, *PNVME_CDW0;

//
// NVMe Submission Queue Entry
//
typedef struct _NVME_COMMAND {
    NVME_CDW0 CDW0;     // Command Dword 0 (Opcode and flags)
    ULONG NSID;         // Namespace ID
    ULONG CDW2;
    ULONG CDW3;
    ULONGLONG MPTR;     // Metadata Pointer
    ULONGLONG PRP1;     // Physical Region Page 1
    ULONGLONG PRP2;     // Physical Region Page 2
    ULONG CDW10;
    ULONG CDW11;
    ULONG CDW12;
    ULONG CDW13;
    ULONG CDW14;
    ULONG CDW15;
} NVME_COMMAND, *PNVME_COMMAND;

//
// NVMe Completion Queue Entry
//
typedef struct _NVME_COMPLETION {
    ULONG DW0;          // Command-specific
    ULONG DW1;          // Reserved
    USHORT SQHead;      // Submission Queue Head
    USHORT SQID;        // Submission Queue ID
    USHORT CID;         // Command ID
    USHORT Status;      // Status and phase
} NVME_COMPLETION, *PNVME_COMPLETION;

//
// NVMe Identify Controller Structure (partial)
//
typedef struct _NVME_IDENTIFY_CONTROLLER {
    USHORT VendorId;                // Offset 0
    USHORT SubsystemVendorId;       // Offset 2
    UCHAR SerialNumber[20];         // Offset 4
    UCHAR ModelNumber[40];          // Offset 24
    UCHAR FirmwareRevision[8];      // Offset 64
    UCHAR RecommendedArbitrationBurst; // Offset 72 (RAB)
    UCHAR Ieee[3];                  // Offset 73-75 (IEEE OUI)
    UCHAR Cmic;                     // Offset 76
    UCHAR MaxDataTransferSize;      // Offset 77 (MDTS - as a power of 2, in units of minimum page size)
    UCHAR Reserved1[438];           // Offset 78-515
    ULONG NumberOfNamespaces;       // Offset 516 (NN field)
    UCHAR Reserved2[3576];          // Offset 520-4095 (rest of 4096 byte structure)
} NVME_IDENTIFY_CONTROLLER, *PNVME_IDENTIFY_CONTROLLER;

//
// NVMe LBA Format Structure (used in Identify Namespace)
//
typedef struct _NVME_LBA_FORMAT {
    USHORT MetadataSize;        // Bits 15:0 - Metadata Size
    UCHAR LbaDataSize;          // Bits 23:16 - LBA Data Size (as a power of 2, 2^n)
    UCHAR RelativePerformance;  // Bits 31:24 - Relative Performance
} NVME_LBA_FORMAT, *PNVME_LBA_FORMAT;

//
// NVMe Identify Namespace Structure (partial)
//
typedef struct _NVME_IDENTIFY_NAMESPACE {
    ULONGLONG NamespaceSize;        // Offset 0: NSZE - Namespace Size (in logical blocks)
    ULONGLONG NamespaceCapacity;    // Offset 8: NCAP - Namespace Capacity
    ULONGLONG NamespaceUtilization; // Offset 16: NUSE - Namespace Utilization
    UCHAR NamespaceFeatures;        // Offset 24: NSFEAT
    UCHAR NumberOfLbaFormats;       // Offset 25: NLBAF
    UCHAR FormattedLbaSize;         // Offset 26: FLBAS - Formatted LBA Size
    UCHAR MetadataCapabilities;     // Offset 27: MC
    UCHAR Reserved1[100];           // Offset 28-127
    UCHAR Nguid[16];                // Offset 104-119: NGUID
    UCHAR Eui64[8];                 // Offset 120-127: EUI64
    NVME_LBA_FORMAT LbaFormats[16]; // Offset 128-191: LBAF0-LBAF15
    UCHAR Reserved2[3904];          // Offset 192-4095
} NVME_IDENTIFY_NAMESPACE, *PNVME_IDENTIFY_NAMESPACE;

#endif // __NVME_H
