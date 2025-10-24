// building and sending NVME commands
#include "nvme2k.h"
#include "utils.h"

//
// NVMe Register Access Functions
//

ULONG NvmeReadReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset)
{
    return ScsiPortReadRegisterUlong((PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset));
}

VOID NvmeWriteReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value)
{
    ScsiPortWriteRegisterUlong((PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset), Value);
}

ULONGLONG NvmeReadReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset)
{
    ULONGLONG value;
    PULONG ptr = (PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset);
    
    value = ScsiPortReadRegisterUlong(ptr);
    value |= ((ULONGLONG)ScsiPortReadRegisterUlong(ptr + 1)) << 32;
    
    return value;
}

VOID NvmeWriteReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONGLONG Value)
{
    PULONG ptr = (PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset);
    
    ScsiPortWriteRegisterUlong(ptr, (ULONG)(Value & 0xFFFFFFFF));
    ScsiPortWriteRegisterUlong(ptr + 1, (ULONG)(Value >> 32));
}

//
// NvmeWaitForReady - Wait for controller ready status
//
BOOLEAN NvmeWaitForReady(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN WaitForReady)
{
    ULONG timeout = 5000;  // 5 seconds
    ULONG csts;
    
    while (timeout > 0) {
        csts = NvmeReadReg32(DevExt, NVME_REG_CSTS);
        
        if (WaitForReady) {
            if (csts & NVME_CSTS_RDY) {
                return TRUE;
            }
        } else {
            if (!(csts & NVME_CSTS_RDY)) {
                return TRUE;
            }
        }
        
        ScsiPortStallExecution(1000);  // Wait 1ms
        timeout--;
    }
    
    return FALSE;
}

//
// NvmeRingDoorbell - Ring submission or completion queue doorbell
//
VOID NvmeRingDoorbell(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT QueueId, IN BOOLEAN IsSubmission, IN USHORT Value)
{
    ULONG offset;

    offset = NVME_REG_DBS + (2 * QueueId * DevExt->DoorbellStride);
    if (!IsSubmission) {
        offset += DevExt->DoorbellStride;
    }

    NvmeWriteReg32(DevExt, offset, Value);
}

