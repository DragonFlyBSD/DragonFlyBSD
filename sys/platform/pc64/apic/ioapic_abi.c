/*
 * Copyright (c) 1991 The Regents of the University of California.
 * Copyright (c) 1996, by Steve Passe.  All rights reserved.
 * Copyright (c) 2005,2008 The DragonFly Project.  All rights reserved.
 * All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/thread2.h>

#include <machine/smp.h>
#include <machine/segments.h>
#include <machine/md_var.h>
#include <machine/intr_machdep.h>
#include <machine/globaldata.h>
#include <machine/msi_var.h>

#include <machine_base/isa/isa_intr.h>
#include <machine_base/icu/icu.h>
#include <machine_base/icu/icu_var.h>
#include <machine_base/apic/ioapic.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine_base/apic/ioapic_ipl.h>
#include <machine_base/apic/apicreg.h>

#include <dev/acpica/acpi_sci_var.h>

#define IOAPIC_HWI_VECTORS	IDT_HWI_VECTORS

extern inthand_t
	IDTVEC(ioapic_intr0),
	IDTVEC(ioapic_intr1),
	IDTVEC(ioapic_intr2),
	IDTVEC(ioapic_intr3),
	IDTVEC(ioapic_intr4),
	IDTVEC(ioapic_intr5),
	IDTVEC(ioapic_intr6),
	IDTVEC(ioapic_intr7),
	IDTVEC(ioapic_intr8),
	IDTVEC(ioapic_intr9),
	IDTVEC(ioapic_intr10),
	IDTVEC(ioapic_intr11),
	IDTVEC(ioapic_intr12),
	IDTVEC(ioapic_intr13),
	IDTVEC(ioapic_intr14),
	IDTVEC(ioapic_intr15),
	IDTVEC(ioapic_intr16),
	IDTVEC(ioapic_intr17),
	IDTVEC(ioapic_intr18),
	IDTVEC(ioapic_intr19),
	IDTVEC(ioapic_intr20),
	IDTVEC(ioapic_intr21),
	IDTVEC(ioapic_intr22),
	IDTVEC(ioapic_intr23),
	IDTVEC(ioapic_intr24),
	IDTVEC(ioapic_intr25),
	IDTVEC(ioapic_intr26),
	IDTVEC(ioapic_intr27),
	IDTVEC(ioapic_intr28),
	IDTVEC(ioapic_intr29),
	IDTVEC(ioapic_intr30),
	IDTVEC(ioapic_intr31),
	IDTVEC(ioapic_intr32),
	IDTVEC(ioapic_intr33),
	IDTVEC(ioapic_intr34),
	IDTVEC(ioapic_intr35),
	IDTVEC(ioapic_intr36),
	IDTVEC(ioapic_intr37),
	IDTVEC(ioapic_intr38),
	IDTVEC(ioapic_intr39),
	IDTVEC(ioapic_intr40),
	IDTVEC(ioapic_intr41),
	IDTVEC(ioapic_intr42),
	IDTVEC(ioapic_intr43),
	IDTVEC(ioapic_intr44),
	IDTVEC(ioapic_intr45),
	IDTVEC(ioapic_intr46),
	IDTVEC(ioapic_intr47),
	IDTVEC(ioapic_intr48),
	IDTVEC(ioapic_intr49),
	IDTVEC(ioapic_intr50),
	IDTVEC(ioapic_intr51),
	IDTVEC(ioapic_intr52),
	IDTVEC(ioapic_intr53),
	IDTVEC(ioapic_intr54),
	IDTVEC(ioapic_intr55),
	IDTVEC(ioapic_intr56),
	IDTVEC(ioapic_intr57),
	IDTVEC(ioapic_intr58),
	IDTVEC(ioapic_intr59),
	IDTVEC(ioapic_intr60),
	IDTVEC(ioapic_intr61),
	IDTVEC(ioapic_intr62),
	IDTVEC(ioapic_intr63),
	IDTVEC(ioapic_intr64),
	IDTVEC(ioapic_intr65),
	IDTVEC(ioapic_intr66),
	IDTVEC(ioapic_intr67),
	IDTVEC(ioapic_intr68),
	IDTVEC(ioapic_intr69),
	IDTVEC(ioapic_intr70),
	IDTVEC(ioapic_intr71),
	IDTVEC(ioapic_intr72),
	IDTVEC(ioapic_intr73),
	IDTVEC(ioapic_intr74),
	IDTVEC(ioapic_intr75),
	IDTVEC(ioapic_intr76),
	IDTVEC(ioapic_intr77),
	IDTVEC(ioapic_intr78),
	IDTVEC(ioapic_intr79),
	IDTVEC(ioapic_intr80),
	IDTVEC(ioapic_intr81),
	IDTVEC(ioapic_intr82),
	IDTVEC(ioapic_intr83),
	IDTVEC(ioapic_intr84),
	IDTVEC(ioapic_intr85),
	IDTVEC(ioapic_intr86),
	IDTVEC(ioapic_intr87),
	IDTVEC(ioapic_intr88),
	IDTVEC(ioapic_intr89),
	IDTVEC(ioapic_intr90),
	IDTVEC(ioapic_intr91),
	IDTVEC(ioapic_intr92),
	IDTVEC(ioapic_intr93),
	IDTVEC(ioapic_intr94),
	IDTVEC(ioapic_intr95),
	IDTVEC(ioapic_intr96),
	IDTVEC(ioapic_intr97),
	IDTVEC(ioapic_intr98),
	IDTVEC(ioapic_intr99),
	IDTVEC(ioapic_intr100),
	IDTVEC(ioapic_intr101),
	IDTVEC(ioapic_intr102),
	IDTVEC(ioapic_intr103),
	IDTVEC(ioapic_intr104),
	IDTVEC(ioapic_intr105),
	IDTVEC(ioapic_intr106),
	IDTVEC(ioapic_intr107),
	IDTVEC(ioapic_intr108),
	IDTVEC(ioapic_intr109),
	IDTVEC(ioapic_intr110),
	IDTVEC(ioapic_intr111),
	IDTVEC(ioapic_intr112),
	IDTVEC(ioapic_intr113),
	IDTVEC(ioapic_intr114),
	IDTVEC(ioapic_intr115),
	IDTVEC(ioapic_intr116),
	IDTVEC(ioapic_intr117),
	IDTVEC(ioapic_intr118),
	IDTVEC(ioapic_intr119),
	IDTVEC(ioapic_intr120),
	IDTVEC(ioapic_intr121),
	IDTVEC(ioapic_intr122),
	IDTVEC(ioapic_intr123),
	IDTVEC(ioapic_intr124),
	IDTVEC(ioapic_intr125),
	IDTVEC(ioapic_intr126),
	IDTVEC(ioapic_intr127),
	IDTVEC(ioapic_intr128),
	IDTVEC(ioapic_intr129),
	IDTVEC(ioapic_intr130),
	IDTVEC(ioapic_intr131),
	IDTVEC(ioapic_intr132),
	IDTVEC(ioapic_intr133),
	IDTVEC(ioapic_intr134),
	IDTVEC(ioapic_intr135),
	IDTVEC(ioapic_intr136),
	IDTVEC(ioapic_intr137),
	IDTVEC(ioapic_intr138),
	IDTVEC(ioapic_intr139),
	IDTVEC(ioapic_intr140),
	IDTVEC(ioapic_intr141),
	IDTVEC(ioapic_intr142),
	IDTVEC(ioapic_intr143),
	IDTVEC(ioapic_intr144),
	IDTVEC(ioapic_intr145),
	IDTVEC(ioapic_intr146),
	IDTVEC(ioapic_intr147),
	IDTVEC(ioapic_intr148),
	IDTVEC(ioapic_intr149),
	IDTVEC(ioapic_intr150),
	IDTVEC(ioapic_intr151),
	IDTVEC(ioapic_intr152),
	IDTVEC(ioapic_intr153),
	IDTVEC(ioapic_intr154),
	IDTVEC(ioapic_intr155),
	IDTVEC(ioapic_intr156),
	IDTVEC(ioapic_intr157),
	IDTVEC(ioapic_intr158),
	IDTVEC(ioapic_intr159),
	IDTVEC(ioapic_intr160),
	IDTVEC(ioapic_intr161),
	IDTVEC(ioapic_intr162),
	IDTVEC(ioapic_intr163),
	IDTVEC(ioapic_intr164),
	IDTVEC(ioapic_intr165),
	IDTVEC(ioapic_intr166),
	IDTVEC(ioapic_intr167),
	IDTVEC(ioapic_intr168),
	IDTVEC(ioapic_intr169),
	IDTVEC(ioapic_intr170),
	IDTVEC(ioapic_intr171),
	IDTVEC(ioapic_intr172),
	IDTVEC(ioapic_intr173),
	IDTVEC(ioapic_intr174),
	IDTVEC(ioapic_intr175),
	IDTVEC(ioapic_intr176),
	IDTVEC(ioapic_intr177),
	IDTVEC(ioapic_intr178),
	IDTVEC(ioapic_intr179),
	IDTVEC(ioapic_intr180),
	IDTVEC(ioapic_intr181),
	IDTVEC(ioapic_intr182),
	IDTVEC(ioapic_intr183),
	IDTVEC(ioapic_intr184),
	IDTVEC(ioapic_intr185),
	IDTVEC(ioapic_intr186),
	IDTVEC(ioapic_intr187),
	IDTVEC(ioapic_intr188),
	IDTVEC(ioapic_intr189),
	IDTVEC(ioapic_intr190),
	IDTVEC(ioapic_intr191);

static inthand_t *ioapic_intr[IOAPIC_HWI_VECTORS] = {
	&IDTVEC(ioapic_intr0),
	&IDTVEC(ioapic_intr1),
	&IDTVEC(ioapic_intr2),
	&IDTVEC(ioapic_intr3),
	&IDTVEC(ioapic_intr4),
	&IDTVEC(ioapic_intr5),
	&IDTVEC(ioapic_intr6),
	&IDTVEC(ioapic_intr7),
	&IDTVEC(ioapic_intr8),
	&IDTVEC(ioapic_intr9),
	&IDTVEC(ioapic_intr10),
	&IDTVEC(ioapic_intr11),
	&IDTVEC(ioapic_intr12),
	&IDTVEC(ioapic_intr13),
	&IDTVEC(ioapic_intr14),
	&IDTVEC(ioapic_intr15),
	&IDTVEC(ioapic_intr16),
	&IDTVEC(ioapic_intr17),
	&IDTVEC(ioapic_intr18),
	&IDTVEC(ioapic_intr19),
	&IDTVEC(ioapic_intr20),
	&IDTVEC(ioapic_intr21),
	&IDTVEC(ioapic_intr22),
	&IDTVEC(ioapic_intr23),
	&IDTVEC(ioapic_intr24),
	&IDTVEC(ioapic_intr25),
	&IDTVEC(ioapic_intr26),
	&IDTVEC(ioapic_intr27),
	&IDTVEC(ioapic_intr28),
	&IDTVEC(ioapic_intr29),
	&IDTVEC(ioapic_intr30),
	&IDTVEC(ioapic_intr31),
	&IDTVEC(ioapic_intr32),
	&IDTVEC(ioapic_intr33),
	&IDTVEC(ioapic_intr34),
	&IDTVEC(ioapic_intr35),
	&IDTVEC(ioapic_intr36),
	&IDTVEC(ioapic_intr37),
	&IDTVEC(ioapic_intr38),
	&IDTVEC(ioapic_intr39),
	&IDTVEC(ioapic_intr40),
	&IDTVEC(ioapic_intr41),
	&IDTVEC(ioapic_intr42),
	&IDTVEC(ioapic_intr43),
	&IDTVEC(ioapic_intr44),
	&IDTVEC(ioapic_intr45),
	&IDTVEC(ioapic_intr46),
	&IDTVEC(ioapic_intr47),
	&IDTVEC(ioapic_intr48),
	&IDTVEC(ioapic_intr49),
	&IDTVEC(ioapic_intr50),
	&IDTVEC(ioapic_intr51),
	&IDTVEC(ioapic_intr52),
	&IDTVEC(ioapic_intr53),
	&IDTVEC(ioapic_intr54),
	&IDTVEC(ioapic_intr55),
	&IDTVEC(ioapic_intr56),
	&IDTVEC(ioapic_intr57),
	&IDTVEC(ioapic_intr58),
	&IDTVEC(ioapic_intr59),
	&IDTVEC(ioapic_intr60),
	&IDTVEC(ioapic_intr61),
	&IDTVEC(ioapic_intr62),
	&IDTVEC(ioapic_intr63),
	&IDTVEC(ioapic_intr64),
	&IDTVEC(ioapic_intr65),
	&IDTVEC(ioapic_intr66),
	&IDTVEC(ioapic_intr67),
	&IDTVEC(ioapic_intr68),
	&IDTVEC(ioapic_intr69),
	&IDTVEC(ioapic_intr70),
	&IDTVEC(ioapic_intr71),
	&IDTVEC(ioapic_intr72),
	&IDTVEC(ioapic_intr73),
	&IDTVEC(ioapic_intr74),
	&IDTVEC(ioapic_intr75),
	&IDTVEC(ioapic_intr76),
	&IDTVEC(ioapic_intr77),
	&IDTVEC(ioapic_intr78),
	&IDTVEC(ioapic_intr79),
	&IDTVEC(ioapic_intr80),
	&IDTVEC(ioapic_intr81),
	&IDTVEC(ioapic_intr82),
	&IDTVEC(ioapic_intr83),
	&IDTVEC(ioapic_intr84),
	&IDTVEC(ioapic_intr85),
	&IDTVEC(ioapic_intr86),
	&IDTVEC(ioapic_intr87),
	&IDTVEC(ioapic_intr88),
	&IDTVEC(ioapic_intr89),
	&IDTVEC(ioapic_intr90),
	&IDTVEC(ioapic_intr91),
	&IDTVEC(ioapic_intr92),
	&IDTVEC(ioapic_intr93),
	&IDTVEC(ioapic_intr94),
	&IDTVEC(ioapic_intr95),
	&IDTVEC(ioapic_intr96),
	&IDTVEC(ioapic_intr97),
	&IDTVEC(ioapic_intr98),
	&IDTVEC(ioapic_intr99),
	&IDTVEC(ioapic_intr100),
	&IDTVEC(ioapic_intr101),
	&IDTVEC(ioapic_intr102),
	&IDTVEC(ioapic_intr103),
	&IDTVEC(ioapic_intr104),
	&IDTVEC(ioapic_intr105),
	&IDTVEC(ioapic_intr106),
	&IDTVEC(ioapic_intr107),
	&IDTVEC(ioapic_intr108),
	&IDTVEC(ioapic_intr109),
	&IDTVEC(ioapic_intr110),
	&IDTVEC(ioapic_intr111),
	&IDTVEC(ioapic_intr112),
	&IDTVEC(ioapic_intr113),
	&IDTVEC(ioapic_intr114),
	&IDTVEC(ioapic_intr115),
	&IDTVEC(ioapic_intr116),
	&IDTVEC(ioapic_intr117),
	&IDTVEC(ioapic_intr118),
	&IDTVEC(ioapic_intr119),
	&IDTVEC(ioapic_intr120),
	&IDTVEC(ioapic_intr121),
	&IDTVEC(ioapic_intr122),
	&IDTVEC(ioapic_intr123),
	&IDTVEC(ioapic_intr124),
	&IDTVEC(ioapic_intr125),
	&IDTVEC(ioapic_intr126),
	&IDTVEC(ioapic_intr127),
	&IDTVEC(ioapic_intr128),
	&IDTVEC(ioapic_intr129),
	&IDTVEC(ioapic_intr130),
	&IDTVEC(ioapic_intr131),
	&IDTVEC(ioapic_intr132),
	&IDTVEC(ioapic_intr133),
	&IDTVEC(ioapic_intr134),
	&IDTVEC(ioapic_intr135),
	&IDTVEC(ioapic_intr136),
	&IDTVEC(ioapic_intr137),
	&IDTVEC(ioapic_intr138),
	&IDTVEC(ioapic_intr139),
	&IDTVEC(ioapic_intr140),
	&IDTVEC(ioapic_intr141),
	&IDTVEC(ioapic_intr142),
	&IDTVEC(ioapic_intr143),
	&IDTVEC(ioapic_intr144),
	&IDTVEC(ioapic_intr145),
	&IDTVEC(ioapic_intr146),
	&IDTVEC(ioapic_intr147),
	&IDTVEC(ioapic_intr148),
	&IDTVEC(ioapic_intr149),
	&IDTVEC(ioapic_intr150),
	&IDTVEC(ioapic_intr151),
	&IDTVEC(ioapic_intr152),
	&IDTVEC(ioapic_intr153),
	&IDTVEC(ioapic_intr154),
	&IDTVEC(ioapic_intr155),
	&IDTVEC(ioapic_intr156),
	&IDTVEC(ioapic_intr157),
	&IDTVEC(ioapic_intr158),
	&IDTVEC(ioapic_intr159),
	&IDTVEC(ioapic_intr160),
	&IDTVEC(ioapic_intr161),
	&IDTVEC(ioapic_intr162),
	&IDTVEC(ioapic_intr163),
	&IDTVEC(ioapic_intr164),
	&IDTVEC(ioapic_intr165),
	&IDTVEC(ioapic_intr166),
	&IDTVEC(ioapic_intr167),
	&IDTVEC(ioapic_intr168),
	&IDTVEC(ioapic_intr169),
	&IDTVEC(ioapic_intr170),
	&IDTVEC(ioapic_intr171),
	&IDTVEC(ioapic_intr172),
	&IDTVEC(ioapic_intr173),
	&IDTVEC(ioapic_intr174),
	&IDTVEC(ioapic_intr175),
	&IDTVEC(ioapic_intr176),
	&IDTVEC(ioapic_intr177),
	&IDTVEC(ioapic_intr178),
	&IDTVEC(ioapic_intr179),
	&IDTVEC(ioapic_intr180),
	&IDTVEC(ioapic_intr181),
	&IDTVEC(ioapic_intr182),
	&IDTVEC(ioapic_intr183),
	&IDTVEC(ioapic_intr184),
	&IDTVEC(ioapic_intr185),
	&IDTVEC(ioapic_intr186),
	&IDTVEC(ioapic_intr187),
	&IDTVEC(ioapic_intr188),
	&IDTVEC(ioapic_intr189),
	&IDTVEC(ioapic_intr190),
	&IDTVEC(ioapic_intr191)
};

#define IOAPIC_HWI_SYSCALL	(IDT_OFFSET_SYSCALL - IDT_OFFSET)

static struct ioapic_irqmap {
	int			im_type;	/* IOAPIC_IMT_ */
	enum intr_trigger	im_trig;
	enum intr_polarity	im_pola;
	int			im_gsi;
	int			im_msi_base;
	uint32_t		im_flags;	/* IOAPIC_IMF_ */
} ioapic_irqmaps[MAXCPU][IOAPIC_HWI_VECTORS];

