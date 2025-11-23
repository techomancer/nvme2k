//
// utils.c - Utility functions for NVMe2K driver
//

#include "nvme2k.h"
#include "utils.h"

//
// RtlCompareMemory - Compare two memory blocks, return number of matching bytes
//
ULONG RtlCompareMemory(IN CONST VOID *Source1, IN CONST VOID *Source2, IN ULONG Length)
{
    PUCHAR s1 = (PUCHAR)Source1;
    PUCHAR s2 = (PUCHAR)Source2;
    ULONG i;

    for (i = 0; i < Length; i++) {
        if (s1[i] != s2[i]) {
            return i;
        }
    }
    return Length;
}

//
// NvmeSmartToAtaSmart - Convert NVMe SMART/Health log to ATA SMART format
// Alpha-safe: All field accesses are byte-aligned
//
VOID NvmeSmartToAtaSmart(IN PNVME_SMART_INFO NvmeSmart, OUT PATA_SMART_DATA AtaSmart)
{
    UCHAR attrIndex;
    UCHAR checksum;
    ULONG i;
    USHORT tempCelsius;
    ULONGLONG zero = 0;

    attrIndex = 0;

    /* Zero out the entire structure */
    RtlZeroMemory(AtaSmart, sizeof(ATA_SMART_DATA));

    /* Set version */
    WRITE_USHORT(AtaSmart->Version, 0x0010);  /* SMART version 1.0 */

    /* Helper macro to add an attribute - Alpha-safe byte access */
#define ADD_ATTRIBUTE(id, current, worst, raw64) \
    if (attrIndex < 30) { \
        AtaSmart->Attributes[attrIndex].Id = (id); \
        WRITE_USHORT(AtaSmart->Attributes[attrIndex].Flags, 0x0003); \
        AtaSmart->Attributes[attrIndex].CurrentValue = (current); \
        AtaSmart->Attributes[attrIndex].WorstValue = (worst); \
        /* Copy lower 6 bytes of the 64-bit raw value */ \
        RtlCopyMemory(AtaSmart->Attributes[attrIndex].RawValue, &(raw64), 6); \
        attrIndex++; \
    }

    /* 1. Power-On Hours (Attribute 9) */
    /* NVMe PowerOnHours is in minutes, ATA expects hours */
    {
        ULONGLONG powerOnMinutes = READ_ULONGLONG(NvmeSmart->PowerOnHours);
        if (powerOnMinutes > 0) {
            /* NVMe spec says this is hours, not minutes. No conversion needed. */
            ADD_ATTRIBUTE(ATA_SMART_ATTR_POWER_ON_HOURS, 100, 100, powerOnMinutes);
        }
    }

    /* 2. Power Cycle Count (Attribute 12) */
    {
        ULONGLONG powerCycles64 = READ_ULONGLONG(NvmeSmart->PowerCycles);
        if (powerCycles64 > 0) {
            ADD_ATTRIBUTE(ATA_SMART_ATTR_POWER_CYCLE_COUNT, 100, 100, powerCycles64);
        }
    }

    /* 3. Temperature (Attribute 194) */
    /* NVMe temperature is in Kelvin, convert to Celsius */
    tempCelsius = READ_USHORT(NvmeSmart->Temperature);
    if (tempCelsius > 0) {
        ULONGLONG temp;
        UCHAR normTemp;
        tempCelsius = (tempCelsius > 273) ? (tempCelsius - 273) : 0;
        temp = (ULONGLONG)tempCelsius;
        /* Normalized value: 200 is a good baseline. Higher temp = lower value. */
        normTemp = (tempCelsius < 100) ? (200 - (UCHAR)tempCelsius) : 100;
        ADD_ATTRIBUTE(ATA_SMART_ATTR_TEMPERATURE, normTemp, normTemp, temp);
    }

    // 4. Wear Leveling / Percentage Used (Attribute 173)
    // NVMe PercentageUsed: 0-100%, higher is more worn
    if (NvmeSmart->PercentageUsed > 0) {
        UCHAR percentUsed = NvmeSmart->PercentageUsed;
        UCHAR wearValue = (percentUsed <= 100) ? (100 - percentUsed) : 0;
        ULONGLONG percentRaw = percentUsed;
        ADD_ATTRIBUTE(ATA_SMART_ATTR_WEAR_LEVELING_COUNT, wearValue, wearValue, percentRaw);
    }

    // 5. Available Spare (not a standard ATA attribute, use vendor-specific 170)
    if (NvmeSmart->AvailableSpare > 0) {
        ULONGLONG spare = NvmeSmart->AvailableSpare;
        UCHAR spareValue = NvmeSmart->AvailableSpare;
        ADD_ATTRIBUTE(170, spareValue, spareValue, spare);
    }

    // 6. Media/Data Integrity Errors (Attribute 199 - UDMA CRC Error as proxy)
    {
        ULONGLONG mediaErrors = READ_ULONGLONG(NvmeSmart->MediaErrors);
        UCHAR errorValue = (mediaErrors == 0) ? 100 : ((mediaErrors < 100) ? (100 - (UCHAR)mediaErrors) : 0);
        ADD_ATTRIBUTE(ATA_SMART_ATTR_UDMA_CRC_ERROR_COUNT, errorValue, errorValue, mediaErrors);
    }

    // 7. Unsafe Shutdowns (Attribute 192 - Power-off Retract Count as proxy)
    {
        ULONGLONG unsafeShutdowns = READ_ULONGLONG(NvmeSmart->UnsafeShutdowns);
        UCHAR shutdownValue = (unsafeShutdowns == 0) ? 100 : ((unsafeShutdowns < 100) ? (100 - (UCHAR)unsafeShutdowns) : 0);
        ADD_ATTRIBUTE(192, shutdownValue, shutdownValue, unsafeShutdowns);
    }

    // 8. Total LBAs Written (Attribute 241) - if data units written is non-zero
    // NVMe reports in units of 1000 * 512-byte blocks = 512,000 bytes
    // Convert to 32MB units: 512,000 / (32 * 1024 * 1024) = 512,000 / 33,554,432 ≈ 1/65.536
    // So multiply by 1000 * 512 and divide by (32 * 1024 * 1024) = multiply by 512000 / 33554432
    // Simplified: divide by ~65.536, or multiply by 125 and divide by 8192
    {
        ULONGLONG dataUnitsWritten = READ_ULONGLONG(NvmeSmart->DataUnitsWritten);
        ULONGLONG units32MB = (dataUnitsWritten * 125) / 8192;
        ADD_ATTRIBUTE(ATA_SMART_ATTR_TOTAL_LBA_WRITTEN, 100, 100, units32MB);
    }

    // 9. Total LBAs Read (Attribute 242)
    // Same conversion as writes: NVMe data units to 32MB units
    {
        ULONGLONG dataUnitsRead = READ_ULONGLONG(NvmeSmart->DataUnitsRead);
        ULONGLONG units32MB = (dataUnitsRead * 125) / 8192;
        ADD_ATTRIBUTE(ATA_SMART_ATTR_TOTAL_LBA_READ, 100, 100, units32MB);
    }

    // 10. Add some zero-filled mechanical drive attributes (not applicable to NVMe)
    // Seek Error Rate (7) - N/A for SSDs
    ADD_ATTRIBUTE(ATA_SMART_ATTR_SEEK_ERROR_RATE, 100, 100, zero);

    // Spin Up Time (3) - N/A for SSDs
    ADD_ATTRIBUTE(ATA_SMART_ATTR_SPIN_UP_TIME, 100, 100, zero);

    // Start/Stop Count (4) - Use power cycles as approximation
    {
        ULONGLONG startStops = READ_ULONGLONG(NvmeSmart->PowerCycles);
        ADD_ATTRIBUTE(ATA_SMART_ATTR_START_STOP_COUNT, 100, 100, startStops);
    }
 
#undef ADD_ATTRIBUTE

    // Set capabilities and status
    AtaSmart->OfflineDataCollectionStatus = 0x00;  // Never started
    AtaSmart->SelfTestExecutionStatus = 0x00;      // No self-test in progress
    WRITE_USHORT(AtaSmart->TotalTimeToCompleteOfflineDataCollection, 0);
    AtaSmart->OfflineDataCollectionCapability = 0x01;  // Offline supported
    WRITE_USHORT(AtaSmart->SmartCapability, 0x0003);  // Attribute autosave, SMART enabled
    AtaSmart->ErrorLoggingCapability = 0x01;  // Error logging supported

    // Calculate checksum (sum of all 512 bytes should be 0)
    checksum = 0;
    for (i = 0; i < 511; i++) {
        checksum += ((PUCHAR)AtaSmart)[i];
    }
    AtaSmart->Checksum = (UCHAR)(0x100 - checksum);
}