//
// NvmeSubmitCommand - Submit a command to a queue
//
static BOOLEAN NvmeSubmitCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_QUEUE Queue, IN PNVME_COMMAND Cmd)
{
    PNVME_COMMAND sqEntry;
    USHORT nextTail;
    ULONG currentHead;
    BOOLEAN result;

#ifdef NVME2K_USE_SUBMISSION_LOCK
    // Acquire submission queue spinlock - protects against concurrent HwStartIo calls
    if (!AtomicCompareExchange(&Queue->SubmissionLock, 1, 0)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeSubmitCommand - lock contention on QID=%d (spinning)\n", Queue->QueueId);
#endif
        while (!AtomicCompareExchange(&Queue->SubmissionLock, 1, 0)) {
            // Spin waiting for lock
            // On a busy system this is very brief since we only hold it for microseconds
        }
    }
#endif

    // Check if queue is full - SubmissionQueueHead is protected by lock
    nextTail = (USHORT)((Queue->SubmissionQueueTail + 1) & Queue->QueueSizeMask);
    currentHead = (USHORT)(Queue->SubmissionQueueHead & Queue->QueueSizeMask);
    if (nextTail == currentHead) {
        // Queue full - release lock and return
#ifdef NVME2K_USE_SUBMISSION_LOCK
        AtomicSet(&Queue->SubmissionLock, 0);
#endif
        return FALSE;
    }

    // Get submission queue entry
    sqEntry = (PNVME_COMMAND)((PUCHAR)Queue->SubmissionQueue +
                              (Queue->SubmissionQueueTail * NVME_SQ_ENTRY_SIZE));

    // Copy command to queue
    RtlCopyMemory(sqEntry, Cmd, sizeof(NVME_COMMAND));

#ifdef NVME2K_DBG_CMD
    // Dump the command for debugging
    ScsiDebugPrint(0, "nvme2k: NvmeSubmitCommand - QID=%d Tail=%d\n", Queue->QueueId, Queue->SubmissionQueueTail);
    ScsiDebugPrint(0, "  CDW0=%08X (OPC=%02X Flags=%02X CID=%04X) NSID=%08X CDW2=%08X CDW3=%08X\n",
                   sqEntry->CDW0.AsUlong, sqEntry->CDW0.Fields.Opcode, sqEntry->CDW0.Fields.Flags,
                   sqEntry->CDW0.Fields.CommandId, sqEntry->NSID, sqEntry->CDW2, sqEntry->CDW3);
    ScsiDebugPrint(0, "  MPTR=%08X%08X\n",
                   (ULONG)(sqEntry->MPTR >> 32), (ULONG)(sqEntry->MPTR & 0xFFFFFFFF));
    ScsiDebugPrint(0, "  PRP1=%08X%08X PRP2=%08X%08X\n",
                   (ULONG)(sqEntry->PRP1 >> 32), (ULONG)(sqEntry->PRP1 & 0xFFFFFFFF),
                   (ULONG)(sqEntry->PRP2 >> 32), (ULONG)(sqEntry->PRP2 & 0xFFFFFFFF));
    ScsiDebugPrint(0, "  CDW10=%08X CDW11=%08X CDW12=%08X CDW13=%08X\n",
                   sqEntry->CDW10, sqEntry->CDW11, sqEntry->CDW12, sqEntry->CDW13);
    ScsiDebugPrint(0, "  CDW14=%08X CDW15=%08X\n",
                   sqEntry->CDW14, sqEntry->CDW15);
#endif
    // Update tail
    Queue->SubmissionQueueTail = nextTail;

    if (DevExt->FallbackTimerNeeded) {
        // if we didnt get an interrupt in a milisecond something is wrong with interrupts
        ScsiPortNotification(RequestTimerCall, (PVOID)DevExt, FallbackTimer, 1000);
    }
    
    // Ring doorbell
    NvmeRingDoorbell(DevExt, Queue->QueueId, TRUE, (USHORT)(Queue->SubmissionQueueTail));

#ifdef NVME2K_USE_SUBMISSION_LOCK
    // Release submission queue spinlock (atomic write to ensure memory ordering)
    AtomicSet(&Queue->SubmissionLock, 0);
#endif

    return TRUE;
}

BOOLEAN NvmeSubmitIoCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd)
{
    BOOLEAN result;

    result = NvmeSubmitCommand(DevExt, &DevExt->IoQueue, Cmd);

    if (result) {
        // Command successfully submitted - update queue depth tracking
        DevExt->CurrentQueueDepth++;

        // Update maximum queue depth if we've reached a new high
        if (DevExt->CurrentQueueDepth > DevExt->MaxQueueDepthReached) {
            DevExt->MaxQueueDepthReached = DevExt->CurrentQueueDepth;
        }
    }

    return result;
}

BOOLEAN NvmeSubmitAdminCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd)
{
    return NvmeSubmitCommand(DevExt, &DevExt->AdminQueue, Cmd);
}

