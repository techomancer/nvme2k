//
// SCSI Miniport Driver for Windows 2000
//

#include "nvme2k.h"
#include "utils.h"

//
// DriverEntry - Main entry point
//
ULONG DriverEntry(IN PVOID DriverObject, IN PVOID Argument2)
{
    HW_INITIALIZATION_DATA hwInitData;
    ULONG status;
#if (_WIN32_WINNT < 0x500)
    ULONG HwContext[2];
#endif
    // Break into debugger if attached

#ifdef NVME2K_DBG
    //__asm { int 3 }
    ScsiDebugPrint(0, "nvme2k: DriverEntry called\n");
#endif

    // Zero out the initialization data structure
    memset(&hwInitData, 0, sizeof(HW_INITIALIZATION_DATA));

    // Set size of structure
    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    // Set entry points
    hwInitData.HwInitialize = HwInitialize;
    hwInitData.HwStartIo = HwStartIo;
    hwInitData.HwInterrupt = HwInterrupt;
    hwInitData.HwFindAdapter = HwFindAdapter;
    hwInitData.HwResetBus = HwResetBus;
#if (_WIN32_WINNT >= 0x500)
    hwInitData.HwAdapterControl = HwAdapterControl;
#else
    hwInitData.HwAdapterState = HwAdapterState;
#endif

    // Set driver-specific parameters
    hwInitData.AdapterInterfaceType = PCIBus;  // Change as needed
    hwInitData.DeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    hwInitData.SpecificLuExtensionSize = 0;
    hwInitData.SrbExtensionSize = sizeof(NVME_SRB_EXTENSION);  // Required for PRP list tracking
    hwInitData.NumberOfAccessRanges = 1;
    hwInitData.MapBuffers = TRUE;
    hwInitData.NeedPhysicalAddresses = TRUE;
    hwInitData.TaggedQueuing = TRUE;
    hwInitData.AutoRequestSense = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;

    // Vendor/Device IDs (for PCI devices)
    hwInitData.VendorIdLength = 0;
    hwInitData.VendorId = NULL;
    hwInitData.DeviceIdLength = 0;
    hwInitData.DeviceId = NULL;

#if (_WIN32_WINNT < 0x500)
    HwContext[0] = 0;
    HwContext[1] = 0;
#endif
    // Call port driver
    status = ScsiPortInitialize(DriverObject, Argument2,
                                &hwInitData, 
#if (_WIN32_WINNT >= 0x500)
                                NULL
#else
                                HwContext
#endif
                            );

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: DriverEntry exiting with status 0x%08X\n", status);
#endif
    return status;
}

ULONG HwFoundAdapter(
    IN PHW_DEVICE_EXTENSION DevExt,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN PUCHAR pciBuffer)
{
    PACCESS_RANGE accessRange;
    SCSI_PHYSICAL_ADDRESS bar0;
    ULONG bar0tmp;
    ULONG barSize;
    ULONG tempSize;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - found NVMe device VID=%04X DID=%04X at bus %d slot %d\n",
                   DevExt->VendorId, DevExt->DeviceId, DevExt->BusNumber, DevExt->SlotNumber);
#endif

    // Read subsystem IDs using helper function
    DevExt->SubsystemVendorId = ReadPciConfigWord(DevExt, PCI_SUBSYSTEM_VENDOR_ID_OFFSET);
    DevExt->SubsystemId = ReadPciConfigWord(DevExt, PCI_SUBSYSTEM_ID_OFFSET);

    // Enable PCI device (Bus Master, Memory Space) but DISABLE interrupts at PCI level
    // We'll re-enable interrupts later after proper initialization
    // This prevents interrupt storms from residual controller state
    WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET,
                      PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE | PCI_INTERRUPT_DISABLE);

#ifdef NVME2K_DBG
    {
        USHORT cmdReg = ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET);
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - PCI Command Register = %04X (IntDis=%d)\n",
                       cmdReg, !!(cmdReg & PCI_INTERRUPT_DISABLE));
    }