//
// ScsiGetLogPageCodeFromSrb - Extract Log Sense Page Code from CDB
//
UCHAR ScsiGetLogPageCodeFromSrb(IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;

    // Check for LOG SENSE (0x4D)
    if (cdb->CDB10.OperationCode == SCSIOP_LOG_SENSE) {
        // Page code is in bits 5:0 of byte 2
        return (cdb->LOGSENSE.PageCode & 0x3F);
    }

    // Should not happen if called from ScsiHandleLogSense
    return 0xFF;
}

//
// NvmeLogPageToScsiLogPage - Convert NVMe SMART/Health log to SCSI Log Page format
// Builds a proper SCSI log page with header and parameters
//
BOOLEAN NvmeLogPageToScsiLogPage(
    IN PNVME_SMART_INFO NvmeSmart,
    IN UCHAR ScsiPageCode,
    OUT PVOID ScsiLogBuffer,
    IN ULONG BufferSize,
    OUT PULONG BytesWritten)
{
    PSCSI_LOG_PAGE_HEADER pageHeader;
    PSCSI_LOG_PARAMETER param;
    PUCHAR buffer = (PUCHAR)ScsiLogBuffer;
    ULONG offset = 0;
    USHORT tempKelvin;
    USHORT tempCelsius;
    ULONGLONG value64;

    // Ensure buffer is large enough for at least the header
    if (BufferSize < sizeof(SCSI_LOG_PAGE_HEADER)) {
        *BytesWritten = 0;
        return FALSE;
    }

    // Clear the buffer
    RtlZeroMemory(ScsiLogBuffer, BufferSize);

    // Build SCSI log page header
    pageHeader = (PSCSI_LOG_PAGE_HEADER)buffer;
    pageHeader->PageCode = ScsiPageCode & 0x3F;  // Only lower 6 bits
    pageHeader->Reserved = 0;
    offset += sizeof(SCSI_LOG_PAGE_HEADER);

    // Based on the page code, add appropriate parameters
    if (ScsiPageCode == SCSI_LOG_PAGE_INFORMATIONAL) {
        // Parameter 0x0000: Temperature
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 2 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;  // Parameter code 0x0000 (big-endian)
            param->ParameterCode[1] = 0x00;
            param->ControlByte = 0x01;  // Format and linking bits
            param->ParameterLength = 2;  // 2 bytes for temperature
            offset += sizeof(SCSI_LOG_PARAMETER);

            // Extract temperature from NVMe SMART (Kelvin, little-endian)
            tempKelvin = (USHORT)(NvmeSmart->Temperature[0] | (NvmeSmart->Temperature[1] << 8));
            tempCelsius = (tempKelvin > 273) ? (tempKelvin - 273) : 0;

            // Store as big-endian in SCSI format
            buffer[offset++] = (UCHAR)(tempCelsius >> 8);
            buffer[offset++] = (UCHAR)(tempCelsius & 0xFF);
        }

        // Parameter 0x0001: Available Spare
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 1 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;
            param->ParameterCode[1] = 0x01;
            param->ControlByte = 0x01;
            param->ParameterLength = 1;
            offset += sizeof(SCSI_LOG_PARAMETER);
            buffer[offset++] = NvmeSmart->AvailableSpare;
        }

        // Parameter 0x0002: Percentage Used
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 1 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;
            param->ParameterCode[1] = 0x02;
            param->ControlByte = 0x01;
            param->ParameterLength = 1;
            offset += sizeof(SCSI_LOG_PARAMETER);
            buffer[offset++] = NvmeSmart->PercentageUsed;
        }

        // Parameter 0x0003: Critical Warning
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 1 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;
            param->ParameterCode[1] = 0x03;
            param->ControlByte = 0x01;
            param->ParameterLength = 1;
            offset += sizeof(SCSI_LOG_PARAMETER);
            buffer[offset++] = NvmeSmart->CriticalWarning;
        }

        // Parameter 0x0004: Power On Hours (first 8 bytes)
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 8 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;
            param->ParameterCode[1] = 0x04;
            param->ControlByte = 0x01;
            param->ParameterLength = 8;
            offset += sizeof(SCSI_LOG_PARAMETER);

            // Copy first 8 bytes of power on hours (little-endian in NVMe)
            RtlCopyMemory(buffer + offset, NvmeSmart->PowerOnHours, 8);
            offset += 8;
        }

        // Parameter 0x0005: Data Units Read (first 8 bytes)
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 8 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;
            param->ParameterCode[1] = 0x05;
            param->ControlByte = 0x01;
            param->ParameterLength = 8;
            offset += sizeof(SCSI_LOG_PARAMETER);

            RtlCopyMemory(buffer + offset, NvmeSmart->DataUnitsRead, 8);
            offset += 8;
        }

        // Parameter 0x0006: Data Units Written (first 8 bytes)
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 8 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;
            param->ParameterCode[1] = 0x06;
            param->ControlByte = 0x01;
            param->ParameterLength = 8;
            offset += sizeof(SCSI_LOG_PARAMETER);

            RtlCopyMemory(buffer + offset, NvmeSmart->DataUnitsWritten, 8);
            offset += 8;
        }

        // Parameter 0x0007: Media Errors (first 8 bytes)
        if (offset + sizeof(SCSI_LOG_PARAMETER) + 8 <= BufferSize) {
            param = (PSCSI_LOG_PARAMETER)(buffer + offset);
            param->ParameterCode[0] = 0x00;
            param->ParameterCode[1] = 0x07;
            param->ControlByte = 0x01;
            param->ParameterLength = 8;
            offset += sizeof(SCSI_LOG_PARAMETER);

            RtlCopyMemory(buffer + offset, NvmeSmart->MediaErrors, 8);
            offset += 8;
        }
    } else {
        // Unsupported page code
        *BytesWritten = 0;
        return FALSE;
    }

    // Update page length in header (big-endian, excluding the header itself)
    {
        USHORT pageLength = (USHORT)(offset - sizeof(SCSI_LOG_PAGE_HEADER));
        pageHeader->PageLength[0] = (UCHAR)(pageLength >> 8);
        pageHeader->PageLength[1] = (UCHAR)(pageLength & 0xFF);
    }

    *BytesWritten = offset;
    return TRUE;
}

