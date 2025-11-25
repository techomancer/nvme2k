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
        ScsiDebugPrint(0, "nvme2k: Get Log Page completion - missing Srb!\n");
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

            if (status != NVME_SC_SUCCESS || !prpBuffer) {
                // Command failed
#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: FAILED Log Page completion for SRB_FUNCTION_IO_CONTROL 0x%02X\n", Srb->Function);
#endif
                sendCmdOut->cBufferSize = 0;
                sendCmdOut->DriverStatus.bDriverError = 1;
                sendCmdOut->DriverStatus.bIDEError = 0x04;  // Aborted
                srbControl->ReturnCode = 1;
                Srb->DataTransferLength = 0;
                Srb->SrbStatus = SRB_STATUS_ERROR;
            } else
            if (RtlCompareMemory(srbControl->Signature, "SCSIDISK", 8) == 8
                && srbControl->ControlCode == IOCTL_SCSI_MINIPORT_READ_SMART_ATTRIBS) {
                    PNVME_SMART_INFO nvmeSmart = (PNVME_SMART_INFO)prpBuffer;
                    PATA_SMART_DATA ataSmart = (PATA_SMART_DATA)sendCmdOut->bBuffer;

                    // Convert NVMe SMART/Health log to ATA SMART format
                    NvmeSmartToAtaSmart(nvmeSmart, ataSmart);

                    sendCmdOut->cBufferSize = 512;
                    RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                    sendCmdOut->DriverStatus.bDriverError = 0;
                    sendCmdOut->DriverStatus.bIDEError = 0;

                    // Set DataTransferLength to total size returned
                    Srb->DataTransferLength = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + 512 - 1;

                    srbControl->ReturnCode = 0;
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Get Log Page completion for SRB_FUNCTION_IO_CONTROL/IOCTL_SCSI_MINIPORT_READ_SMART_ATTRIBS 0x%02X\n", Srb->Function);
#endif
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
#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: I/O CQ created successfully - DW0=%08X SQID=%u\n",
                                       cqEntry->DW0, cqEntry->SQID);
#endif
                        NvmeCreateIoSQ(DevExt);
                    } else {
#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: ERROR - I/O CQ creation failed with status 0x%04X\n", status);
#endif
                    }
                    break;

                case ADMIN_CID_CREATE_IO_SQ:
                    if (status == NVME_SC_SUCCESS) {
#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: I/O SQ created successfully - DW0=%08X SQID=%u\n",
                                       cqEntry->DW0, cqEntry->SQID);
#endif
                        NvmeIdentifyController(DevExt);
                    } else {
#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: ERROR - I/O SQ creation failed with status 0x%04X\n", status);
#endif
                    }
                    break;

                case ADMIN_CID_IDENTIFY_CONTROLLER:
                    if (status == NVME_SC_SUCCESS) {
                        PNVME_IDENTIFY_CONTROLLER ctrlData = (PNVME_IDENTIFY_CONTROLLER)DevExt->UtilityBuffer;
                        ULONG driverMaxTransfer;

                        // Copy and null-terminate strings
                        RtlCopyMemory(DevExt->ControllerSerialNumber, ctrlData->SerialNumber, 20);
                        DevExt->ControllerSerialNumber[20] = 0;

                        RtlCopyMemory(DevExt->ControllerModelNumber, ctrlData->ModelNumber, 40);
                        DevExt->ControllerModelNumber[40] = 0;

                        RtlCopyMemory(DevExt->ControllerFirmwareRevision, ctrlData->FirmwareRevision, 8);
                        DevExt->ControllerFirmwareRevision[8] = 0;

                        DevExt->NumberOfNamespaces = ctrlData->NumberOfNamespaces;

                        // Read MDTS (Maximum Data Transfer Size)
                        // Per NVMe spec: MDTS specifies the maximum data transfer size for a command
                        // Value is in units of minimum memory page size (CAP.MPSMIN)
                        // Maximum transfer = 2^MDTS * minimum page size
                        // If MDTS is 0, there is no maximum transfer size limit from the controller
                        DevExt->MaxDataTransferSizePower = ctrlData->MaxDataTransferSize;

                        // Driver maximum: One PRP list page with 512 entries * 4KB per entry = 2MB
                        driverMaxTransfer = 512 * PAGE_SIZE;

                        if (DevExt->MaxDataTransferSizePower == 0) {
                            // No controller-imposed limit
                            DevExt->MaxTransferSizeBytes = driverMaxTransfer;
                        } else {
                            // Calculate: 2^MDTS * PageSize (PageSize = 4KB)
                            DevExt->MaxTransferSizeBytes = (1UL << DevExt->MaxDataTransferSizePower) * PAGE_SIZE;

                            // Clamp to driver maximum
                            if (DevExt->MaxTransferSizeBytes > driverMaxTransfer) {
                                DevExt->MaxTransferSizeBytes = driverMaxTransfer;
                            }
                        }

#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: Identified controller - Model: %.40s SN: %.20s FW: %.8s NN: %u\n",
                                    DevExt->ControllerModelNumber, DevExt->ControllerSerialNumber,
                                    DevExt->ControllerFirmwareRevision, DevExt->NumberOfNamespaces);
                        if (DevExt->MaxDataTransferSizePower == 0) {
                            ScsiDebugPrint(0, "nvme2k: MDTS=0 (no controller limit), using driver max %u bytes\n",
                                        DevExt->MaxTransferSizeBytes);
                        } else {
                            ScsiDebugPrint(0, "nvme2k: MDTS=%u (%u bytes), final max transfer = %u bytes\n",
                                        DevExt->MaxDataTransferSizePower,
                                        (1UL << DevExt->MaxDataTransferSizePower) * PAGE_SIZE,
                                        DevExt->MaxTransferSizeBytes);
                        }
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

