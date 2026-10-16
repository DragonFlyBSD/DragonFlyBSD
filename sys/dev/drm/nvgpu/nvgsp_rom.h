/*-
 * SPDX-License-Identifier: MIT
 *
 * PCI Option ROM image-header / PCIR data structure / NVIDIA private NPDE
 * extension definitions.
 *
 * Constant values are taken verbatim from NVIDIA open-gpu-kernel-modules
 * 570.144,
 *   src/nvidia/inc/kernel/platform/pci_exp_table.h
 * Copyright (c) 1993-2021 NVIDIA CORPORATION & AFFILIATES, MIT license.
 *
 * Reformatted with DFly-style naming. The semantics are NVIDIA's; bug
 * reports about those should go to upstream open-rm. Bug reports about
 * the layout/naming should go to us.
 */

#ifndef _NVGSP_ROM_H_
#define _NVGSP_ROM_H_

/*
 * PCI Option ROM image header. The first 16-bit LE word is a signature.
 * NVIDIA's reader accepts three: the standard PCI 0xAA55 and two NVIDIA
 * private values used to chain firmware images that aren't proper PCI
 * Option ROMs.
 */
#define NVGSP_ROM_SIG_STD		0xaa55
#define NVGSP_ROM_SIG_NV			0x4e56	/* "VN" — NVIDIA private */
#define NVGSP_ROM_SIG_NV2		0xbb77	/* another NVIDIA private */
#define NVGSP_ROM_HDR_OFF_PCIR_PTR	0x18	/* 16-bit LE: offset to PCIR */

/* Block size used in PCIR.image_len and NPDE.subimage_len */
#define NVGSP_ROM_BLOCK_SIZE		512u

/*
 * PCIR ("PCI Data Structure"). Three signatures accepted, mirroring the
 * three ROM-header signatures above.
 */
#define NVGSP_PCIR_SIG_PCIR		0x52494350u	/* "PCIR" */
#define NVGSP_PCIR_SIG_NPDS		0x5344504Eu	/* "NPDS" — NV private */
#define NVGSP_PCIR_SIG_RGIS		0x53494752u	/* "RGIS" — NV private */
#define NVGSP_PCIR_OFF_SIG		0x00
#define NVGSP_PCIR_OFF_VENDOR_ID		0x04
#define NVGSP_PCIR_OFF_DEVICE_ID		0x06
#define NVGSP_PCIR_OFF_STRUCT_LEN	0x0a	/* 16-bit, length of PCIR itself */
#define NVGSP_PCIR_OFF_CLASS_CODE	0x0d	/* 3 bytes */
#define NVGSP_PCIR_OFF_IMAGE_LEN		0x10	/* 16-bit, image len / 512 */
#define NVGSP_PCIR_OFF_CODE_TYPE		0x14
#define NVGSP_PCIR_OFF_INDICATOR		0x15
#define NVGSP_PCIR_LAST_IMAGE_BIT	0x80	/* bit 7 of indicator byte */

/*
 * NPDE ("NV PCI Data Extension"), an NVIDIA private structure placed
 * 16-byte aligned after PCIR. When present, NPDE.subimage_len overrides
 * PCIR.image_len, and (if the NPDE is long enough to include it)
 * NPDE.last_image overrides PCIR.indicator.
 */
#define NVGSP_NPDE_SIG			0x4544504Eu	/* "NPDE" LE */
#define NVGSP_NPDE_REV_10		0x0100
#define NVGSP_NPDE_REV_11		0x0101
#define NVGSP_NPDE_OFF_SIG		0x00
#define NVGSP_NPDE_OFF_REV		0x04
#define NVGSP_NPDE_OFF_LEN		0x06
#define NVGSP_NPDE_OFF_SUBIMAGE_LEN	0x08
#define NVGSP_NPDE_OFF_LAST_IMAGE	0x0a
#define NVGSP_NPDE_OFF_FLAGS		0x0b

#endif /* _NVGSP_ROM_H_ */