#endif

    // Read interrupt configuration from PCI config space
    // This is probably redundant on Win2K and can be ifdefed out
    {
        UCHAR interruptLine = ReadPciConfigByte(DevExt, PCI_INTERRUPT_LINE_OFFSET);
        UCHAR interruptPin = ReadPciConfigByte(DevExt, PCI_INTERRUPT_PIN_OFFSET);

#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - PCI Interrupt Line=%d Pin=%d\n",
                       interruptLine, interruptPin);
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - ConfigInfo BEFORE: BusInterruptLevel=%d Vector=%d Mode=%d\n",
                       ConfigInfo->BusInterruptLevel, ConfigInfo->BusInterruptVector, ConfigInfo->InterruptMode);
#endif

        // CRITICAL FOR NT4: When manually setting SystemIoBusNumber/SlotNumber,
        // SCSI port doesn't automatically query interrupt configuration from HAL.
        // We MUST set valid interrupt parameters or we'll get STATUS_INVALID_PARAMETER.
        // For PCI: BusInterruptLevel = interrupt line, mode = LevelSensitive
        if (interruptPin != 0 && interruptLine != 0 && interruptLine != 0xFF) {
            ConfigInfo->BusInterruptLevel = interruptLine;
            ConfigInfo->BusInterruptVector = interruptLine;
            ConfigInfo->InterruptMode = LevelSensitive;
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - Set interrupt from PCI config: Level=%d\n", interruptLine);
#endif
        } else {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - WARNING: No valid PCI interrupt configured\n");
#endif
            // If no valid interrupt, still need to set something valid for NT4
            // Use polling mode - but this might not work on NT4!
            ConfigInfo->BusInterruptLevel = 0;
            ConfigInfo->BusInterruptVector = 0;
        }
    }

    // Set the number of access ranges we're using (1 for BAR0)
    // NT4 may require this to be explicitly set for resource translation
    ConfigInfo->NumberOfAccessRanges = 1;

    ConfigInfo->MaximumTransferLength = 32u << NVME_PAGE_SHIFT;  // safe default
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = FALSE;
    ConfigInfo->AdapterScansDown = FALSE;
    ConfigInfo->Dma32BitAddresses = TRUE;
#if (_WIN32_WINNT >= 0x500)
    ConfigInfo->Dma64BitAddresses = TRUE;  // NVMe supports 64-bit addressing
#endif    
    ConfigInfo->MaximumNumberOfTargets = 2;  // Support TargetId 0 and 1
    ConfigInfo->NumberOfPhysicalBreaks = 511;  // PRP1 + PRP list (512 entries)
    ConfigInfo->AlignmentMask = 0x3;  // DWORD alignment
    ConfigInfo->NeedPhysicalAddresses = TRUE;  // Required for ScsiPortGetPhysicalAddress to work
    ConfigInfo->TaggedQueuing = TRUE;  // Support tagged command queuing
    ConfigInfo->MultipleRequestPerLu = TRUE;  // Allow multiple outstanding commands per LUN
    ConfigInfo->AutoRequestSense = TRUE;  // Automatically provide sense data on errors
    // Note: SrbExtensionSize must be set in HW_INITIALIZATION_DATA, not here

#ifdef NVME2K_DBG_EXTRA
    {
        ULONG a;
        accessRange = &((*(ConfigInfo->AccessRanges))[0]);
        for (a = 0; a < ConfigInfo->NumberOfAccessRanges; a++) {
            ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - range:%u start:%08X, length:%08X, inMemory:%d\n",
                           a, (ULONG)(accessRange->RangeStart.QuadPart),
                           accessRange->RangeLength,
                           accessRange->RangeInMemory);
            accessRange++;
        }
    }
#endif
    // Again, most of this BAR dance has been already done by Win2k PnP
    // We can probably ifdef it out on Win2k
    // Get BAR0 (Controller registers for NVMe)
    accessRange = &((*(ConfigInfo->AccessRanges))[0]);
    accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(0);
    accessRange->RangeLength = 0;

    bar0tmp = ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_0);
    bar0.HighPart = 0;
    bar0.LowPart = bar0tmp & 0xFFFFFFF0;
    if ((bar0tmp & 0x6) == 0x6) {
        bar0.HighPart = ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_1);
        if (bar0.HighPart) {
            ScsiDebugPrint(0, "nvme2k: BAR0 base=0x%08X%08X beyond 4GB, it may not end well on 32bit kernel\n",
                        accessRange->RangeStart.HighPart,
                        accessRange->RangeStart.LowPart);
        }
    }

#if (_WIN32_WINNT >= 0x500 && !defined(ALPHA))
    accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(ScsiPortConvertPhysicalAddressToULongPtr(bar0));
