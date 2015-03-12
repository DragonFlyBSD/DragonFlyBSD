/*
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * $FreeBSD: src/sys/i386/i386/mpapic.c,v 1.37.2.7 2003/01/25 02:31:47 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/machintr.h>
#include <sys/thread2.h>

#include <machine/pmap.h>
#include <machine_base/isa/isa_intr.h>
#include <machine_base/icu/icu_var.h>
#include <machine_base/apic/lapic.h>
#include <machine_base/apic/ioapic.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine_base/apic/apicvar.h>

#define IOAPIC_COUNT_MAX	16
#define IOAPIC_ID_MASK		(IOAPIC_COUNT_MAX - 1)

struct ioapic_info {
	int		io_idx;
	int		io_apic_id;
	void		*io_addr;
	int		io_npin;
	int		io_gsi_base;

	TAILQ_ENTRY(ioapic_info) io_link;
};
TAILQ_HEAD(ioapic_info_list, ioapic_info);

struct ioapic_intsrc {
	int		int_gsi;
	enum intr_trigger int_trig;
	enum intr_polarity int_pola;
};

struct ioapic_conf {
	struct ioapic_info_list ioc_list;
	struct ioapic_intsrc ioc_intsrc[ISA_IRQ_CNT];
};

static int	ioapic_config(void);
static void	ioapic_setup(const struct ioapic_info *);
static int	ioapic_alloc_apic_id(int);
static void	ioapic_set_apic_id(const struct ioapic_info *);
static void	ioapic_gsi_setup(int);
static const struct ioapic_info *
		ioapic_gsi_search(int);
static void	ioapic_pin_prog(void *, int, int,
		    enum intr_trigger, enum intr_polarity, uint32_t, int);

static struct ioapic_conf	ioapic_conf;

static TAILQ_HEAD(, ioapic_enumerator) ioapic_enumerators =
	TAILQ_HEAD_INITIALIZER(ioapic_enumerators);

int		ioapic_enable = -1; /* I/O APIC auto-enable mode */

static int
ioapic_config(void)
{
	struct ioapic_enumerator *e;
	struct ioapic_info *info;
	int start_apic_id = 0;
	int error, i, probe;
	register_t ef = 0;

	TAILQ_INIT(&ioapic_conf.ioc_list);
	for (i = 0; i < ISA_IRQ_CNT; ++i)
		ioapic_conf.ioc_intsrc[i].int_gsi = -1;

	probe = 1;
	TUNABLE_INT_FETCH("hw.ioapic_probe", &probe);
	if (!probe) {
		kprintf("IOAPIC: warning I/O APIC will not be probed\n");
		return ENXIO;
	}

	TAILQ_FOREACH(e, &ioapic_enumerators, ioapic_link) {
		error = e->ioapic_probe(e);
		if (!error)
			break;
	}
	if (e == NULL) {
		kprintf("IOAPIC: can't find I/O APIC\n");
		return ENXIO;
	}

	crit_enter();

	ef = read_rflags();
	cpu_disable_intr();

	/*
	 * Switch to I/O APIC MachIntrABI and reconfigure
	 * the default IDT entries.
	 */
	MachIntrABI = MachIntrABI_IOAPIC;
	MachIntrABI.setdefault();

	e->ioapic_enumerate(e);

	/*
	 * Setup index
	 */
	i = 0;
	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link)
		info->io_idx = i++;

	if (i > IOAPIC_COUNT_MAX)
		panic("ioapic_config: more than 16 I/O APIC");

	/*
	 * Setup APIC ID
	 */
	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		int apic_id;

		apic_id = ioapic_alloc_apic_id(start_apic_id);
		if (apic_id == NAPICID) {
			kprintf("IOAPIC: can't alloc APIC ID for "
				"%dth I/O APIC\n", info->io_idx);
			break;
		}
		info->io_apic_id = apic_id;

		start_apic_id = apic_id + 1;
	}
	if (info != NULL) {
		/*
		 * xAPIC allows I/O APIC's APIC ID to be same
		 * as the LAPIC's APIC ID
		 */
		kprintf("IOAPIC: use xAPIC model to alloc APIC ID "
			"for I/O APIC\n");

		TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link)
			info->io_apic_id = info->io_idx;
	}

	/*
	 * Warning about any GSI holes
	 */
	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		const struct ioapic_info *prev_info;

		prev_info = TAILQ_PREV(info, ioapic_info_list, io_link);
		if (prev_info != NULL) {
			if (info->io_gsi_base !=
			prev_info->io_gsi_base + prev_info->io_npin) {
				kprintf("IOAPIC: warning gsi hole "
					"[%d, %d]\n",
					prev_info->io_gsi_base +
					prev_info->io_npin,
					info->io_gsi_base - 1);
			}
		}
	}

	if (bootverbose) {
		TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
			kprintf("IOAPIC: idx %d, apic id %d, "
				"gsi base %d, npin %d\n",
				info->io_idx,
				info->io_apic_id,
				info->io_gsi_base,
				info->io_npin);
		}
	}

	/*
	 * Setup all I/O APIC
	 */
	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link)
		ioapic_setup(info);
	ioapic_fixup_legacy_irqmaps();

	write_rflags(ef);

	MachIntrABI.cleanup();

	crit_exit();

	return 0;
}