static struct lwkt_token ioapic_irqmap_tok =
	LWKT_TOKEN_INITIALIZER(ioapic_irqmap_token);

#define IOAPIC_IMT_UNUSED	0
#define IOAPIC_IMT_RESERVED	1
#define IOAPIC_IMT_LEGACY	2
#define IOAPIC_IMT_SYSCALL	3
#define IOAPIC_IMT_MSI		4
#define IOAPIC_IMT_MSIX		5

#define IOAPIC_IMT_ISHWI(map)	((map)->im_type != IOAPIC_IMT_RESERVED && \
				 (map)->im_type != IOAPIC_IMT_SYSCALL)

#define IOAPIC_IMF_CONF		0x1

extern void	IOAPIC_INTREN(int);
extern void	IOAPIC_INTRDIS(int);

extern int	imcr_present;

static void	ioapic_abi_intr_enable(int);
static void	ioapic_abi_intr_disable(int);
static void	ioapic_abi_intr_setup(int, int);
static void	ioapic_abi_intr_teardown(int);

static void	ioapic_abi_legacy_intr_config(int,
		    enum intr_trigger, enum intr_polarity);
static int	ioapic_abi_legacy_intr_cpuid(int);
static int	ioapic_abi_legacy_intr_find(int,
		    enum intr_trigger, enum intr_polarity);
