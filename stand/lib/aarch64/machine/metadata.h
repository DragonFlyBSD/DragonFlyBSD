#ifndef _MACHINE_METADATA_H_
#define _MACHINE_METADATA_H_

#include <sys/stdint.h>

#define MODINFOMD_SMAP 0x1001
#define MODINFOMD_RES1 0x1002
#define MODINFOMD_RES2 0x1003
#define MODINFOMD_EFI_MAP 0x1004
#define MODINFOMD_EFI_FB 0x1005

struct efi_map_header {
	uint64_t memory_size;
	uint64_t descriptor_size;
	uint32_t descriptor_version;
};

struct efi_fb {
	uint64_t fb_addr;
	uint64_t fb_size;
	uint32_t fb_height;
	uint32_t fb_width;
	uint32_t fb_stride;
	uint32_t fb_mask_red;
	uint32_t fb_mask_green;
	uint32_t fb_mask_blue;
	uint32_t fb_mask_reserved;
};

#endif