BOOLEAN NvmeCreateIoCQ(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_CREATE_CQ;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_CREATE_IO_CQ;
    cmd.CDW10 = ((DevExt->IoQueue.QueueSize - 1) << 16) | DevExt->IoQueue.QueueId;
    // CDW11: PC=1 (bit 0), IEN=1 (bit 1) to enable interrupts, IV=0 (bits 31:16)
    cmd.CDW11 = NVME_QUEUE_PHYS_CONTIG | NVME_QUEUE_IRQ_ENABLED | (0 << 16);
    cmd.PRP1 = DevExt->IoQueue.CompletionQueuePhys.QuadPart;

    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

BOOLEAN NvmeCreateIoSQ(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_CREATE_SQ;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_CREATE_IO_SQ;
    cmd.CDW10 = ((DevExt->IoQueue.QueueSize - 1) << 16) | DevExt->IoQueue.QueueId;
    cmd.CDW11 = NVME_QUEUE_PHYS_CONTIG | (DevExt->IoQueue.QueueId << 16);  // CQID
    cmd.PRP1 = DevExt->IoQueue.SubmissionQueuePhys.QuadPart;

    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

//
// NvmeIdentifyController - Send Identify Controller command
//
BOOLEAN NvmeIdentifyController(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    // Build Identify Controller command
    cmd.CDW0.Fields.Opcode = NVME_ADMIN_IDENTIFY;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_IDENTIFY_CONTROLLER;
    cmd.NSID = 0;  // Not used for controller identify
    cmd.PRP1 = DevExt->UtilityBufferPhys.QuadPart;
    cmd.PRP2 = 0;  // Single page transfer, no PRP2 needed
    cmd.CDW10 = NVME_CNS_CONTROLLER;

#ifdef NVME2K_DBG_CMD
    ScsiDebugPrint(0, "nvme2k: NvmeIdentifyController - CDW0=%08X (OPC=%02X CID=%04X) NSID=%08X PRP1=%08X%08X CDW10=%08X\n",
                   cmd.CDW0.AsUlong, cmd.CDW0.Fields.Opcode, cmd.CDW0.Fields.CommandId,
                   cmd.NSID, (ULONG)(cmd.PRP1 >> 32), (ULONG)(cmd.PRP1 & 0xFFFFFFFF),
                   cmd.CDW10);
#endif
    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

//
// NvmeIdentifyNamespace - Send Identify Namespace command
//
BOOLEAN NvmeIdentifyNamespace(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    // Build Identify Namespace command
    cmd.CDW0.Fields.Opcode = NVME_ADMIN_IDENTIFY;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_IDENTIFY_NAMESPACE;
    cmd.NSID = 1;  // Namespace ID 1
    cmd.PRP1 = DevExt->UtilityBufferPhys.QuadPart;
    cmd.PRP2 = 0;  // Single page transfer, no PRP2 needed
    cmd.CDW10 = NVME_CNS_NAMESPACE;

#ifdef NVME2K_DBG_CMD
    ScsiDebugPrint(0, "nvme2k: NvmeIdentifyNamespace - CDW0=%08X (OPC=%02X CID=%04X) NSID=%08X PRP1=%08X%08X CDW10=%08X\n",
                   cmd.CDW0.AsUlong, cmd.CDW0.Fields.Opcode, cmd.CDW0.Fields.CommandId,
                   cmd.NSID, (ULONG)(cmd.PRP1 >> 32), (ULONG)(cmd.PRP1 & 0xFFFFFFFF),
                   cmd.CDW10);
#endif
    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

//
// NvmeGetLogPage - Retrieve a log page from NVMe device asynchronously
// Uses PRP page allocator for DMA buffer, untagged operation
// ScsiPort guarantees only one untagged request at a time
//
BOOLEAN NvmeGetLogPage(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR LogPageId)
{
    NVME_COMMAND cmd;
    PHYSICAL_ADDRESS physAddr;
    PVOID prpPageVirt;
    UCHAR prpPageIndex;
    ULONG numdl;
    PNVME_SRB_EXTENSION srbExt;

    // Allocate a PRP page for the log data buffer (4KB)
    prpPageIndex = AllocatePrpListPage(DevExt);
    if (prpPageIndex == 0xFF) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeGetLogPage - Failed to allocate PRP page\n");
#endif
        return FALSE;
    }

    // Zero out the page to prevent stale data issues
    prpPageVirt = GetPrpListPageVirtual(DevExt, prpPageIndex);
    RtlZeroMemory(prpPageVirt, PAGE_SIZE);

    // Get physical address of the PRP buffer
    physAddr = DevExt->PrpListPagesPhys;
    physAddr.QuadPart += (prpPageIndex * PAGE_SIZE);

    // Store PRP page index in SRB extension so we can free it on completion
    if (!Srb->SrbExtension) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeGetLogPage - SRB has no extension\n");
#endif
        FreePrpListPage(DevExt, prpPageIndex);
        return FALSE;
    }

    srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    srbExt->PrpListPage = prpPageIndex;

    // Calculate NUMDL (number of dwords - 1) for 512 bytes (SMART log size)
    numdl = (512 / 4) - 1;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_GET_LOG_PAGE;
    cmd.CDW0.Fields.CommandId = (ADMIN_CID_GET_LOG_PAGE + prpPageIndex) | CID_NON_TAGGED_FLAG;
    cmd.NSID = 0xFFFFFFFF;  // Global log page (not namespace-specific)
    cmd.PRP1 = (ULONGLONG)physAddr.LowPart | ((ULONGLONG)physAddr.HighPart << 32);
    cmd.PRP2 = 0;  // Single page transfer

    // Bits 31:16 = NUMDL (Number of Dwords Lower)
    // Bits 15:08 = Reserved
    // Bits 07:00 = LID (Log Page Identifier)
    cmd.CDW10 = (LogPageId & 0xFF) | ((numdl & 0xFFFF) << 16);

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: NvmeGetLogPage - LID=0x%02X PRP=%u Phys=%08X%08X\n",
                   LogPageId, prpPageIndex, physAddr.HighPart, physAddr.LowPart);
#endif

    // ScsiPort manages the untagged SRB, we can retrieve it in completion with ScsiPortGetSrb()
    
    DevExt->NonTaggedInFlight = Srb;
    if (!NvmeSubmitAdminCommand(DevExt, &cmd)) {
        DevExt->NonTaggedInFlight = NULL;
        return FALSE;
    } else {
        return TRUE;
    }
}

//
// NvmeBuildCommandId - Build NVMe Command ID from SRB
// For tagged requests: Use QueueTag directly (bit 15 clear)
// For non-tagged requests: Generate sequence number with bit 15 set
//
USHORT NvmeBuildCommandId(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    USHORT commandId;

    // Check if this is a tagged request
    // Note: QueueTag == SP_UNTAGGED (0xFF) means non-tagged even if SRB_FLAGS_QUEUE_ACTION_ENABLE is set
    if ((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb->QueueTag != SP_UNTAGGED)) {
        // Tagged request - use QueueTag directly
        commandId = (USHORT)Srb->QueueTag & CID_VALUE_MASK;
    } else {
        // Non-tagged request - generate sequence number with flag bit set
        commandId = (DevExt->NextNonTaggedId & CID_VALUE_MASK) | CID_NON_TAGGED_FLAG;
        DevExt->NextNonTaggedId++;
        // Wrap around at 15 bits
        if (DevExt->NextNonTaggedId >= 0x8000) {
            DevExt->NextNonTaggedId = 0;
        }
    }

    return commandId;
}

//
// NvmeBuildFlushCommandId - Build CID for ORDERED tag flush command
// Uses the SRB's QueueTag with the ORDERED_FLUSH flag bit set
//
USHORT NvmeBuildFlushCommandId(IN PSCSI_REQUEST_BLOCK Srb)
{
    USHORT commandId;

    // For ORDERED flush, use QueueTag with flush flag bit set
    commandId = ((USHORT)Srb->QueueTag & CID_VALUE_MASK) | CID_ORDERED_FLUSH_FLAG;

    return commandId;
}

//
// NvmeGetSrbFromCommandId - Retrieve SRB from Command ID
// Decodes the CID and calls ScsiPortGetSrb with appropriate parameters
//
PSCSI_REQUEST_BLOCK NvmeGetSrbFromCommandId(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT commandId)
{
    LONG queueTag;
    PSCSI_REQUEST_BLOCK Srb;

    // Check if this is a non-tagged request (bit 15 set)
    if (commandId & CID_NON_TAGGED_FLAG) {
        // Non-tagged request - use SP_UNTAGGED
        queueTag = SP_UNTAGGED;
    } else if (commandId & CID_ORDERED_FLUSH_FLAG) {
        // There is no actual SRB for the flush, the next command has SRB
        return NULL;
    } else {
        // Normal tagged request - extract QueueTag
        queueTag = (LONG)(commandId & CID_VALUE_MASK);
    }

    // Call ScsiPortGetSrb to retrieve the SRB
    // PathId=0, TargetId=0, Lun=0 for our single device
    Srb = ScsiPortGetSrb(DevExt, 0, 0, 0, queueTag);
    if (queueTag == SP_UNTAGGED) {
        if (!Srb)
            Srb = DevExt->NonTaggedInFlight;
        DevExt->NonTaggedInFlight = NULL;
    }
    return Srb;
}

//
// NvmeBuildReadWriteCommand - Build NVMe Read/Write command from SCSI CDB
//
BOOLEAN NvmeBuildReadWriteCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN PNVME_COMMAND Cmd, IN USHORT CommandId)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    ULONGLONG lba = 0;
    ULONG numBlocks = 0;
    BOOLEAN isWrite = FALSE;
    PHYSICAL_ADDRESS physAddr;
    PHYSICAL_ADDRESS physAddr2;
    ULONG length;
    ULONG pageSize = 4096;
    ULONG offsetInPage;
    ULONG firstPageBytes;
    PVOID currentPageVirtual;
    ULONG remainingBytes;
    ULONG currentOffset;
    UCHAR prpListPage;
    PULONGLONG prpList;
    PHYSICAL_ADDRESS prpListPhys;
    ULONG prpIndex;
    ULONG numPrpEntries;
    PNVME_SRB_EXTENSION srbExt;

    // Initialize SRB extension
    srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    srbExt->PrpListPage = 0xFF;  // No PRP list initially

    // Parse CDB based on opcode
    switch (cdb->CDB10.OperationCode) {
        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
            lba = ((ULONG)(cdb->CDB6READWRITE.LogicalBlockMsb1) << 16) |
                  ((ULONG)(cdb->CDB6READWRITE.LogicalBlockMsb0) << 8) |
                  ((ULONG)(cdb->CDB6READWRITE.LogicalBlockLsb));
            numBlocks = cdb->CDB6READWRITE.TransferBlocks;
            if (numBlocks == 0) {
                numBlocks = 256;  // 0 means 256 blocks in READ(6)/WRITE(6)
            }
            isWrite = (cdb->CDB6READWRITE.OperationCode == SCSIOP_WRITE6);
            break;

        case SCSIOP_READ:
        case SCSIOP_WRITE:
            lba = ((ULONG)cdb->CDB10.LogicalBlockByte0 << 24) |
                  ((ULONG)cdb->CDB10.LogicalBlockByte1 << 16) |
                  ((ULONG)cdb->CDB10.LogicalBlockByte2 << 8) |
                  ((ULONG)cdb->CDB10.LogicalBlockByte3);
            numBlocks = ((ULONG)cdb->CDB10.TransferBlocksMsb << 8) |
                        ((ULONG)cdb->CDB10.TransferBlocksLsb);
            isWrite = (cdb->CDB10.OperationCode == SCSIOP_WRITE);
            break;
    }

    // Build NVMe command
    if (isWrite) {
        Cmd->CDW0.Fields.Opcode = NVME_CMD_WRITE;
    } else {
        Cmd->CDW0.Fields.Opcode = NVME_CMD_READ;
    }
    Cmd->CDW0.Fields.Flags = NVME_CMD_PRP;
    Cmd->CDW0.Fields.CommandId = CommandId;

    Cmd->NSID = 1;  // Namespace ID 1

    // Track I/O statistics
    DevExt->TotalRequests++;
    if (isWrite) {
        DevExt->TotalWrites++;
        DevExt->TotalBytesWritten += Srb->DataTransferLength;
        if (Srb->DataTransferLength > DevExt->MaxWriteSize) {
            DevExt->MaxWriteSize = Srb->DataTransferLength;
        }
    } else {
        DevExt->TotalReads++;
        DevExt->TotalBytesRead += Srb->DataTransferLength;
        if (Srb->DataTransferLength > DevExt->MaxReadSize) {
            DevExt->MaxReadSize = Srb->DataTransferLength;
        }
    }
