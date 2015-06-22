/*
 * Copyright (c) 2000 Doug Rabson
 * Copyright (c) 2000 Ruslan Ermilov
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Fixes for 830/845G support: David Dawes <dawes@xfree86.org>
 * 852GM/855GM/865G support added by David Dawes <dawes@xfree86.org>
 *
 * This is generic Intel GTT handling code, morphed from the AGP
 * bridge code.
 */

#if 0
#define	KTR_AGP_I810	KTR_DEV
#else
#define	KTR_AGP_I810	0
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/rman.h>

#include "pcidevs.h"
#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include "agppriv.h"
#include "agpreg.h"
#include <drm/intel-gtt.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/pmap.h>

#include <machine/md_var.h>

#define bus_read_1(r, o) \
		   bus_space_read_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_4(r, o) \
		   bus_space_read_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_write_4(r, o, v) \
		    bus_space_write_4((r)->r_bustag, (r)->r_bushandle, (o), (v))

MALLOC_DECLARE(M_AGP);

struct agp_i810_match;

static int agp_i915_check_active(device_t bridge_dev);
static int agp_sb_check_active(device_t bridge_dev);

static void agp_i810_set_desc(device_t dev, const struct agp_i810_match *match);

static void agp_i915_dump_regs(device_t dev);
static void agp_i965_dump_regs(device_t dev);
static void agp_sb_dump_regs(device_t dev);

static int agp_i915_get_stolen_size(device_t dev);
static int agp_sb_get_stolen_size(device_t dev);
static int agp_gen8_get_stolen_size(device_t dev);

static int agp_i915_get_gtt_mappable_entries(device_t dev);

static int agp_i810_get_gtt_total_entries(device_t dev);
static int agp_i965_get_gtt_total_entries(device_t dev);
static int agp_gen5_get_gtt_total_entries(device_t dev);
static int agp_sb_get_gtt_total_entries(device_t dev);
static int agp_gen8_get_gtt_total_entries(device_t dev);

static int agp_i830_install_gatt(device_t dev);

static void agp_i830_deinstall_gatt(device_t dev);

static void agp_i915_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_i965_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_g4x_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_sb_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_gen8_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);

static void agp_i915_write_gtt(device_t dev, u_int index, uint32_t pte);
static void agp_i965_write_gtt(device_t dev, u_int index, uint32_t pte);
static void agp_g4x_write_gtt(device_t dev, u_int index, uint32_t pte);
static void agp_sb_write_gtt(device_t dev, u_int index, uint32_t pte);

static void agp_i915_sync_gtt_pte(device_t dev, u_int index);
static void agp_i965_sync_gtt_pte(device_t dev, u_int index);
static void agp_g4x_sync_gtt_pte(device_t dev, u_int index);

static int agp_i915_set_aperture(device_t dev, u_int32_t aperture);

static int agp_i810_chipset_flush_setup(device_t dev);
static int agp_i915_chipset_flush_setup(device_t dev);
static int agp_i965_chipset_flush_setup(device_t dev);

static void agp_i810_chipset_flush_teardown(device_t dev);
static void agp_i915_chipset_flush_teardown(device_t dev);
static void agp_i965_chipset_flush_teardown(device_t dev);

static void agp_i810_chipset_flush(device_t dev);
static void agp_i915_chipset_flush(device_t dev);

enum {
	CHIP_I810,	/* i810/i815 */
	CHIP_I830,	/* 830M/845G */
	CHIP_I855,	/* 852GM/855GM/865G */
	CHIP_I915,	/* 915G/915GM */
	CHIP_I965,	/* G965 */
	CHIP_G33,	/* G33/Q33/Q35 */
	CHIP_IGD,	/* Pineview */
	CHIP_G4X,	/* G45/Q45 */
	CHIP_SB,	/* SandyBridge */
};

/* The i810 through i855 have the registers at BAR 1, and the GATT gets
 * allocated by us.  The i915 has registers in BAR 0 and the GATT is at the
 * start of the stolen memory, and should only be accessed by the OS through
 * BAR 3.  The G965 has registers and GATT in the same BAR (0) -- first 512KB
 * is registers, second 512KB is GATT.
 */