//
// ScsiParseSatCommand - Parse SAT ATA PASS-THROUGH command and validate it's a SMART read
// Returns TRUE if it's a valid SMART READ command (READ DATA or IDENTIFY)
//
BOOLEAN ScsiParseSatCommand(
    IN PSCSI_REQUEST_BLOCK Srb,
    OUT PUCHAR AtaCommand,
    OUT PUCHAR AtaFeatures,
    OUT PUCHAR AtaCylLow,
    OUT PUCHAR AtaCylHigh)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    PSAT_PASSTHROUGH_16 sat16;
    PSAT_PASSTHROUGH_12 sat12;
    UCHAR command;
    UCHAR features;
    UCHAR lbaMid;
    UCHAR lbaHigh;
    UCHAR protocol;

    if (cdb->CDB6GENERIC.OperationCode == SCSIOP_ATA_PASSTHROUGH16) {
        // ATA PASS-THROUGH (16)
        sat16 = (PSAT_PASSTHROUGH_16)Srb->Cdb;
        command = sat16->Command;
        features = sat16->Features7_0;
        lbaMid = sat16->LbaMid7_0;
        lbaHigh = sat16->LbaHigh7_0;
        protocol = sat16->Protocol;
    } else if (cdb->CDB6GENERIC.OperationCode == SCSIOP_ATA_PASSTHROUGH12) {
        // ATA PASS-THROUGH (12)
        sat12 = (PSAT_PASSTHROUGH_12)Srb->Cdb;
        command = sat12->Command;
        features = sat12->Features;
        lbaMid = sat12->LbaMid;
        lbaHigh = sat12->LbaHigh;
        protocol = sat12->Protocol;
    } else {
        return FALSE;
    }

    // We only support PIO/UDMA Data-In protocol (read operations)
    if (protocol != SAT_PROTOCOL_PIO_DATA_IN && protocol != SAT_PROTOCOL_UDMA_DATA_IN && protocol != SAT_PROTOCOL_DEVICE_DIAGNOSTIC) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: parse sat command unsupported protocol %02X\n", protocol);
