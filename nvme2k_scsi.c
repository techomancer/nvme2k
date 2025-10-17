// handling SCSI commands
#include "nvme2k.h"
#include "utils.h"

static BOOLEAN IsTagged(IN PSCSI_REQUEST_BLOCK Srb)
{
    return (Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE && Srb->QueueTag != SP_UNTAGGED);
}

BOOLEAN ScsiSuccess(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    ScsiPortNotification(RequestComplete, DevExt, Srb);
    ScsiPortNotification(NextRequest, DevExt, NULL);
    return TRUE;
}

BOOLEAN ScsiBusy(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    Srb->SrbStatus = SRB_STATUS_BUSY;
    ScsiPortNotification(RequestComplete, DevExt, Srb);
    ScsiPortNotification(NextRequest, DevExt, NULL);
    // alternatively return FALSE?
    return TRUE;
}

BOOLEAN ScsiError(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    ScsiPortNotification(RequestComplete, DevExt, Srb);
    ScsiPortNotification(NextRequest, DevExt, NULL);
    return TRUE;
}

BOOLEAN ScsiPending(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    Srb->SrbStatus = SRB_STATUS_PENDING;
    if (IsTagged(Srb))
        ScsiPortNotification(NextLuRequest, DevExt, 0, 0, 0);
    else
        ScsiPortNotification(NextRequest, DevExt, NULL);
    return TRUE;
}

//
// ScsiHandleInquiry - Handle SCSI INQUIRY command
//
BOOLEAN ScsiHandleInquiry(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    PUCHAR inquiryData;
    ULONG i, j;

    if (Srb->DataTransferLength < 5) {
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }

    inquiryData = (PUCHAR)Srb->DataBuffer;
    RtlZeroMemory(inquiryData, Srb->DataTransferLength);

    // Check for EVPD (Enable Vital Product Data)
    // In CDB6INQUIRY, bit 0 of PageCode field is EVPD
    if (cdb->CDB6INQUIRY.PageCode & 0x01) {
        // Return error for VPD pages (not implemented)
        return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }

    // Standard INQUIRY data
    if (Srb->DataTransferLength >= 36) {
        inquiryData[0] = 0x00;  // Peripheral Device Type: Direct access block device
        inquiryData[1] = 0x00;  // RMB = 0 (not removable)
        inquiryData[2] = 0x05;  // Version: SPC-3
        inquiryData[3] = 0x02;  // Response Data Format
        inquiryData[4] = 0x1F;  // Additional Length (31 bytes following)
        inquiryData[5] = 0x00;  // SCCS = 0
        inquiryData[6] = 0x00;  // No special features
        inquiryData[7] = 0x02;  // CmdQue = 1 (supports tagged command queuing)

        // Vendor Identification (8 bytes) - Extract first 8 chars from model number
        // Skip leading spaces in model number
        i = 0;
        while (i < 40 && DevExt->ControllerModelNumber[i] == ' ') {
            i++;
        }

        // Copy up to 8 characters for vendor field
        for (j = 0; j < 8 && i < 40 && DevExt->ControllerModelNumber[i] != 0; j++, i++) {
            inquiryData[8 + j] = DevExt->ControllerModelNumber[i];
        }
        // Pad with spaces if needed
        while (j < 8) {
            inquiryData[8 + j] = ' ';
            j++;
        }

        // Product Identification (16 bytes) - Continue from model number
        for (j = 0; j < 16 && i < 40 && DevExt->ControllerModelNumber[i] != 0; j++, i++) {
            inquiryData[16 + j] = DevExt->ControllerModelNumber[i];
        }
        // Pad with spaces if needed
        while (j < 16) {
            inquiryData[16 + j] = ' ';
            j++;
        }

        // Product Revision Level (4 bytes) - Use firmware revision
        for (j = 0; j < 4 && j < 8 && DevExt->ControllerFirmwareRevision[j] != 0; j++) {
            inquiryData[32 + j] = DevExt->ControllerFirmwareRevision[j];
        }
        // Pad with spaces if needed
        while (j < 4) {
            inquiryData[32 + j] = ' ';
            j++;
        }

        Srb->DataTransferLength = 36;
    }

    return ScsiSuccess(DevExt, Srb);
}