static int	ioapic_abi_legacy_intr_find_bygsi(int,
		    enum intr_trigger, enum intr_polarity);

static int	ioapic_abi_msi_alloc(int [], int, int);
static void	ioapic_abi_msi_release(const int [], int, int);
static void	ioapic_abi_msi_map(int, uint64_t *, uint32_t *, int);
static int	ioapic_abi_msix_alloc(int *, int);
static void	ioapic_abi_msix_release(int, int);

static int	ioapic_abi_msi_alloc_intern(int, const char *,
		    int [], int, int);
static void	ioapic_abi_msi_release_intern(int, const char *,
		    const int [], int, int);

static void	ioapic_abi_finalize(void);
static void	ioapic_abi_cleanup(void);
static void	ioapic_abi_setdefault(void);
static void	ioapic_abi_stabilize(void);
static void	ioapic_abi_initmap(void);
static void	ioapic_abi_rman_setup(struct rman *);

static int	ioapic_abi_gsi_cpuid(int, int);
static int	ioapic_find_unused_irqmap(int);

struct machintr_abi MachIntrABI_IOAPIC = {
	MACHINTR_IOAPIC,
	.intr_disable	= ioapic_abi_intr_disable,
	.intr_enable	= ioapic_abi_intr_enable,
	.intr_setup	= ioapic_abi_intr_setup,
	.intr_teardown	= ioapic_abi_intr_teardown,

