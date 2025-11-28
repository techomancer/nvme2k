//
// utils.h - Utility functions for NVMe2K driver
//

#ifndef _NVME2K_UTILS_H_
#define _NVME2K_UTILS_H_

// Forward declare to avoid circular dependency if nvme2k.h includes utils.h first
struct _NVME_SMART_INFO;
struct _ATA_SMART_DATA;



//
// Byte manipulation helper macros for handling unaligned little-endian data.
// These are safe for architectures like Alpha AXP that fault on unaligned access.
//
#define READ_USHORT(p) ((USHORT)((p)[0] | ((USHORT)(p)[1] << 8)))
#define READ_ULONG(p)  ((ULONG)((p)[0] | ((ULONG)(p)[1] << 8) | ((ULONG)(p)[2] << 16) | ((ULONG)(p)[3] << 24)))
#define READ_ULONGLONG(p) ((ULONGLONG)READ_ULONG(p) | ((ULONGLONG)READ_ULONG((p)+4) << 32))

#define WRITE_USHORT(p, val) do { (p)[0] = (UCHAR)(val); (p)[1] = (UCHAR)((val) >> 8); } while(0)
#define WRITE_ULONG(p, val) do { (p)[0] = (UCHAR)(val); (p)[1] = (UCHAR)((val) >> 8); \
                                  (p)[2] = (UCHAR)((val) >> 16); (p)[3] = (UCHAR)((val) >> 24); } while(0)


VOID NvmeSmartToAtaSmart(IN struct _NVME_SMART_INFO *NvmeSmart, OUT struct _ATA_SMART_DATA *AtaSmart);
BOOLEAN NvmeGetLogPage(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR LogPageId);
ULONG log2(ULONG n);

#endif // _NVME2K_UTILS_H_