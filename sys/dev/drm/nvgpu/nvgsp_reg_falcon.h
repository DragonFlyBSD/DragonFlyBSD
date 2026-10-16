/*-
 * SPDX-License-Identifier: MIT
 *
 * Falcon engine register definitions (Turing v4 and onward).
 *
 * Offsets and field positions are taken verbatim from NVIDIA
 * open-gpu-kernel-modules 570.144,
 *   src/common/inc/swref/published/turing/tu102/dev_falcon_v4.h
 * Copyright (c) 2003-2022 NVIDIA CORPORATION & AFFILIATES, MIT licensed.
 *
 * Naming converted from NV_PFALCON_FALCON_* to NVGSP_FLCN_* for brevity;
 * bit-field expressions converted from NVIDIA's "high:low" notation to
 * explicit shift/mask constants. Semantics unchanged.
 *
 * All offsets are relative to the engine's Falcon register base. Each
 * Falcon-class engine (GSP, SEC2, NVDEC, ...) sits at a different absolute
 * BAR0 offset; users of these defines must add the engine base.
 */

#ifndef _NVGSP_REG_FALCON_H_
#define _NVGSP_REG_FALCON_H_

/* IRQ control */
#define NVGSP_FLCN_IRQSCLR			0x0004
#define   NVGSP_FLCN_IRQSCLR_HALT		(1u << 4)
#define   NVGSP_FLCN_IRQSCLR_SWGEN0		(1u << 6)
#define NVGSP_FLCN_IRQSTAT			0x0008
#define   NVGSP_FLCN_IRQSTAT_HALT		(1u << 4)
#define   NVGSP_FLCN_IRQSTAT_SWGEN0		(1u << 6)
#define NVGSP_FLCN_IRQMSET			0x0010
#define NVGSP_FLCN_IRQMCLR			0x0014
#define NVGSP_FLCN_IRQMASK			0x0018
#define NVGSP_FLCN_IRQDEST			0x001c

/* Scratch / mailbox */
#define NVGSP_FLCN_MAILBOX0			0x0040
#define NVGSP_FLCN_MAILBOX1			0x0044
#define NVGSP_FLCN_OS				0x0080
#define NVGSP_FLCN_RM				0x0084
#define NVGSP_FLCN_DEBUGINFO			0x0094

/* HWCFG: chip config (read-only) */
#define NVGSP_FLCN_HWCFG				0x0108
#define   NVGSP_FLCN_HWCFG_IMEM_SIZE_MASK	0x000001ffu
#define NVGSP_FLCN_HWCFG2			0x00f4
#define   NVGSP_FLCN_HWCFG2_RISCV		(1u << 10)

/* CPU control */
#define NVGSP_FLCN_CPUCTL			0x0100
#define   NVGSP_FLCN_CPUCTL_STARTCPU		(1u << 1)
#define   NVGSP_FLCN_CPUCTL_HALTED		(1u << 4)
#define   NVGSP_FLCN_CPUCTL_ALIAS_EN		(1u << 6)
#define NVGSP_FLCN_BOOTVEC			0x0104
#define NVGSP_FLCN_CPUCTL_ALIAS			0x0130
#define   NVGSP_FLCN_CPUCTL_ALIAS_STARTCPU	(1u << 1)

/* DMA control */
#define NVGSP_FLCN_DMACTL			0x010c
#define   NVGSP_FLCN_DMACTL_REQUIRE_CTX		(1u << 0)
#define   NVGSP_FLCN_DMACTL_DMEM_SCRUBBING	(1u << 1)
#define   NVGSP_FLCN_DMACTL_IMEM_SCRUBBING	(1u << 2)
#define   NVGSP_FLCN_DMACTL_SCRUBBING_MASK	(NVGSP_FLCN_DMACTL_DMEM_SCRUBBING | \
						 NVGSP_FLCN_DMACTL_IMEM_SCRUBBING)

/*
 * IMEM (instruction store) PIO access.
 * Multiple "ports" allow concurrent loads. Address layout in IMEMC:
 *   bits 7:2  offset within block (4-byte granularity)
 *   bits 15:8 block index (block size = 256 bytes)
 *   bit  24   AINCW: post-increment offset after every data write
 *   bit  28   SECURE: load into secure region (HS code)
 */
#define NVGSP_FLCN_IMEMC(i)			(0x0180u + (i) * 0x10u)
#define   NVGSP_FLCN_IMEMC_OFFS_SHIFT		2
#define   NVGSP_FLCN_IMEMC_OFFS_MASK		(0x3fu << 2)
#define   NVGSP_FLCN_IMEMC_BLK_SHIFT		8
#define   NVGSP_FLCN_IMEMC_BLK_MASK		(0xffu << 8)
#define   NVGSP_FLCN_IMEMC_AINCW			(1u << 24)
#define   NVGSP_FLCN_IMEMC_SECURE		(1u << 28)
#define NVGSP_FLCN_IMEMD(i)			(0x0184u + (i) * 0x10u)
#define NVGSP_FLCN_IMEMT(i)			(0x0188u + (i) * 0x10u)

/*
 * DMEM (data store) PIO access. Same OFFS/BLK layout as IMEM; supports
 * post-increment on read (AINCR) as well as write (AINCW).
 */
#define NVGSP_FLCN_DMEMC(i)			(0x01c0u + (i) * 0x08u)
#define   NVGSP_FLCN_DMEMC_OFFS_SHIFT		2
#define   NVGSP_FLCN_DMEMC_OFFS_MASK		(0x3fu << 2)
#define   NVGSP_FLCN_DMEMC_BLK_SHIFT		8
#define   NVGSP_FLCN_DMEMC_BLK_MASK		(0xffu << 8)
#define   NVGSP_FLCN_DMEMC_AINCW			(1u << 24)
#define   NVGSP_FLCN_DMEMC_AINCR			(1u << 25)
#define NVGSP_FLCN_DMEMD(i)			(0x01c4u + (i) * 0x08u)

/*
 * FBIF (Framebuffer Interface) registers live at a different base offset
 * from the main Falcon block; per-engine fbif_base is provided by the
 * caller.
 */
#define NVGSP_FBIF_CTL				0x0024
#define   NVGSP_FBIF_CTL_ALLOW_PHYS_NO_CTX	(1u << 7)

/* Falcon memory layout constants (from open-rm falcon_common.h) */
#define NVGSP_FLCN_IMEM_BLKSIZE			256u
#define NVGSP_FLCN_DMEM_BLKSIZE			256u
#define NVGSP_FLCN_ACCESS_ALIGN			4u

#endif /* _NVGSP_REG_FALCON_H_ */