	.legacy_intr_config = ioapic_abi_legacy_intr_config,
	.legacy_intr_cpuid = ioapic_abi_legacy_intr_cpuid,
	.legacy_intr_find = ioapic_abi_legacy_intr_find,
	.legacy_intr_find_bygsi = ioapic_abi_legacy_intr_find_bygsi,

	.msi_alloc	= ioapic_abi_msi_alloc,
	.msi_release	= ioapic_abi_msi_release,
	.msi_map	= ioapic_abi_msi_map,
	.msix_alloc	= ioapic_abi_msix_alloc,
	.msix_release	= ioapic_abi_msix_release,

	.finalize	= ioapic_abi_finalize,
	.cleanup	= ioapic_abi_cleanup,
	.setdefault	= ioapic_abi_setdefault,
	.stabilize	= ioapic_abi_stabilize,
	.initmap	= ioapic_abi_initmap,
	.rman_setup	= ioapic_abi_rman_setup
};

static int	ioapic_abi_extint_irq = -1;
static int	ioapic_abi_legacy_irq_max;
static int	ioapic_abi_gsi_balance = 1;
static int	ioapic_abi_msi_start;	/* NOTE: for testing only */

struct ioapic_irqinfo	ioapic_irqs[IOAPIC_HWI_VECTORS];

static void
ioapic_abi_intr_enable(int irq)
{
	const struct ioapic_irqmap *map;

	KASSERT(irq >= 0 && irq < IOAPIC_HWI_VECTORS,
	    ("ioapic enable, invalid irq %d", irq));

	map = &ioapic_irqmaps[mycpuid][irq];
	KASSERT(IOAPIC_IMT_ISHWI(map),
	    ("ioapic enable, not hwi irq %d, type %d, cpu%d",
	     irq, map->im_type, mycpuid));
	if (map->im_type != IOAPIC_IMT_LEGACY)
		return;

	IOAPIC_INTREN(irq);
}

static void
ioapic_abi_intr_disable(int irq)
{
	const struct ioapic_irqmap *map;

	KASSERT(irq >= 0 && irq < IOAPIC_HWI_VECTORS,
	    ("ioapic disable, invalid irq %d", irq));

	map = &ioapic_irqmaps[mycpuid][irq];
	KASSERT(IOAPIC_IMT_ISHWI(map),
	    ("ioapic disable, not hwi irq %d, type %d, cpu%d",
	     irq, map->im_type, mycpuid));
	if (map->im_type != IOAPIC_IMT_LEGACY)
		return;

	IOAPIC_INTRDIS(irq);
}

static void
ioapic_abi_finalize(void)
{
	KKASSERT(MachIntrABI.type == MACHINTR_IOAPIC);
	KKASSERT(ioapic_enable);

	/*
	 * If an IMCR is present, program bit 0 to disconnect the 8259
	 * from the BSP.
	 */
	if (imcr_present) {
		outb(0x22, 0x70);	/* select IMCR */
		outb(0x23, 0x01);	/* disconnect 8259 */
	}
}

/*
 * This routine is called after physical interrupts are enabled but before
 * the critical section is released.  We need to clean out any interrupts
 * that had already been posted to the cpu.
 */
static void
ioapic_abi_cleanup(void)
{
	bzero(mdcpu->gd_ipending, sizeof(mdcpu->gd_ipending));
}

/* Must never be called */
static void
ioapic_abi_stabilize(void)
{
	panic("ioapic_stabilize is called");
}

