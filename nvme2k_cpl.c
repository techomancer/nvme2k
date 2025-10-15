// handling NVME completions
#include "nvme2k.h"
#include "utils.h"

//
// NvmeProcessGetLogPageCompletion - Handle Get Log Page command completion
//
VOID NvmeProcessGetLogPageCompletion(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT status, USHORT commandId)
{
    PSCSI_REQUEST_BLOCK Srb;
    PVOID prpBuffer;
    PNVME_SRB_EXTENSION srbExt;
    UCHAR prpPageIndex = (UCHAR)((commandId & CID_VALUE_MASK) - ADMIN_CID_GET_LOG_PAGE);

    // Get the untagged SRB from ScsiPort
    // ScsiPort guarantees only one untagged request at a time
    Srb = NvmeGetSrbFromCommandId(DevExt, commandId);

    if (!Srb) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: Get Log Page completion - ScsiPortGetSrb returned NULL\n");
#endif
        // we can use prpPageIndex to free the log page so we dont leak them
    } else {
        // Get PRP page index from SRB extension
        srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;

#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: GetLogPageCpl - CID=%u PRP=%u srbPRP=%u Status=0x%04X\n",
                       ADMIN_CID_GET_LOG_PAGE, prpPageIndex, srbExt->PrpListPage, status);
        if (srbExt->PrpListPage != prpPageIndex) {
            ScsiDebugPrint(0, "nvme2k: THIS IS BAD\n");
        }
#endif
        prpBuffer = (PVOID)((PUCHAR)DevExt->PrpListPages + (prpPageIndex * PAGE_SIZE));

        // Determine the request type: LOG SENSE, SAT PASS-THROUGH, or SMART IOCTL
        if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI && Srb->Cdb[0] == SCSIOP_LOG_SENSE) {
            // This is a LOG SENSE completion
            if (status == NVME_SC_SUCCESS && prpBuffer) {
                PNVME_SMART_INFO nvmeSmart = (PNVME_SMART_INFO)prpBuffer;
                UCHAR pageCode = ScsiGetLogPageCodeFromSrb(Srb);
                ULONG bytesWritten = 0;

                // Convert NVMe log page to proper SCSI log page format
                if (NvmeLogPageToScsiLogPage(nvmeSmart, pageCode, Srb->DataBuffer,
                                             Srb->DataTransferLength, &bytesWritten)) {
                    Srb->DataTransferLength = bytesWritten;
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                } else {
                    Srb->DataTransferLength = 0;
                    Srb->SrbStatus = SRB_STATUS_ERROR;
                }
            } else {
                Srb->DataTransferLength = 0;
                Srb->SrbStatus = SRB_STATUS_ERROR;
            }
        } else if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
                   (Srb->Cdb[0] == SCSIOP_ATA_PASSTHROUGH16 || Srb->Cdb[0] == SCSIOP_ATA_PASSTHROUGH12)) {
            // This is a SAT ATA PASS-THROUGH completion
            if (status == NVME_SC_SUCCESS && prpBuffer) {
                UCHAR ataCommand;
                UCHAR ataFeatures;
                UCHAR ataCylLow;
                UCHAR ataCylHigh;

                if (!ScsiParseSatCommand(Srb, &ataCommand, &ataFeatures, &ataCylLow, &ataCylHigh)) {
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                } else {
                    // Check which ATA command was requested
                    if (ataCommand == ATA_SMART_CMD && ataFeatures == ATA_SMART_READ_DATA) {
                        // SMART READ DATA - convert NVMe SMART to ATA SMART format
                        // For SAT commands, the data buffer is the raw 512-byte payload.
                        // It does NOT include a SENDCMDOUTPARAMS header.
                        PNVME_SMART_INFO nvmeSmart = (PNVME_SMART_INFO)prpBuffer;
                        PATA_SMART_DATA ataSmart = (PATA_SMART_DATA)Srb->DataBuffer;

                        NvmeSmartToAtaSmart(nvmeSmart, ataSmart);

                        Srb->DataTransferLength = 512;
                        Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    } else {
                        Srb->SrbStatus = SRB_STATUS_ERROR;
                    }
                }
            } else {
                Srb->DataTransferLength = 0;
                Srb->SrbStatus = SRB_STATUS_ERROR;
            }
        } else if (Srb->Function == SRB_FUNCTION_IO_CONTROL) {
            // This is a SMART IOCTL completion
            PSRB_IO_CONTROL srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;
            PSENDCMDOUTPARAMS sendCmdOut = (PSENDCMDOUTPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));

            if (status == NVME_SC_SUCCESS && prpBuffer) {
                PNVME_SMART_INFO nvmeSmart = (PNVME_SMART_INFO)prpBuffer;
                PATA_SMART_DATA ataSmart = (PATA_SMART_DATA)sendCmdOut->bBuffer;

                // Convert NVMe SMART/Health log to ATA SMART format
                NvmeSmartToAtaSmart(nvmeSmart, ataSmart);

                sendCmdOut->cBufferSize = 512;
                RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                sendCmdOut->DriverStatus.bDriverError = 0;
                sendCmdOut->DriverStatus.bIDEError = 0;

                srbControl->ReturnCode = 0;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;

            } else {
                // Command failed
                sendCmdOut->cBufferSize = 0;
                sendCmdOut->DriverStatus.bDriverError = 1;
                sendCmdOut->DriverStatus.bIDEError = 0x04;  // Aborted
                srbControl->ReturnCode = 1;
                Srb->SrbStatus = SRB_STATUS_ERROR;
            }
        } else {
            // Unknown request type for Get Log Page
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Get Log Page completion for unknown SRB function 0x%02X\n", Srb->Function);
#endif
            Srb->SrbStatus = SRB_STATUS_ERROR;
        }

        // Complete the SRB, scsiport takes control
        ScsiPortNotification(RequestComplete, DevExt, Srb);
    }

    DevExt->NonTaggedInFlight = FALSE;
    DevExt->NonTaggedSrbFallback = NULL;
    FreePrpListPage(DevExt, prpPageIndex);
}

