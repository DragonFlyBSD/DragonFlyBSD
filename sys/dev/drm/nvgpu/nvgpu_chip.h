/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Chip capability and PCI ID table for supported NVIDIA GPUs.
 */

#ifndef _NVGPU_CHIP_H_
#define _NVGPU_CHIP_H_

#include <sys/param.h>
#include <sys/stdint.h>

#define NVGPU_PCI_VENDOR_NVIDIA	0x10de
#define NVGPU_NUM_BARS		6
#define NVGPU_CARD_TU100	0x160u

#define NVGPU_TU10X_SEC2_BASE	0x00840000u
#define NVGPU_TU10X_SEC2_FBIF	0x00840600u
#define NVGPU_TU10X_GSP_BASE	0x00110000u
#define NVGPU_TU10X_GSP_FBIF	0x00110600u
#define NVGPU_TU10X_GSP_RISCV	0x00111000u

#define NVGPU_GMMU_PD3_SHIFT	47
#define NVGPU_GMMU_PD2_SHIFT	38
#define NVGPU_GMMU_PD1_SHIFT	29
#define NVGPU_GMMU_PD0_SHIFT	21
#define NVGPU_GMMU_LPT_SHIFT	16
#define NVGPU_GMMU_SPT_SHIFT	12

enum nvgpu_firmware_image {
	NVGPU_FIRMWARE_BOOTER_LOAD,
	NVGPU_FIRMWARE_BOOTER_UNLOAD,
	NVGPU_FIRMWARE_ACR_BL,
	NVGPU_FIRMWARE_GSP,
	NVGPU_FIRMWARE_BOOTLOADER,
};

struct nvgpu_chip_config {
	const char *chip;
	const char *device_name;
	const char *gsp_firmware_chip;
	const char *fallback_name;
	uint32_t chipset;
	uint32_t card_type;
	uint32_t graph_units;
	const char *fw_signature;
	uint32_t sec2_base;
	uint32_t sec2_fbif;
	uint32_t gsp_base;
	uint32_t gsp_fbif;
	uint32_t gsp_riscv;
	uint32_t display_heads;
	uint32_t display_sors;
	uint32_t display_windows;
	uint32_t display_cursors;
	uint32_t class_display_root;
	uint32_t class_display_core;
	uint32_t class_display_window;
	uint32_t class_display_window_imm;
	uint32_t class_display_cursor;
	uint32_t gmmu_pd3_shift;
	uint32_t gmmu_pd2_shift;
	uint32_t gmmu_pd1_shift;
	uint32_t gmmu_pd0_shift;
	uint32_t gmmu_big_shift;
	uint32_t gmmu_small_shift;
	uint32_t class_3d;
	uint32_t class_compute;
	uint32_t class_copy;
	uint32_t class_twod;
	uint32_t class_m2mf;
};

struct nvgpu_pci_device {
	uint16_t device;
	const char *name;
	const struct nvgpu_chip_config *chip;
};

/* Return borrowed static chip metadata for device, or NULL.  Lock-free. */
const struct nvgpu_pci_device *nvgpu_chip_lookup_pci(uint16_t device);

/*
 * Format the fixed GSP firmware path for a supported chip.
 *
 * The caller owns `name` and retains it until firmware_get() consumes it.
 * This function is lock-free and does not retain `chip`.
 */
int nvgpu_chip_format_firmware_name(const struct nvgpu_chip_config *chip,
    enum nvgpu_firmware_image image, char *name, size_t name_size);

#endif /* _NVGPU_CHIP_H_ */