static struct resource_spec agp_i915_res_spec[] = {
	{ SYS_RES_MEMORY, AGP_I915_MMADR, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_MEMORY, AGP_I915_GTTADR, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct resource_spec agp_i965_res_spec[] = {
	{ SYS_RES_MEMORY, AGP_I965_GTTMMADR, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct resource_spec agp_g4x_res_spec[] = {
	{ SYS_RES_MEMORY, AGP_G4X_MMADR, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_MEMORY, AGP_G4X_GTTADR, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

struct agp_i810_softc {
	struct agp_softc agp;
	u_int32_t initial_aperture;	/* aperture size at startup */
	struct agp_gatt *gatt;
	u_int32_t dcache_size;		/* i810 only */
	u_int32_t stolen;		/* number of i830/845 gtt
					   entries for stolen memory */
	u_int stolen_size;		/* BIOS-reserved graphics memory */
	u_int gtt_total_entries;	/* Total number of gtt ptes */
	u_int gtt_mappable_entries;	/* Number of gtt ptes mappable by CPU */
	device_t bdev;			/* bridge device */
	void *argb_cursor;		/* contigmalloc area for ARGB cursor */
	struct resource *sc_res[2];
	const struct agp_i810_match *match;
	int sc_flush_page_rid;
	struct resource *sc_flush_page_res;
	void *sc_flush_page_vaddr;
	int sc_bios_allocated_flush_page;
};

static device_t intel_agp;

struct agp_i810_driver {
	int chiptype;
	int gen;
	int busdma_addr_mask_sz;
	struct resource_spec *res_spec;
	int (*check_active)(device_t);
	void (*set_desc)(device_t, const struct agp_i810_match *);
	void (*dump_regs)(device_t);
	int (*get_stolen_size)(device_t);
	int (*get_gtt_total_entries)(device_t);
	int (*get_gtt_mappable_entries)(device_t);
	int (*install_gatt)(device_t);
	void (*deinstall_gatt)(device_t);
	void (*write_gtt)(device_t, u_int, uint32_t);
	void (*install_gtt_pte)(device_t, u_int, vm_offset_t, int);
	void (*sync_gtt_pte)(device_t, u_int);
	int (*set_aperture)(device_t, u_int32_t);
	int (*chipset_flush_setup)(device_t);
	void (*chipset_flush_teardown)(device_t);
	void (*chipset_flush)(device_t);
};

static struct {
	struct intel_gtt base;
} intel_private;

static const struct agp_i810_driver agp_i810_i915_driver = {
	.chiptype = CHIP_I915,
	.gen = 3,
	.busdma_addr_mask_sz = 32,
	.res_spec = agp_i915_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i915_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i810_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i915_write_gtt,
	.install_gtt_pte = agp_i915_install_gtt_pte,
	.sync_gtt_pte = agp_i915_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i915_chipset_flush_setup,
	.chipset_flush_teardown = agp_i915_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_g965_driver = {
	.chiptype = CHIP_I965,
	.gen = 4,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i965_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i965_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i965_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i965_write_gtt,
	.install_gtt_pte = agp_i965_install_gtt_pte,
	.sync_gtt_pte = agp_i965_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_g33_driver = {
	.chiptype = CHIP_G33,
	.gen = 3,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i915_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i965_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i965_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i915_write_gtt,
	.install_gtt_pte = agp_i965_install_gtt_pte,
	.sync_gtt_pte = agp_i915_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver pineview_gtt_driver = {
	.chiptype = CHIP_IGD,
	.gen = 3,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i915_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i915_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i965_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i915_write_gtt,
	.install_gtt_pte = agp_i965_install_gtt_pte,
	.sync_gtt_pte = agp_i915_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_g4x_driver = {
	.chiptype = CHIP_G4X,
	.gen = 5,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i965_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i965_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_gen5_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_g4x_write_gtt,
	.install_gtt_pte = agp_g4x_install_gtt_pte,
	.sync_gtt_pte = agp_g4x_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_sb_driver = {
	.chiptype = CHIP_SB,
	.gen = 6,
	.busdma_addr_mask_sz = 40,
	.res_spec = agp_g4x_res_spec,
	.check_active = agp_sb_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_sb_dump_regs,
	.get_stolen_size = agp_sb_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_sb_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_sb_write_gtt,
	.install_gtt_pte = agp_sb_install_gtt_pte,
	.sync_gtt_pte = agp_g4x_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i810_chipset_flush,
};

static const struct agp_i810_driver valleyview_gtt_driver = {
	.chiptype = CHIP_SB,
	.gen = 7,
	.busdma_addr_mask_sz = 40,
	.res_spec = agp_g4x_res_spec,
	.check_active = agp_sb_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_sb_dump_regs,
	.get_stolen_size = agp_sb_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_sb_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_sb_write_gtt,
	.install_gtt_pte = agp_sb_install_gtt_pte,
	.sync_gtt_pte = agp_g4x_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i810_chipset_flush,
};

static const struct agp_i810_driver broadwell_gtt_driver = {
	.chiptype = CHIP_SB,
	.gen = 8,
	.busdma_addr_mask_sz = 40,
	.res_spec = agp_g4x_res_spec,
	.check_active = agp_sb_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_sb_dump_regs,
	.get_stolen_size = agp_gen8_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_gen8_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = NULL,
	.install_gtt_pte = agp_gen8_install_gtt_pte,
	.sync_gtt_pte = agp_g4x_sync_gtt_pte,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i810_chipset_flush,
};

/* For adding new devices, devid is the id of the graphics controller
 * (pci:0:2:0, for example).  The placeholder (usually at pci:0:2:1) for the
 * second head should never be added.  The bridge_offset is the offset to
 * subtract from devid to get the id of the hostb that the device is on.
 */
static const struct agp_i810_match {
	uint16_t devid;
	char *name;
	const struct agp_i810_driver *driver;
} agp_i810_matches[] = {
	{
		.devid = 0x2582,
		.name = "Intel 82915G (915G GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x258A,
		.name = "Intel E7221 SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x2592,
		.name = "Intel 82915GM (915GM GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x2772,
		.name = "Intel 82945G (945G GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x27A2,
		.name = "Intel 82945GM (945GM GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x27AE,
		.name = "Intel 945GME SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x2972,
		.name = "Intel 946GZ SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x2982,
		.name = "Intel G965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x2992,
		.name = "Intel Q965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x29A2,
		.name = "Intel G965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x29B2,
		.name = "Intel Q35 SVGA controller",
		.driver = &agp_i810_g33_driver
	},
	{
		.devid = 0x29C2,
		.name = "Intel G33 SVGA controller",
		.driver = &agp_i810_g33_driver
	},
	{
		.devid = 0x29D2,
		.name = "Intel Q33 SVGA controller",
		.driver = &agp_i810_g33_driver
	},
	{
		.devid = 0xA001,
		.name = "Intel Pineview SVGA controller",
		.driver = &pineview_gtt_driver
	},
	{
		.devid = 0xA011,
		.name = "Intel Pineview (M) SVGA controller",
		.driver = &pineview_gtt_driver
	},
	{
		.devid = 0x2A02,
		.name = "Intel GM965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x2A12,
		.name = "Intel GME965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x2A42,
		.name = "Intel GM45 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E02,
		.name = "Intel Eaglelake SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E12,
		.name = "Intel Q45 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E22,
		.name = "Intel G45 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E32,
		.name = "Intel G41 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x0042,
		.name = "Intel Ironlake (D) SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x0046,
		.name = "Intel Ironlake (M) SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x0102,
		.name = "SandyBridge desktop GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0112,
		.name = "SandyBridge desktop GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0122,
		.name = "SandyBridge desktop GT2+ IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0106,
		.name = "SandyBridge mobile GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0116,
		.name = "SandyBridge mobile GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0126,
		.name = "SandyBridge mobile GT2+ IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x010a,
		.name = "SandyBridge server IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0152,
		.name = "IvyBridge desktop GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0162,
		.name = "IvyBridge desktop GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0156,
		.name = "IvyBridge mobile GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0166,
		.name = "IvyBridge mobile GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x015a,
		.name = "IvyBridge server GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x016a,
		.name = "IvyBridge server GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0f30,
		.name = "ValleyView",
		.driver = &valleyview_gtt_driver
	},
	{
		.devid = 0x0402,
		.name = "Haswell desktop GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0412,
		.name = "Haswell desktop GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{	0x041e, "Haswell", &agp_i810_sb_driver },
	{	0x0422, "Haswell", &agp_i810_sb_driver },
	{
		.devid = 0x0406,
		.name = "Haswell mobile GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x0416,
		.name = "Haswell mobile GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{	0x0426, "Haswell", &agp_i810_sb_driver },
	{
		.devid = 0x040a,
		.name = "Haswell server GT1 IG",
		.driver = &agp_i810_sb_driver
	},
	{
		.devid = 0x041a,
		.name = "Haswell server GT2 IG",
		.driver = &agp_i810_sb_driver
	},
	{	0x042a, "Haswell", &agp_i810_sb_driver },
	{	0x0c02, "Haswell", &agp_i810_sb_driver },
	{	0x0c12, "Haswell", &agp_i810_sb_driver },
	{	0x0c22, "Haswell", &agp_i810_sb_driver },
	{	0x0c06, "Haswell", &agp_i810_sb_driver },
	{
		.devid = 0x0c16,
		.name = "Haswell SDV",
		.driver = &agp_i810_sb_driver
	},
	{	0x0c26, "Haswell", &agp_i810_sb_driver },
	{	0x0c0a, "Haswell", &agp_i810_sb_driver },
	{	0x0c1a, "Haswell", &agp_i810_sb_driver },
	{	0x0c2a, "Haswell", &agp_i810_sb_driver },
	{	0x0a02, "Haswell", &agp_i810_sb_driver },
	{	0x0a12, "Haswell", &agp_i810_sb_driver },
	{	0x0a22, "Haswell", &agp_i810_sb_driver },
	{	0x0a06, "Haswell", &agp_i810_sb_driver },
	{	0x0a16, "Haswell", &agp_i810_sb_driver },
	{	0x0a26, "Haswell", &agp_i810_sb_driver },
	{	0x0a0a, "Haswell", &agp_i810_sb_driver },
	{	0x0a1a, "Haswell", &agp_i810_sb_driver },
	{	0x0a2a, "Haswell", &agp_i810_sb_driver },
	{	0x0d12, "Haswell", &agp_i810_sb_driver },
	{	0x0d22, "Haswell", &agp_i810_sb_driver },
	{	0x0d32, "Haswell", &agp_i810_sb_driver },
	{	0x0d16, "Haswell", &agp_i810_sb_driver },
	{	0x0d26, "Haswell", &agp_i810_sb_driver },
	{	0x0d36, "Haswell", &agp_i810_sb_driver },
	{	0x0d1a, "Haswell", &agp_i810_sb_driver },
	{	0x0d2a, "Haswell", &agp_i810_sb_driver },
	{	0x0d3a, "Haswell", &agp_i810_sb_driver },

	{	0x1602, "Broadwell", &broadwell_gtt_driver }, /* m */
	{	0x1606, "Broadwell", &broadwell_gtt_driver },
	{	0x160B, "Broadwell", &broadwell_gtt_driver },
	{	0x160E, "Broadwell", &broadwell_gtt_driver },
	{	0x1616, "Broadwell", &broadwell_gtt_driver },
	{	0x161E, "Broadwell", &broadwell_gtt_driver },

	{	0x160A, "Broadwell", &broadwell_gtt_driver }, /* d */
	{	0x160D, "Broadwell", &broadwell_gtt_driver },
	{
		.devid = 0,
	}
};

static const struct agp_i810_match*
agp_i810_match(device_t dev)
{
	int i, devid;

	if (pci_get_vendor(dev) != PCI_VENDOR_INTEL)
		return (NULL);

	devid = pci_get_device(dev);
	for (i = 0; agp_i810_matches[i].devid != 0; i++) {
		if (agp_i810_matches[i].devid == devid)
			break;
	}
	if (agp_i810_matches[i].devid == 0)
		return (NULL);
	else
		return (&agp_i810_matches[i]);
}

/*
 * Find bridge device.
 */
static device_t
agp_i810_find_bridge(device_t dev)
{

	return (pci_find_dbsf(0, 0, 0, 0));
}

static void
agp_i810_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "agp", -1) == NULL &&
	    agp_i810_match(parent))
		device_add_child(parent, "agp", -1);
}

static int
agp_i915_check_active(device_t bridge_dev)
{
	int deven;

	deven = pci_read_config(bridge_dev, AGP_I915_DEVEN, 4);
	if ((deven & AGP_I915_DEVEN_D2F0) == AGP_I915_DEVEN_D2F0_DISABLED)
		return (ENXIO);
	return (0);
}

static int
agp_sb_check_active(device_t bridge_dev)
{
	int deven;

	deven = pci_read_config(bridge_dev, AGP_I915_DEVEN, 4);
	if ((deven & AGP_SB_DEVEN_D2EN) == AGP_SB_DEVEN_D2EN_DISABLED)
		return (ENXIO);
	return (0);
}

static void
agp_i810_set_desc(device_t dev, const struct agp_i810_match *match)
{

	device_set_desc(dev, match->name);
}

static int
agp_i810_probe(device_t dev)
{
	device_t bdev;
	const struct agp_i810_match *match;
	int err;

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);
	match = agp_i810_match(dev);
	if (match == NULL)
		return (ENXIO);

	bdev = agp_i810_find_bridge(dev);
	if (bdev == NULL) {
		if (bootverbose)
			kprintf("I810: can't find bridge device\n");
		return (ENXIO);
	}

	/*
	 * checking whether internal graphics device has been activated.
	 */
	err = match->driver->check_active(bdev);
	if (err != 0) {
		if (bootverbose)
			kprintf("i810: disabled, not probing\n");
		return (err);
	}

	match->driver->set_desc(dev, match);
	return (BUS_PROBE_DEFAULT);
}

static void
agp_i915_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_I810_PGTBL_CTL: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL));
	device_printf(dev, "AGP_I855_GCC1: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I855_GCC1, 1));
	device_printf(dev, "AGP_I915_MSAC: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I915_MSAC, 1));
}

static void
agp_i965_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_I965_PGTBL_CTL2: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_I965_PGTBL_CTL2));
	device_printf(dev, "AGP_I855_GCC1: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I855_GCC1, 1));
	device_printf(dev, "AGP_I965_MSAC: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I965_MSAC, 1));
}

static void
agp_sb_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_SNB_GFX_MODE: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_SNB_GFX_MODE));
	device_printf(dev, "AGP_SNB_GCC1: 0x%04x\n",
	    pci_read_config(sc->bdev, AGP_SNB_GCC1, 2));
}

static int
agp_i915_get_stolen_size(device_t dev)
{
	struct agp_i810_softc *sc;
	unsigned int gcc1, stolen, gtt_size;

	sc = device_get_softc(dev);

	/*
	 * Stolen memory is set up at the beginning of the aperture by
	 * the BIOS, consisting of the GATT followed by 4kb for the
	 * BIOS display.
	 */
	switch (sc->match->driver->chiptype) {
	case CHIP_I855:
		gtt_size = 128;
		break;
	case CHIP_I915:
		gtt_size = 256;
		break;
	case CHIP_I965:
		switch (bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL) &
			AGP_I810_PGTBL_SIZE_MASK) {
		case AGP_I810_PGTBL_SIZE_128KB:
			gtt_size = 128;
			break;
		case AGP_I810_PGTBL_SIZE_256KB:
			gtt_size = 256;
			break;
		case AGP_I810_PGTBL_SIZE_512KB:
			gtt_size = 512;
			break;
		case AGP_I965_PGTBL_SIZE_1MB:
			gtt_size = 1024;
			break;
		case AGP_I965_PGTBL_SIZE_2MB:
			gtt_size = 2048;
			break;
		case AGP_I965_PGTBL_SIZE_1_5MB:
			gtt_size = 1024 + 512;
			break;
		default:
			device_printf(dev, "Bad PGTBL size\n");
			return (EINVAL);
		}
		break;
	case CHIP_G33:
		gcc1 = pci_read_config(sc->bdev, AGP_I855_GCC1, 2);
		switch (gcc1 & AGP_G33_MGGC_GGMS_MASK) {
		case AGP_G33_MGGC_GGMS_SIZE_1M:
			gtt_size = 1024;
			break;
		case AGP_G33_MGGC_GGMS_SIZE_2M:
			gtt_size = 2048;
			break;
		default:
			device_printf(dev, "Bad PGTBL size\n");
			return (EINVAL);
		}
		break;
	case CHIP_IGD:
	case CHIP_G4X:
		gtt_size = 0;
		break;
	default:
		device_printf(dev, "Bad chiptype\n");
		return (EINVAL);
	}

	/* GCC1 is called MGGC on i915+ */
	gcc1 = pci_read_config(sc->bdev, AGP_I855_GCC1, 1);
	switch (gcc1 & AGP_I855_GCC1_GMS) {
	case AGP_I855_GCC1_GMS_STOLEN_1M:
		stolen = 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_4M:
		stolen = 4 * 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_8M:
		stolen = 8 * 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_16M:
		stolen = 16 * 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_32M:
		stolen = 32 * 1024;
		break;
	case AGP_I915_GCC1_GMS_STOLEN_48M:
		stolen = sc->match->driver->gen > 2 ? 48 * 1024 : 0;
		break;
	case AGP_I915_GCC1_GMS_STOLEN_64M:
		stolen = sc->match->driver->gen > 2 ? 64 * 1024 : 0;
		break;
	case AGP_G33_GCC1_GMS_STOLEN_128M:
		stolen = sc->match->driver->gen > 2 ? 128 * 1024 : 0;
		break;
	case AGP_G33_GCC1_GMS_STOLEN_256M:
		stolen = sc->match->driver->gen > 2 ? 256 * 1024 : 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_96M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 96 * 1024;
		else
			stolen = 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_160M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 160 * 1024;
		else
			stolen = 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_224M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 224 * 1024;
		else
			stolen = 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_352M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 352 * 1024;
		else
			stolen = 0;
		break;
	default:
		device_printf(dev,
		    "unknown memory configuration, disabling (GCC1 %x)\n",
		    gcc1);
		return (EINVAL);
	}

	gtt_size += 4;
	sc->stolen_size = stolen * 1024;
	sc->stolen = (stolen - gtt_size) * 1024 / 4096;

	return (0);
}

static int
agp_sb_get_stolen_size(device_t dev)
{
	struct agp_i810_softc *sc;
	uint16_t gmch_ctl;

	sc = device_get_softc(dev);
	gmch_ctl = pci_read_config(sc->bdev, AGP_SNB_GCC1, 2);
	switch (gmch_ctl & AGP_SNB_GMCH_GMS_STOLEN_MASK) {
	case AGP_SNB_GMCH_GMS_STOLEN_32M:
		sc->stolen_size = 32 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_64M:
		sc->stolen_size = 64 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_96M:
		sc->stolen_size = 96 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_128M:
		sc->stolen_size = 128 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_160M:
		sc->stolen_size = 160 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_192M:
		sc->stolen_size = 192 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_224M:
		sc->stolen_size = 224 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_256M:
		sc->stolen_size = 256 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_288M:
		sc->stolen_size = 288 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_320M:
		sc->stolen_size = 320 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_352M:
		sc->stolen_size = 352 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_384M:
		sc->stolen_size = 384 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_416M:
		sc->stolen_size = 416 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_448M:
		sc->stolen_size = 448 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_480M:
		sc->stolen_size = 480 * 1024 * 1024;
		break;
	case AGP_SNB_GMCH_GMS_STOLEN_512M:
		sc->stolen_size = 512 * 1024 * 1024;
		break;
	}
	sc->stolen = (sc->stolen_size - 4) / 4096;
	return (0);
}

static int
agp_gen8_get_stolen_size(device_t dev)
{
	struct agp_i810_softc *sc;
	uint16_t gcc1;
	int v;

	sc = device_get_softc(dev);
	gcc1 = pci_read_config(sc->bdev, AGP_SNB_GCC1, 2);
	v = (gcc1 >> 8) & 0xFF;
	sc->stolen_size = v * (32L * 1024 * 1024);	/* 32MB increments */
	kprintf("GTT STOLEN %ld\n", (long)sc->stolen_size);

	return 0;
}

static int
agp_i915_get_gtt_mappable_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t ap;

	sc = device_get_softc(dev);
	ap = AGP_GET_APERTURE(dev);
	sc->gtt_mappable_entries = ap >> AGP_PAGE_SHIFT;
	return (0);
}

static int
agp_i810_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	sc->gtt_total_entries = sc->gtt_mappable_entries;
	return (0);
}

static int
agp_i965_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t pgetbl_ctl;
	int error;

