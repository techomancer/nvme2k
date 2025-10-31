//
// NVMe Driver for Windows 2000 - Header File
// Contains all defines, constants, and type definitions
//

#ifndef _NVME2K_H_
#define _NVME2K_H_

#include <miniport.h>
#include <scsi.h>
#include <devioctl.h>
#include <ntddscsi.h>
#include "atomic.h"
#include "nvme.h"
#include "scsiext.h"

#define NVME2K_DBG
// extra spammy logging for NVMe commands
//#define NVME2K_DBG_CMD
//#define NVME2K_DBG_EXTRA
// uncomment to enable debugging and logging
/* for debug messages use
 * ScsiDebugPrint(0, "nvme2k: ...\n");
 * it seems only level 0 messages are displayed, possibly registry changes are needed
 * #define NVME2K_DBG_STATS
 */

//
// Synchronization control - comment out to disable specific locks
//
//#define NVME2K_USE_INTERRUPT_LOCK       // Lock to serialize HwInterrupt on SMP
//#define NVME2K_USE_SUBMISSION_LOCK      // Lock to serialize command submission
//#define NVME2K_USE_COMPLETION_LOCK      // Lock to serialize completion processing

//
// Memory constants
//
#define PAGE_SIZE                           0x1000  // 4KB page size

//
// Uncached memory size calculation:
// - Admin SQ: 4096 bytes (4KB aligned)
// - I/O SQ: 4096 bytes (4KB aligned)
// - Utility buffer / PRP list pool: 40960 bytes (10 pages * 4KB, page-aligned)
// - Admin CQ: 4096 bytes (4KB aligned)
// - I/O CQ: 4096 bytes (4KB aligned)
// Total: ~60KB with alignment
//
#define UNCACHED_EXTENSION_SIZE             (PAGE_SIZE * 16)  // 64KB for safety

//
// NVMe PCI Class Codes
//
#define PCI_CLASS_MASS_STORAGE_CONTROLLER   0x01
#define PCI_SUBCLASS_NON_VOLATILE_MEMORY    0x08
#define PCI_PROGIF_NVME                     0x02

//
// PCI Configuration Space Offsets
//
#define PCI_VENDOR_ID_OFFSET                0x00
#define PCI_DEVICE_ID_OFFSET                0x02
#define PCI_COMMAND_OFFSET                  0x04
#define PCI_STATUS_OFFSET                   0x06
#define PCI_REVISION_ID_OFFSET              0x08
#define PCI_CLASS_CODE_OFFSET               0x09
#define PCI_HEADER_TYPE_OFFSET              0x0E
#define PCI_BASE_ADDRESS_0                  0x10
#define PCI_SUBSYSTEM_VENDOR_ID_OFFSET      0x2C
#define PCI_SUBSYSTEM_ID_OFFSET             0x2E
#define PCI_INTERRUPT_LINE_OFFSET           0x3C
#define PCI_INTERRUPT_PIN_OFFSET            0x3D

//
// PCI Command Register bits
//
#define PCI_ENABLE_IO_SPACE                 0x0001
#define PCI_ENABLE_MEMORY_SPACE             0x0002
#define PCI_ENABLE_BUS_MASTER               0x0004
#define PCI_INTERRUPT_DISABLE               0x0400  // Bit 10: Disable INTx interrupt assertion

//
// Doorbell registers (stride determined by CAP.DSTRD)
// (not defined in nvme.h as it's driver-specific)
//
#define NVME_REG_DBS        0x1000  // Doorbell base

//
// Queue sizes and scatter-gather limits
//
#define NVME_MAX_QUEUE_SIZE     (PAGE_SIZE/NVME_SQ_ENTRY_SIZE)  // Maximum we can fit in a page (64)
                                                                 // Actual size determined by min(NVME_MAX_QUEUE_SIZE, MQES+1)
#define SG_LIST_PAGES           10      // Number of PRP list pages (shared pool, reused across commands)
                                        // Each page holds 512 PRP entries (8 bytes each)
                                        // Max transfer per page: 512 * 4KB = 2MB
                                        // With 10 pages: up to 20MB transfers