//
// ScsiHandleReadCapacity - Handle SCSI READ CAPACITY(10) command
//
BOOLEAN ScsiHandleReadCapacity(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PULONG capacityData;
    ULONG lastLba;
    ULONG blockSize;
    
    if (Srb->DataTransferLength < 8) {
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }
    
    capacityData = (PULONG)Srb->DataBuffer;
    
    // Check if namespace has been identified
    if (DevExt->NamespaceSizeInBlocks == 0) {
        // Return default values
        lastLba = 0xFFFFFFFF;
        blockSize = 512;
    } else {
        // Check if capacity exceeds 32-bit
        if (DevExt->NamespaceSizeInBlocks > 0xFFFFFFFF) {
            lastLba = 0xFFFFFFFF;  // Indicate to use READ CAPACITY(16)
        } else {
            lastLba = (ULONG)(DevExt->NamespaceSizeInBlocks - 1);
        }
        blockSize = DevExt->NamespaceBlockSize;
    }
    
    // Return in big-endian format
    capacityData[0] = ((lastLba & 0xFF) << 24) |
                      ((lastLba & 0xFF00) << 8) |
                      ((lastLba & 0xFF0000) >> 8) |
                      ((lastLba & 0xFF000000) >> 24);
    
    capacityData[1] = ((blockSize & 0xFF) << 24) |
                      ((blockSize & 0xFF00) << 8) |
                      ((blockSize & 0xFF0000) >> 8) |
                      ((blockSize & 0xFF000000) >> 24);
    
    Srb->DataTransferLength = 8;
    return ScsiSuccess(DevExt, Srb);
}

//
// ScsiHandleReadWrite - Handle SCSI READ/WRITE commands
//
BOOLEAN ScsiHandleReadWrite(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    NVME_COMMAND nvmeCmd;
    USHORT commandId;
    PNVME_SRB_EXTENSION srbExt;

    // Check if namespace is identified. If not, the device is not ready for I/O.
    if (DevExt->NamespaceSizeInBlocks == 0) {
        return ScsiBusy(DevExt, Srb);
    }

    // Check if this is a non-tagged request (QueueTag == SP_UNTAGGED or no queue action enabled)
    if (!((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb->QueueTag != SP_UNTAGGED))) {
        // Non-tagged request - only one can be in flight at a time
        if (DevExt->NonTaggedInFlight) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Non-tagged request rejected - another non-tagged request in flight\n");
#endif
            return ScsiBusy(DevExt, Srb);
        }
        // Mark that we now have a non-tagged request in flight
        DevExt->NonTaggedInFlight = TRUE;
    }

#ifdef NVME2K_DBG_EXTRA
    // Log HEAD_OF_QUEUE tags (treated as SIMPLE since NVMe has no priority queuing)
    if ((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) &&
        (Srb->QueueAction == SRB_HEAD_OF_QUEUE_TAG_REQUEST)) {
        ScsiDebugPrint(0, "nvme2k: HEAD_OF_QUEUE tag - treating as SIMPLE (no HW priority support)\n");
    }
#endif

    // For ORDERED tags, submit a Flush command first to drain all prior operations
    if ((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) &&
        (Srb->QueueAction == SRB_ORDERED_QUEUE_TAG_REQUEST)) {
        NVME_COMMAND flushCmd;
        USHORT flushCommandId;

#ifdef NVME2K_DBG_EXTRA
        ScsiDebugPrint(0, "nvme2k: ORDERED tag on I/O - submitting Flush before I/O\n");
#endif
        // Build flush command with special ORDERED flush CID
        flushCommandId = NvmeBuildFlushCommandId(Srb);

        RtlZeroMemory(&flushCmd, sizeof(NVME_COMMAND));
        flushCmd.CDW0.Fields.Opcode = NVME_CMD_FLUSH;
        flushCmd.CDW0.Fields.Flags = 0;
        flushCmd.CDW0.Fields.CommandId = flushCommandId;
        flushCmd.NSID = 1;

#ifdef NVME2K_DBG_EXTRA
        ScsiDebugPrint(0, "nvme2k: Submitting Flush (CID=%d) before ORDERED I/O\n", flushCommandId);
#endif
        if (!NvmeSubmitIoCommand(DevExt, &flushCmd)) {
            // Flush submission failed
            return ScsiBusy(DevExt, Srb);
        }
    }

    // Build command ID for the I/O command
    commandId = NvmeBuildCommandId(DevExt, Srb);

    // Initialize SRB extension
    srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    srbExt->PrpListPage = 0xFF;  // No PRP list initially

    // Build the NVMe Read/Write command from the SCSI CDB.
    RtlZeroMemory(&nvmeCmd, sizeof(NVME_COMMAND));
    NvmeBuildReadWriteCommand(DevExt, Srb, &nvmeCmd, commandId);

    // Submit the command to the I/O queue.
    if (NvmeSubmitIoCommand(DevExt, &nvmeCmd)) {
        // Command submitted successfully, mark SRB as pending.
        return ScsiPending(DevExt, Srb);
    } else {
        // Submission failed, likely a full queue. Free resources and mark as busy.
        if (srbExt->PrpListPage != 0xFF) {
            FreePrpListPage(DevExt, srbExt->PrpListPage);
            srbExt->PrpListPage = 0xFF;
        }
        return ScsiBusy(DevExt, Srb);
    }
}