	sc = device_get_softc(dev);
	error = 0;
	pgetbl_ctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	switch (pgetbl_ctl & AGP_I810_PGTBL_SIZE_MASK) {
	case AGP_I810_PGTBL_SIZE_128KB:
		sc->gtt_total_entries = 128 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_256KB:
		sc->gtt_total_entries = 256 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_512KB:
		sc->gtt_total_entries = 512 * 1024 / 4;
		break;
	/* GTT pagetable sizes bigger than 512KB are not possible on G33! */
	case AGP_I810_PGTBL_SIZE_1MB:
		sc->gtt_total_entries = 1024 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_2MB:
		sc->gtt_total_entries = 2 * 1024 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_1_5MB:
		sc->gtt_total_entries = (1024 + 512) * 1024 / 4;
		break;
	default:
		device_printf(dev, "Unknown page table size\n");
		error = ENXIO;
	}
	return (error);
}

static void
agp_gen5_adjust_pgtbl_size(device_t dev, uint32_t sz)
{
	struct agp_i810_softc *sc;
	uint32_t pgetbl_ctl, pgetbl_ctl2;

	sc = device_get_softc(dev);

	/* Disable per-process page table. */
	pgetbl_ctl2 = bus_read_4(sc->sc_res[0], AGP_I965_PGTBL_CTL2);
	pgetbl_ctl2 &= ~AGP_I810_PGTBL_ENABLED;
	bus_write_4(sc->sc_res[0], AGP_I965_PGTBL_CTL2, pgetbl_ctl2);

	/* Write the new ggtt size. */
	pgetbl_ctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	pgetbl_ctl &= ~AGP_I810_PGTBL_SIZE_MASK;
	pgetbl_ctl |= sz;
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL, pgetbl_ctl);
}