#else
    accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(bar0.LowPart);
#endif
    if (bar0tmp & 0x1) {
        return SP_RETURN_ERROR;
    }
    accessRange->RangeInMemory = TRUE;
    
    WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_0, 0xFFFFFFFF);

    // Read back the modified value
    barSize = ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_0);

    // Restore original BAR value
    WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_0, bar0tmp);
    accessRange->RangeLength = ~(barSize & 0xFFFFFFF0) + 1;

    DevExt->ControllerRegistersLength = accessRange->RangeLength;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - BAR0 base=0x%08X size=0x%08X\n",
                   (ULONG)accessRange->RangeStart.QuadPart,
                   accessRange->RangeLength);
#endif

    // Validate the access range (required for NT4, optional for Win2K+)
    // This checks if the range is available and doesn't conflict with other devices
    if (!ScsiPortValidateRange(
            (PVOID)DevExt,
            ConfigInfo->AdapterInterfaceType,
            ConfigInfo->SystemIoBusNumber,
            accessRange->RangeStart,
            accessRange->RangeLength,
            (BOOLEAN)!accessRange->RangeInMemory)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - ScsiPortValidateRange failed for BAR0\n");
#endif
        return SP_RETURN_ERROR;
    }

    // Map the controller registers
    DevExt->ControllerRegisters = ScsiPortGetDeviceBase(
        (PVOID)DevExt,
        ConfigInfo->AdapterInterfaceType,
        ConfigInfo->SystemIoBusNumber,
        accessRange->RangeStart,
        accessRange->RangeLength,
        (BOOLEAN)!accessRange->RangeInMemory);

    if (DevExt->ControllerRegisters == NULL) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - failed to map controller registers\n");
#endif
        return SP_RETURN_ERROR;
    }
#if 0    
// can be reported in newer OSes
    // Read controller capabilities to determine max queue size
    // This must be done here so we can set ConfigInfo->NumberOfRequests
    {
        ULONGLONG cap;
        USHORT mqes, maxQueueSize;

        cap = NvmeReadReg64(DevExt, NVME_REG_CAP);
        mqes = (USHORT)(cap & NVME_CAP_MQES_MASK);
        maxQueueSize = mqes + 1;  // MQES is 0-based

        // Limit to our maximum (what fits in one page)
        if (maxQueueSize > NVME_MAX_QUEUE_SIZE) {
            maxQueueSize = NVME_MAX_QUEUE_SIZE;
        }

        // Tell SCSI port driver how many outstanding requests we can handle
        // This prevents the OS from sending more requests than we can queue
        ConfigInfo->NumberOfRequests = maxQueueSize;

        ScsiDebugPrint(0, "nvme2k: HwFindAdapter - MQES=%u, setting NumberOfRequests=%u\n",
                       mqes, maxQueueSize);
    }
#endif

//
// Uncached memory size calculation:
// - Admin SQ: 4096 bytes (4KB aligned)
// - I/O SQ: 4096 bytes (4KB aligned)
// - Utility buffer / PRP list pool: (SgListPages pages * 4KB, page-aligned)
// - Admin CQ: 4096 bytes (4KB aligned)
// - I/O CQ: 4096 bytes (4KB aligned)
// Total: ~60KB with alignment
//

    // Allocate uncached memory block
    DevExt->SgListPages = 32;
    DevExt->UncachedExtensionSize = (NVME_PAGE_SIZE * (DevExt->SgListPages + 4 + 1));

    DevExt->UncachedExtensionBase = ScsiPortGetUncachedExtension(
        (PVOID)DevExt,
        ConfigInfo,
        DevExt->UncachedExtensionSize);

    if (DevExt->UncachedExtensionBase == NULL) {
        DevExt->SgListPages = 16;
        DevExt->UncachedExtensionSize = (NVME_PAGE_SIZE * (DevExt->SgListPages + 4 + 1));

        DevExt->UncachedExtensionBase = ScsiPortGetUncachedExtension(
            (PVOID)DevExt,
            ConfigInfo,
            DevExt->UncachedExtensionSize);

        if (DevExt->UncachedExtensionBase == NULL) {
    #ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - failed to allocate uncached memory\n");
    #endif
            return SP_RETURN_ERROR;
        }
    }

    // Get physical address of the entire uncached block
    tempSize = DevExt->UncachedExtensionSize;
    DevExt->UncachedExtensionPhys = ScsiPortGetPhysicalAddress(
        (PVOID)DevExt,
        NULL,
        DevExt->UncachedExtensionBase,
        &tempSize);
    
    // Zero out the entire uncached memory
    memset(DevExt->UncachedExtensionBase, 0, DevExt->UncachedExtensionSize);

    // Initialize allocator
    DevExt->UncachedExtensionOffset = 0;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - allocated %u bytes of uncached memory at virt=%p phys=%08X%08X\n",
                   DevExt->UncachedExtensionSize, DevExt->UncachedExtensionBase,
                   (ULONG)(DevExt->UncachedExtensionPhys.QuadPart >> 32),
                   (ULONG)(DevExt->UncachedExtensionPhys.QuadPart & 0xFFFFFFFF));