//
// ScsiHandleLogSense - Handle SCSI LOG SENSE command
// Translates to NVMe Get Log Page for SMART/Health data
//
BOOLEAN ScsiHandleLogSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR pageCode;

    // Check if namespace is identified
    if (DevExt->NamespaceSizeInBlocks == 0) {
        return ScsiBusy(DevExt, Srb);
    }

    // Extract page code from CDB
    pageCode = ScsiGetLogPageCodeFromSrb(Srb);

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ScsiHandleLogSense - Page Code=0x%02X\n", pageCode);
#endif

    // We only support the SMART/Health Information log page for now
    // A real implementation would check for other pages.
    // For simplicity, we assume any log sense is for SMART data.
    if (pageCode == SCSI_LOG_PAGE_INFORMATIONAL) {

        // Issue async NVMe Get Log Page command for SMART/Health info
        if (!NvmeGetLogPage(DevExt, Srb, NVME_LOG_PAGE_SMART_HEALTH)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Failed to submit Get Log Page for LOG SENSE\n");
#endif        
            return ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
        }

        // Mark SRB as pending - will be completed in interrupt handler
        return ScsiPending(DevExt, Srb);
    } else {
        // Unsupported log page
        return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }
}

//
// ScsiHandleSatPassthrough - Handle SAT ATA PASS-THROUGH commands (0x85, 0xA1)
// Only supports SMART read operations (SMART READ DATA and IDENTIFY DEVICE)
//
BOOLEAN ScsiHandleSatPassthrough(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR ataCommand;
    UCHAR ataFeatures;
    UCHAR ataCylLow;
    UCHAR ataCylHigh;

    // Check if namespace is identified
    if (DevExt->NamespaceSizeInBlocks == 0) {
        return ScsiBusy(DevExt, Srb);
    }

    // Parse and validate the SAT command
    if (!ScsiParseSatCommand(Srb, &ataCommand, &ataFeatures, &ataCylLow, &ataCylHigh)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: SAT command rejected - not a supported SMART read\n");
#endif
        return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: SAT PASS-THROUGH - ATA Cmd=0x%02X Feature=0x%02X CylLow=0x%02X CylHi=0x%02X\n",
                   ataCommand, ataFeatures, ataCylLow, ataCylHigh);
#endif

    // Check if this is SMART READ DATA or IDENTIFY DEVICE
    if (ataCommand == ATA_SMART_CMD && ataFeatures == ATA_SMART_READ_DATA) {
        // SMART READ DATA - translate to NVMe Get Log Page
        // Ensure we have buffer space for output (512 bytes)
        if (Srb->DataTransferLength < 512) {
            return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
        }

        // Issue async NVMe Get Log Page command for SMART/Health info
        if (!NvmeGetLogPage(DevExt, Srb, NVME_LOG_PAGE_SMART_HEALTH)) {
            return ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
        }

        // Mark SRB as pending - will be completed in interrupt handler
        return ScsiPending(DevExt, Srb);
    } else if (ataCommand == ATA_SMART_CMD && ataFeatures == ATA_SMART_READ_LOG) {
        // SMART READ LOG - return empty log (NVMe doesn't support ATA-style log pages)
        // Ensure we have buffer space for output (512 bytes)
        if (Srb->DataTransferLength < 512) {
            return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
        }

        // Return empty 512-byte buffer (all zeros)
        RtlZeroMemory(Srb->DataBuffer, 512);
        Srb->DataTransferLength = 512;

        return ScsiSuccess(DevExt, Srb);    
    } else if (ataCommand == ATA_IDENTIFY_DEVICE) {
        if (Srb->DataTransferLength < 512) {
            return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
        }

        NvmeToAtaIdentify(DevExt, (PATA_IDENTIFY_DEVICE_STRUCT)Srb->DataBuffer);
        return ScsiSuccess(DevExt, Srb);
    } else {
        // Unknown command (should not reach here due to validation)
        return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }
}

//
// ScsiHandleFlush - Handle SCSI SYNCHRONIZE_CACHE command by sending NVMe Flush
//
BOOLEAN ScsiHandleFlush(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    NVME_COMMAND nvmeCmd;
    USHORT commandId;

    // Check if namespace is identified
    if (DevExt->NamespaceSizeInBlocks == 0) {
        return ScsiBusy(DevExt, Srb);
    }

    // Check if this is a non-tagged request (QueueTag == SP_UNTAGGED or no queue action enabled)
    if (!((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb->QueueTag != SP_UNTAGGED))) {
        // Non-tagged request - only one can be in flight at a time
        if (DevExt->NonTaggedInFlight) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Non-tagged flush rejected - another non-tagged request in flight\n");
#endif
            return ScsiBusy(DevExt, Srb);
        }
        // Mark that we now have a non-tagged request in flight
        DevExt->NonTaggedInFlight = TRUE;
        DevExt->NonTaggedSrbFallback = Srb;
    }

    // Build command ID (flushes from SYNCHRONIZE_CACHE are standalone, not ORDERED tag flushes)
    commandId = NvmeBuildCommandId(DevExt, Srb);

    // Build NVMe Flush command
    RtlZeroMemory(&nvmeCmd, sizeof(NVME_COMMAND));
    nvmeCmd.CDW0.Fields.Opcode = NVME_CMD_FLUSH;
    nvmeCmd.CDW0.Fields.Flags = 0;
    nvmeCmd.CDW0.Fields.CommandId = commandId;
    nvmeCmd.NSID = 1;  // Namespace ID 1

    // Submit the Flush command
    if (NvmeSubmitIoCommand(DevExt, &nvmeCmd)) {
        return ScsiPending(DevExt, Srb);
    } else {
        // Submission failed
        DevExt->NonTaggedSrbFallback = NULL;
        DevExt->NonTaggedInFlight = FALSE;
        return ScsiBusy(DevExt, Srb);
    }
}