//
// NVMe Queue Pair
//
typedef struct _NVME_QUEUE {
    PVOID SubmissionQueue;
    PVOID CompletionQueue;
    PHYSICAL_ADDRESS SubmissionQueuePhys;
    PHYSICAL_ADDRESS CompletionQueuePhys;
    ULONG SubmissionQueueHead;   // Protected by SubmissionLock
    ULONG SubmissionQueueTail;   // Protected by SubmissionLock
    ULONG CompletionQueueHead;   // Monotonic counter, never wraps. Phase = (head >> QueueSizeBits) & 1. Protected by CompletionLock
    ULONG CompletionQueueTail;
    USHORT QueueSizeMask;        // QueueSize - 1, for index masking
    USHORT QueueId;
    USHORT QueueSize;
    UCHAR QueueSizeBits;         // log2(QueueSize), for phase calculation
    UCHAR Reserved;              // Padding for alignment
    ATOMIC SubmissionLock;       // Spinlock for NvmeSubmitCommand (0 = unlocked, 1 = locked)
    ATOMIC CompletionLock;       // Spinlock for completion processing (0 = unlocked, 1 = locked)
} NVME_QUEUE, *PNVME_QUEUE;

// Command ID encoding
// Bit 15: Set for non-tagged request
// Bit 14: Set for ORDERED flush (used with bit 15 clear)
// Bits 0-13: QueueTag (tagged) or sequence number (non-tagged)
#define CID_NON_TAGGED_FLAG 0x8000
#define CID_ORDERED_FLUSH_FLAG 0x4000
#define CID_VALUE_MASK      0x3FFF

//
// SRB Extension - per-request data stored by ScsiPort
//
typedef struct _NVME_SRB_EXTENSION {
    UCHAR PrpListPage;              // Which PRP list page is allocated (0xFF if none)
    UCHAR Reserved[3];              // Padding for alignment
} NVME_SRB_EXTENSION, *PNVME_SRB_EXTENSION;

//
// Admin Command IDs for initialization sequence
// These double as both Command IDs and state tracking
//
#define ADMIN_CID_CREATE_IO_CQ          1
#define ADMIN_CID_CREATE_IO_SQ          2
#define ADMIN_CID_IDENTIFY_CONTROLLER   3
#define ADMIN_CID_IDENTIFY_NAMESPACE    4
#define ADMIN_CID_INIT_COMPLETE         5

//
// Admin Command IDs for post-init operations (must be > ADMIN_CID_INIT_COMPLETE)
//
#define ADMIN_CID_GET_LOG_PAGE          6   // Get Log Page (untagged, only one at a time)
// SG_LIST_PAGES IDs reserved for page index

//
// Admin Command IDs for shutdown sequence (special, non-colliding values)
//
#define ADMIN_CID_SHUTDOWN_DELETE_SQ    0xFFFE
#define ADMIN_CID_SHUTDOWN_DELETE_CQ    0xFFFD