#if NVME2K_DBG_STATS
    // Print statistics every 10000 requests
    // Note: QDepth tracking removed (always 0) since we no longer store SRBs
    if ((DevExt->TotalRequests % 10000) == 0) {
        ScsiDebugPrint(0, "nvme2k: Stats - Reqs=%u R=%u W=%u BytesR=%I64u BytesW=%I64u MaxR=%u MaxW=%u PRP=%u/%u Rejected=%u\n",
                       DevExt->TotalRequests,
                       DevExt->TotalReads,
                       DevExt->TotalWrites,
                       DevExt->TotalBytesRead,
                       DevExt->TotalBytesWritten,
                       DevExt->MaxReadSize,
                       DevExt->MaxWriteSize,
                       DevExt->CurrentPrpListPagesUsed,
                       DevExt->MaxPrpListPagesUsed,
                       DevExt->RejectedRequests);
    }
#endif
    // Get physical address of data buffer
    length = Srb->DataTransferLength;
    physAddr = ScsiPortGetPhysicalAddress(DevExt, Srb, Srb->DataBuffer, &length);

#ifdef NVME2K_DBG_CMD
    ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - DataBuffer=%p TransferLen=%u PhysAddr=%08X%08X ReturnedLen=%u\n",
                   Srb->DataBuffer, Srb->DataTransferLength,
                   (ULONG)(physAddr.QuadPart >> 32), (ULONG)(physAddr.QuadPart & 0xFFFFFFFF),
                   length);