#endif
        return FALSE;
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: parse sat command %02X features %02X lbaMid %02X lbaHigh %02X\n", command, features, lbaMid, lbaHigh);
#endif
    // Check if this is SMART READ DATA (0xB0 with feature 0xD0)
    if (command == ATA_SMART_CMD && features == ATA_SMART_READ_DATA &&
        lbaMid == SMART_CYL_LOW && lbaHigh == SMART_CYL_HI) {
        *AtaCommand = command;
        *AtaFeatures = features;
        *AtaCylLow = lbaMid;
        *AtaCylHigh = lbaHigh;
        return TRUE;
    }

    // Check if this is SMART READ LOG (0xB0 with feature 0xD5)
    if (command == ATA_SMART_CMD && features == ATA_SMART_READ_LOG &&
        lbaMid == SMART_CYL_LOW && lbaHigh == SMART_CYL_HI) {
        *AtaCommand = command;
        *AtaFeatures = features;
        *AtaCylLow = lbaMid;
        *AtaCylHigh = lbaHigh;
        return TRUE;
    }

    // Check if this is ATA IDENTIFY DEVICE (0xEC)
    if (command == ATA_IDENTIFY_DEVICE) {
        *AtaCommand = command;
        *AtaFeatures = features;
        *AtaCylLow = lbaMid;
        *AtaCylHigh = lbaHigh;
        return TRUE;
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: FAIL parse sat command %02X features %02X lbaMid %02X lbaHigh %02X\n", command, features, lbaMid, lbaHigh);
#endif
    // Not a supported SMART read command
    return FALSE;
}