static void
ioapic_abi_intr_setup(int intr, int flags)
{
	const struct ioapic_irqmap *map;
	int vector, select;
	uint32_t value;
	register_t ef;

	KASSERT(intr >= 0 && intr < IOAPIC_HWI_VECTORS,
	    ("ioapic setup, invalid irq %d", intr));

	map = &ioapic_irqmaps[mycpuid][intr];
	KASSERT(IOAPIC_IMT_ISHWI(map),
	    ("ioapic setup, not hwi irq %d, type %d, cpu%d",
	     intr, map->im_type, mycpuid));
	if (map->im_type != IOAPIC_IMT_LEGACY)
		return;

	KASSERT(ioapic_irqs[intr].io_addr != NULL,
	    ("ioapic setup, no GSI information, irq %d", intr));

	ef = read_rflags();
	cpu_disable_intr();

	vector = IDT_OFFSET + intr;

	/*
	 * Now reprogram the vector in the IO APIC.  In order to avoid
	 * losing an EOI for a level interrupt, which is vector based,
	 * make sure that the IO APIC is programmed for edge-triggering
	 * first, then reprogrammed with the new vector.  This should
	 * clear the IRR bit.
	 */
	imen_lock();

	select = ioapic_irqs[intr].io_idx;
	value = ioapic_read(ioapic_irqs[intr].io_addr, select);
	value |= IOART_INTMSET;

	ioapic_write(ioapic_irqs[intr].io_addr, select,
	    (value & ~APIC_TRIGMOD_MASK));
	ioapic_write(ioapic_irqs[intr].io_addr, select,
	    (value & ~IOART_INTVEC) | vector);

	imen_unlock();

	IOAPIC_INTREN(intr);

	write_rflags(ef);
}

static void
ioapic_abi_intr_teardown(int intr)
{
	const struct ioapic_irqmap *map;
	int vector, select;
	uint32_t value;
	register_t ef;

	KASSERT(intr >= 0 && intr < IOAPIC_HWI_VECTORS,
	    ("ioapic teardown, invalid irq %d", intr));

	map = &ioapic_irqmaps[mycpuid][intr];
	KASSERT(IOAPIC_IMT_ISHWI(map),
	    ("ioapic teardown, not hwi irq %d, type %d, cpu%d",
	     intr, map->im_type, mycpuid));
	if (map->im_type != IOAPIC_IMT_LEGACY)
		return;

	KASSERT(ioapic_irqs[intr].io_addr != NULL,
	    ("ioapic teardown, no GSI information, irq %d", intr));

	ef = read_rflags();
	cpu_disable_intr();

	/*
	 * Teardown an interrupt vector.  The vector should already be
	 * installed in the cpu's IDT, but make sure.
	 */
	IOAPIC_INTRDIS(intr);

	vector = IDT_OFFSET + intr;

	/*
	 * In order to avoid losing an EOI for a level interrupt, which
	 * is vector based, make sure that the IO APIC is programmed for
	 * edge-triggering first, then reprogrammed with the new vector.
	 * This should clear the IRR bit.
	 */
	imen_lock();

	select = ioapic_irqs[intr].io_idx;
	value = ioapic_read(ioapic_irqs[intr].io_addr, select);

	ioapic_write(ioapic_irqs[intr].io_addr, select,
	    (value & ~APIC_TRIGMOD_MASK));
	ioapic_write(ioapic_irqs[intr].io_addr, select,
	    (value & ~IOART_INTVEC) | vector);

	imen_unlock();

	write_rflags(ef);
}

static void
ioapic_abi_setdefault(void)
{
	int intr;

	for (intr = 0; intr < IOAPIC_HWI_VECTORS; ++intr) {
		if (intr == IOAPIC_HWI_SYSCALL)
			continue;
		setidt_global(IDT_OFFSET + intr, ioapic_intr[intr],
		    SDT_SYSIGT, SEL_KPL, 0);
	}
}

static void
ioapic_abi_initmap(void)
{
	int cpu;

	kgetenv_int("hw.ioapic.gsi.balance", &ioapic_abi_gsi_balance);

	kgetenv_int("hw.ioapic.msi_start", &ioapic_abi_msi_start);
	ioapic_abi_msi_start &= ~0x1f;	/* MUST be 32 aligned */

	/*
	 * NOTE: ncpus is not ready yet
	 */
	for (cpu = 0; cpu < MAXCPU; ++cpu) {
		int i;

		for (i = 0; i < IOAPIC_HWI_VECTORS; ++i) {
			ioapic_irqmaps[cpu][i].im_gsi = -1;
			ioapic_irqmaps[cpu][i].im_msi_base = -1;
		}
		ioapic_irqmaps[cpu][IOAPIC_HWI_SYSCALL].im_type =
		    IOAPIC_IMT_SYSCALL;
	}
}

static int
ioapic_find_unused_irqmap(int gsi)
{
	int cpuid, i;

	cpuid = ioapic_abi_gsi_cpuid(-1, gsi);

	for (i = ISA_IRQ_CNT; i < IOAPIC_HWI_VECTORS; ++i) {
		if (i == acpi_sci_irqno())
			continue;
		if (ioapic_irqmaps[cpuid][i].im_type == IOAPIC_IMT_UNUSED)
			return i;
	}
	return -1;
}

void
ioapic_set_legacy_irqmap(int irq, int gsi, enum intr_trigger trig,
    enum intr_polarity pola)
{
	struct ioapic_irqinfo *info;
	struct ioapic_irqmap *map;
	void *ioaddr;
	int pin, cpuid;

	KKASSERT(trig == INTR_TRIGGER_EDGE || trig == INTR_TRIGGER_LEVEL);
	KKASSERT(pola == INTR_POLARITY_HIGH || pola == INTR_POLARITY_LOW);

	KKASSERT(irq >= 0);
	if (irq >= IOAPIC_HWI_VECTORS) {
		/*
		 * Some BIOSes seem to assume that all 256 IDT vectors
		 * could be used, while we limit the available IDT
		 * vectors to 192; find an unused IRQ for this GSI.
		 */
		irq = ioapic_find_unused_irqmap(gsi);
		if (irq < 0) {
			kprintf("failed to find unused irq for gsi %d, "
			    "overflow\n", gsi);
			return;
		}
	}
	KKASSERT(irq < IOAPIC_HWI_VECTORS);

	cpuid = ioapic_abi_gsi_cpuid(irq, gsi);
	map = &ioapic_irqmaps[cpuid][irq];

	if (map->im_type != IOAPIC_IMT_UNUSED) {
		/*
		 * There are so many IOAPICs, that 1:1 mapping
		 * of GSI and IRQ hits SYSCALL entry.
		 */
		irq = ioapic_find_unused_irqmap(gsi);
		if (irq < 0) {
			kprintf("failed to find unused irq for gsi %d, "
			    "conflict\n", gsi);
			return;
		}
		KKASSERT(irq < IOAPIC_HWI_VECTORS);

		cpuid = ioapic_abi_gsi_cpuid(irq, gsi);
		map = &ioapic_irqmaps[cpuid][irq];
	}

	if (irq > ioapic_abi_legacy_irq_max)
		ioapic_abi_legacy_irq_max = irq;