void
ioapic_enumerator_register(struct ioapic_enumerator *ne)
{
	struct ioapic_enumerator *e;

	TAILQ_FOREACH(e, &ioapic_enumerators, ioapic_link) {
		if (e->ioapic_prio < ne->ioapic_prio) {
			TAILQ_INSERT_BEFORE(e, ne, ioapic_link);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&ioapic_enumerators, ne, ioapic_link);
}

void
ioapic_add(void *addr, int gsi_base, int npin)
{
	struct ioapic_info *info, *ninfo;
	int gsi_end;

	gsi_end = gsi_base + npin - 1;
	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		if ((gsi_base >= info->io_gsi_base &&
		     gsi_base < info->io_gsi_base + info->io_npin) ||
		    (gsi_end >= info->io_gsi_base &&
		     gsi_end < info->io_gsi_base + info->io_npin)) {
			panic("ioapic_add: overlapped gsi, base %d npin %d, "
			      "hit base %d, npin %d\n", gsi_base, npin,
			      info->io_gsi_base, info->io_npin);
		}
		if (info->io_addr == addr)
			panic("ioapic_add: duplicated addr %p", addr);
	}

	ninfo = kmalloc(sizeof(*ninfo), M_DEVBUF, M_WAITOK | M_ZERO);
	ninfo->io_addr = addr;
	ninfo->io_npin = npin;
	ninfo->io_gsi_base = gsi_base;
	ninfo->io_apic_id = -1;

	/*
	 * Create IOAPIC list in ascending order of GSI base
	 */
	TAILQ_FOREACH_REVERSE(info, &ioapic_conf.ioc_list,
	    ioapic_info_list, io_link) {
		if (ninfo->io_gsi_base > info->io_gsi_base) {
			TAILQ_INSERT_AFTER(&ioapic_conf.ioc_list,
			    info, ninfo, io_link);
			break;
		}
	}
	if (info == NULL)
		TAILQ_INSERT_HEAD(&ioapic_conf.ioc_list, ninfo, io_link);
}

void
ioapic_intsrc(int irq, int gsi, enum intr_trigger trig, enum intr_polarity pola)
{
	struct ioapic_intsrc *int_src;

	KKASSERT(irq < ISA_IRQ_CNT);
	int_src = &ioapic_conf.ioc_intsrc[irq];

	if (gsi == 0) {
		/* Don't allow mixed mode */
		kprintf("IOAPIC: warning intsrc irq %d -> gsi 0\n", irq);
		return;
	}

	if (int_src->int_gsi != -1) {
		if (int_src->int_gsi != gsi) {
			kprintf("IOAPIC: warning intsrc irq %d, gsi "
				"%d -> %d\n", irq, int_src->int_gsi, gsi);
		}
		if (int_src->int_trig != trig) {
			kprintf("IOAPIC: warning intsrc irq %d, trig "
				"%s -> %s\n", irq,
				intr_str_trigger(int_src->int_trig),
				intr_str_trigger(trig));
		}
		if (int_src->int_pola != pola) {
			kprintf("IOAPIC: warning intsrc irq %d, pola "
				"%s -> %s\n", irq,
				intr_str_polarity(int_src->int_pola),
				intr_str_polarity(pola));
		}
	}
	int_src->int_gsi = gsi;
	int_src->int_trig = trig;
	int_src->int_pola = pola;
}

static void
ioapic_set_apic_id(const struct ioapic_info *info)
{
	uint32_t id;
	int apic_id;

	id = ioapic_read(info->io_addr, IOAPIC_ID);

	id &= ~APIC_ID_MASK;
	id |= (info->io_apic_id << 24);

	ioapic_write(info->io_addr, IOAPIC_ID, id);

	/*
	 * Re-read && test
	 */
	id = ioapic_read(info->io_addr, IOAPIC_ID);
	apic_id = (id & APIC_ID_MASK) >> 24;

	/*
	 * I/O APIC ID is a 4bits field
	 */
	if ((apic_id & IOAPIC_ID_MASK) !=
	    (info->io_apic_id & IOAPIC_ID_MASK)) {
		panic("ioapic_set_apic_id: can't set apic id to %d, "
		      "currently set to %d\n", info->io_apic_id, apic_id);
	}
}