#endif

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - success, returning SP_RETURN_FOUND\n");
#endif

    if (!NvmeSanitizeController(DevExt)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwInHwFoundAdapteritialize - NvmeSanitizeController failed\n");
#endif
        return SP_RETURN_ERROR;
    }

    if (!NvmeInitializeController(DevExt)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - NvmeInitializeController failed\n");
#endif
        return SP_RETURN_ERROR;
    }

    // We need to update some of the config info now from the data we got from the controller.
    ConfigInfo->MaximumTransferLength = DevExt->MaxTransferSizeBytes;
    if (((ConfigInfo->MaximumTransferLength >> NVME_PAGE_SHIFT) - 1) < ConfigInfo->NumberOfPhysicalBreaks)
        ConfigInfo->NumberOfPhysicalBreaks = (ConfigInfo->MaximumTransferLength >> NVME_PAGE_SHIFT) - 1;

    return SP_RETURN_FOUND;
}

//
// HwFindAdapter - Locate and configure the adapter
//
ULONG HwFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    UCHAR pciBuffer[256];
    ULONG slotNumber;
    ULONG busNumber;
    UCHAR baseClass, subClass, progIf;
    ULONG bytesRead;
    BOOLEAN PNP = ConfigInfo->NumberOfAccessRanges != 0;

    if (HwContext) {
        busNumber = ((PULONG)HwContext)[0];
        slotNumber = ((PULONG)HwContext)[1];
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFindAdapter:%p called with HwContext - Bus=%d Slot=%d\n",
                       DeviceExtension, busNumber, slotNumber);
#endif
    } else {
        // Win2k, PnP gives us this directly
        busNumber = ConfigInfo->SystemIoBusNumber;
        slotNumber = ConfigInfo->SlotNumber;
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFindAdapter:%p called w/o HwContext - Bus=%d Slot=%d PNP=%d\n",
                       DeviceExtension, busNumber, slotNumber, PNP);
#endif
    }


scanloop:
    // Read PCI configuration space for THIS slot only
    bytesRead = ScsiPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        busNumber,
        slotNumber,
        pciBuffer,
        256);

    if (bytesRead == 0) {
        // No device in this slot - tell SCSI port to keep scanning other slots
        goto scannext;
    }

    // Extract Vendor ID and Device ID
    DevExt->VendorId = *(USHORT*)&pciBuffer[PCI_VENDOR_ID_OFFSET];
    DevExt->DeviceId = *(USHORT*)&pciBuffer[PCI_DEVICE_ID_OFFSET];

    // Check for invalid vendor ID
    if (DevExt->VendorId == 0xFFFF || DevExt->VendorId == 0x0000) {
        // No valid device in this slot - tell SCSI port to keep scanning other slots
        goto scannext;
    }

    // Extract class code information
    DevExt->RevisionId = pciBuffer[PCI_REVISION_ID_OFFSET];
    progIf = pciBuffer[PCI_CLASS_CODE_OFFSET];
    subClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 1];
    baseClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 2];

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - bus %d slot %d: VID=%04X DID=%04X Class=%02X%02X%02X\n",
                   busNumber, slotNumber, DevExt->VendorId, DevExt->DeviceId,
                   baseClass, subClass, progIf);