	KKASSERT(map->im_type == IOAPIC_IMT_UNUSED);
	map->im_type = IOAPIC_IMT_LEGACY;

	map->im_gsi = gsi;
	map->im_trig = trig;
	map->im_pola = pola;

	if (bootverbose) {
		kprintf("IOAPIC: irq %d -> gsi %d %s/%s\n",
			irq, map->im_gsi,
			intr_str_trigger(map->im_trig),
			intr_str_polarity(map->im_pola));
	}

	pin = ioapic_gsi_pin(map->im_gsi);
	ioaddr = ioapic_gsi_ioaddr(map->im_gsi);

	info = &ioapic_irqs[irq];

	imen_lock();

	info->io_addr = ioaddr;
	info->io_idx = IOAPIC_REDTBL + (2 * pin);
	info->io_flags = IOAPIC_IRQI_FLAG_MASKED;
	if (map->im_trig == INTR_TRIGGER_LEVEL)
		info->io_flags |= IOAPIC_IRQI_FLAG_LEVEL;

	ioapic_pin_setup(ioaddr, pin, IDT_OFFSET + irq,
	    map->im_trig, map->im_pola, cpuid);

	imen_unlock();
}

void
ioapic_fixup_legacy_irqmaps(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		int i;

		for (i = 0; i < ISA_IRQ_CNT; ++i) {
			struct ioapic_irqmap *map = &ioapic_irqmaps[cpu][i];

			if (map->im_type == IOAPIC_IMT_UNUSED) {
				map->im_type = IOAPIC_IMT_RESERVED;
				if (bootverbose) {
					kprintf("IOAPIC: "
					    "cpu%d irq %d reserved\n", cpu, i);
				}
			}
		}
	}

	ioapic_abi_legacy_irq_max += 1;
	if (bootverbose) {
		kprintf("IOAPIC: legacy irq max %d\n",
		    ioapic_abi_legacy_irq_max);
	}
}

static int
ioapic_abi_legacy_intr_find_bygsi(int gsi, enum intr_trigger trig,
    enum intr_polarity pola)
{
	int cpu;

#ifdef INVARIANTS
	if (trig == INTR_TRIGGER_CONFORM) {
		KKASSERT(pola == INTR_POLARITY_CONFORM);
	} else {
		KKASSERT(trig == INTR_TRIGGER_EDGE ||
		    trig == INTR_TRIGGER_LEVEL);
		KKASSERT(pola == INTR_POLARITY_HIGH ||
		    pola == INTR_POLARITY_LOW);
	}
#endif

	for (cpu = 0; cpu < ncpus; ++cpu) {
		int irq;

		for (irq = 0; irq < ioapic_abi_legacy_irq_max; ++irq) {
			const struct ioapic_irqmap *map =
			    &ioapic_irqmaps[cpu][irq];

			if (map->im_gsi == gsi) {
				KKASSERT(map->im_type == IOAPIC_IMT_LEGACY);

				if ((map->im_flags & IOAPIC_IMF_CONF) &&
				    trig != INTR_TRIGGER_CONFORM &&
				    pola != INTR_POLARITY_CONFORM) {
					if (map->im_trig != trig ||
					    map->im_pola != pola)
						return -1;
				}
				return irq;
			}
		}
	}
	return -1;
}

static int
ioapic_abi_legacy_intr_find(int irq, enum intr_trigger trig,
    enum intr_polarity pola)
{
	int cpu;

#ifdef INVARIANTS
	if (trig == INTR_TRIGGER_CONFORM) {
		KKASSERT(pola == INTR_POLARITY_CONFORM);
	} else {
		KKASSERT(trig == INTR_TRIGGER_EDGE ||
		    trig == INTR_TRIGGER_LEVEL);
		KKASSERT(pola == INTR_POLARITY_HIGH ||
		    pola == INTR_POLARITY_LOW);
	}
#endif

	if (irq < 0 || irq >= ioapic_abi_legacy_irq_max)
		return -1;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		const struct ioapic_irqmap *map = &ioapic_irqmaps[cpu][irq];

		if (map->im_type == IOAPIC_IMT_LEGACY) {
			if ((map->im_flags & IOAPIC_IMF_CONF) &&
			    trig != INTR_TRIGGER_CONFORM &&
			    pola != INTR_POLARITY_CONFORM) {
				if (map->im_trig != trig ||
				    map->im_pola != pola)
					return -1;
			}
			return irq;
		}
	}
	return -1;
}

static void
ioapic_abi_legacy_intr_config(int irq, enum intr_trigger trig,
    enum intr_polarity pola)
{
	struct ioapic_irqinfo *info;
	struct ioapic_irqmap *map = NULL;
	void *ioaddr;
	int pin, cpuid;

	KKASSERT(trig == INTR_TRIGGER_EDGE || trig == INTR_TRIGGER_LEVEL);
	KKASSERT(pola == INTR_POLARITY_HIGH || pola == INTR_POLARITY_LOW);

	KKASSERT(irq >= 0 && irq < ioapic_abi_legacy_irq_max);
	for (cpuid = 0; cpuid < ncpus; ++cpuid) {
		map = &ioapic_irqmaps[cpuid][irq];
		if (map->im_type == IOAPIC_IMT_LEGACY)
			break;
	}
	KKASSERT(cpuid < ncpus);

#ifdef notyet
	if (map->im_flags & IOAPIC_IMF_CONF) {
		if (trig != map->im_trig) {
			panic("ioapic_intr_config: trig %s -> %s",
			      intr_str_trigger(map->im_trig),
			      intr_str_trigger(trig));
		}
		if (pola != map->im_pola) {
			panic("ioapic_intr_config: pola %s -> %s",
			      intr_str_polarity(map->im_pola),
			      intr_str_polarity(pola));
		}
		return;
	}
#endif
	map->im_flags |= IOAPIC_IMF_CONF;

	if (trig == map->im_trig && pola == map->im_pola)
		return;

	if (bootverbose) {
		kprintf("IOAPIC: irq %d, gsi %d %s/%s -> %s/%s\n",
			irq, map->im_gsi,
			intr_str_trigger(map->im_trig),
			intr_str_polarity(map->im_pola),
			intr_str_trigger(trig),
			intr_str_polarity(pola));
	}
	map->im_trig = trig;
	map->im_pola = pola;

	pin = ioapic_gsi_pin(map->im_gsi);
	ioaddr = ioapic_gsi_ioaddr(map->im_gsi);

	info = &ioapic_irqs[irq];

	imen_lock();

	info->io_flags &= ~IOAPIC_IRQI_FLAG_LEVEL;
	if (map->im_trig == INTR_TRIGGER_LEVEL)
		info->io_flags |= IOAPIC_IRQI_FLAG_LEVEL;