static int
agp_gen5_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint16_t gcc1;

	sc = device_get_softc(dev);

	gcc1 = pci_read_config(sc->bdev, AGP_I830_GCC1, 2);
	switch (gcc1 & AGP_G4x_GCC1_SIZE_MASK) {
	case AGP_G4x_GCC1_SIZE_1M:
	case AGP_G4x_GCC1_SIZE_VT_1M:
		agp_gen5_adjust_pgtbl_size(dev, AGP_I810_PGTBL_SIZE_1MB);
		break;
	case AGP_G4x_GCC1_SIZE_VT_1_5M:
		agp_gen5_adjust_pgtbl_size(dev, AGP_I810_PGTBL_SIZE_1_5MB);
		break;
	case AGP_G4x_GCC1_SIZE_2M:
	case AGP_G4x_GCC1_SIZE_VT_2M:
		agp_gen5_adjust_pgtbl_size(dev, AGP_I810_PGTBL_SIZE_2MB);
		break;
	default:
		device_printf(dev, "Unknown page table size\n");
		return (ENXIO);
	}

	return (agp_i965_get_gtt_total_entries(dev));
}

static int
agp_sb_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint16_t gcc1;

	sc = device_get_softc(dev);

	gcc1 = pci_read_config(sc->bdev, AGP_SNB_GCC1, 2);
	switch (gcc1 & AGP_SNB_GTT_SIZE_MASK) {
	default:
	case AGP_SNB_GTT_SIZE_0M:
		kprintf("Bad GTT size mask: 0x%04x\n", gcc1);
		return (ENXIO);
	case AGP_SNB_GTT_SIZE_1M:
		sc->gtt_total_entries = 1024 * 1024 / 4;
		break;
	case AGP_SNB_GTT_SIZE_2M:
		sc->gtt_total_entries = 2 * 1024 * 1024 / 4;
		break;
	}
	return (0);
}

static int
agp_gen8_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint16_t gcc1;
	int v;

	sc = device_get_softc(dev);

	gcc1 = pci_read_config(sc->bdev, AGP_SNB_GCC1, 2);
	v = (gcc1 >> 6) & 3;
	if (v)
		v = 1 << v;
	sc->gtt_total_entries = (v << 20) / 8;

	/*
	 * XXX limit to 2GB due to misc integer overflows calculated on
	 * this field.
	 */
	while ((long)sc->gtt_total_entries * PAGE_SIZE >= 4LL*1024*1024*1024)
		sc->gtt_total_entries >>= 1;

	kprintf("GTT SIZE %ld representing %ldM vmap\n",
		(long)sc->gtt_total_entries * 8,
		(long)sc->gtt_total_entries * PAGE_SIZE);

	return 0;
}