#endif
    // Set PRP1 to the start of the data
    Cmd->PRP1 = physAddr.QuadPart;

    // Calculate offset within the page
    offsetInPage = (ULONG)(physAddr.QuadPart & (PAGE_SIZE - 1));

    // Calculate how many bytes fit in the first page
    firstPageBytes = PAGE_SIZE - offsetInPage;

    // Determine if we need PRP2 or a PRP list
    if (Srb->DataTransferLength <= firstPageBytes) {
        // Transfer fits in one page
        Cmd->PRP2 = 0;
#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - Single page transfer, PRP2=0\n");
#endif
    } else if (Srb->DataTransferLength <= (firstPageBytes + PAGE_SIZE)) {
        // Transfer spans exactly 2 pages, use PRP2 directly
        currentPageVirtual = (PVOID)((PUCHAR)Srb->DataBuffer + firstPageBytes);
        length = Srb->DataTransferLength - firstPageBytes;
        physAddr2 = ScsiPortGetPhysicalAddress(DevExt, Srb, currentPageVirtual, &length);

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - Two page transfer: PhysAddr2=%08X%08X\n",
                       (ULONG)(physAddr2.QuadPart >> 32), (ULONG)(physAddr2.QuadPart & 0xFFFFFFFF));
#endif
        Cmd->PRP2 = physAddr2.QuadPart;
    } else {
        // Transfer spans more than 2 pages, need PRP list
        prpListPage = AllocatePrpListPage(DevExt);
        if (prpListPage == 0xFF) {
            // No PRP list pages available - this shouldn't happen if we sized correctly
            ScsiDebugPrint(0, "nvme2k: ERROR - No PRP list pages available!\n");
            Cmd->PRP2 = 0;
            return FALSE;
        }

        // Store PRP list page in SRB extension
        {
            PNVME_SRB_EXTENSION srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
            srbExt->PrpListPage = prpListPage;
        }

        // Get virtual and physical addresses of PRP list
        prpList = (PULONGLONG)GetPrpListPageVirtual(DevExt, prpListPage);
        prpListPhys = GetPrpListPagePhysical(DevExt, prpListPage);

        // Build PRP list for remaining pages
        remainingBytes = Srb->DataTransferLength - firstPageBytes;
        currentOffset = firstPageBytes;
        prpIndex = 0;

        while (remainingBytes > 0 && prpIndex < 512) {
            currentPageVirtual = (PVOID)((PUCHAR)Srb->DataBuffer + currentOffset);
            length = remainingBytes;
            physAddr2 = ScsiPortGetPhysicalAddress(DevExt, Srb, currentPageVirtual, &length);

            prpList[prpIndex] = physAddr2.QuadPart;
            prpIndex++;

            if (remainingBytes <= PAGE_SIZE) {
                break;
            }

            remainingBytes -= PAGE_SIZE;
            currentOffset += PAGE_SIZE;
        }

        numPrpEntries = prpIndex;

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - PRP list: page=%u entries=%u listPhys=%08X%08X\n",
                       prpListPage, numPrpEntries,
                       (ULONG)(prpListPhys.QuadPart >> 32), (ULONG)(prpListPhys.QuadPart & 0xFFFFFFFF));