	ioapic_pin_setup(ioaddr, pin, IDT_OFFSET + irq,
	    map->im_trig, map->im_pola, cpuid);

	imen_unlock();
}

int
ioapic_conf_legacy_extint(int irq)
{
	struct ioapic_irqinfo *info;
	struct ioapic_irqmap *map;
	void *ioaddr;
	int pin, error, vec;

	/* XXX only irq0 is allowed */
	KKASSERT(irq == 0);

	vec = IDT_OFFSET + irq;

	if (ioapic_abi_extint_irq == irq)
		return 0;
	else if (ioapic_abi_extint_irq >= 0)
		return EEXIST;

	error = icu_ioapic_extint(irq, vec);
	if (error)
		return error;

	/* ExtINT is always targeted to cpu0 */
	map = &ioapic_irqmaps[0][irq];

	KKASSERT(map->im_type == IOAPIC_IMT_RESERVED ||
		 map->im_type == IOAPIC_IMT_LEGACY);
	if (map->im_type == IOAPIC_IMT_LEGACY) {
		if (map->im_flags & IOAPIC_IMF_CONF)
			return EEXIST;
	}
	ioapic_abi_extint_irq = irq;

	map->im_type = IOAPIC_IMT_LEGACY;
	map->im_trig = INTR_TRIGGER_EDGE;
	map->im_pola = INTR_POLARITY_HIGH;
	map->im_flags = IOAPIC_IMF_CONF;

	map->im_gsi = ioapic_extpin_gsi();
	KKASSERT(map->im_gsi >= 0);

	if (bootverbose) {
		kprintf("IOAPIC: irq %d -> extint gsi %d %s/%s\n",
			irq, map->im_gsi,
			intr_str_trigger(map->im_trig),
			intr_str_polarity(map->im_pola));
	}

	pin = ioapic_gsi_pin(map->im_gsi);
	ioaddr = ioapic_gsi_ioaddr(map->im_gsi);

	info = &ioapic_irqs[irq];

	imen_lock();

	info->io_addr = ioaddr;
	info->io_idx = IOAPIC_REDTBL + (2 * pin);
	info->io_flags = IOAPIC_IRQI_FLAG_MASKED;

	ioapic_extpin_setup(ioaddr, pin, vec);

	imen_unlock();

	return 0;
}

static int
ioapic_abi_legacy_intr_cpuid(int irq)
{
	const struct ioapic_irqmap *map = NULL;
	int cpuid;

	KKASSERT(irq >= 0 && irq < ioapic_abi_legacy_irq_max);

	for (cpuid = 0; cpuid < ncpus; ++cpuid) {
		map = &ioapic_irqmaps[cpuid][irq];
		if (map->im_type == IOAPIC_IMT_LEGACY)
			return cpuid;
	}

	/* XXX some drivers tries to peek at reserved IRQs */
	for (cpuid = 0; cpuid < ncpus; ++cpuid) {
		map = &ioapic_irqmaps[cpuid][irq];
		KKASSERT(map->im_type == IOAPIC_IMT_RESERVED);
	}
	return 0;
}

static int
ioapic_abi_gsi_cpuid(int irq, int gsi)
{
	char envpath[32];
	int cpuid = -1;

	KKASSERT(gsi >= 0);

	if (irq == 0 || gsi == 0) {
		KKASSERT(irq >= 0);
		if (bootverbose) {
			kprintf("IOAPIC: irq %d, gsi %d -> cpu0 (0)\n",
			    irq, gsi);
		}
		return 0;
	}

	if (irq >= 0 && irq == acpi_sci_irqno()) {
		if (bootverbose) {
			kprintf("IOAPIC: irq %d, gsi %d -> cpu0 (sci)\n",
			    irq, gsi);
		}
		return 0;
	}

	ksnprintf(envpath, sizeof(envpath), "hw.ioapic.gsi.%d.cpu", gsi);
	kgetenv_int(envpath, &cpuid);

	if (cpuid < 0) {
		if (!ioapic_abi_gsi_balance) {
			if (irq >= 0 && bootverbose) {
				kprintf("IOAPIC: irq %d, gsi %d -> cpu0 "
				    "(fixed)\n", irq, gsi);
			}
			return 0;
		}

		cpuid = gsi % ncpus;
		if (irq >= 0 && bootverbose) {
			kprintf("IOAPIC: irq %d, gsi %d -> cpu%d (auto)\n",
			    irq, gsi, cpuid);
		}
	} else if (cpuid >= ncpus) {
		cpuid = ncpus - 1;
		if (irq >= 0 && bootverbose) {
			kprintf("IOAPIC: irq %d, gsi %d -> cpu%d (fixup)\n",
			    irq, gsi, cpuid);
		}
	} else {
		if (irq >= 0 && bootverbose) {
			kprintf("IOAPIC: irq %d, gsi %d -> cpu%d (user)\n",
			    irq, gsi, cpuid);
		}
	}
	return cpuid;
}

static void
ioapic_abi_rman_setup(struct rman *rm)
{
	int start, end, i;

	KASSERT(rm->rm_cpuid >= 0 && rm->rm_cpuid < MAXCPU,
	    ("invalid rman cpuid %d", rm->rm_cpuid));

	start = end = -1;
	for (i = 0; i < IOAPIC_HWI_VECTORS; ++i) {
		const struct ioapic_irqmap *map =
		    &ioapic_irqmaps[rm->rm_cpuid][i];

		if (start < 0) {
			if (IOAPIC_IMT_ISHWI(map))
				start = end = i;
		} else {
			if (IOAPIC_IMT_ISHWI(map)) {
				end = i;
			} else {
				KKASSERT(end >= 0);
				if (bootverbose) {
					kprintf("IOAPIC: rman cpu%d %d - %d\n",
					    rm->rm_cpuid, start, end);
				}
				if (rman_manage_region(rm, start, end)) {
					panic("rman_manage_region"
					    "(cpu%d %d - %d)", rm->rm_cpuid,
					    start, end);
				}
				start = end = -1;
			}
		}
	}
	if (start >= 0) {
		KKASSERT(end >= 0);
		if (bootverbose) {
			kprintf("IOAPIC: rman cpu%d %d - %d\n",
			    rm->rm_cpuid, start, end);
		}
		if (rman_manage_region(rm, start, end)) {
			panic("rman_manage_region(cpu%d %d - %d)",
			    rm->rm_cpuid, start, end);
		}
	}
}