static void
ioapic_gsi_setup(int gsi)
{
	enum intr_trigger trig;
	enum intr_polarity pola;
	int irq;

	if (gsi == 0) {
		/* ExtINT */
		imen_lock();
		ioapic_extpin_setup(ioapic_gsi_ioaddr(gsi),
		    ioapic_gsi_pin(gsi), 0);
		imen_unlock();
		return;
	}

	trig = 0;	/* silence older gcc's */
	pola = 0;	/* silence older gcc's */

	for (irq = 0; irq < ISA_IRQ_CNT; ++irq) {
		const struct ioapic_intsrc *int_src =
		    &ioapic_conf.ioc_intsrc[irq];

		if (gsi == int_src->int_gsi) {
			trig = int_src->int_trig;
			pola = int_src->int_pola;
			break;
		}
	}

	if (irq == ISA_IRQ_CNT) {
		/*
		 * No explicit IRQ to GSI mapping;
		 * use the default 1:1 mapping
		 */
		irq = gsi;
		if (irq < ISA_IRQ_CNT) {
			if (ioapic_conf.ioc_intsrc[irq].int_gsi >= 0) {
				/*
				 * This IRQ is mapped to different GSI,
				 * don't do the default configuration.
				 * The configuration of the target GSI
				 * will finally setup this IRQ.
				 *
				 * This GSI is not used, disable it.
				 */
				imen_lock();
				ioapic_pin_setup(ioapic_gsi_ioaddr(gsi),
				    ioapic_gsi_pin(gsi), 0,
				    INTR_TRIGGER_EDGE, INTR_POLARITY_HIGH, 0);
				imen_unlock();
				return;
			}
			trig = INTR_TRIGGER_EDGE;
			pola = INTR_POLARITY_HIGH;
		} else {
			trig = INTR_TRIGGER_LEVEL;
			pola = INTR_POLARITY_LOW;
		}
	}

	ioapic_set_legacy_irqmap(irq, gsi, trig, pola);
}

void *
ioapic_gsi_ioaddr(int gsi)
{
	const struct ioapic_info *info;

	info = ioapic_gsi_search(gsi);
	return info->io_addr;
}

int
ioapic_gsi_pin(int gsi)
{
	const struct ioapic_info *info;

	info = ioapic_gsi_search(gsi);
	return gsi - info->io_gsi_base;
}

static const struct ioapic_info *
ioapic_gsi_search(int gsi)
{
	const struct ioapic_info *info;

	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		if (gsi >= info->io_gsi_base &&
		    gsi < info->io_gsi_base + info->io_npin)
			return info;
	}
	panic("ioapic_gsi_search: no I/O APIC");
}

int
ioapic_gsi(int idx, int pin)
{
	const struct ioapic_info *info;

	TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
		if (info->io_idx == idx)
			break;
	}
	if (info == NULL)
		return -1;
	if (pin >= info->io_npin)
		return -1;
	return info->io_gsi_base + pin;
}

void
ioapic_extpin_setup(void *addr, int pin, int vec)
{
	ioapic_pin_prog(addr, pin, vec,
	    INTR_TRIGGER_CONFORM, INTR_POLARITY_CONFORM, IOART_DELEXINT, 0);
}

int
ioapic_extpin_gsi(void)
{
	return 0;
}

void
ioapic_pin_setup(void *addr, int pin, int vec,
    enum intr_trigger trig, enum intr_polarity pola, int cpuid)
{
	/*
	 * Always clear an I/O APIC pin before [re]programming it.  This is
	 * particularly important if the pin is set up for a level interrupt
	 * as the IOART_REM_IRR bit might be set.   When we reprogram the
	 * vector any EOI from pending ints on this pin could be lost and
	 * IRR might never get reset.
	 *
	 * To fix this problem, clear the vector and make sure it is 
	 * programmed as an edge interrupt.  This should theoretically
	 * clear IRR so we can later, safely program it as a level 
	 * interrupt.
	 */
	ioapic_pin_prog(addr, pin, vec, INTR_TRIGGER_EDGE, INTR_POLARITY_HIGH,
	    IOART_DELFIXED, cpuid);
	ioapic_pin_prog(addr, pin, vec, trig, pola, IOART_DELFIXED, cpuid);
}

