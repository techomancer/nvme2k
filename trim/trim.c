/*
 * trim.c - Windows 2000 NVMe TRIM/UNMAP utility
 *
 * Console application to send TRIM commands to NVMe devices
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Debug: Limit number of pages to write (0 = no limit)
#define DEBUG_MAX_PAGES 0

// TRIM chunk size - write this many bytes at a time (must be multiple of 4096)
#define TRIM_CHUNK_SIZE (1024 * 1024)  // 1MB chunks

// SCSI IOCTL definitions
#define IOCTL_SCSI_BASE                 FILE_DEVICE_CONTROLLER
#define IOCTL_SCSI_MINIPORT             CTL_CODE(IOCTL_SCSI_BASE, 0x0402, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

// Custom NVME2KDB control codes
#define NVME2KDB_IOCTL_QUERY_INFO       0x1000
#define NVME2KDB_IOCTL_TRIM_MODE_ON     0x1001
#define NVME2KDB_IOCTL_TRIM_MODE_OFF    0x1002

// SRB_IO_CONTROL structure for SCSI miniport communication
#pragma pack(push, 1)
typedef struct _SRB_IO_CONTROL {
    ULONG HeaderLength;
    UCHAR Signature[8];
    ULONG Timeout;
    ULONG ControlCode;
    ULONG ReturnCode;
    ULONG Length;
} SRB_IO_CONTROL, *PSRB_IO_CONTROL;
#pragma pack(pop)

// Global buffers for random pattern
ULONG *g_random_buffer = NULL;      // 4KB pattern sent to driver
ULONG *g_write_buffer = NULL;       // Large buffer for writing (TRIM_CHUNK_SIZE)

// MSVC 6.0 compatibility
#if _MSC_VER <= 1200
#define snprintf _snprintf
#endif

/*
 * Send NVME2KDB IOCTL to the driver
 * Returns 0 on success, -1 on failure
 */
int send_nvme2kdb_ioctl(HANDLE hDevice, ULONG control_code, void *data_buffer, ULONG data_size)
{
    UCHAR buffer[8192];  // SRB_IO_CONTROL (28 bytes) + 4KB data
    PSRB_IO_CONTROL srb_control;
    DWORD bytes_returned;
    BOOL result;
    ULONG total_size;

    // Calculate total size: SRB_IO_CONTROL header + data
    total_size = sizeof(SRB_IO_CONTROL) + data_size;

    if (total_size > sizeof(buffer)) {
        printf("Error: Data size too large (%lu bytes, max %lu)\n",
               data_size, sizeof(buffer) - sizeof(SRB_IO_CONTROL));
        return -1;
    }

    // Zero out the buffer
    memset(buffer, 0, sizeof(buffer));

    // Set up SRB_IO_CONTROL header
    srb_control = (PSRB_IO_CONTROL)buffer;
    srb_control->HeaderLength = sizeof(SRB_IO_CONTROL);
    memcpy(srb_control->Signature, "NVME2KDB", 8);
    srb_control->Timeout = 30;  // 30 second timeout
    srb_control->ControlCode = control_code;
    srb_control->ReturnCode = 0;
    srb_control->Length = data_size;

    // Copy data after the header if provided
    if (data_buffer && data_size > 0) {
        memcpy(buffer + sizeof(SRB_IO_CONTROL), data_buffer, data_size);
    }

    printf("Sending NVME2KDB IOCTL 0x%08lX...\n", control_code);

    // Send the IOCTL
    result = DeviceIoControl(
        hDevice,
        IOCTL_SCSI_MINIPORT,
        buffer,
        total_size,
        buffer,
        sizeof(buffer),
        &bytes_returned,
        NULL
    );

    if (!result) {
        printf("Error: DeviceIoControl failed. Error code: %lu\n", GetLastError());
        return -1;
    }

    // Check the return code from the driver
    if (srb_control->ReturnCode != 0) {
        printf("Error: Driver returned error code: %lu\n", srb_control->ReturnCode);
        return -1;
    }

    printf("IOCTL completed successfully. Bytes returned: %lu\n", bytes_returned);

    // Copy response data back if caller provided a buffer
    if (data_buffer && data_size > 0 && bytes_returned > sizeof(SRB_IO_CONTROL)) {
        ULONG response_size = bytes_returned - sizeof(SRB_IO_CONTROL);
        if (response_size > data_size) {
            response_size = data_size;
        }
        memcpy(data_buffer, buffer + sizeof(SRB_IO_CONTROL), response_size);
    }

    return 0;
}

/*
 * Initialize the global random buffer and write buffer
 * Returns 0 on success, -1 on failure
 */