//
// NvmeToAtaIdentify - Build ATA IDENTIFY DEVICE structure from NVMe controller/namespace info
// Emulates an LBA-capable IDE hard drive
//
VOID NvmeToAtaIdentify(
    IN PHW_DEVICE_EXTENSION DevExt,
    OUT PATA_IDENTIFY_DEVICE_STRUCT AtaIdentify)
{
    ULONG i;
    ULONGLONG totalSectors;
    ULONG cylinders, heads, sectors;

    // Zero out the entire structure
    RtlZeroMemory(AtaIdentify, sizeof(ATA_IDENTIFY_DEVICE_STRUCT));

    // Word 0: General configuration
    // Bit 15: 0=ATA device, Bit 7: Removable=0, Bit 6: Fixed=1
    WRITE_USHORT(AtaIdentify->GeneralConfiguration, 0x0040);

    // Get total sectors from namespace
    totalSectors = DevExt->NamespaceSizeInBlocks;

    // Calculate CHS geometry (emulate old IDE drives)
    // Use standard translation: 16 heads, 63 sectors per track
    heads = 16;
    sectors = 63;
    if (totalSectors > 0xFFFFFFF) {  // > 268M sectors (137GB)
        cylinders = 16383;  // Maximum for CHS
    } else {
        cylinders = (ULONG)(totalSectors / (heads * sectors));
        if (cylinders > 16383) cylinders = 16383;
    }

    // Word 1: Number of logical cylinders
    WRITE_USHORT(AtaIdentify->NumCylinders, (USHORT)cylinders);

    // Word 3: Number of logical heads
    WRITE_USHORT(AtaIdentify->NumHeads, (USHORT)heads);

    // Word 6: Number of logical sectors per track
    WRITE_USHORT(AtaIdentify->NumSectorsPerTrack, (USHORT)sectors);

    // Words 10-19: Serial number (20 bytes, ASCII, byte-swapped pairs)
    {
        UCHAR serial[20];
        RtlZeroMemory(serial, 20);
        for (i = 0; i < 20 && i < sizeof(DevExt->ControllerSerialNumber); i++) {
            serial[i] = DevExt->ControllerSerialNumber[i];
        }
        // Pad with spaces
        for (; i < 20; i++) {
            serial[i] = ' ';
        }
        // Byte-swap pairs for ATA format
        for (i = 0; i < 20; i += 2) {
            AtaIdentify->SerialNumber[i] = serial[i + 1];
            AtaIdentify->SerialNumber[i + 1] = serial[i];
        }
    }

    // Words 23-26: Firmware revision (8 bytes, ASCII, byte-swapped)
    {
        UCHAR firmware[8];
        RtlZeroMemory(firmware, 8);
        for (i = 0; i < 8 && i < sizeof(DevExt->ControllerFirmwareRevision); i++) {
            firmware[i] = DevExt->ControllerFirmwareRevision[i];
        }
        // Pad with spaces
        for (; i < 8; i++) {
            firmware[i] = ' ';
        }
        // Byte-swap pairs
        for (i = 0; i < 8; i += 2) {
            AtaIdentify->FirmwareRevision[i] = firmware[i + 1];
            AtaIdentify->FirmwareRevision[i + 1] = firmware[i];
        }
    }

    // Words 27-46: Model number (40 bytes, ASCII, byte-swapped)
    {
        UCHAR model[40];
        RtlZeroMemory(model, 40);
        for (i = 0; i < 40 && i < sizeof(DevExt->ControllerModelNumber); i++) {
            model[i] = DevExt->ControllerModelNumber[i];
        }
        // Pad with spaces
        for (; i < 40; i++) {
            model[i] = ' ';
        }
        // Byte-swap pairs
        for (i = 0; i < 40; i += 2) {
            AtaIdentify->ModelNumber[i] = model[i + 1];
            AtaIdentify->ModelNumber[i + 1] = model[i];
        }
    }

    // Word 47: Maximum sectors per interrupt
    WRITE_USHORT(AtaIdentify->MaxMultipleSectors, 0x8010);  // Max 16 sectors

    // Word 49: Capabilities - Bit 9: LBA supported, Bit 8: DMA supported
    WRITE_USHORT(AtaIdentify->Capabilities, 0x0300);

    // Word 50: Capabilities
    WRITE_USHORT(AtaIdentify->Capabilities + 2, 0x4000);

    // Word 53: Translation fields valid
    WRITE_USHORT(AtaIdentify->ValidFields, 0x0007);

    // Word 54-56: Current CHS translation
    WRITE_USHORT(AtaIdentify->CurrentCylinders, (USHORT)cylinders);
    WRITE_USHORT(AtaIdentify->CurrentHeads, (USHORT)heads);
    WRITE_USHORT(AtaIdentify->CurrentSectorsPerTrack, (USHORT)sectors);

    // Word 57-58: Current capacity in sectors
    {
        ULONG chsCapacity = cylinders * heads * sectors;
        WRITE_USHORT(AtaIdentify->CurrentCapacityLow, (USHORT)(chsCapacity & 0xFFFF));
        WRITE_USHORT(AtaIdentify->CurrentCapacityHigh, (USHORT)(chsCapacity >> 16));
    }

    // Word 59: Multiple sector setting
    WRITE_USHORT(AtaIdentify->MultipleSectorSetting, 0x0110);

    // Word 60-61: Total addressable sectors (LBA-28)
    {
        ULONG lba28Sectors = (totalSectors > 0x0FFFFFFF) ? 0x0FFFFFFF : (ULONG)totalSectors;
        WRITE_USHORT(AtaIdentify->TotalAddressableSectors, (USHORT)(lba28Sectors & 0xFFFF));
        WRITE_USHORT(AtaIdentify->TotalAddressableSectors + 2, (USHORT)(lba28Sectors >> 16));
    }

    // Word 63: Multiword DMA mode
    WRITE_USHORT(AtaIdentify->MultiwordDmaMode, 0x0407);

    // Word 64: PIO modes supported
    WRITE_USHORT(AtaIdentify->PioModesSupported, 0x0003);

    // Word 65-68: DMA and PIO cycle times
    WRITE_USHORT(AtaIdentify->MinMdmaCycleTime, 120);
    WRITE_USHORT(AtaIdentify->RecommendedMdmaCycleTime, 120);
    WRITE_USHORT(AtaIdentify->MinPioCycleTime, 240);
    WRITE_USHORT(AtaIdentify->MinPioCycleTimeIordy, 120);

    // Word 75: Queue depth
    WRITE_USHORT(AtaIdentify->QueueDepth, 0x0000);

    // Word 80: Major version (ATA/ATAPI-7)
    WRITE_USHORT(AtaIdentify->MajorVersion, 0x007E);

    // Word 81: Minor version
    WRITE_USHORT(AtaIdentify->MinorVersion, 0x0019);

    // Word 82: Command sets supported
    // Bit 14: NOP, Bit 10: Write Buffer, Bit 9: Read Buffer
    // Bit 6: Read/Write Buffer DMA, Bit 5: Power Management
    // Bit 1: SMART feature set supported
    // Bit 0: SMART error logging supported
    WRITE_USHORT(AtaIdentify->CommandSetSupported1, 0x442B);

    // Word 83: Command sets supported
    // Bit 14: Must be 1, Bit 13: Must be 0
    // Bit 10: 48-bit Address feature set supported
    // Bit 0: SMART self-test supported
    WRITE_USHORT(AtaIdentify->CommandSetSupported2, 0x4401);

    // Word 84: Command sets supported extended
    // Bit 14: Must be 1, Bit 8: WWN supported
    WRITE_USHORT(AtaIdentify->CommandSetSupportedExt, 0x4100);

    // Word 85-86: Command sets enabled (match supported)
    // Bit 1: SMART feature set enabled
    // Bit 0: SMART error logging enabled
    WRITE_USHORT(AtaIdentify->CommandSetEnabled1, 0x4428 | (DevExt->SMARTEnabled ? 3 : 0));
    // Bit 0: SMART self-test enabled
    WRITE_USHORT(AtaIdentify->CommandSetEnabled2, 0x4400 | (DevExt->SMARTEnabled ? 1 : 0));

    // Word 87: Command set default
    WRITE_USHORT(AtaIdentify->CommandSetDefault, 0x4000);

    // Word 88: Ultra DMA modes
    WRITE_USHORT(AtaIdentify->UltraDmaMode, 0x203F);

    // Word 93: Hardware reset result
    WRITE_USHORT(AtaIdentify->HardwareResetResult, 0x4000);

    // Word 100-103: Total addressable sectors (LBA-48)
    WRITE_USHORT(AtaIdentify->TotalAddressableSectors48, (USHORT)(totalSectors & 0xFFFF));
    WRITE_USHORT(AtaIdentify->TotalAddressableSectors48 + 2, (USHORT)((totalSectors >> 16) & 0xFFFF));
    WRITE_USHORT(AtaIdentify->TotalAddressableSectors48 + 4, (USHORT)((totalSectors >> 32) & 0xFFFF));
    WRITE_USHORT(AtaIdentify->TotalAddressableSectors48 + 6, (USHORT)((totalSectors >> 48) & 0xFFFF));

    // Word 108-111: World Wide Name (WWN) - 64-bit identifier
    // Set to "NVME2K\0\0" with word byte-swapping for ATA format
    // In ATA, strings are stored with bytes swapped within each word
    {
        UCHAR wwn[8];

        // Set up the WWN string "NVME2K\0\0"
        wwn[0] = 'N'; wwn[1] = 'V'; wwn[2] = 'M'; wwn[3] = 'E';
        wwn[4] = '2'; wwn[5] = 'K'; wwn[6] = '\0'; wwn[7] = '\0';

        // Byte-swap pairs for ATA word format
        for (i = 0; i < 8; i += 2) {
            AtaIdentify->WorldWideName[i] = wwn[i + 1];
            AtaIdentify->WorldWideName[i + 1] = wwn[i];
        }
    }

    // Word 127: Removable media status notification
    WRITE_USHORT(AtaIdentify->RemovableMediaStatus, 0x0000);

    // Word 128: Security status
    WRITE_USHORT(AtaIdentify->SecurityStatus, 0x0000);

    // Word 217: Nominal Media Rotation Rate (1 = SSD)
    WRITE_USHORT(AtaIdentify->NominalMediaRotationRate, 0x0001);
}