static void
ioapic_pin_prog(void *addr, int pin, int vec,
    enum intr_trigger trig, enum intr_polarity pola,
    uint32_t del_mode, int cpuid)
{
	uint32_t flags, target;
	int select;

	KKASSERT(del_mode == IOART_DELEXINT || del_mode == IOART_DELFIXED);

	select = IOAPIC_REDTBL0 + (2 * pin);

	flags = ioapic_read(addr, select) & IOART_RESV;
	flags |= IOART_INTMSET | IOART_DESTPHY;
#ifdef foo
	flags |= del_mode;
#else
	/*
	 * We only support limited I/O APIC mixed mode,
	 * so even for ExtINT, we still use "fixed"
	 * delivery mode.
	 */
	flags |= IOART_DELFIXED;
#endif

	if (del_mode == IOART_DELEXINT) {
		KKASSERT(trig == INTR_TRIGGER_CONFORM &&
			 pola == INTR_POLARITY_CONFORM);
		flags |= IOART_TRGREDG | IOART_INTAHI;
	} else {
		switch (trig) {
		case INTR_TRIGGER_EDGE:
			flags |= IOART_TRGREDG;
			break;

		case INTR_TRIGGER_LEVEL:
			flags |= IOART_TRGRLVL;
			break;

		case INTR_TRIGGER_CONFORM:
			panic("ioapic_pin_prog: trig conform is not "
			      "supported\n");
		}
		switch (pola) {
		case INTR_POLARITY_HIGH:
			flags |= IOART_INTAHI;
			break;

		case INTR_POLARITY_LOW:
			flags |= IOART_INTALO;
			break;

		case INTR_POLARITY_CONFORM:
			panic("ioapic_pin_prog: pola conform is not "
			      "supported\n");
		}
	}

	target = ioapic_read(addr, select + 1) & IOART_HI_DEST_RESV;
	target |= (CPUID_TO_APICID(cpuid) << IOART_HI_DEST_SHIFT) &
		  IOART_HI_DEST_MASK;

	ioapic_write(addr, select, flags | vec);
	ioapic_write(addr, select + 1, target);
}

static void
ioapic_setup(const struct ioapic_info *info)
{
	int i;

	ioapic_set_apic_id(info);

	for (i = 0; i < info->io_npin; ++i)
		ioapic_gsi_setup(info->io_gsi_base + i);
}

static int
ioapic_alloc_apic_id(int start)
{
	for (;;) {
		const struct ioapic_info *info;
		int apic_id, apic_id16;

		apic_id = lapic_unused_apic_id(start);
		if (apic_id == NAPICID) {
			kprintf("IOAPIC: can't find unused APIC ID\n");
			return apic_id;
		}
		apic_id16 = apic_id & IOAPIC_ID_MASK;

		/*
		 * Check against other I/O APIC's APIC ID's lower 4bits.
		 *
		 * The new APIC ID will have to be different from others
		 * in the lower 4bits, no matter whether xAPIC is used
		 * or not.
		 */
		TAILQ_FOREACH(info, &ioapic_conf.ioc_list, io_link) {
			if (info->io_apic_id == -1) {
				info = NULL;
				break;
			}
			if ((info->io_apic_id & IOAPIC_ID_MASK) == apic_id16)
				break;
		}
		if (info == NULL)
			return apic_id;

		kprintf("IOAPIC: APIC ID %d has same lower 4bits as "
			"%dth I/O APIC, keep searching...\n",
			apic_id, info->io_idx);

		start = apic_id + 1;
	}
	panic("ioapic_unused_apic_id: never reached");
}

/*
 * Map a physical memory address representing I/O into KVA.  The I/O
 * block is assumed not to cross a page boundary.
 */
void *
ioapic_map(vm_paddr_t pa)
{
	KKASSERT(pa < 0x100000000LL);

	return pmap_mapdev_uncacheable(pa, PAGE_SIZE);
}

static void
ioapic_sysinit(void *dummy __unused)
{
	int error;

	if (!ioapic_enable)
		return;

	KASSERT(lapic_enable, ("I/O APIC is enabled, but LAPIC is disabled"));
	error = ioapic_config();
	if (error) {
		ioapic_enable = 0;
		icu_reinit_noioapic();
		lapic_fixup_noioapic();
	}
}
SYSINIT(ioapic, SI_BOOT2_IOAPIC, SI_ORDER_FIRST, ioapic_sysinit, NULL);
