/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)isa.c	7.2 (Berkeley) 5/13/91
 * $FreeBSD: src/sys/i386/isa/intr_machdep.c,v 1.29.2.5 2001/10/14 06:54:27 luigi Exp $
 * $DragonFly: src/sys/platform/pc32/isa/intr_machdep.c,v 1.48 2008/08/02 01:14:43 dillon Exp $
 */
/*
 * This file contains an aggregated module marked:
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 * See the notice for details.
 */

#include "opt_auto_eoi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>
#include <bus/isa/isareg.h>
#include <cpu/cpufunc.h>
#include <machine/smp.h>
#include <machine/intr_machdep.h>
#include <machine_base/icu/icu.h>
#include <machine_base/icu/icu_var.h>

static void	icu_init(void);

static void
icu_init(void)
{
#ifdef AUTO_EOI_1
	int auto_eoi = 2;		/* auto EOI, 8086 mode */
#else
	int auto_eoi = 0;		/* 8086 mode */
#endif

#ifdef SMP
	if (apic_io_enable)
		auto_eoi = 2;		/* auto EOI, 8086 mode */
#endif

	/*
	 * Program master
	 */
	outb(IO_ICU1, 0x11);		/* reset; program device, four bytes */
	outb(IO_ICU1 + ICU_IMR_OFFSET, IDT_OFFSET);
					/* starting at this vector index */
	outb(IO_ICU1 + ICU_IMR_OFFSET, 1 << ICU_IRQ_SLAVE);
					/* slave on line 2 */
	outb(IO_ICU1 + ICU_IMR_OFFSET, auto_eoi | 1); /* 8086 mode */

	outb(IO_ICU1 + ICU_IMR_OFFSET, 0xff); /* leave interrupts masked */
	outb(IO_ICU1, 0x0a);		/* default to IRR on read */
	outb(IO_ICU1, 0xc0 | (3 - 1));	/* pri order 3-7, 0-2 (com2 first) */

	/*
	 * Program slave
	 */
	outb(IO_ICU2, 0x11);		/* reset; program device, four bytes */
	outb(IO_ICU2 + ICU_IMR_OFFSET, IDT_OFFSET + 8);
					/* staring at this vector index */
	outb(IO_ICU2 + ICU_IMR_OFFSET, ICU_IRQ_SLAVE);
#ifdef AUTO_EOI_2
	outb(IO_ICU2 + ICU_IMR_OFFSET, 2 | 1); /* auto EOI, 8086 mode */
#else
	outb(IO_ICU2 + ICU_IMR_OFFSET, 1); /* 8086 mode */
#endif

	outb(IO_ICU2 + ICU_IMR_OFFSET, 0xff); /* leave interrupts masked */
	outb(IO_ICU2, 0x0a);		/* default to IRR on read */
}

void
icu_definit(void)
{
	u_long ef;

	KKASSERT(MachIntrABI.type == MACHINTR_ICU);

	ef = read_eflags();
	cpu_disable_intr();

	/* Leave interrupts masked */
	outb(IO_ICU1 + ICU_IMR_OFFSET, 0xff);
	outb(IO_ICU2 + ICU_IMR_OFFSET, 0xff);

	MachIntrABI.setdefault();
	icu_init();

	write_eflags(ef);
}

/*
 *  ICU reinitialize when ICU configuration has lost.
 */
void
icu_reinit(void)
{
	int i;

	icu_init();
	for (i = 0; i < MAX_HARDINTS; ++i) {
		if (count_registered_ints(i))
			machintr_intren(i);
	}
}

/*
 * Return a bitmap of the current interrupt requests.  This is 8259-specific
 * and is only suitable for use at probe time.
 */
intrmask_t
icu_irq_pending(void)
{
	u_char irr1;
	u_char irr2;

	irr1 = inb(IO_ICU1);
	irr2 = inb(IO_ICU2);
	return ((irr2 << 8) | irr1);
}

int
icu_ioapic_extint(int irq, int vec)
{
	uint8_t mask;

	/*
	 * Only first 8 interrupt is supported.
	 * Don't allow setup for the slave link.
	 */
	if (irq >= 8 || irq == 2)
		return EOPNOTSUPP;

	mask = ~(1 << irq);

	/*
	 * Re-initialize master 8259:
	 *   reset; prog 4 bytes, single ICU, edge triggered
	 */
	outb(IO_ICU1, 0x13);
	outb(IO_ICU1 + 1, vec);		/* start vector (unused) */
	outb(IO_ICU1 + 1, 0x00);	/* ignore slave */
	outb(IO_ICU1 + 1, 0x03);	/* auto EOI, 8086 */
	outb(IO_ICU1 + 1, mask);

	return 0;
}