//
// ScsiHandleReadDefectData10 - Handle SCSI READ DEFECT DATA (10) command
//
BOOLEAN ScsiHandleReadDefectData10(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PUCHAR buffer;
    PCDB_READ_DEFECT_DATA cdb = (PCDB_READ_DEFECT_DATA)Srb->Cdb;
    ULONG allocationLength;
    UCHAR defectListFormat;
    BOOLEAN requestPList;
    BOOLEAN requestGList;

    // Parse CDB
    // Allocation length is at bytes 7-8 (big-endian)
    allocationLength = (cdb->AllocationLength[0] << 8) |
                       cdb->AllocationLength[1];

    // Defect list format byte (byte 2 for READ DEFECT DATA 10):
    // Bit 4: PLIST (request Primary defect list - factory defects)
    // Bit 3: GLIST (request Grown defect list - defects developed during use)
    // Bits 2-0: Format of defect descriptors
    requestPList = (cdb->FormatByte & 0x10) ? TRUE : FALSE;
    requestGList = (cdb->FormatByte & 0x08) ? TRUE : FALSE;
    defectListFormat = cdb->FormatByte & 0x07;

    // Check buffer size - need at least 4 bytes for header
    if (Srb->DataTransferLength < 4) {
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }

    buffer = (PUCHAR)Srb->DataBuffer;
    RtlZeroMemory(buffer, Srb->DataTransferLength);

    // Build READ DEFECT DATA (10) response header (4 bytes)
    // Per SBC-3 specification section 5.7

    // Byte 0: Reserved
    buffer[0] = 0x00;

    // Byte 1: Response flags
    // Bit 7: PS (Parameters Saveable) = 0 (not saveable)
    // Bit 4: PLIST = echo from CDB (indicates if P-list is being returned)
    // Bit 3: GLIST = echo from CDB (indicates if G-list is being returned)
    // Bits 2-0: Defect List Format = echo from CDB
    // For NVMe SSDs: We acknowledge the request but return empty lists
    buffer[1] = (cdb->FormatByte & 0x1F);  // Copy PLIST, GLIST, and format bits

    // Bytes 2-3: Defect list length (big-endian, excludes the 4-byte header)
    // For NVMe SSDs, both P-list and G-list are empty (length = 0)
    // NVMe drives handle bad blocks transparently via internal wear leveling
    buffer[2] = 0x00;
    buffer[3] = 0x00;

    // Set actual transfer length (just the 4-byte header, no defect descriptors)
    Srb->DataTransferLength = 4;
    return ScsiSuccess(DevExt, Srb);
}

//
// ScsiHandleModeSense - Handle SCSI MODE SENSE(6) and MODE SENSE(10) commands
//
BOOLEAN ScsiHandleModeSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    PUCHAR buffer;
    ULONG allocationLength;
    UCHAR pageCode;
    UCHAR pageControl;
    BOOLEAN dbd;  // Disable Block Descriptors
    BOOLEAN isModeSense10;
    ULONG offset;
    ULONG headerSize;
    ULONG blockDescLength;
    ULONG totalLength;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ScsiHandleModeSense called DataTransferLength:%d\n", Srb->DataTransferLength);
#endif

    buffer = (PUCHAR)Srb->DataBuffer;
    isModeSense10 = (Srb->Cdb[0] == SCSIOP_MODE_SENSE10);

    // Parse the CDB based on command type
    if (isModeSense10) {
        pageCode = cdb->MODE_SENSE10.PageCode;
        pageControl = cdb->MODE_SENSE10.Pc;
        dbd = cdb->MODE_SENSE10.Dbd;
        allocationLength = (cdb->MODE_SENSE10.AllocationLength[0] << 8) | cdb->MODE_SENSE10.AllocationLength[1];
        headerSize = 8;  // MODE SENSE(10) header is 8 bytes
    } else {
        pageCode = cdb->MODE_SENSE.PageCode;
        pageControl = cdb->MODE_SENSE.Pc;
        dbd = cdb->MODE_SENSE.Dbd;
        allocationLength = cdb->MODE_SENSE.AllocationLength;
        headerSize = 4;  // MODE SENSE(6) header is 4 bytes
    }

    // Check buffer size
    if (Srb->DataTransferLength < headerSize) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: ModeSense buffer too small\n");