//
// NvmeProcessAdminCompletion - Process admin queue completions
//
BOOLEAN NvmeProcessAdminCompletion(IN PHW_DEVICE_EXTENSION DevExt)
{
    PNVME_QUEUE Queue = &DevExt->AdminQueue;
    PNVME_COMPLETION cqEntry;
    BOOLEAN processed = FALSE;
    USHORT status;
    USHORT commandId;
    PNVME_IDENTIFY_NAMESPACE nsData;
    ULONG queueIndex;
    ULONG expectedPhase;

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Acquire completion queue spinlock - protects against concurrent interrupt handler calls
    if (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeProcessAdminCompletion - lock contention on QID=%d (spinning)\n", Queue->QueueId);
#endif
        while (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
            // Spin waiting for lock
        }
    }
#endif

    while (TRUE) {
        // Calculate queue index from current head
        queueIndex = Queue->CompletionQueueHead & Queue->QueueSizeMask;

        // Calculate expected phase from counter (toggles every QueueSize completions)
        expectedPhase = (Queue->CompletionQueueHead >> Queue->QueueSizeBits) & 1;

        // Get completion queue entry
        cqEntry = (PNVME_COMPLETION)((PUCHAR)Queue->CompletionQueue +
                                     (queueIndex * NVME_CQ_ENTRY_SIZE));

        // Check phase bit
        if ((cqEntry->Status & 1u) != expectedPhase) {
            // No more completions
            break;
        }

        processed = TRUE;

        // Extract status and command ID
        status = (cqEntry->Status >> 1) & 0xFF;
        commandId = cqEntry->CID;

        // Update submission queue head from completion entry
        Queue->SubmissionQueueHead = cqEntry->SQHead;

        // Increment completion queue head
        Queue->CompletionQueueHead++;

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeProcessAdminCompletion - CID=%d Status=0x%04X SQHead=%d\n",
                       commandId, status, Queue->SubmissionQueueHead);
#endif
        if (!DevExt->InitComplete) {
            switch (commandId) {
                case ADMIN_CID_CREATE_IO_CQ:
                    if (status == NVME_SC_SUCCESS) {
                        NvmeCreateIoSQ(DevExt);
                    }
                    break;

                case ADMIN_CID_CREATE_IO_SQ:
                    if (status == NVME_SC_SUCCESS) {
                        NvmeIdentifyController(DevExt);
                    }
                    break;

                case ADMIN_CID_IDENTIFY_CONTROLLER:
                    if (status == NVME_SC_SUCCESS) {
                        PNVME_IDENTIFY_CONTROLLER ctrlData = (PNVME_IDENTIFY_CONTROLLER)DevExt->UtilityBuffer;

                        // Copy and null-terminate strings
                        RtlCopyMemory(DevExt->ControllerSerialNumber, ctrlData->SerialNumber, 20);
                        DevExt->ControllerSerialNumber[20] = 0;

                        RtlCopyMemory(DevExt->ControllerModelNumber, ctrlData->ModelNumber, 40);
                        DevExt->ControllerModelNumber[40] = 0;

                        RtlCopyMemory(DevExt->ControllerFirmwareRevision, ctrlData->FirmwareRevision, 8);
                        DevExt->ControllerFirmwareRevision[8] = 0;

                        DevExt->NumberOfNamespaces = ctrlData->NumberOfNamespaces;

#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: Identified controller - Model: %.40s SN: %.20s FW: %.8s NN: %u\n",
                                    DevExt->ControllerModelNumber, DevExt->ControllerSerialNumber,
                                    DevExt->ControllerFirmwareRevision, DevExt->NumberOfNamespaces);
#endif
                        NvmeIdentifyNamespace(DevExt);
                    }
                    break;

                case ADMIN_CID_IDENTIFY_NAMESPACE:
                    if (status == NVME_SC_SUCCESS) {
                        nsData = (PNVME_IDENTIFY_NAMESPACE)DevExt->UtilityBuffer;
                        DevExt->NamespaceSizeInBlocks = nsData->NamespaceSize;

                        // Extract block size from formatted LBA size
                        DevExt->NamespaceBlockSize = 1 << (nsData->FormattedLbaSize & 0x0F);
                        if (DevExt->NamespaceBlockSize == 1) {
                            DevExt->NamespaceBlockSize = 512;  // Default
                        }

#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: Identified namespace - blocks=%I64u blocksize=%u bytes\n",
                                    DevExt->NamespaceSizeInBlocks, DevExt->NamespaceBlockSize);
#endif

                        DevExt->InitComplete = TRUE;
                    }
                    break;

                default:
#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: unknown init time admin CID %04X\n", commandId);
#endif
            }
        } else {
            // Post-initialization admin commands
            // Check if this is a Get Log Page command
            if ((commandId & CID_VALUE_MASK) >= ADMIN_CID_GET_LOG_PAGE && (commandId & CID_VALUE_MASK)< ADMIN_CID_GET_LOG_PAGE + SG_LIST_PAGES) {
                NvmeProcessGetLogPageCompletion(DevExt, status, commandId);
            } else {
                if (ADMIN_CID_SHUTDOWN_DELETE_SQ == commandId) {
                    if (status != NVME_SC_SUCCESS) {
#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: SHUTDOWN_DELETE_SQ failed with status 0x%04X\n", status);
#endif
                    }
                } else if (ADMIN_CID_SHUTDOWN_DELETE_CQ == commandId) {
                    if (status != NVME_SC_SUCCESS) {
#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: SHUTDOWN_DELETE_CQ failed with status 0x%04X\n", status);
#endif
                    }
                } else {
#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: unknown admin CID %04X\n", commandId);
#endif
                }
            }
        }

        // Ring completion doorbell with the new head position
        NvmeRingDoorbell(DevExt, Queue->QueueId, FALSE, (USHORT)(Queue->CompletionQueueHead & Queue->QueueSizeMask));
    }

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Release completion queue spinlock
    AtomicSet(&Queue->CompletionLock, 0);