//
// Device extension structure - stores per-adapter data
//
typedef struct _HW_DEVICE_EXTENSION {
    ULONG AdapterIndex;                             // Offset 0x00 (0)
    PVOID MappedAddress;                            // Offset 0x04 (4)
    ULONG IoPortBase;                               // Offset 0x08 (8)
    ULONG BusNumber;                                // Offset 0x0C (12)
    ULONG SlotNumber;                               // Offset 0x10 (16)
    USHORT VendorId;                                // Offset 0x14 (20)
    USHORT DeviceId;                                // Offset 0x16 (22)
    USHORT SubsystemVendorId;                       // Offset 0x18 (24)
    USHORT SubsystemId;                             // Offset 0x1A (26)
    UCHAR RevisionId;                               // Offset 0x1C (28)
    UCHAR Reserved2[3];                             // Offset 0x1D (29)
    PVOID ControllerRegisters;                      // Offset 0x20 (32)
    ULONG ControllerRegistersLength;                // Offset 0x24 (36)

    // NVMe specific fields
    ULONGLONG ControllerCapabilities;               // Offset 0x28 (40) [8-byte aligned]
    ULONG Version;                                  // Offset 0x30 (48)
    ULONG PageSize;                                 // Offset 0x34 (52)
    ULONG DoorbellStride;                           // Offset 0x38 (56)
    USHORT MaxQueueEntries;                         // Offset 0x3C (60)
    USHORT Reserved3;                               // Offset 0x3E (62)

    // Admin Queue
    NVME_QUEUE AdminQueue;                          // Offset 0x40 (64) - 56 bytes [8-byte aligned]

    // I/O Queue (single queue for simplicity)
    NVME_QUEUE IoQueue;                             // Offset 0x78 (120) - 56 bytes [8-byte aligned]

    // Command tracking
    PSCSI_REQUEST_BLOCK NonTaggedInFlight;          // Offset 0xB0 (176)
    USHORT NextNonTaggedId;                         // Offset 0xB4 (180)
    BOOLEAN Reserved3_1;                            // Offset 0xB6 (182)
    BOOLEAN InitComplete;                           // Offset 0xB7 (183)

    // SMP synchronization for interrupt handler
    ATOMIC InterruptLock;                           // Offset 0xB8 (184)
    ULONG Reserved4;                                // Offset 0xBC (188)

    // PRP list pages for scatter-gather (shared pool, allocated after init)
    // Note: During init, UtilityBuffer points to the same memory
    PHYSICAL_ADDRESS PrpListPagesPhys;              // Offset 0xC0 (192) [8-byte aligned]
    PVOID PrpListPages;                             // Offset 0xC8 (200)
    ULONG PrpListPageBitmap;                        // Offset 0xCC (204)

    // Statistics (current and maximum)
    ULONG CurrentQueueDepth;                        // Offset 0xD0 (208)
    ULONG MaxQueueDepthReached;                     // Offset 0xD4 (212)
    ULONG CurrentPrpListPagesUsed;                  // Offset 0xD8 (216)
    ULONG MaxPrpListPagesUsed;                      // Offset 0xDC (220)

    // I/O statistics
    ULONGLONG TotalBytesRead;                       // Offset 0xE0 (224) [8-byte aligned]
    ULONGLONG TotalBytesWritten;                    // Offset 0xE8 (232) [8-byte aligned]
    ULONG TotalRequests;                            // Offset 0xF0 (240)
    ULONG TotalReads;                               // Offset 0xF4 (244)
    ULONG TotalWrites;                              // Offset 0xF8 (248)
    ULONG MaxReadSize;                              // Offset 0xFC (252)
    ULONG MaxWriteSize;                             // Offset 0x100 (256)
    ULONG RejectedRequests;                         // Offset 0x104 (260)

    // Utility buffer (4KB, used during init, then aliased as PRP list pages)
    PHYSICAL_ADDRESS UtilityBufferPhys;             // Offset 0x108 (264) [8-byte aligned]
    PVOID UtilityBuffer;                            // Offset 0x110 (272)

    // Controller information
    ULONG NumberOfNamespaces;                       // Offset 0x114 (276)
    UCHAR ControllerSerialNumber[21];               // Offset 0x118 (280)
    UCHAR ControllerModelNumber[41];                // Offset 0x12D (301)
    UCHAR ControllerFirmwareRevision[9];            // Offset 0x156 (342)
    BOOLEAN SMARTEnabled;                           // Offset 0x15F (351)

    // Namespace information
    ULONGLONG NamespaceSizeInBlocks;                // Offset 0x160 (352) [8-byte aligned]
    ULONG NamespaceBlockSize;                       // Offset 0x168 (360)
    ULONG UncachedExtensionOffset;                  // Offset 0x16C (364)

    // Uncached memory allocation
    PHYSICAL_ADDRESS UncachedExtensionPhys;         // Offset 0x170 (368) [8-byte aligned]
    PVOID UncachedExtensionBase;                    // Offset 0x178 (376)
    ULONG UncachedExtensionSize;                    // Offset 0x17C (380)
    ULONG FallbackTimerNeeded;                      // Offset 0x180 (384)

    // TRIM mode support
    BOOLEAN TrimEnable;                             // Offset 0x184 (388)
    UCHAR Reserved5[3];                             // Offset 0x185 (389) - 3 byte alignment
    ULONG TrimPattern[1024];                        // Offset 0x188 (392) - 4KB pattern buffer [4-byte aligned]

} HW_DEVICE_EXTENSION, *PHW_DEVICE_EXTENSION;       // Total size: 0x1188 (4488) bytes

//
// Forward declarations of miniport entry points
//
ULONG DriverEntry(IN PVOID DriverObject, IN PVOID Argument2);
ULONG HwFindAdapter(IN PVOID DeviceExtension, IN PVOID HwContext,
                    IN PVOID BusInformation, IN PCHAR ArgumentString,
                    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                    OUT PBOOLEAN Again);