#endif
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ModeSense pageCode=0x%02X pageControl=0x%02X dbd=%d\n",
                   pageCode, pageControl, dbd);
#endif

    // If unsupported page requested and not "return all", return error
    if (pageCode != MODE_SENSE_RETURN_ALL &&
        pageCode != MODE_PAGE_CACHING &&
        pageCode != MODE_PAGE_CONTROL &&
        pageCode != MODE_PAGE_POWER_CONDITION &&
        pageCode != MODE_PAGE_FAULT_REPORTING) {
        return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }

    // Clear the buffer
    RtlZeroMemory(buffer, Srb->DataTransferLength);

    // Determine block descriptor length
    // For simplicity, we include a block descriptor unless DBD is set
    blockDescLength = dbd ? 0 : 8;

    offset = headerSize;
#if 1
    // Add block descriptor if not disabled
    if (!dbd && (offset + blockDescLength) <= Srb->DataTransferLength) {
        PUCHAR blockDesc = buffer + offset;
        ULONG blockSize = DevExt->NamespaceBlockSize ? DevExt->NamespaceBlockSize : 512;
        ULONGLONG numBlocks = DevExt->NamespaceSizeInBlocks;

        // Block descriptor format (8 bytes):
        // Byte 0: Density code (0 = default)
        blockDesc[0] = 0x00;

        // Bytes 1-3: Number of blocks (or 0xFFFFFF if > 24-bit)
        if (numBlocks > 0xFFFFFF) {
            blockDesc[1] = 0xFF;
            blockDesc[2] = 0xFF;
            blockDesc[3] = 0xFF;
        } else {
            blockDesc[1] = (UCHAR)((numBlocks >> 16) & 0xFF);
            blockDesc[2] = (UCHAR)((numBlocks >> 8) & 0xFF);
            blockDesc[3] = (UCHAR)(numBlocks & 0xFF);
        }

        // Byte 4: Reserved
        blockDesc[4] = 0x00;

        // Bytes 5-7: Block length (big-endian)
        blockDesc[5] = (UCHAR)((blockSize >> 16) & 0xFF);
        blockDesc[6] = (UCHAR)((blockSize >> 8) & 0xFF);
        blockDesc[7] = (UCHAR)(blockSize & 0xFF);

        offset += blockDescLength;
    }
#endif
#if 1
    // Add mode pages based on pageCode
    // Note: We only support reading current values for now
    if (pageControl == MODE_SENSE_CHANGEABLE_VALUES) {
        // Return all zeros for changeable values (nothing is changeable)
        // Header already zeroed, just set lengths
    } else if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_CACHING) {
        // Page 08h: Caching Parameters Page
        if ((offset + 20) <= Srb->DataTransferLength) {
            PUCHAR cachePage = buffer + offset;

            cachePage[0] = MODE_PAGE_CACHING;  // Page code
            cachePage[1] = 18;  // Page length (n-1, total 20 bytes)

            // Byte 2: Flags
            // IC (Initiator Control) = 0
            // ABPF (Abort Pre-Fetch) = 0
            // CAP (Caching Analysis Permitted) = 0
            // DISC (Discontinuity) = 0
            // SIZE (Size enable) = 0
            // WCE (Write Cache Enable) = 1 (NVMe typically has write cache)
            // MF (Multiplication Factor) = 0
            // RCD (Read Cache Disable) = 0 (read cache enabled)
            cachePage[2] = 0x04;  // WCE=1, others=0

            // Byte 3: Read retention priority (4 bits) and Write retention priority (4 bits)
            cachePage[3] = 0x00;  // Equal priority

            // Bytes 4-5: Disable pre-fetch transfer length (big-endian)
            cachePage[4] = 0x00;
            cachePage[5] = 0x00;

            // Bytes 6-7: Minimum pre-fetch (big-endian)
            cachePage[6] = 0x00;
            cachePage[7] = 0x00;

            // Bytes 8-9: Maximum pre-fetch (big-endian)
            cachePage[8] = 0xFF;
            cachePage[9] = 0xFF;

            // Bytes 10-11: Maximum pre-fetch ceiling (big-endian)
            cachePage[10] = 0xFF;
            cachePage[11] = 0xFF;

            // Byte 12: Flags
            // FSW (Force Sequential Write) = 0
            // LBCSS (Logical Block Cache Segment Size) = 0
            // DRA (Disable Read Ahead) = 0
            // NV_DIS (Non-Volatile Cache Disable) = 0
            cachePage[12] = 0x00;

            // Byte 13: Number of cache segments
            cachePage[13] = 0x00;

            // Bytes 14-15: Cache segment size (big-endian)
            cachePage[14] = 0x00;
            cachePage[15] = 0x00;

            // Bytes 16-19: Reserved
            cachePage[16] = 0x00;
            cachePage[17] = 0x00;
            cachePage[18] = 0x00;
            cachePage[19] = 0x00;

            offset += 20;
        }
    }