//
// AllocatePrpListPage - Allocate a PRP list page from the pool
// Returns page index (0-9) or 0xFF if none available
//
UCHAR AllocatePrpListPage(IN PHW_DEVICE_EXTENSION DevExt)
{
    UCHAR i;

    for (i = 0; i < DevExt->SgListPages; i++) {
        if ((DevExt->PrpListPageBitmap & (1 << i)) == 0) {
            // Found free page
            DevExt->PrpListPageBitmap |= (1 << i);
#ifdef NVME2K_DBG_TOOMUCH
            ScsiDebugPrint(0, "nvme2k: Allocated PRP list page %d\n", i);
#endif

            // Track maximum PRP list pages used
            DevExt->CurrentPrpListPagesUsed++;
            if (DevExt->CurrentPrpListPagesUsed > DevExt->MaxPrpListPagesUsed) {
                DevExt->MaxPrpListPagesUsed = DevExt->CurrentPrpListPagesUsed;
            }

            return i;
        }
    }

#ifdef NVME2K_DBG_TOOMUCH
    ScsiDebugPrint(0, "nvme2k: No free PRP list pages available\n");
#endif
    return 0xFF;  // No pages available
}

//
// FreePrpListPage - Free a PRP list page back to the pool
//
VOID FreePrpListPage(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR pageIndex)
{
    if (pageIndex < DevExt->SgListPages) {
        DevExt->PrpListPageBitmap &= ~(1 << pageIndex);
        DevExt->CurrentPrpListPagesUsed--;
#ifdef NVME2K_DBG_TOOMUCH
        ScsiDebugPrint(0, "nvme2k: Freed PRP list page %d\n", pageIndex);
#endif
    }
}