BOOLEAN HwInitialize(IN PVOID DeviceExtension);
BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN HwInterrupt(IN PVOID DeviceExtension);
BOOLEAN HwResetBus(IN PVOID DeviceExtension, IN ULONG PathId);
#if (_WIN32_WINNT >= 0x500)
SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(IN PVOID DeviceExtension,
                                              IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
                                              IN PVOID Parameters);
#else
VOID HwAdapterState(
    IN PVOID DeviceExtension,
    IN PVOID Context,
    IN BOOLEAN SaveState);
#endif
//
// Helper functions
//
BOOLEAN IsNvmeDevice(UCHAR BaseClass, UCHAR SubClass, UCHAR ProgIf);
UCHAR ReadPciConfigByte(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
USHORT ReadPciConfigWord(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
ULONG ReadPciConfigDword(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
VOID WritePciConfigWord(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN USHORT Value);
VOID WritePciConfigDword(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value);

//
// NVMe helper functions
//
ULONG NvmeReadReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
VOID NvmeWriteReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value);
ULONGLONG NvmeReadReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
VOID NvmeWriteReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONGLONG Value);
BOOLEAN NvmeWaitForReady(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN WaitForReady);
BOOLEAN NvmeSubmitIoCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd);
BOOLEAN NvmeSubmitAdminCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd);
BOOLEAN NvmeProcessAdminCompletion(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeShutdownController(IN PHW_DEVICE_EXTENSION DevExt);
VOID FallbackTimer(IN PVOID DeviceExtension);
VOID NvmeProcessGetLogPageCompletion(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT status, USHORT commandId);
BOOLEAN NvmeProcessIoCompletion(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeRingDoorbell(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT QueueId, IN BOOLEAN IsSubmission, IN USHORT Value);
BOOLEAN NvmeCreateIoCQ(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeCreateIoSQ(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeBuildReadWriteCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN PNVME_COMMAND Cmd, IN USHORT CommandId);
USHORT NvmeBuildCommandId(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
USHORT NvmeBuildFlushCommandId(IN PSCSI_REQUEST_BLOCK Srb);
PSCSI_REQUEST_BLOCK NvmeGetSrbFromCommandId(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT CommandId);
BOOLEAN NvmeIdentifyController(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeIdentifyNamespace(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeToAtaIdentify(IN PHW_DEVICE_EXTENSION DevExt, OUT PATA_IDENTIFY_DEVICE_STRUCT AtaIdentify);
BOOLEAN NvmeLogPageToScsiLogPage(IN PNVME_SMART_INFO NvmeSmart, IN UCHAR ScsiPageCode, OUT PVOID ScsiLogBuffer, IN ULONG BufferSize, OUT PULONG BytesWritten);

// SCSI helper functions
BOOLEAN ScsiParseSatCommand(IN PSCSI_REQUEST_BLOCK Srb, OUT PUCHAR AtaCommand, OUT PUCHAR AtaFeatures, OUT PUCHAR AtaCylLow, OUT PUCHAR AtaCylHigh);
UCHAR ScsiGetLogPageCodeFromSrb(IN PSCSI_REQUEST_BLOCK Srb);

// helpers for completing SRBs
BOOLEAN ScsiSuccess(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiBusy(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiError(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR SrbStatus);
BOOLEAN ScsiPending(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);

//
// SMART/IOCTL functions
//
BOOLEAN HandleIO_NVME2KDB(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN HandleIO_SCSIDISK(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleInquiry(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadCapacity(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadWrite(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleFlush(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleLogSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleSatPassthrough(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleModeSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadDefectData10(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);

//
// PRP list page allocator
//
UCHAR AllocatePrpListPage(IN PHW_DEVICE_EXTENSION DevExt);
VOID FreePrpListPage(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR PageIndex);
PVOID GetPrpListPageVirtual(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR PageIndex);
PHYSICAL_ADDRESS GetPrpListPagePhysical(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR PageIndex);

//
// Uncached memory allocator
//
BOOLEAN AllocateUncachedMemory(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Size, IN ULONG Alignment,
                               OUT PVOID* VirtualAddress, OUT PHYSICAL_ADDRESS* PhysicalAddress);

#endif // _NVME2K_H_