#endif
#if 1
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_CONTROL) {
        // Page 0Ah: Control Mode Page
        if ((offset + 12) <= Srb->DataTransferLength) {
            PUCHAR controlPage = buffer + offset;

            controlPage[0] = MODE_PAGE_CONTROL;  // Page code
            controlPage[1] = 10;  // Page length (n-1, total 12 bytes)

            // Byte 2: TST (Task Set Type), TMF_ONLY, etc.
            controlPage[2] = 0x00;

            // Byte 3: QERR (Queue Error Management), etc.
            // Bits 1-0: QERR=00b (restricted reordering - safest default)
            // Bits 3-2: Reserved = 0
            // Bits 5-4: Queue Algorithm Modifier = 0 (restricted reordering)
            // Bits 7-6: Reserved = 0
            controlPage[3] = 0x00;  // QERR=0, all other bits=0

            // Byte 4: Flags (ATO, TAS, AUTOLOAD MODE, etc.)
            controlPage[4] = 0x00;

            // Bytes 5-6: Reserved
            controlPage[5] = 0x00;
            controlPage[6] = 0x00;

            // Bytes 7-8: Busy timeout period (big-endian)
            controlPage[7] = 0x00;
            controlPage[8] = 0x00;

            // Bytes 9-10: Extended self-test completion time (big-endian)
            controlPage[9] = 0x00;
            controlPage[10] = 0x00;

            // Byte 11: Reserved
            controlPage[11] = 0x00;

            offset += 12;
        }
    }
#endif
#if 1
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_POWER_CONDITION) {
        // Page 1Ah: Power Condition Mode Page
        if ((offset + 12) <= Srb->DataTransferLength) {
            PUCHAR powerPage = buffer + offset;

            powerPage[0] = MODE_PAGE_POWER_CONDITION;  // Page code
            powerPage[1] = 10;  // Page length (n-1, total 12 bytes)

            // Byte 2: Reserved
            powerPage[2] = 0x00;

            // Byte 3: Flags (STANDBY, IDLE)
            // All power conditions disabled (NVMe manages power internally)
            powerPage[3] = 0x00;

            // Bytes 4-7: Idle condition timer (big-endian, in 100ms units)
            powerPage[4] = 0x00;
            powerPage[5] = 0x00;
            powerPage[6] = 0x00;
            powerPage[7] = 0x00;

            // Bytes 8-11: Standby condition timer (big-endian, in 100ms units)
            powerPage[8] = 0x00;
            powerPage[9] = 0x00;
            powerPage[10] = 0x00;
            powerPage[11] = 0x00;

            offset += 12;
        }
    }
#endif
#if 1
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_FAULT_REPORTING) {
        // Page 1Ch: Informational Exceptions Control (IEC) Page
        // This page indicates SMART-like health monitoring capability
        if ((offset + 12) <= Srb->DataTransferLength) {
            PUCHAR iecPage = buffer + offset;

            iecPage[0] = MODE_PAGE_FAULT_REPORTING;  // Page code 0x1C
            iecPage[1] = 10;  // Page length (n-1, total 12 bytes)

            // Byte 2: Flags
            // Bit 7: PERF (Performance) = 0
            // Bit 6: EBF (Enable Background Function) = 0
            // Bit 5: EWASC (Enable Warning) = 0
            // Bit 4: DEXCPT (Disable Exceptions) = 0 (enabled - we support SMART)
            // Bit 3: TEST = 0
            // Bit 2: EBACKERR = 0
            // Bit 1: Reserved
            // Bit 0: LOGERR (Log Errors) = 0
            iecPage[2] = 0x00;  // DEXCPT=0 means informational exceptions enabled

            // Byte 3: MRIE (Method of Reporting Informational Exceptions)
            // 0h = No reporting of informational exception conditions
            // 2h = Generate unit attention
            // 3h = Conditionally generate recovered error
            // 4h = Unconditionally generate recovered error
            // 5h = Generate no sense
            // 6h = Only report informational exception on request (like SMART)
            iecPage[3] = 0x06;  // Report on request (LOG SENSE)

            // Bytes 4-7: Interval Timer (big-endian, in 100ms units)
            // 0 = vendor specific
            iecPage[4] = 0x00;
            iecPage[5] = 0x00;
            iecPage[6] = 0x00;
            iecPage[7] = 0x00;

            // Bytes 8-11: Report Count (big-endian)
            // Number of times to report an exception
            // 0 = unlimited
            iecPage[8] = 0x00;
            iecPage[9] = 0x00;
            iecPage[10] = 0x00;
            iecPage[11] = 0x01;  // Report once

            offset += 12;
        }
    }