#endif

    // Check if this is an NVMe device
    if (IsNvmeDevice(baseClass, subClass, progIf)) {
        // Found an NVMe device at the slot SCSI port asked us to check!
        DevExt->BusNumber = busNumber;
        DevExt->SlotNumber = slotNumber;
        // Store next slot/bus so we can resume scanning on next call
        if (HwContext) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFindAdapter - NT4: store HwContext to resume scanning\n");
#endif
            ((PULONG)HwContext)[0] = DevExt->BusNumber;
            ((PULONG)HwContext)[1] = DevExt->SlotNumber + 1;
            if (((PULONG)HwContext)[1] >= (PCI_MAX_DEVICES*PCI_MAX_FUNCTION)) {
                ((PULONG)HwContext)[1] = 0;
                ((PULONG)HwContext)[0]++;
            }
            *Again = TRUE;
        } else {
            *Again = FALSE;
        }
#ifdef NVME2K_DBG
        if (!HwContext) {
            ScsiDebugPrint(0, "nvme2k: HwFindAdapter - uh oh no HwContext! are we going to scan whole thing again?\n");
        }
#endif
        return HwFoundAdapter(DevExt, ConfigInfo, pciBuffer);
    }
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - not an NVMe device\n");
#endif

scannext:
    // NT4 or install time: Continue scanning the bus
    slotNumber++;
    if (slotNumber == (PCI_MAX_DEVICES*PCI_MAX_FUNCTION)) {
        busNumber++;
        slotNumber = 0;
        if (busNumber == 16) { // theoretically 256 but we arent going to waste cycles
            *Again = FALSE;
            return SP_RETURN_NOT_FOUND;
        }
    }
    goto scanloop;
}

//
// HwInitialize - Initialize the adapter
//
BOOLEAN HwInitialize(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwInitialize called\n");
#endif

    // Step 3: Enable interrupts
    // This is done after initialization is complete but before HwInitialize returns,
    // so ScsiPort knows we're interrupt-capable
    NvmeEnableInterrupts(DevExt);

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwInitialize finished successfully\n");
#endif
    return TRUE;
}

//
// HwStartIo - Process SCSI request
//
BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    NVME_COMMAND nvmeCmd;
    USHORT commandId;

#ifdef NVME2K_DBG_EXTRA
    ScsiDebugPrint(0, "nvme2k: HwStartIo called - Function=%02X Path=%d Target=%d Lun=%d\n",
                   Srb->Function, Srb->PathId, Srb->TargetId, Srb->Lun);
#endif

#if 0
    // Poll completion queues as a backup in case interrupts are delayed
    // This helps performance on busy systems
    if (DevExt->InitComplete) {
        if (DevExt->CurrentQueueDepth > 0 || DevExt->NonTaggedInFlight) {
            NvmeProcessAdminCompletion(DevExt);
            NvmeProcessIoCompletion(DevExt);
        }
    }