static int
agp_i830_install_gatt(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t pgtblctl;

	sc = device_get_softc(dev);

	/*
	 * The i830 automatically initializes the 128k gatt on boot.
	 * GATT address is already in there, make sure it's enabled.
	 */
	pgtblctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	pgtblctl |= 1;
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL, pgtblctl);

	sc->gatt->ag_physical = pgtblctl & ~1;
	return (0);
}

static int
agp_i810_attach(device_t dev)
{
	struct agp_i810_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->bdev = agp_i810_find_bridge(dev);
	if (sc->bdev == NULL)
		return (ENOENT);

	sc->match = agp_i810_match(dev);

	agp_set_aperture_resource(dev, sc->match->driver->gen <= 2 ?
	    AGP_APBASE : AGP_I915_GMADR);
	error = agp_generic_attach(dev);
	if (error)
		return (error);

	if (ptoa((vm_paddr_t)Maxmem) >
	    (1ULL << sc->match->driver->busdma_addr_mask_sz) - 1) {
		device_printf(dev, "agp_i810 does not support physical "
		    "memory above %ju.\n", (uintmax_t)(1ULL <<
		    sc->match->driver->busdma_addr_mask_sz) - 1);
		return (ENOENT);
	}

	if (bus_alloc_resources(dev, sc->match->driver->res_spec, sc->sc_res)) {
		agp_generic_detach(dev);
		return (ENODEV);
	}

	sc->initial_aperture = AGP_GET_APERTURE(dev);
	sc->gatt = kmalloc(sizeof(struct agp_gatt), M_AGP, M_WAITOK);
	sc->gatt->ag_entries = AGP_GET_APERTURE(dev) >> AGP_PAGE_SHIFT;

	if ((error = sc->match->driver->get_stolen_size(dev)) != 0 ||
	    (error = sc->match->driver->install_gatt(dev)) != 0 ||
	    (error = sc->match->driver->get_gtt_mappable_entries(dev)) != 0 ||
	    (error = sc->match->driver->get_gtt_total_entries(dev)) != 0 ||
	    (error = sc->match->driver->chipset_flush_setup(dev)) != 0) {
		bus_release_resources(dev, sc->match->driver->res_spec,
		    sc->sc_res);
		kfree(sc->gatt, M_AGP);
		agp_generic_detach(dev);
		return (error);
	}

	intel_agp = dev;
	device_printf(dev, "aperture size is %dM",
	    sc->initial_aperture / 1024 / 1024);
	if (sc->stolen > 0)
		kprintf(", detected %dk stolen memory\n", sc->stolen * 4);
	else
		kprintf("\n");
	if (bootverbose) {
		sc->match->driver->dump_regs(dev);
		device_printf(dev, "Mappable GTT entries: %d\n",
		    sc->gtt_mappable_entries);
		device_printf(dev, "Total GTT entries: %d\n",
		    sc->gtt_total_entries);
	}
	return (0);
}

static void
agp_i830_deinstall_gatt(device_t dev)
{
	struct agp_i810_softc *sc;
	unsigned int pgtblctl;

	sc = device_get_softc(dev);
	pgtblctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	pgtblctl &= ~1;
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL, pgtblctl);
}

static int
agp_i810_detach(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	agp_free_cdev(dev);

	/* Clear the GATT base. */
	sc->match->driver->deinstall_gatt(dev);

	sc->match->driver->chipset_flush_teardown(dev);

	/* Put the aperture back the way it started. */
	AGP_SET_APERTURE(dev, sc->initial_aperture);

	kfree(sc->gatt, M_AGP);
	bus_release_resources(dev, sc->match->driver->res_spec, sc->sc_res);
	agp_free_res(dev);

	return (0);
}

static int
agp_i810_resume(device_t dev)
{
	struct agp_i810_softc *sc;
	sc = device_get_softc(dev);

	AGP_SET_APERTURE(dev, sc->initial_aperture);

	/* Install the GATT. */
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL,
	sc->gatt->ag_physical | 1);

	return (bus_generic_resume(dev));
}

/**
 * Sets the PCI resource size of the aperture on i830-class and below chipsets,
 * while returning failure on later chipsets when an actual change is
 * requested.
 *
 * This whole function is likely bogus, as the kernel would probably need to
 * reconfigure the placement of the AGP aperture if a larger size is requested,
 * which doesn't happen currently.
 */

static int
agp_i915_set_aperture(device_t dev, u_int32_t aperture)
{

	return (agp_generic_set_aperture(dev, aperture));
}

static int
agp_i810_method_set_aperture(device_t dev, u_int32_t aperture)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	return (sc->match->driver->set_aperture(dev, aperture));
}

/**
 * Writes a GTT entry mapping the page at the given offset from the
 * beginning of the aperture to the given physical address.  Setup the
 * caching mode according to flags.
 *
 * For gen 1, 2 and 3, GTT start is located at AGP_I810_GTT offset
 * from corresponding BAR start. For gen 4, offset is 512KB +
 * AGP_I810_GTT, for gen 5 and 6 it is 2MB + AGP_I810_GTT.
 *
 * Also, the bits of the physical page address above 4GB needs to be
 * placed into bits 40-32 of PTE.
 */
static void
agp_i915_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;

	agp_i915_write_gtt(dev, index, pte);
}

static void
agp_i915_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[1], index * 4, pte);
}

static void
agp_i965_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;

	pte |= (physical >> 28) & 0xf0;
	agp_i965_write_gtt(dev, index, pte);
}

static void
agp_i965_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], index * 4 + (512 * 1024), pte);
}

static void
agp_g4x_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;

	pte |= (physical >> 28) & 0xf0;
	agp_g4x_write_gtt(dev, index, pte);
}

static void
agp_g4x_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], index * 4 + (2 * 1024 * 1024), pte);
}

static void
agp_sb_install_gtt_pte(device_t dev, u_int index,
		       vm_offset_t physical, int flags)
{
	int type_mask, gfdt;
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	type_mask = flags & ~AGP_USER_CACHED_MEMORY_GFDT;
	gfdt = (flags & AGP_USER_CACHED_MEMORY_GFDT) != 0 ? GEN6_PTE_GFDT : 0;

	if (type_mask == AGP_USER_MEMORY)
		pte |= GEN6_PTE_UNCACHED;
	else if (type_mask == AGP_USER_CACHED_MEMORY_LLC_MLC)
		pte |= GEN6_PTE_LLC_MLC | gfdt;
	else
		pte |= GEN6_PTE_LLC | gfdt;

	pte |= (physical & 0x000000ff00000000ull) >> 28;
	agp_sb_write_gtt(dev, index, pte);
}

static void
agp_gen8_install_gtt_pte(device_t dev, u_int index,
		       vm_offset_t physical, int flags)
{
	struct agp_i810_softc *sc;
	int type_mask;
	uint64_t pte;

	pte = (u_int64_t)physical | GEN8_PTE_PRESENT | GEN8_PTE_RW;
	type_mask = flags & ~AGP_USER_CACHED_MEMORY_GFDT;

	if (type_mask == AGP_USER_MEMORY)
		pte |= GEN8_PTE_PWT;	/* XXX */
	else if (type_mask == AGP_USER_CACHED_MEMORY_LLC_MLC)
		pte |= GEN8_PTE_PWT;	/* XXX */
	else
		pte |= GEN8_PTE_PWT;	/* XXX */

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], index * 8 + (2 * 1024 * 1024),
		    (uint32_t)pte);
	bus_write_4(sc->sc_res[0], index * 8 + (2 * 1024 * 1024) + 4,
		    (uint32_t)(pte >> 32));
}