#endif

        // Set PRP2 to point to the PRP list
        Cmd->PRP2 = prpListPhys.QuadPart;
    }

    // Set LBA and number of blocks (0-based, so subtract 1)
    Cmd->CDW10 = (ULONG)(lba & 0xFFFFFFFF);
    Cmd->CDW11 = (ULONG)(lba >> 32);
    Cmd->CDW12 = (numBlocks > 0) ? (numBlocks - 1) : 0;
    Cmd->CDW13 = 0;
    Cmd->CDW14 = 0;
    Cmd->CDW15 = 0;
    return TRUE;
}

//
// NvmeShutdownController - Perform clean shutdown of NVMe controller
// Deletes I/O queues, issues shutdown notification, and disables controller
// Robust version that handles partially initialized or unknown controller state
//
VOID NvmeShutdownController(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG cc, csts;
    ULONG timeoutMs = 5000;  // 5 second timeout
    ULONG elapsed = 0;
    ULONG shutdownStatus;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: NvmeShutdownController - starting shutdown sequence\n");
#endif

    // First, mask all interrupts to prevent interrupt storms during shutdown
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: NvmeShutdownController - masked all interrupts\n");
#endif

    // Check if controller is even enabled before trying to delete queues
    csts = NvmeReadReg32(DevExt, NVME_REG_CSTS);
    if (!(csts & NVME_CSTS_RDY)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeShutdownController - controller not ready, skipping queue deletion\n");
#endif
        // Controller is already disabled, just clean up state and return
        goto cleanup_state;
    }

    // Step 1: Delete I/O Submission Queue (must be deleted before CQ)
    if (DevExt->InitComplete && DevExt->IoQueue.SubmissionQueue != NULL) {
        NVME_COMMAND cmd;

        RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));
        cmd.CDW0.Fields.Opcode = NVME_ADMIN_DELETE_SQ;
        cmd.CDW0.Fields.CommandId = ADMIN_CID_SHUTDOWN_DELETE_SQ;
        cmd.CDW10 = 1;  // QID = 1 (I/O queue)