#endif

    return processed;
}

//
// NvmeProcessIoCompletion - Process I/O queue completions
//
BOOLEAN NvmeProcessIoCompletion(IN PHW_DEVICE_EXTENSION DevExt)
{
    PNVME_QUEUE Queue = &DevExt->IoQueue;
    PNVME_COMPLETION cqEntry;
    BOOLEAN processed = FALSE;
    USHORT status;
    USHORT commandId;
    PSCSI_REQUEST_BLOCK Srb;
    ULONG queueIndex;
    ULONG expectedPhase;

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Acquire completion queue spinlock - protects against concurrent interrupt handler calls
    if (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeProcessIoCompletion - lock contention on QID=%d (spinning)\n", Queue->QueueId);
#endif
        while (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
            // Spin waiting for lock
        }
    }
#endif

    while (TRUE) {
        // Calculate queue index from current head
        queueIndex = Queue->CompletionQueueHead & Queue->QueueSizeMask;

        // Calculate expected phase from counter (toggles every QueueSize completions)
        expectedPhase = (Queue->CompletionQueueHead >> Queue->QueueSizeBits) & 1;

        // Get completion queue entry
        cqEntry = (PNVME_COMPLETION)((PUCHAR)Queue->CompletionQueue +
                                     (queueIndex * NVME_CQ_ENTRY_SIZE));

        // Check phase bit
        if ((cqEntry->Status & 1u) != expectedPhase) {
            // No more completions
            break;
        }

        processed = TRUE;

        // Extract status and command ID
        status = (cqEntry->Status >> 1) & 0xFF;
        commandId = cqEntry->CID;

        // Update submission queue head from completion entry
        Queue->SubmissionQueueHead = cqEntry->SQHead;

        // Increment completion queue head
        Queue->CompletionQueueHead++;

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeProcessIoCompletion - CID=%d Status=0x%04X SQHead=%d\n",
                       commandId, status, Queue->SubmissionQueueHead);
#endif
        // Retrieve SRB from command ID using ScsiPortGetSrb
        Srb = NvmeGetSrbFromCommandId(DevExt, commandId);

        if (Srb == NULL) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: ERROR - Got NULL SRB for CID=%d! This should not happen.\n", commandId);
#endif
            // Clear non-tagged flag to prevent driver from getting stuck
            if (commandId & CID_NON_TAGGED_FLAG) {
#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: Clearing NonTaggedInFlight flag for orphaned CID\n");
#endif
                DevExt->NonTaggedInFlight = FALSE;
            }
        } else {
            PNVME_SRB_EXTENSION srbExt;

            // Validate SRB before processing
            if (Srb->SrbStatus != SRB_STATUS_PENDING) {
#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: ERROR - SRB CID=%d has invalid status 0x%02X (expected PENDING=0x%02X)\n",
                               commandId, Srb->SrbStatus, SRB_STATUS_PENDING);
                ScsiDebugPrint(0, "nvme2k:        This suggests DOUBLE COMPLETION! SRB=%p\n", Srb);
#endif
                // Skip this - it's already been completed
                goto io_ring_doorbell;
            }

            if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI) {
#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: ERROR - SRB CID=%d has invalid function 0x%02X\n",
                               commandId, Srb->Function);