static void
agp_sb_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], index * 4 + (2 * 1024 * 1024), pte);
}

static int
agp_i810_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_i810_softc *sc = device_get_softc(dev);
	u_int index;

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT)) {
		device_printf(dev, "failed: offset is 0x%08jx, "
		    "shift is %d, entries is %d\n", (intmax_t)offset,
		    AGP_PAGE_SHIFT, sc->gatt->ag_entries);
		return (EINVAL);
	}
	index = offset >> AGP_PAGE_SHIFT;
	if (sc->stolen != 0 && index < sc->stolen) {
		device_printf(dev, "trying to bind into stolen memory\n");
		return (EINVAL);
	}
	sc->match->driver->install_gtt_pte(dev, index, physical, 0);
	return (0);
}

static int
agp_i810_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_i810_softc *sc;
	u_int index;

	sc = device_get_softc(dev);
	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return (EINVAL);
	index = offset >> AGP_PAGE_SHIFT;
	if (sc->stolen != 0 && index < sc->stolen) {
		device_printf(dev, "trying to unbind from stolen memory\n");
		return (EINVAL);
	}
	sc->match->driver->install_gtt_pte(dev, index, 0, 0);
	return (0);
}

static void
agp_i915_sync_gtt_pte(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_read_4(sc->sc_res[1], index * 4);
}

static void
agp_i965_sync_gtt_pte(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_read_4(sc->sc_res[0], index * 4 + (512 * 1024));
}

static void
agp_g4x_sync_gtt_pte(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_read_4(sc->sc_res[0], index * 4 + (2 * 1024 * 1024));
}

/*
 * Writing via memory mapped registers already flushes all TLBs.
 */
static void
agp_i810_flush_tlb(device_t dev)
{
}

static int
agp_i810_enable(device_t dev, u_int32_t mode)
{

	return (0);
}

static struct agp_memory *
agp_i810_alloc_memory(device_t dev, int type, vm_size_t size)
{
	struct agp_i810_softc *sc;
	struct agp_memory *mem;
	vm_page_t m;

	sc = device_get_softc(dev);

	if ((size & (AGP_PAGE_SIZE - 1)) != 0 ||
	    sc->agp.as_allocated + size > sc->agp.as_maxmem)
		return (0);

	if (type == 1) {
		/*
		 * Mapping local DRAM into GATT.
		 */
		if (sc->match->driver->chiptype != CHIP_I810)
			return (0);
		if (size != sc->dcache_size)
			return (0);
	} else if (type == 2) {
		/*
		 * Type 2 is the contiguous physical memory type, that hands
		 * back a physical address.  This is used for cursors on i810.
		 * Hand back as many single pages with physical as the user
		 * wants, but only allow one larger allocation (ARGB cursor)
		 * for simplicity.
		 */
		if (size != AGP_PAGE_SIZE) {
			if (sc->argb_cursor != NULL)
				return (0);

			/* Allocate memory for ARGB cursor, if we can. */
			sc->argb_cursor = contigmalloc(size, M_AGP,
			   0, 0, ~0, PAGE_SIZE, 0);
			if (sc->argb_cursor == NULL)
				return (0);
		}
	}

	mem = kmalloc(sizeof *mem, M_AGP, M_INTWAIT);
	mem->am_id = sc->agp.as_nextid++;
	mem->am_size = size;
	mem->am_type = type;
	if (type != 1 && (type != 2 || size == AGP_PAGE_SIZE))
		mem->am_obj = vm_object_allocate(OBJT_DEFAULT,
		    atop(round_page(size)));
	else
		mem->am_obj = 0;

	if (type == 2) {
		if (size == AGP_PAGE_SIZE) {
			/*
			 * Allocate and wire down the page now so that we can
			 * get its physical address.
			 */
			VM_OBJECT_LOCK(mem->am_obj);
			m = vm_page_grab(mem->am_obj, 0, VM_ALLOC_NORMAL |
							 VM_ALLOC_ZERO |
							 VM_ALLOC_RETRY);
			vm_page_wire(m);
			VM_OBJECT_UNLOCK(mem->am_obj);
			mem->am_physical = VM_PAGE_TO_PHYS(m);
			vm_page_wakeup(m);
		} else {
			/* Our allocation is already nicely wired down for us.
			 * Just grab the physical address.
			 */
			mem->am_physical = vtophys(sc->argb_cursor);
		}
	} else
		mem->am_physical = 0;

	mem->am_offset = 0;
	mem->am_is_bound = 0;
	TAILQ_INSERT_TAIL(&sc->agp.as_memory, mem, am_link);
	sc->agp.as_allocated += size;

	return (mem);
}

static int
agp_i810_free_memory(device_t dev, struct agp_memory *mem)
{
	struct agp_i810_softc *sc;

	if (mem->am_is_bound)
		return (EBUSY);

	sc = device_get_softc(dev);

	if (mem->am_type == 2) {
		if (mem->am_size == AGP_PAGE_SIZE) {
			/*
			 * Unwire the page which we wired in alloc_memory.
			 */
			vm_page_t m;

			vm_object_hold(mem->am_obj);
			m = vm_page_lookup_busy_wait(mem->am_obj, 0,
						     FALSE, "agppg");
			vm_object_drop(mem->am_obj);
			vm_page_unwire(m, 0);
			vm_page_wakeup(m);
		} else {
			contigfree(sc->argb_cursor, mem->am_size, M_AGP);
			sc->argb_cursor = NULL;
		}
	}

	sc->agp.as_allocated -= mem->am_size;
	TAILQ_REMOVE(&sc->agp.as_memory, mem, am_link);
	if (mem->am_obj)
		vm_object_deallocate(mem->am_obj);
	kfree(mem, M_AGP);
	return (0);
}

static int
agp_i810_bind_memory(device_t dev, struct agp_memory *mem, vm_offset_t offset)
{
	struct agp_i810_softc *sc;
	vm_offset_t i;

	/* Do some sanity checks first. */
	if ((offset & (AGP_PAGE_SIZE - 1)) != 0 ||
	    offset + mem->am_size > AGP_GET_APERTURE(dev)) {
		device_printf(dev, "binding memory at bad offset %#x\n",
		    (int)offset);
		return (EINVAL);
	}

	sc = device_get_softc(dev);
	if (mem->am_type == 2 && mem->am_size != AGP_PAGE_SIZE) {
		lockmgr(&sc->agp.as_lock, LK_EXCLUSIVE);
		if (mem->am_is_bound) {
			lockmgr(&sc->agp.as_lock, LK_RELEASE);
			return EINVAL;
		}
		/* The memory's already wired down, just stick it in the GTT. */
		for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE) {
			sc->match->driver->install_gtt_pte(dev, (offset + i) >>
			    AGP_PAGE_SHIFT, mem->am_physical + i, 0);
		}
		agp_flush_cache();
		mem->am_offset = offset;
		mem->am_is_bound = 1;
		lockmgr(&sc->agp.as_lock, LK_RELEASE);
		return (0);
	}

	if (mem->am_type != 1)
		return (agp_generic_bind_memory(dev, mem, offset));

	/*
	 * Mapping local DRAM into GATT.
	 */
	if (sc->match->driver->chiptype != CHIP_I810)
		return (EINVAL);
	for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE)
		bus_write_4(sc->sc_res[0],
		    AGP_I810_GTT + (i >> AGP_PAGE_SHIFT) * 4, i | 3);

	return (0);
}