#endif

    // Check if the request is for our device (PathId=0, TargetId=0, Lun=0)
    if (Srb->PathId != 0 || Srb->TargetId != 0 || Srb->Lun != 0) {
        // Not our device - distinguish between invalid target and invalid LUN
        if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI) {
            // Check if this is an invalid LUN on our target (Path=0, Target=0, but Lun != 0)
            if (Srb->PathId == 0 && Srb->TargetId == 0 && Srb->Lun != 0) {
                // Invalid LUN on our target - return error with sense data
                PSENSE_DATA senseBuffer;

                Srb->SrbStatus = SRB_STATUS_ERROR;
                Srb->ScsiStatus = 0x02;  // CHECK_CONDITION

                // Fill in sense data if AutoRequestSense is enabled and buffer is available
                if (Srb->SenseInfoBuffer != NULL && Srb->SenseInfoBufferLength >= sizeof(SENSE_DATA)) {
                    senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;
                    memset(senseBuffer, 0, sizeof(SENSE_DATA));

                    senseBuffer->ErrorCode = 0x70;  // Current error, fixed format
                    senseBuffer->Valid = 0;
                    senseBuffer->SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
                    senseBuffer->AdditionalSenseLength = sizeof(SENSE_DATA) - 8;
                    senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_INVALID_LUN;
                    senseBuffer->AdditionalSenseCodeQualifier = 0x00;

                    Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                }
            } else {
                // Invalid target or path - selection timeout
                Srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
            }
        } else {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        }
        ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
        ScsiPortNotification(NextRequest, DeviceExtension, NULL);
        return TRUE;
    }

    // Process the SRB based on its function
    switch (Srb->Function) {
        case SRB_FUNCTION_EXECUTE_SCSI:
#ifdef NVME2K_DBG_EXTRA
            ScsiDebugPrint(0, "nvme2k: processing op=%02X\n", Srb->Cdb[0]);

            // Log tagged queuing usage
            if (Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) {
                const char* queueType = "Unknown";
                switch (Srb->QueueAction) {
                    case SRB_SIMPLE_TAG_REQUEST: queueType = "SIMPLE"; break;
                    case SRB_HEAD_OF_QUEUE_TAG_REQUEST: queueType = "HEAD_OF_QUEUE"; break;
                    case SRB_ORDERED_QUEUE_TAG_REQUEST: queueType = "ORDERED"; break;
                }
                ScsiDebugPrint(0, "nvme2k: Tagged queuing enabled - QueueAction=%s (0x%02X) Tag:%02X\n",
                               queueType, Srb->QueueAction, Srb->QueueTag);
            }
#endif
            // Process SCSI CDB
            switch (Srb->Cdb[0]) {
                case SCSIOP_READ6:
                case SCSIOP_READ:
                case SCSIOP_WRITE6:
                case SCSIOP_WRITE:
                    return ScsiHandleReadWrite(DevExt, Srb);
                    
                case SCSIOP_TEST_UNIT_READY:
                    if (DevExt->InitComplete)
                        Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    else
                        Srb->SrbStatus = SRB_STATUS_BUSY;
                    break;

                case SCSIOP_VERIFY6:
                case SCSIOP_VERIFY:
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    break;
                    
                case SCSIOP_INQUIRY:
                    return ScsiHandleInquiry(DevExt, Srb);
                    
                case SCSIOP_READ_CAPACITY:
                    return ScsiHandleReadCapacity(DevExt, Srb);
                    
                case SCSIOP_LOG_SENSE:
                    return ScsiHandleLogSense(DevExt, Srb);

                case SCSIOP_MODE_SENSE:
                case SCSIOP_MODE_SENSE10:
                    return ScsiHandleModeSense(DevExt, Srb);
                    
                case SCSIOP_START_STOP_UNIT:
                    // Accept but do nothing
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    break;
                    
                case SCSIOP_SYNCHRONIZE_CACHE:
                    return ScsiHandleFlush(DevExt, Srb);

                case SCSIOP_ATA_PASSTHROUGH16:
                case SCSIOP_ATA_PASSTHROUGH12:
                    // SAT (SCSI/ATA Translation) ATA PASS-THROUGH commands
                    return ScsiHandleSatPassthrough(DevExt, Srb);

                case SCSIOP_READ_DEFECT_DATA10:
                    return ScsiHandleReadDefectData10(DevExt, Srb);

                default:
#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: HwStartIo - unimplemented SCSI opcode 0x%02X\n", Srb->Cdb[0]);
#endif
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                    break;
            }
            break;

        case SRB_FUNCTION_FLUSH:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_FLUSH\n");
#endif
            return ScsiHandleFlush(DevExt, Srb);

        case SRB_FUNCTION_FLUSH_QUEUE:
            // No internal queue to flush - requests go directly to hardware
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_SHUTDOWN:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo - SRB_FUNCTION_SHUTDOWN - flushing cache\n");
#endif
            // Flush all cached writes before shutdown
            return ScsiHandleFlush(DevExt, Srb);

        case SRB_FUNCTION_ABORT_COMMAND:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_ABORT_COMMAND\n");
#endif
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_RESET_BUS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_RESET_BUS\n");
#endif
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_RESET_DEVICE:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_RESET_DEVICE\n");
#endif
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_IO_CONTROL:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo - SRB_FUNCTION_IO_CONTROL\n");
#endif
            // Handle NVME2KDB custom IOCTLs first
            if (HandleIO_NVME2KDB(DevExt, Srb)) {
                if (Srb->SrbStatus != SRB_STATUS_PENDING) {
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                }
            }
            // Handle SMART and other miniport IOCTLs
            else if (HandleIO_SCSIDISK(DevExt, Srb)) {
                // If HandleIO_SCSIDISK returns TRUE, it may have set the status
                // to PENDING for async operations.
                if (Srb->SrbStatus != SRB_STATUS_PENDING) {
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                }
            } else {
#ifdef NVME2K_DBG
                if (Srb->DataTransferLength >= sizeof(SRB_IO_CONTROL)) {
                    PSRB_IO_CONTROL srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;
                    ScsiDebugPrint(0, "nvme2k: Unhandled IO_CONTROL, Sig: %.8s\n", srbControl->Signature);
                }
#endif
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            }
            break;

        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo - unimplemented SRB function 0x%02X\n", Srb->Function);
#endif
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    // Complete the request if not pending
    if (Srb->SrbStatus != SRB_STATUS_PENDING) {
        ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
        ScsiPortNotification(NextRequest, DeviceExtension, NULL);
    }

    return TRUE;
}