#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: Deleting I/O Submission Queue\n");
#endif
        NvmeSubmitAdminCommand(DevExt, &cmd);

        // Poll for completion (simple polling, no interrupt)
        timeoutMs = 1000; // 1 second timeout
        elapsed = 0;
        while (elapsed < timeoutMs) {
            if (NvmeProcessAdminCompletion(DevExt)) {
                break;
            }
            ScsiPortStallExecution(1000);  // 1 millisecond
            elapsed++;
        }
    }

    // Step 2: Delete I/O Completion Queue
    if (DevExt->InitComplete && DevExt->IoQueue.CompletionQueue != NULL) {
        NVME_COMMAND cmd;

        RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));
        cmd.CDW0.Fields.Opcode = NVME_ADMIN_DELETE_CQ;
        cmd.CDW0.Fields.CommandId = ADMIN_CID_SHUTDOWN_DELETE_CQ;
        cmd.CDW10 = 1;  // QID = 1 (I/O queue)

#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: Deleting I/O Completion Queue\n");
#endif
        NvmeSubmitAdminCommand(DevExt, &cmd);

        // Poll for completion
        timeoutMs = 1000; // 1 second timeout
        elapsed = 0;
        while (elapsed < timeoutMs) {
            if (NvmeProcessAdminCompletion(DevExt)) {
                break;
            }
            ScsiPortStallExecution(1000); // 1 millisecond
            elapsed++;
        }
    }

    // Step 3: Issue shutdown notification
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);

    // Clear existing shutdown bits and set normal shutdown
    cc = (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL;
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc);

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: Shutdown notification sent, waiting for completion\n");
#endif

    // Step 4: Wait for shutdown to complete (CSTS.SHST = 10b)
    timeoutMs = 5000; // 5 second timeout
    elapsed = 0;
    while (elapsed < timeoutMs) {
        csts = NvmeReadReg32(DevExt, NVME_REG_CSTS);
        shutdownStatus = csts & NVME_CSTS_SHST_MASK;

        if (shutdownStatus == NVME_CSTS_SHST_COMPLETE) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Shutdown complete\n");
#endif
            break;
        }
        ScsiPortStallExecution(1000); // 1 millisecond
        elapsed++;
    }

    if (elapsed >= timeoutMs) {
        ScsiDebugPrint(0, "nvme2k: WARNING - Shutdown timeout, CSTS=0x%08X\n", csts);
        // Controller may not be in a clean state, but we must continue shutdown
    }

    // Step 6: Disable the controller (CC.EN = 0)
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    cc &= ~NVME_CC_ENABLE;
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc);

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: Controller disabled\n");
#endif

    // Step 7: Wait for controller to become not ready (CSTS.RDY = 0)
    // This is critical - controller must be fully stopped before driver unload
    if (!NvmeWaitForReady(DevExt, FALSE)) {
        csts = NvmeReadReg32(DevExt, NVME_REG_CSTS);
        ScsiDebugPrint(0, "nvme2k: WARNING - Controller failed to clear RDY bit, CSTS=0x%08X\n", csts);
        // Continue anyway - we've done what we can
    }