//
// GetPrpListPageVirtual - Get virtual address of a PRP list page
//
PVOID GetPrpListPageVirtual(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR pageIndex)
{
    if (pageIndex >= DevExt->SgListPages) {
        return NULL;
    }
    return (PVOID)((PUCHAR)DevExt->PrpListPages + (pageIndex * PAGE_SIZE));
}

//
// GetPrpListPagePhysical - Get physical address of a PRP list page
//
PHYSICAL_ADDRESS GetPrpListPagePhysical(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR pageIndex)
{
    PHYSICAL_ADDRESS addr;

    if (pageIndex >= DevExt->SgListPages) {
        addr.QuadPart = 0;
        return addr;
    }

    addr.QuadPart = DevExt->PrpListPagesPhys.QuadPart + (pageIndex * PAGE_SIZE);
    return addr;
}

//
// AllocateUncachedMemory - Allocate memory from uncached extension with alignment
//
BOOLEAN AllocateUncachedMemory(
    IN PHW_DEVICE_EXTENSION DevExt,
    IN ULONG Size,
    IN ULONG Alignment,
    OUT PVOID* VirtualAddress,
    OUT PHYSICAL_ADDRESS* PhysicalAddress)
{
    ULONG alignedOffset;
    ULONG alignmentMask;

    // Calculate alignment mask (e.g., 0x1000 - 1 = 0xFFF)
    alignmentMask = Alignment - 1;

    // Align current offset up to the requested alignment
    alignedOffset = (DevExt->UncachedExtensionOffset + alignmentMask) & ~alignmentMask;

    // Check if we have enough space
    if (alignedOffset + Size > DevExt->UncachedExtensionSize) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: AllocateUncachedMemory - OUT OF MEMORY! Need %u bytes at offset %u, total %u\n",
                       Size, alignedOffset, DevExt->UncachedExtensionSize);
        ScsiDebugPrint(0, "nvme2k: AllocateUncachedMemory - Increase UNCACHED_EXTENSION_SIZE in nvme2k.h!\n");