static int
agp_i810_unbind_memory(device_t dev, struct agp_memory *mem)
{
	struct agp_i810_softc *sc;
	vm_offset_t i;

	sc = device_get_softc(dev);

	if (mem->am_type == 2 && mem->am_size != AGP_PAGE_SIZE) {
		lockmgr(&sc->agp.as_lock, LK_EXCLUSIVE);
		if (!mem->am_is_bound) {
			lockmgr(&sc->agp.as_lock, LK_RELEASE);
			return (EINVAL);
		}

		for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE) {
			sc->match->driver->install_gtt_pte(dev,
			    (mem->am_offset + i) >> AGP_PAGE_SHIFT, 0, 0);
		}
		agp_flush_cache();
		mem->am_is_bound = 0;
		lockmgr(&sc->agp.as_lock, LK_RELEASE);
		return (0);
	}

	if (mem->am_type != 1)
		return (agp_generic_unbind_memory(dev, mem));

	if (sc->match->driver->chiptype != CHIP_I810)
		return (EINVAL);
	for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE) {
		sc->match->driver->install_gtt_pte(dev, i >> AGP_PAGE_SHIFT,
		    0, 0);
	}
	return (0);
}

static device_method_t agp_i810_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	agp_i810_identify),
	DEVMETHOD(device_probe,		agp_i810_probe),
	DEVMETHOD(device_attach,	agp_i810_attach),
	DEVMETHOD(device_detach,	agp_i810_detach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	agp_i810_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_generic_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_i810_method_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_i810_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_i810_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_i810_flush_tlb),
	DEVMETHOD(agp_enable,		agp_i810_enable),
	DEVMETHOD(agp_alloc_memory,	agp_i810_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_i810_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_i810_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_i810_unbind_memory),
	DEVMETHOD(agp_chipset_flush,	agp_intel_gtt_chipset_flush),

	DEVMETHOD_END
};