#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: Init complete - driver ready for I/O\n");
#endif
                    }
                    break;

                default:
#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: unknown init time admin CID %04X\n", commandId);
#endif
                    ;
                    
            }
        } else {
            // Post-initialization admin commands
            // Check if this is a Get Log Page command
            if ((commandId & CID_VALUE_MASK) >= ADMIN_CID_GET_LOG_PAGE && (commandId & CID_VALUE_MASK)< ADMIN_CID_GET_LOG_PAGE + DevExt->SgListPages) {
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
    }

    // Ring completion doorbell ONCE with the final head position after processing all completions
    // This acknowledges all processed completions and clears the interrupt
    if (processed) {
        NvmeRingDoorbell(DevExt, Queue->QueueId, FALSE, (USHORT)(Queue->CompletionQueueHead & Queue->QueueSizeMask));
    }

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

#ifdef NVME2K_DBG_EXTRA
    if (DevExt->TotalRequests) {
        ScsiDebugPrint(0, "nvme2k: NvmeProcessIoCompletion called\n");
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

#ifdef NVME2K_DBG_EXTRA
        ScsiDebugPrint(0, "nvme2k: I/O completion found! QueueIdx=%d Phase=%d\n", queueIndex, expectedPhase);
#endif

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
                // Skip this - it's already been completed, continue to next completion
                continue;
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
                // Skip this completion, continue to next completion
                continue;
            }

            // Get SRB extension for PRP list cleanup
            srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;

            // Free PRP list page if allocated
            if (srbExt->PrpListPage != 0xFF) {
                FreePrpListPage(DevExt, srbExt->PrpListPage);
                srbExt->PrpListPage = 0xFF;
            }

            // Check if this was a TRIM operation that we need to restore buffer for
            if (DevExt->TrimEnable && Srb->DataTransferLength >= 4096) {
                PCDB cdb = (PCDB)Srb->Cdb;
                BOOLEAN isWrite = FALSE;

                // Check if this is a write operation
                switch (cdb->CDB10.OperationCode) {
                    case SCSIOP_WRITE6:
                    case SCSIOP_WRITE:
                        isWrite = TRUE;
                        break;
                }

                // If write, check if bytes 16-4095 match TrimPattern (excluding first 16 bytes we corrupted)
                if (isWrite && Srb->DataBuffer) {
                    PUCHAR dataBuffer = (PUCHAR)Srb->DataBuffer;
                    // Compare bytes 16-4095 with TrimPattern offset by 4 ULONGs (16 bytes)
                    if (RtlCompareMemory(dataBuffer + 16, (PUCHAR)DevExt->TrimPattern + 16, 4096 - 16) == (4096 - 16)) {
                        // This was a TRIM operation - restore the first 16 bytes from TrimPattern
#ifdef NVME2K_DBG_EXTRA
                        ScsiDebugPrint(0, "nvme2k: Restoring first 16 bytes of TRIM buffer\n");
#endif
                        RtlCopyMemory(dataBuffer, DevExt->TrimPattern, 16);
                    }
                }
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
            if (DevExt->Busy) {
                // hopefully some resources freed up so signal that we can process next request
                DevExt->Busy = FALSE;
                ScsiPortNotification(NextRequest, DevExt, NULL);
            }


            // Decrement queue depth tracking
            if (DevExt->CurrentQueueDepth > 0) {
                DevExt->CurrentQueueDepth--;
            }
        }
    }

    // Ring completion doorbell ONCE with the final head position after processing all completions
    // This acknowledges all processed completions and clears the interrupt
    if (processed) {
        NvmeRingDoorbell(DevExt, Queue->QueueId, FALSE, (USHORT)(Queue->CompletionQueueHead & Queue->QueueSizeMask));
    }

    return processed;
}