#endif
        return FALSE;
    }

    // Calculate addresses
    *VirtualAddress = (PUCHAR)DevExt->UncachedExtensionBase + alignedOffset;
    PhysicalAddress->QuadPart = DevExt->UncachedExtensionPhys.QuadPart + alignedOffset;

    // Update offset for next allocation
    DevExt->UncachedExtensionOffset = alignedOffset + Size;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: AllocateUncachedMemory - allocated %u bytes at offset %u (virt=%p phys=%08X%08X)\n",
                   Size, alignedOffset, *VirtualAddress,
                   (ULONG)(PhysicalAddress->QuadPart >> 32),
                   (ULONG)(PhysicalAddress->QuadPart & 0xFFFFFFFF));
#endif
    return TRUE;
}

//
// IsNvmeDevice - Check if PCI class codes match NVMe
//
BOOLEAN IsNvmeDevice(UCHAR BaseClass, UCHAR SubClass, UCHAR ProgIf)
{
    return (BaseClass == PCI_CLASS_MASS_STORAGE_CONTROLLER &&
            SubClass == PCI_SUBCLASS_NON_VOLATILE_MEMORY &&
            ProgIf == PCI_PROGIF_NVME);
}

//
// ReadPciConfigByte - Read 8-bit value from PCI config space
//
UCHAR ReadPciConfigByte(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset)
{
    UCHAR buffer[256];

    ScsiPortGetBusData(
        (PVOID)DevExt,
        PCIConfiguration,
        DevExt->BusNumber,
        DevExt->SlotNumber,
        buffer,
        256);

    return buffer[Offset];
}

//
// ReadPciConfigWord - Read 16-bit value from PCI config space
//
USHORT ReadPciConfigWord(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset)
{
    UCHAR buffer[256];

    ScsiPortGetBusData(
        (PVOID)DevExt,
        PCIConfiguration,
        DevExt->BusNumber,
        DevExt->SlotNumber,
        buffer,
        256);

    return *(USHORT*)&buffer[Offset];
}

//
// ReadPciConfigDword - Read 32-bit value from PCI config space
//
ULONG ReadPciConfigDword(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset)
{
    UCHAR buffer[256];

    ScsiPortGetBusData(
        (PVOID)DevExt,
        PCIConfiguration,
        DevExt->BusNumber,
        DevExt->SlotNumber,
        buffer,
        256);

    return *(ULONG*)&buffer[Offset];
}

//
// WritePciConfigWord - Write 16-bit value to PCI config space
//
VOID WritePciConfigWord(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN USHORT Value)
{
    ScsiPortSetBusDataByOffset(
        (PVOID)DevExt,
        PCIConfiguration,
        DevExt->BusNumber,
        DevExt->SlotNumber,
        &Value,
        Offset,
        sizeof(USHORT));
}

//
// WritePciConfigDword - Write 32-bit value to PCI config space
//
VOID WritePciConfigDword(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value)
{
    ScsiPortSetBusDataByOffset(
        (PVOID)DevExt,
        PCIConfiguration,
        DevExt->BusNumber,
        DevExt->SlotNumber,
        &Value,
        Offset,
        sizeof(ULONG));
}

//
// log2 - Calculate integer base-2 logarithm
// Returns the highest bit set, e.g., log2(4096) = 12
//
ULONG log2(ULONG n)
{
    ULONG bits;
    if (n == 0) {
        return 0; // log2(0) is undefined, return 0 as a safe value
    }
    // Find the position of the most significant bit
    for (bits = 0; n > 1; bits++) {
        n >>= 1;
    }
    return bits;
}