#endif
            }

            if (!Srb->SrbExtension) {
#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: ERROR - SRB CID=%d has NULL SrbExtension!\n", commandId);
#endif
                // Skip this completion
                goto io_ring_doorbell;
            }

            // Get SRB extension for PRP list cleanup
            srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;

            // Free PRP list page if allocated
            if (srbExt->PrpListPage != 0xFF) {
                FreePrpListPage(DevExt, srbExt->PrpListPage);
                srbExt->PrpListPage = 0xFF;
            }

            // Check if this was a non-tagged request and clear the flag
            if (commandId & CID_NON_TAGGED_FLAG) {
                DevExt->NonTaggedInFlight = FALSE;
                DevExt->NonTaggedSrbFallback = NULL;
            }

            // Set SRB status based on NVMe status
            if (status == NVME_SC_SUCCESS) {
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
#ifdef NVME_DBG_EXTRA
                // to spammy enable by default
                ScsiDebugPrint(0, "nvme2k: Completing CID=%d SRB=%p SUCCESS\n", commandId, Srb);
#endif
            } else {
                // Command failed - provide auto-sense data
                Srb->SrbStatus = SRB_STATUS_ERROR;
                Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

                // Fill in sense data if buffer is available
                if (Srb->SenseInfoBuffer && Srb->SenseInfoBufferLength >= 18) {
                    PUCHAR sense = (PUCHAR)Srb->SenseInfoBuffer;
                    RtlZeroMemory(sense, Srb->SenseInfoBufferLength);

                    // Build standard SCSI sense data
                    sense[0] = 0x70;  // Error code: Current error
                    sense[2] = 0x04;  // Sense Key: Hardware Error
                    sense[7] = 0x0A;  // Additional sense length
                    sense[12] = 0x44; // ASC: Internal target failure
                    sense[13] = 0x00; // ASCQ

                    Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                }

#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: I/O command failed - CID=%d NVMe Status=0x%02X\n",
                               commandId, status);
#endif
            }

            // Complete the request - ScsiPort takes ownership of the SRB
            ScsiPortNotification(RequestComplete, DevExt, Srb);

            // Decrement queue depth tracking
            if (DevExt->CurrentQueueDepth > 0) {
                DevExt->CurrentQueueDepth--;
            }
        }
io_ring_doorbell:
        // Ring completion doorbell with the new head position
        NvmeRingDoorbell(DevExt, Queue->QueueId, FALSE, (USHORT)(Queue->CompletionQueueHead & Queue->QueueSizeMask));
    }

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Release completion queue spinlock
    AtomicSet(&Queue->CompletionLock, 0);
#endif

    return processed;
}