#ifdef NVME2K_DBG
    else {
        ScsiDebugPrint(0, "nvme2k: Controller RDY bit cleared\n");
    }
#endif

    // Step 5 (Moved): Clear admin queue hardware registers AFTER disabling controller
    // This ensures the controller doesn't try to access stale queue pointers during shutdown
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: Clearing admin queue registers\n");
#endif

    NvmeWriteReg32(DevExt, NVME_REG_AQA, 0);
    NvmeWriteReg64(DevExt, NVME_REG_ASQ, 0);
    NvmeWriteReg64(DevExt, NVME_REG_ACQ, 0);

cleanup_state:
    // Step 8: Reset queue software state to match hardware reset state
    // When controller is disabled (CC.EN=0), hardware resets doorbell registers to 0
    // We must reset our software state to match, otherwise re-initialization will fail
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: Resetting queue state\n");
#endif

    // Reset Admin Queue state
    DevExt->AdminQueue.SubmissionQueueHead = 0;
    DevExt->AdminQueue.SubmissionQueueTail = 0;
    // Reset CQ head to QueueSize to re-establish phase bit as 1 for next init
    DevExt->AdminQueue.CompletionQueueHead = DevExt->AdminQueue.QueueSize;
    DevExt->AdminQueue.CompletionQueueTail = 0;
#ifdef NVME2K_USE_SUBMISSION_LOCK
    DevExt->AdminQueue.SubmissionLock = 0;
#endif
#ifdef NVME2K_USE_COMPLETION_LOCK
    DevExt->AdminQueue.CompletionLock = 0;
#endif

    // Reset I/O Queue state
    DevExt->IoQueue.SubmissionQueueHead = 0;
    DevExt->IoQueue.SubmissionQueueTail = 0;
    // Reset CQ head to QueueSize to re-establish phase bit as 1 for next init
    DevExt->IoQueue.CompletionQueueHead = DevExt->IoQueue.QueueSize;
    DevExt->IoQueue.CompletionQueueTail = 0;
#ifdef NVME2K_USE_SUBMISSION_LOCK
    DevExt->IoQueue.SubmissionLock = 0;
#endif
#ifdef NVME2K_USE_COMPLETION_LOCK
    DevExt->IoQueue.CompletionLock = 0;
#endif

    // Clear init state
    DevExt->InitComplete = FALSE;
    DevExt->NonTaggedInFlight = NULL;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: Shutdown sequence complete\n");
#endif
}