static int
ioapic_abi_msi_alloc_intern(int type, const char *desc,
    int intrs[], int count, int cpuid)
{
	int i, error;

	KASSERT(cpuid >= 0 && cpuid < ncpus,
	    ("invalid cpuid %d", cpuid));

	KASSERT(count > 0 && count <= 32, ("invalid count %d", count));
	KASSERT((count & (count - 1)) == 0,
	    ("count %d is not power of 2", count));

	lwkt_gettoken(&ioapic_irqmap_tok);

	/*
	 * NOTE:
	 * Since IDT_OFFSET is 32, which is the maximum valid 'count',
	 * we do not need to find out the first properly aligned
	 * interrupt vector.
	 */

	error = EMSGSIZE;
	for (i = ioapic_abi_msi_start; i < IOAPIC_HWI_VECTORS; i += count) {
		int j;

		if (ioapic_irqmaps[cpuid][i].im_type != IOAPIC_IMT_UNUSED)
			continue;

		for (j = 1; j < count; ++j) {
			if (ioapic_irqmaps[cpuid][i + j].im_type !=
			    IOAPIC_IMT_UNUSED)
				break;
		}
		if (j != count)
			continue;

		for (j = 0; j < count; ++j) {
			struct ioapic_irqmap *map;
			int intr = i + j;

			map = &ioapic_irqmaps[cpuid][intr];
			KASSERT(map->im_msi_base < 0,
			    ("intr %d, stale %s-base %d",
			     intr, desc, map->im_msi_base));

			map->im_type = type;
			map->im_msi_base = i;

			intrs[j] = intr;
			msi_setup(intr, cpuid);

			if (bootverbose) {
				kprintf("alloc %s intr %d on cpu%d\n",
				    desc, intr, cpuid);
			}
		}
		error = 0;
		break;
	}

	lwkt_reltoken(&ioapic_irqmap_tok);

	return error;
}

static void
ioapic_abi_msi_release_intern(int type, const char *desc,
    const int intrs[], int count, int cpuid)
{
	int i, msi_base = -1, intr_next = -1, mask;

	KASSERT(cpuid >= 0 && cpuid < ncpus,
	    ("invalid cpuid %d", cpuid));

	KASSERT(count > 0 && count <= 32, ("invalid count %d", count));

	mask = count - 1;
	KASSERT((count & mask) == 0, ("count %d is not power of 2", count));

	lwkt_gettoken(&ioapic_irqmap_tok);

	for (i = 0; i < count; ++i) {
		struct ioapic_irqmap *map;
		int intr = intrs[i];

		KASSERT(intr >= 0 && intr < IOAPIC_HWI_VECTORS,
		    ("invalid intr %d", intr));

		map = &ioapic_irqmaps[cpuid][intr];
		KASSERT(map->im_type == type,
		    ("trying to release non-%s intr %d, type %d", desc,
		     intr, map->im_type));
		KASSERT(map->im_msi_base >= 0 && map->im_msi_base <= intr,
		    ("intr %d, invalid %s-base %d", intr, desc,
		     map->im_msi_base));
		KASSERT((map->im_msi_base & mask) == 0,
		    ("intr %d, %s-base %d is not properly aligned %d",
		     intr, desc, map->im_msi_base, count));

		if (msi_base < 0) {
			msi_base = map->im_msi_base;
		} else {
			KASSERT(map->im_msi_base == msi_base,
			    ("intr %d, inconsistent %s-base, "
			     "was %d, now %d",
			     intr, desc, msi_base, map->im_msi_base));
		}

		if (intr_next < intr)
			intr_next = intr;

		map->im_type = IOAPIC_IMT_UNUSED;
		map->im_msi_base = -1;

		if (bootverbose) {
			kprintf("release %s intr %d on cpu%d\n",
			    desc, intr, cpuid);
		}
	}

	KKASSERT(intr_next > 0);
	KKASSERT(msi_base >= 0);

	++intr_next;
	if (intr_next < IOAPIC_HWI_VECTORS) {
		const struct ioapic_irqmap *map =
		    &ioapic_irqmaps[cpuid][intr_next];

		if (map->im_type == type) {
			KASSERT(map->im_msi_base != msi_base,
			    ("more than %d %s was allocated", count, desc));
		}
	}

	lwkt_reltoken(&ioapic_irqmap_tok);
}

static int
ioapic_abi_msi_alloc(int intrs[], int count, int cpuid)
{
	return ioapic_abi_msi_alloc_intern(IOAPIC_IMT_MSI, "MSI",
	    intrs, count, cpuid);
}

static void
ioapic_abi_msi_release(const int intrs[], int count, int cpuid)
{
	ioapic_abi_msi_release_intern(IOAPIC_IMT_MSI, "MSI",
	    intrs, count, cpuid);
}

static int
ioapic_abi_msix_alloc(int *intr, int cpuid)
{
	return ioapic_abi_msi_alloc_intern(IOAPIC_IMT_MSIX, "MSI-X",
	    intr, 1, cpuid);
}

static void
ioapic_abi_msix_release(int intr, int cpuid)
{
	ioapic_abi_msi_release_intern(IOAPIC_IMT_MSIX, "MSI-X",
	    &intr, 1, cpuid);
}

static void
ioapic_abi_msi_map(int intr, uint64_t *addr, uint32_t *data, int cpuid)
{
	const struct ioapic_irqmap *map;

	KASSERT(cpuid >= 0 && cpuid < ncpus,
	    ("invalid cpuid %d", cpuid));

	KASSERT(intr >= 0 && intr < IOAPIC_HWI_VECTORS,
	    ("invalid intr %d", intr));

	lwkt_gettoken(&ioapic_irqmap_tok);

	map = &ioapic_irqmaps[cpuid][intr];
	KASSERT(map->im_type == IOAPIC_IMT_MSI ||
	    map->im_type == IOAPIC_IMT_MSIX,
	    ("trying to map non-MSI/MSI-X intr %d, type %d", intr, map->im_type));
	KASSERT(map->im_msi_base >= 0 && map->im_msi_base <= intr,
	    ("intr %d, invalid %s-base %d", intr,
	     map->im_type == IOAPIC_IMT_MSI ? "MSI" : "MSI-X",
	     map->im_msi_base));

	msi_map(map->im_msi_base, addr, data, cpuid);

	if (bootverbose) {
		kprintf("map %s intr %d on cpu%d\n",
		    map->im_type == IOAPIC_IMT_MSI ? "MSI" : "MSI-X",
		    intr, cpuid);
	}

	lwkt_reltoken(&ioapic_irqmap_tok);
}