static driver_t agp_i810_driver = {
	"agp",
	agp_i810_methods,
	sizeof(struct agp_i810_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_i810, vgapci, agp_i810_driver, agp_devclass, NULL, NULL);
MODULE_DEPEND(agp_i810, agp, 1, 1, 1);
MODULE_DEPEND(agp_i810, pci, 1, 1, 1);

extern vm_page_t bogus_page;

void
agp_intel_gtt_clear_range(device_t dev, u_int first_entry, u_int num_entries)
{
	struct agp_i810_softc *sc;
	u_int i;

	sc = device_get_softc(dev);
	for (i = 0; i < num_entries; i++)
		sc->match->driver->install_gtt_pte(dev, first_entry + i,
		    VM_PAGE_TO_PHYS(bogus_page), 0);
	sc->match->driver->sync_gtt_pte(dev, first_entry + num_entries - 1);
}

void
agp_intel_gtt_insert_pages(device_t dev, u_int first_entry, u_int num_entries,
    vm_page_t *pages, u_int flags)
{
	struct agp_i810_softc *sc;
	u_int i;

	sc = device_get_softc(dev);
	for (i = 0; i < num_entries; i++) {
		KKASSERT(pages[i]->valid == VM_PAGE_BITS_ALL);
		KKASSERT(pages[i]->wire_count > 0);
		sc->match->driver->install_gtt_pte(dev, first_entry + i,
		    VM_PAGE_TO_PHYS(pages[i]), flags);
	}
	sc->match->driver->sync_gtt_pte(dev, first_entry + num_entries - 1);
}

struct intel_gtt
agp_intel_gtt_get(device_t dev)
{
	struct agp_i810_softc *sc;
	struct intel_gtt res;

	sc = device_get_softc(dev);
	res.stolen_size = sc->stolen_size;
	res.gtt_total_entries = sc->gtt_total_entries;
	res.gtt_mappable_entries = sc->gtt_mappable_entries;
	res.do_idle_maps = 0;
	res.scratch_page_dma = VM_PAGE_TO_PHYS(bogus_page);
	return (res);
}

static int
agp_i810_chipset_flush_setup(device_t dev)
{

	return (0);
}

static void
agp_i810_chipset_flush_teardown(device_t dev)
{

	/* Nothing to do. */
}

static void
agp_i810_chipset_flush(device_t dev)
{

	/* Nothing to do. */
}

static int
agp_i915_chipset_flush_alloc_page(device_t dev, uint64_t start, uint64_t end)
{
	struct agp_i810_softc *sc;
	device_t vga;

	sc = device_get_softc(dev);
	vga = device_get_parent(dev);
	sc->sc_flush_page_rid = 100;
	sc->sc_flush_page_res = BUS_ALLOC_RESOURCE(device_get_parent(vga), dev,
	    SYS_RES_MEMORY, &sc->sc_flush_page_rid, start, end, PAGE_SIZE,
	    RF_ACTIVE, -1);
	if (sc->sc_flush_page_res == NULL) {
		device_printf(dev, "Failed to allocate flush page at 0x%jx\n",
		    (uintmax_t)start);
		return (EINVAL);
	}
	sc->sc_flush_page_vaddr = rman_get_virtual(sc->sc_flush_page_res);
	if (bootverbose) {
		device_printf(dev, "Allocated flush page phys 0x%jx virt %p\n",
		    (uintmax_t)rman_get_start(sc->sc_flush_page_res),
		    sc->sc_flush_page_vaddr);
	}
	return (0);
}

static void
agp_i915_chipset_flush_free_page(device_t dev)
{
	struct agp_i810_softc *sc;
	device_t vga;

	sc = device_get_softc(dev);
	vga = device_get_parent(dev);
	if (sc->sc_flush_page_res == NULL)
		return;
	BUS_DEACTIVATE_RESOURCE(device_get_parent(vga), dev, SYS_RES_MEMORY,
	    sc->sc_flush_page_rid, sc->sc_flush_page_res);
	BUS_RELEASE_RESOURCE(device_get_parent(vga), dev, SYS_RES_MEMORY,
	    sc->sc_flush_page_rid, sc->sc_flush_page_res);
}

static int
agp_i915_chipset_flush_setup(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t temp;
	int error;

	sc = device_get_softc(dev);
	temp = pci_read_config(sc->bdev, AGP_I915_IFPADDR, 4);
	if ((temp & 1) != 0) {
		temp &= ~1;
		if (bootverbose)
			device_printf(dev,
			    "Found already configured flush page at 0x%jx\n",
			    (uintmax_t)temp);
		sc->sc_bios_allocated_flush_page = 1;
		/*
		 * In the case BIOS initialized the flush pointer (?)
		 * register, expect that BIOS also set up the resource
		 * for the page.
		 */
		error = agp_i915_chipset_flush_alloc_page(dev, temp,
		    temp + PAGE_SIZE - 1);
		if (error != 0)
			return (error);
	} else {
		sc->sc_bios_allocated_flush_page = 0;
		error = agp_i915_chipset_flush_alloc_page(dev, 0, 0xffffffff);
		if (error != 0)
			return (error);
		temp = rman_get_start(sc->sc_flush_page_res);
		pci_write_config(sc->bdev, AGP_I915_IFPADDR, temp | 1, 4);
	}
	return (0);
}

static void
agp_i915_chipset_flush_teardown(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t temp;

	sc = device_get_softc(dev);
	if (sc->sc_flush_page_res == NULL)
		return;
	if (!sc->sc_bios_allocated_flush_page) {
		temp = pci_read_config(sc->bdev, AGP_I915_IFPADDR, 4);
		temp &= ~1;
		pci_write_config(sc->bdev, AGP_I915_IFPADDR, temp, 4);
	}
	agp_i915_chipset_flush_free_page(dev);
}

static int
agp_i965_chipset_flush_setup(device_t dev)
{
	struct agp_i810_softc *sc;
	uint64_t temp;
	uint32_t temp_hi, temp_lo;
	int error;

	sc = device_get_softc(dev);

	temp_hi = pci_read_config(sc->bdev, AGP_I965_IFPADDR + 4, 4);
	temp_lo = pci_read_config(sc->bdev, AGP_I965_IFPADDR, 4);

	if ((temp_lo & 1) != 0) {
		temp = ((uint64_t)temp_hi << 32) | (temp_lo & ~1);
		if (bootverbose)
			device_printf(dev,
			    "Found already configured flush page at 0x%jx\n",
			    (uintmax_t)temp);
		sc->sc_bios_allocated_flush_page = 1;
		/*
		 * In the case BIOS initialized the flush pointer (?)
		 * register, expect that BIOS also set up the resource
		 * for the page.
		 */
		error = agp_i915_chipset_flush_alloc_page(dev, temp,
		    temp + PAGE_SIZE - 1);
		if (error != 0)
			return (error);
	} else {
		sc->sc_bios_allocated_flush_page = 0;
		error = agp_i915_chipset_flush_alloc_page(dev, 0, ~0);
		if (error != 0)
			return (error);
		temp = rman_get_start(sc->sc_flush_page_res);
		pci_write_config(sc->bdev, AGP_I965_IFPADDR + 4,
		    (temp >> 32) & UINT32_MAX, 4);
		pci_write_config(sc->bdev, AGP_I965_IFPADDR,
		    (temp & UINT32_MAX) | 1, 4);
	}
	return (0);
}

static void
agp_i965_chipset_flush_teardown(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t temp_lo;

	sc = device_get_softc(dev);
	if (sc->sc_flush_page_res == NULL)
		return;
	if (!sc->sc_bios_allocated_flush_page) {
		temp_lo = pci_read_config(sc->bdev, AGP_I965_IFPADDR, 4);
		temp_lo &= ~1;
		pci_write_config(sc->bdev, AGP_I965_IFPADDR, temp_lo, 4);
	}
	agp_i915_chipset_flush_free_page(dev);
}

static void
agp_i915_chipset_flush(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	*(uint32_t *)sc->sc_flush_page_vaddr = 1;
}

int
agp_intel_gtt_chipset_flush(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	sc->match->driver->chipset_flush(dev);
	return (0);
}

void
agp_intel_gtt_unmap_memory(device_t dev, struct sglist *sg_list)
{
}

int
agp_intel_gtt_map_memory(device_t dev, vm_page_t *pages, u_int num_entries,
    struct sglist **sg_list)
{
#if 0
	struct agp_i810_softc *sc;
#endif
	struct sglist *sg;
	int i;
#if 0
	int error;
	bus_dma_tag_t dmat;
#endif

	if (*sg_list != NULL)
		return (0);
#if 0
	sc = device_get_softc(dev);
#endif
	sg = sglist_alloc(num_entries, M_WAITOK /* XXXKIB */);
	for (i = 0; i < num_entries; i++) {
		sg->sg_segs[i].ss_paddr = VM_PAGE_TO_PHYS(pages[i]);
		sg->sg_segs[i].ss_len = PAGE_SIZE;
	}

#if 0
	error = bus_dma_tag_create(bus_get_dma_tag(dev),
	    1 /* alignment */, 0 /* boundary */,
	    1ULL << sc->match->busdma_addr_mask_sz /* lowaddr */,
	    BUS_SPACE_MAXADDR /* highaddr */,
            NULL /* filtfunc */, NULL /* filtfuncarg */,
	    BUS_SPACE_MAXADDR /* maxsize */,
	    BUS_SPACE_UNRESTRICTED /* nsegments */,
	    BUS_SPACE_MAXADDR /* maxsegsz */,
	    0 /* flags */, NULL /* lockfunc */, NULL /* lockfuncarg */,
	    &dmat);
	if (error != 0) {
		sglist_free(sg);
		return (error);
	}
	/* XXXKIB */
#endif
	*sg_list = sg;
	return (0);
}

void
agp_intel_gtt_insert_sg_entries(device_t dev, struct sglist *sg_list,
    u_int first_entry, u_int flags)
{
	struct agp_i810_softc *sc;
	vm_paddr_t spaddr;
	size_t slen;
	u_int i, j;

	sc = device_get_softc(dev);
	for (i = j = 0; j < sg_list->sg_nseg; j++) {
		spaddr = sg_list->sg_segs[i].ss_paddr;
		slen = sg_list->sg_segs[i].ss_len;
		for (; slen > 0; i++) {
			sc->match->driver->install_gtt_pte(dev, first_entry + i,
			    spaddr, flags);
			spaddr += AGP_PAGE_SIZE;
			slen -= AGP_PAGE_SIZE;
		}
	}
	sc->match->driver->sync_gtt_pte(dev, first_entry + i - 1);
}

void
intel_gtt_clear_range(u_int first_entry, u_int num_entries)
{

	agp_intel_gtt_clear_range(intel_agp, first_entry, num_entries);
}

void
intel_gtt_insert_pages(u_int first_entry, u_int num_entries, vm_page_t *pages,
    u_int flags)
{

	agp_intel_gtt_insert_pages(intel_agp, first_entry, num_entries,
	    pages, flags);
}

void intel_gtt_get(size_t *gtt_total, size_t *stolen_size,
		   phys_addr_t *mappable_base, unsigned long *mappable_end)
{
	intel_private.base = agp_intel_gtt_get(intel_agp);

	*gtt_total = intel_private.base.gtt_total_entries << PAGE_SHIFT;
	*stolen_size = intel_private.base.stolen_size;
	*mappable_base = intel_private.base.gma_bus_addr;
	*mappable_end = intel_private.base.gtt_mappable_entries << PAGE_SHIFT;
}

int
intel_gtt_chipset_flush(void)
{

	return (agp_intel_gtt_chipset_flush(intel_agp));
}

void
intel_gtt_unmap_memory(struct sglist *sg_list)
{

	agp_intel_gtt_unmap_memory(intel_agp, sg_list);
}

int
intel_gtt_map_memory(vm_page_t *pages, u_int num_entries,
    struct sglist **sg_list)
{

	return (agp_intel_gtt_map_memory(intel_agp, pages, num_entries,
	    sg_list));
}

void
intel_gtt_insert_sg_entries(struct sglist *sg_list, u_int first_entry,
    u_int flags)
{

	agp_intel_gtt_insert_sg_entries(intel_agp, sg_list, first_entry, flags);
}

/*
 * Only used by gen6
 */
void
intel_gtt_sync_pte(u_int entry)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(intel_agp);
	sc->match->driver->sync_gtt_pte(intel_agp, entry);
}

/*
 * Only used by gen6
 */
void
intel_gtt_write(u_int entry, uint32_t val)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(intel_agp);
	sc->match->driver->write_gtt(intel_agp, entry, val);
}