#endif
    // Calculate total length
    totalLength = offset;

    // Fill in the mode parameter header
    if (isModeSense10) {
        // MODE SENSE(10) header
        USHORT modeDataLength = (USHORT)(totalLength - 2);  // Length excludes the length field itself

        buffer[0] = (UCHAR)((modeDataLength >> 8) & 0xFF);
        buffer[1] = (UCHAR)(modeDataLength & 0xFF);
        buffer[2] = 0x00;  // Medium type (0 = default)
        buffer[3] = 0x00;  // Device-specific parameter
        buffer[4] = 0x00;  // Reserved
        buffer[5] = 0x00;  // Reserved
        buffer[6] = (UCHAR)((blockDescLength >> 8) & 0xFF);
        buffer[7] = (UCHAR)(blockDescLength & 0xFF);
    } else {
        // MODE SENSE(6) header
        UCHAR modeDataLength = (UCHAR)(totalLength - 1);  // Length excludes the length field itself

        if (modeDataLength > 0xFF) {
            modeDataLength = 0xFF;  // Truncate for MODE SENSE(6)
        }

        buffer[0] = modeDataLength;
        buffer[1] = 0x00;  // Medium type (0 = default)
        buffer[2] = 0x00;  // Device-specific parameter
        buffer[3] = (UCHAR)blockDescLength;
    }

    // Update transfer length
    if (totalLength > allocationLength) {
        totalLength = allocationLength;
    }
    if (totalLength > Srb->DataTransferLength) {
        totalLength = Srb->DataTransferLength;
    }

    Srb->DataTransferLength = totalLength;
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ModeSense completed, returning %d bytes\n", totalLength);
#endif
    return ScsiSuccess(DevExt, Srb);
}

//
// HandleIO_SCSIDISK - Process SMART/ATA pass-through IOCTLs
//
// for now we dont really support any SCSIDISK IOCTLS so all return FALSE
// smartctl calls 001B0500 which seems to be from windows XP
BOOLEAN HandleIO_SCSIDISK(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PSRB_IO_CONTROL srbControl;
    PSENDCMDINPARAMS sendCmdIn;
    PSENDCMDOUTPARAMS sendCmdOut;
    PIDEREGS regs;
    ULONG dataBufferSize;
    ULONG requiredSize;
    NVME_SMART_INFO smartInfo;
    PHYSICAL_ADDRESS smartPhys;
    ULONG returnedLength;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl called - DataTransferLength=%u\n",
                   Srb->DataTransferLength);
#endif

    // Validate minimum size for SRB_IO_CONTROL header
    if (Srb->DataTransferLength < sizeof(SRB_IO_CONTROL)) {
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl invalid transfer length - DataTransferLength=%u\n",
                   Srb->DataTransferLength);
#endif
        return FALSE;
    }

    srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;

    // Check signature (Windows 2000 uses "SCSIDISK" for ATA pass-through)
    if (RtlCompareMemory(srbControl->Signature, "SCSIDISK", 8) != 8) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: Invalid SRB_IO_CONTROL signature\n");
#endif
        return FALSE;
    }

    switch (srbControl->ControlCode) {
        case IOCTL_SCSI_PASS_THROUGH:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_PASS_THROUGH\n");
#endif
            return FALSE;
        case IOCTL_SCSI_MINIPORT:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_MINIPORT\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_INQUIRY_DATA:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_INQUIRY_DATA\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_CAPABILITIES:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_CAPABILITIES\n");
#endif
            return FALSE;
        case IOCTL_SCSI_PASS_THROUGH_DIRECT:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_PASS_THROUGH_DIRECT\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_ADDRESS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_ADDRESS\n");
#endif
            return FALSE;
        case IOCTL_SCSI_RESCAN_BUS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_RESCAN_BUS\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_DUMP_POINTERS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_DUMP_POINTERS\n");
#endif
            return FALSE;
        case IOCTL_SCSI_FREE_DUMP_POINTERS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_FREE_DUMP_POINTERS\n");
#endif
            return FALSE;
        case IOCTL_IDE_PASS_THROUGH:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_IDE_PASS_THROUGH\n");
#endif
            return FALSE;
        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK unknown IOCTL:%08X function:%03X\n", srbControl->ControlCode, (srbControl->ControlCode >> 2) & 0xFFF);
#endif
            //srbControl->ReturnCode = STATUS_INVALID_DEVICE_REQUEST;
            return FALSE; // returns SRB_STATUS_INVALID_REQUEST
    }

    // Validate that we have enough space for SENDCMDINPARAMS
    requiredSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDINPARAMS) - 1;
    if (Srb->DataTransferLength < requiredSize) {
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl called - DataTransferLength=%u required size = %u\n",
                   Srb->DataTransferLength, requiredSize);