int initialize_random_buffer(void)
{
    DWORD tick_count;
    ULONG i;
    ULONG chunks_in_write_buffer;
    PUCHAR dest;

    // Allocate 4KB aligned buffer for pattern
    g_random_buffer = (ULONG *)VirtualAlloc(
        NULL,
        4096,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (g_random_buffer == NULL) {
        printf("Error: Failed to allocate pattern buffer. Error code: %lu\n", GetLastError());
        return -1;
    }

    // Allocate large write buffer
    g_write_buffer = (ULONG *)VirtualAlloc(
        NULL,
        TRIM_CHUNK_SIZE,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (g_write_buffer == NULL) {
        printf("Error: Failed to allocate write buffer. Error code: %lu\n", GetLastError());
        VirtualFree(g_random_buffer, 0, MEM_RELEASE);
        g_random_buffer = NULL;
        return -1;
    }

    // Get current time in milliseconds since system start
    tick_count = GetTickCount();

    printf("Initializing random seed with GetTickCount(): %lu ms\n", tick_count);
    srand(tick_count);

    // Fill pattern buffer with random numbers
    printf("Generating 4KB random pattern (1024 ULONGs)...\n");
    for (i = 0; i < 1024; i++) {
        g_random_buffer[i] = ((ULONG)rand() << 16) | (ULONG)rand();
    }

    // Replicate the 4KB pattern throughout the write buffer using memcpy
    chunks_in_write_buffer = TRIM_CHUNK_SIZE / 4096;
    printf("Replicating pattern into %lu KB write buffer (%lu copies)...\n",
           TRIM_CHUNK_SIZE / 1024, chunks_in_write_buffer);

    dest = (PUCHAR)g_write_buffer;
    for (i = 0; i < chunks_in_write_buffer; i++) {
        memcpy(dest, g_random_buffer, 4096);
        dest += 4096;
    }

    return 0;
}

/*
 * Free the global buffers
 */
void free_random_buffer(void)
{
    if (g_write_buffer != NULL) {
        VirtualFree(g_write_buffer, 0, MEM_RELEASE);
        g_write_buffer = NULL;
    }
    if (g_random_buffer != NULL) {
        VirtualFree(g_random_buffer, 0, MEM_RELEASE);
        g_random_buffer = NULL;
    }
}

/*
 * Enable TRIM mode with the global random 4KB buffer
 * Returns 0 on success, -1 on failure
 */
int enable_trim_mode(HANDLE hDevice)
{
    if (g_random_buffer == NULL) {
        printf("Error: Random buffer not initialized.\n");
        return -1;
    }

    printf("Enabling TRIM mode...\n");
    return send_nvme2kdb_ioctl(hDevice, NVME2KDB_IOCTL_TRIM_MODE_ON, g_random_buffer, 4096);
}

/*
 * Fill the volume with random pattern until disk is full
 * Returns 0 on success, -1 on failure
 */
int fill_volume_with_pattern(const char *volume_letter)
{
    char file_path[MAX_PATH];
    char volume_root[8];
    HANDLE hFile;
    ULARGE_INTEGER free_bytes_available;
    ULARGE_INTEGER total_bytes;
    ULARGE_INTEGER total_free_bytes;
    ULONGLONG chunks_to_write;
    ULONGLONG chunks_written = 0;
    ULONGLONG bytes_to_write;
    DWORD bytes_written;
    DWORD write_size;
    BOOL write_result;
    int last_percent = -1;
    int current_percent;

    if (g_write_buffer == NULL) {
        printf("Error: Write buffer not initialized.\n");
        return -1;
    }

    // Build volume root path (e.g., "C:\")
    snprintf(volume_root, sizeof(volume_root), "%s\\", volume_letter);

    // Build file path on the volume
    snprintf(file_path, sizeof(file_path), "%s\\nvme2ktrim", volume_letter);

    printf("\nQuerying free space on volume %s...\n", volume_root);

    if (!GetDiskFreeSpaceEx(
            volume_root,
            &free_bytes_available,
            &total_bytes,
            &total_free_bytes)) {
        printf("Error: Failed to get disk free space. Error code: %lu\n", GetLastError());
        return -1;
    }

    // Calculate number of chunks to write
    bytes_to_write = free_bytes_available.QuadPart;
    chunks_to_write = bytes_to_write / TRIM_CHUNK_SIZE;

#if DEBUG_MAX_PAGES > 0
    // Debug: Limit number of pages (convert to chunks)
    {
        ULONGLONG debug_chunks = (DEBUG_MAX_PAGES * 4096ULL) / TRIM_CHUNK_SIZE;
        if (chunks_to_write > debug_chunks) {
            printf("DEBUG: Limiting chunks from 0x%08lX%08lX to 0x%08lX%08lX for testing\n",
                   (ULONG)(chunks_to_write >> 32), (ULONG)(chunks_to_write & 0xFFFFFFFF),
                   (ULONG)(debug_chunks >> 32), (ULONG)(debug_chunks & 0xFFFFFFFF));
            chunks_to_write = debug_chunks;
            bytes_to_write = chunks_to_write * TRIM_CHUNK_SIZE;
        }
    }
#endif

    printf("Volume: %s\n", volume_root);
    printf("Total space: 0x%08lX%08lX bytes (%.2f GB)\n",
           (ULONG)(total_bytes.QuadPart >> 32),
           (ULONG)(total_bytes.QuadPart & 0xFFFFFFFF),
           (double)(LONGLONG)total_bytes.QuadPart / (1024.0 * 1024.0 * 1024.0));
    printf("Free space: 0x%08lX%08lX bytes (%.2f GB)\n",
           (ULONG)(free_bytes_available.QuadPart >> 32),
           (ULONG)(free_bytes_available.QuadPart & 0xFFFFFFFF),
           (double)(LONGLONG)free_bytes_available.QuadPart / (1024.0 * 1024.0 * 1024.0));
    printf("Chunks to write: 0x%08lX%08lX (%lu KB each)\n\n",
           (ULONG)(chunks_to_write >> 32),
           (ULONG)(chunks_to_write & 0xFFFFFFFF),
           TRIM_CHUNK_SIZE / 1024);

    // Create the file
    printf("Creating file: %s\n", file_path);
    hFile = CreateFile(
        file_path,
        GENERIC_WRITE,
        0,  // No sharing during write
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        printf("Error: Failed to create file. Error code: %lu\n", GetLastError());
        return -1;
    }

    printf("Writing random pattern to disk in %lu KB chunks...\n", TRIM_CHUNK_SIZE / 1024);

    // Write chunks until we run out of space or reach the calculated limit
    while (bytes_to_write > 0) {
        // Determine write size (last chunk might be smaller)
        write_size = (bytes_to_write >= TRIM_CHUNK_SIZE) ? TRIM_CHUNK_SIZE : (DWORD)bytes_to_write;

        write_result = WriteFile(
            hFile,
            g_write_buffer,
            write_size,
            &bytes_written,
            NULL
        );

        if (!write_result || bytes_written != write_size) {
            // Disk full or write error
            if (GetLastError() == ERROR_DISK_FULL) {
                printf("\nDisk full reached.\n");
            } else {
                printf("\nWrite failed. Error code: %lu\n", GetLastError());
            }
            break;
        }

        bytes_to_write -= bytes_written;
        chunks_written++;

        // Update progress every 1%
        current_percent = (int)((chunks_written * 100) / chunks_to_write);
        if (current_percent != last_percent) {
            printf("\rProgress: %d%% (0x%08lX%08lX / 0x%08lX%08lX chunks)",
                   current_percent,
                   (ULONG)(chunks_written >> 32), (ULONG)(chunks_written & 0xFFFFFFFF),
                   (ULONG)(chunks_to_write >> 32), (ULONG)(chunks_to_write & 0xFFFFFFFF));
            fflush(stdout);
            last_percent = current_percent;
        }
    }

    printf("\n\nTotal chunks written: 0x%08lX%08lX (%.2f GB)\n",
           (ULONG)(chunks_written >> 32),
           (ULONG)(chunks_written & 0xFFFFFFFF),
           (double)(LONGLONG)(chunks_written * TRIM_CHUNK_SIZE) / (1024.0 * 1024.0 * 1024.0));

    // Close the file
    CloseHandle(hFile);

    // Delete the file
    printf("Deleting file: %s\n", file_path);
    if (!DeleteFile(file_path)) {
        printf("Warning: Failed to delete file. Error code: %lu\n", GetLastError());
        return -1;
    }

    printf("File deleted successfully.\n");
    return 0;
}

/*
 * Resolve a volume letter (like "C:" or "E:") to its physical drive number
 * Returns physical drive number on success, -1 on failure
 */
int resolve_volume_to_physical_drive(const char *volume_letter)
{
    char volume_path[32];
    HANDLE hVolume;
    DWORD bytes_returned;
    STORAGE_DEVICE_NUMBER sdn;
    int physical_drive = -1;

    // Construct volume path like "\\.\C:"
    snprintf(volume_path, sizeof(volume_path), "\\\\.\\%s", volume_letter);

    printf("Resolving volume %s to physical drive...\n", volume_letter);

    hVolume = CreateFile(
        volume_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hVolume == INVALID_HANDLE_VALUE) {
        printf("Error: Failed to open volume %s. Error code: %lu\n",
               volume_letter, GetLastError());
        return -1;
    }

    // Query the storage device number
    if (DeviceIoControl(
            hVolume,
            IOCTL_STORAGE_GET_DEVICE_NUMBER,
            NULL,
            0,
            &sdn,
            sizeof(sdn),
            &bytes_returned,
            NULL)) {

        physical_drive = sdn.DeviceNumber;
        printf("Volume %s is on PhysicalDrive%d (Type: %lu, Partition: %lu)\n",
               volume_letter, physical_drive, sdn.DeviceType, sdn.PartitionNumber);
    } else {
        printf("Error: Failed to get device number. Error code: %lu\n", GetLastError());
    }

    CloseHandle(hVolume);
    return physical_drive;
}

/*
 * Parse volume letter argument and get physical drive path
 * Returns 0 on success, -1 on failure
 */
int parse_volume_argument(const char *arg, char *device_path, size_t path_size, char *volume_letter)
{
    char volume[3];
    int physical_drive;

    // Check if it looks like a volume letter (e.g., "C:" or "C")
    if (strlen(arg) > 2 || !((arg[0] >= 'A' && arg[0] <= 'Z') ||
                              (arg[0] >= 'a' && arg[0] <= 'z'))) {
        printf("Error: Invalid volume letter. Must be A-Z or a-z.\n");
        return -1;
    }

    // Normalize to "X:" format
    volume[0] = arg[0];
    volume[1] = ':';
    volume[2] = '\0';

    physical_drive = resolve_volume_to_physical_drive(volume);
    if (physical_drive < 0) {
        return -1;
    }

    snprintf(device_path, path_size, "\\\\.\\PhysicalDrive%d", physical_drive);

    // Copy volume letter for later use
    volume_letter[0] = volume[0];
    volume_letter[1] = ':';
    volume_letter[2] = '\0';

    return 0;
}

int main(int argc, char *argv[])
{
    char device_path[128];
    char volume_letter[3];
    HANDLE hDevice;
    int result = 0;

    printf("NVMe TRIM Utility for Windows 2000\n");
    printf("===================================\n\n");

    if (argc < 2) {
        printf("Usage: %s <volume_letter>\n", argv[0]);
        printf("Examples:\n");
        printf("  %s C:               (volume letter)\n", argv[0]);
        printf("  %s E                (volume letter without colon)\n", argv[0]);
        return 1;
    }

    // Initialize the global random buffer
    if (initialize_random_buffer() < 0) {
        return 1;
    }

    // Parse the volume argument and get physical drive path
    if (parse_volume_argument(argv[1], device_path, sizeof(device_path), volume_letter) < 0) {
        free_random_buffer();
        return 1;
    }

    printf("\nOpening device: %s\n", device_path);

    hDevice = CreateFile(
        device_path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Error: Failed to open device. Error code: %lu\n", GetLastError());
        free_random_buffer();
        return 1;
    }

    printf("Device opened successfully.\n\n");

    // Test NVME2KDB IOCTL communication
    printf("Testing NVME2KDB IOCTL interface...\n");

    // Query device info
    if (send_nvme2kdb_ioctl(hDevice, NVME2KDB_IOCTL_QUERY_INFO, NULL, 0) == 0) {
        printf("QUERY_INFO IOCTL succeeded.\n\n");
    } else {
        printf("QUERY_INFO IOCTL failed.\n\n");
    }

    // Enable TRIM mode with random buffer
    if (enable_trim_mode(hDevice) == 0) {
        printf("TRIM mode enabled successfully.\n\n");

        // Fill volume with random pattern
        if (fill_volume_with_pattern(volume_letter) < 0) {
            printf("Warning: Failed to fill volume with pattern.\n");
            result = 1;
        }

        // Disable TRIM mode
        printf("\nDisabling TRIM mode...\n");
        if (send_nvme2kdb_ioctl(hDevice, NVME2KDB_IOCTL_TRIM_MODE_OFF, NULL, 0) == 0) {
            printf("TRIM mode disabled successfully.\n");
        } else {
            printf("Failed to disable TRIM mode.\n");
            result = 1;
        }
    } else {
        printf("Failed to enable TRIM mode.\n");
        result = 1;
    }

    CloseHandle(hDevice);
    free_random_buffer();

    printf("\nOperation completed.\n");
    return result;
}