VOID FallbackTimer(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    DevExt->FallbackTimerNeeded++;
    if (!DevExt->FallbackTimerNeeded) // wraparound
        DevExt->FallbackTimerNeeded = 2; // because 1 means fallbacktime didnt fire

    NvmeProcessAdminCompletion(DevExt);
    NvmeProcessIoCompletion(DevExt);
}

//
// HwInterrupt - ISR for adapter
//
BOOLEAN HwInterrupt(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    BOOLEAN interruptHandled = FALSE;

    DevExt->InterruptCount++;
    if (DevExt->FallbackTimerNeeded) {
        // cancel the fallback timer
        ScsiPortNotification(RequestTimerCall, DeviceExtension, FallbackTimer, 0);
        // interrupts worked a million times, and callback didnt fire we probably dont need a fallback
        if (DevExt->InterruptCount >= 1000000 && DevExt->FallbackTimerNeeded == 1) {
            DevExt->FallbackTimerNeeded = 0;
        }
    }   

    // Process Admin Queue completions first
    if (NvmeProcessAdminCompletion(DevExt)) {
        interruptHandled = TRUE;
    }

    // Process I/O Queue completions
    if (NvmeProcessIoCompletion(DevExt)) {
        interruptHandled = TRUE;
    }

    return interruptHandled;
}

//
// HwResetBus - Reset the SCSI bus
//
BOOLEAN HwResetBus(IN PVOID DeviceExtension, IN ULONG PathId)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    // TODO: Reset the SCSI bus
    // Perform hardware reset

    // Complete all outstanding requests
    ScsiPortCompleteRequest(DeviceExtension, (UCHAR)PathId, 
                           SP_UNTAGGED, SP_UNTAGGED,
                           SRB_STATUS_BUS_RESET);

    return TRUE;
}

#if (_WIN32_WINNT >= 0x500)
//
// HwAdapterControl - Handle adapter power and PnP events (Windows 2000+)
//
SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    SCSI_ADAPTER_CONTROL_STATUS status = ScsiAdapterControlSuccess;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwAdapterControl called - ControlType=%d\n", ControlType);
#endif
    switch (ControlType) {
        case ScsiQuerySupportedControlTypes:
            {
                PSCSI_SUPPORTED_CONTROL_TYPE_LIST list =
                    (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;

                // Indicate which control types we support
                list->SupportedTypeList[ScsiStopAdapter] = TRUE;
                list->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            }
            break;

        case ScsiStopAdapter:
            // Stop the adapter - perform clean shutdown
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: ScsiStopAdapter - performing shutdown\n");
#endif
            NvmeShutdownController(DevExt);
            break;

        case ScsiRestartAdapter:
            // Restart the adapter - reinitialize controller
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: ScsiRestartAdapter - reinitializing\n");
#endif
            // Controller was reset, need to reinitialize
            if (HwInitialize(DevExt)) {
                status = ScsiAdapterControlSuccess;
            } else {
                status = ScsiAdapterControlUnsuccessful;
            }
            break;

        default:
            status = ScsiAdapterControlUnsuccessful;
            break;
    }

    return status;
}
#else
//
// HwAdapterState - Handle adapter state changes (Windows NT 4)
//
BOOLEAN HwAdapterState(
    IN PVOID DeviceExtension,
    IN PVOID Context,
    IN BOOLEAN SaveState)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwAdapterState called - SaveState=%d\n", SaveState);
#endif

    if (SaveState) {
        // Save adapter state - prepare for power down or hibernation
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwAdapterState - saving state and shutting down\n");
#endif
        // Perform clean shutdown of the NVMe controller
        NvmeShutdownController(DevExt);
    } else {
        // Restore adapter state - reinitialize after power up
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwAdapterState - restoring state\n");
#endif
        // Reinitialize the controller
        return HwInitialize(DevExt);
    }
    return TRUE;
}
#endif
 