#endif
        return FALSE;
    }

    // Get the SENDCMDINPARAMS structure (follows SRB_IO_CONTROL header)
    sendCmdIn = (PSENDCMDINPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));
    sendCmdOut = (PSENDCMDOUTPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));
    regs = &sendCmdIn->irDriveRegs;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl SMART IOCTL IO:%08X sz:%08X Cmd=0x%02X Feature=0x%02X CylLow=0x%02X CylHi=0x%02X Drive:%02X\n",
                   srbControl->ControlCode, sendCmdIn->cBufferSize, regs->bCommandReg, regs->bFeaturesReg, regs->bCylLowReg, regs->bCylHighReg, sendCmdIn->bDriveNumber);
#endif

    switch (regs->bCommandReg) {
        case ATA_IDENTIFY_DEVICE:
            // Validate SMART signature in cylinder registers
            if (regs->bCylLowReg != SMART_CYL_LOW || regs->bCylHighReg != SMART_CYL_HI) {
                // Not a SMART command, could be IDENTIFY or other ATA command
                dataBufferSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + 512 - 1;
                if (Srb->DataTransferLength < dataBufferSize) {
                    return FALSE;
                }
                NvmeToAtaIdentify(DevExt, (PATA_IDENTIFY_DEVICE_STRUCT)sendCmdOut->bBuffer);
                RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                sendCmdOut->DriverStatus.bDriverError = 0;
                sendCmdOut->DriverStatus.bIDEError = 0;
                sendCmdOut->cBufferSize = 512;
                srbControl->ReturnCode = 0;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                return TRUE;
            } else {
                return FALSE;
            }
            break;
        case ATA_SMART_CMD:
            // Process based on SMART subcommand (features register)
            switch (regs->bFeaturesReg) {
                case ATA_SMART_READ_DATA:
                    // Read SMART data / attributes - translate to NVMe Get Log Page
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART READ DATA/ATTRIBS request\n");
        #endif
                    // Ensure we have buffer space for output
                    dataBufferSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + 512 - 1;
                    if (Srb->DataTransferLength < dataBufferSize) {
                        return FALSE;
                    }

                    // Issue async NVMe Get Log Page command for SMART/Health info
                    // The command will complete in the interrupt handler which will copy data
                    if (!NvmeGetLogPage(DevExt, Srb, NVME_LOG_PAGE_SMART_HEALTH)) {
        #ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: Failed to submit Get Log Page command\n");
        #endif
                        return FALSE;
                    }

                    // Mark SRB as pending - will be completed in interrupt handler
                    Srb->SrbStatus = SRB_STATUS_PENDING;
                    return TRUE;

                case ATA_SMART_RETURN_STATUS:
                    // Return SMART status - check if device is healthy
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART RETURN STATUS request\n");
        #endif
                    // Initialize output structure
                    RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                    sendCmdOut->cBufferSize = 0;
                    sendCmdOut->DriverStatus.bDriverError = 0;  // Success
                    sendCmdOut->DriverStatus.bIDEError = 0;     // No error

                    // In ATA SMART, threshold exceeded is indicated by:
                    // Cylinder registers: 0x2C (low) and 0xF4 (high) = FAILING
                    // Cylinder registers: 0x4F (low) and 0xC2 (high) = PASSING
                    // We'd need to read NVMe SMART critical warning to determine this
                    // For now, return passing status
                    regs->bCylLowReg = SMART_CYL_LOW;   // 0x4F = passing
                    regs->bCylHighReg = SMART_CYL_HI;   // 0xC2 = passing

                    srbControl->ReturnCode = 0;  // Success
                    return TRUE;

                case ATA_SMART_ENABLE:
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART ENABLE (NVMe always enabled)\n");
        #endif
                    // NVMe SMART is always enabled
                    RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                    sendCmdOut->DriverStatus.bDriverError = 0;
                    srbControl->ReturnCode = 0;
                    return TRUE;

                case ATA_SMART_DISABLE:
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART DISABLE (NVMe cannot disable)\n");
        #endif
                    // NVMe SMART cannot be disabled, return success anyway
                    RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                    sendCmdOut->DriverStatus.bDriverError = 0;
                    srbControl->ReturnCode = 0;
                    return TRUE;

                case ATA_SMART_AUTOSAVE:
                case ATA_SMART_READ_THRESHOLDS:
                default:
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: Unsupported SMART subcommand 0x%02X\n",
                                regs->bFeaturesReg);
        #endif
                    return FALSE;
            } // switch bFeatursReg
            break;

        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl unhandled - Cmd=0x%02X Feature=0x%02X CylLow=0x%02X CylHi=0x%02X\n",
                   regs->bCommandReg, regs->bFeaturesReg, regs->bCylLowReg, regs->bCylHighReg);
#endif
            return FALSE;
    } // switch bCommandReg
}
