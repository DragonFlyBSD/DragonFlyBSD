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
 * $DragonFly: src/sys/i386/isa/Attic/intr_machdep.c,v 1.38 2005/11/02 20:23:22 dillon Exp $
 */
/*
 * This file contains an aggregated module marked:
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 * See the notice for details.
 */

#include "use_isa.h"
#include "opt_auto_eoi.h"

#include <sys/param.h>
#ifndef SMP
#include <machine/lock.h>
#endif
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/interrupt.h>
#include <machine/ipl.h>
#include <machine/md_var.h>
#include <machine/segments.h>
#include <sys/bus.h> 
#include <machine/globaldata.h>
#include <sys/proc.h>
#include <sys/thread2.h>
#include <sys/machintr.h>

#include <machine/smptests.h>
#include <machine/smp.h>
#include <bus/isa/i386/isa.h>
#include <i386/icu/icu.h>

#if NISA > 0
#include <bus/isa/isavar.h>
#endif
#include <i386/isa/intr_machdep.h>
#include <bus/isa/isavar.h>
#include <sys/interrupt.h>
#ifdef APIC_IO
#include <machine/clock.h>
#endif
#include <machine/cpu.h>

/* XXX should be in suitable include files */
#define	ICU_IMR_OFFSET		1		/* IO_ICU{1,2} + 1 */

#ifdef APIC_IO
/*
 * This is to accommodate "mixed-mode" programming for 
 * motherboards that don't connect the 8254 to the IO APIC.
 */
#define	AUTO_EOI_1	1
#endif

#define	NR_INTRNAMES	(1 + ICU_LEN + 2 * ICU_LEN)

static void	init_i8259(void);

#define NMI_PARITY (1 << 7)
#define NMI_IOCHAN (1 << 6)
#define ENMI_WATCHDOG (1 << 7)
#define ENMI_BUSTIMER (1 << 6)
#define ENMI_IOSTATUS (1 << 5)

/*
 * Handle a NMI, possibly a machine check.
 * return true to panic system, false to ignore.
 */
int
isa_nmi(cd)
	int cd;
{
	int retval = 0;
	int isa_port = inb(0x61);
	int eisa_port = inb(0x461);

	log(LOG_CRIT, "NMI ISA %x, EISA %x\n", isa_port, eisa_port);
	
	if (isa_port & NMI_PARITY) {
		log(LOG_CRIT, "RAM parity error, likely hardware failure.");
		retval = 1;
	}

	if (isa_port & NMI_IOCHAN) {
		log(LOG_CRIT, "I/O channel check, likely hardware failure.");
		retval = 1;
	}

	/*
	 * On a real EISA machine, this will never happen.  However it can
	 * happen on ISA machines which implement XT style floating point
	 * error handling (very rare).  Save them from a meaningless panic.
	 */
	if (eisa_port == 0xff)
		return(retval);

	if (eisa_port & ENMI_WATCHDOG) {
		log(LOG_CRIT, "EISA watchdog timer expired, likely hardware failure.");
		retval = 1;
	}

	if (eisa_port & ENMI_BUSTIMER) {
		log(LOG_CRIT, "EISA bus timeout, likely hardware failure.");
		retval = 1;
	}

	if (eisa_port & ENMI_IOSTATUS) {
		log(LOG_CRIT, "EISA I/O port status error.");
		retval = 1;
	}
	return(retval);
}

/*
 *  ICU reinitialize when ICU configuration has lost.
 */
void
icu_reinit()
{
	int i;

	init_i8259();
	for (i = 0; i < ICU_LEN; ++i) {
		if (count_registered_ints(i))
			machintr_intren(i);
	}
}

/*
 * Fill in default interrupt table (in case of spurious interrupt
 * during configuration of kernel, setup interrupt control unit
 */
void
isa_defaultirq()
{
	int i;

	/* icu vectors */
	for (i = 0; i < ICU_LEN; i++)
		machintr_vector_teardown(i);
	init_i8259();
}

static void
init_i8259(void)
{

	/* initialize 8259's */
	outb(IO_ICU1, 0x11);		/* reset; program device, four bytes */
	outb(IO_ICU1+ICU_IMR_OFFSET, NRSVIDT);	/* starting at this vector index */
	outb(IO_ICU1+ICU_IMR_OFFSET, 1 << ICU_IRQ_SLAVE); /* slave on line 7 */
#ifdef AUTO_EOI_1
	outb(IO_ICU1+ICU_IMR_OFFSET, 2 | 1);		/* auto EOI, 8086 mode */
#else
	outb(IO_ICU1+ICU_IMR_OFFSET, 1);		/* 8086 mode */
#endif
	outb(IO_ICU1+ICU_IMR_OFFSET, 0xff);		/* leave interrupts masked */
	outb(IO_ICU1, 0x0a);		/* default to IRR on read */
	outb(IO_ICU1, 0xc0 | (3 - 1));	/* pri order 3-7, 0-2 (com2 first) */
	outb(IO_ICU2, 0x11);		/* reset; program device, four bytes */
	outb(IO_ICU2+ICU_IMR_OFFSET, NRSVIDT+8); /* staring at this vector index */
	outb(IO_ICU2+ICU_IMR_OFFSET, ICU_IRQ_SLAVE);
#ifdef AUTO_EOI_2
	outb(IO_ICU2+ICU_IMR_OFFSET, 2 | 1);		/* auto EOI, 8086 mode */
#else
	outb(IO_ICU2+ICU_IMR_OFFSET,1);		/* 8086 mode */
#endif
	outb(IO_ICU2+ICU_IMR_OFFSET, 0xff);          /* leave interrupts masked */
	outb(IO_ICU2, 0x0a);		/* default to IRR on read */
}

#if NISA > 0
/*
 * Return a bitmap of the current interrupt requests.  This is 8259-specific
 * and is only suitable for use at probe time.
 */
intrmask_t
isa_irq_pending(void)
{
	u_char irr1;
	u_char irr2;

	irr1 = inb(IO_ICU1);
	irr2 = inb(IO_ICU2);
	return ((irr2 << 8) | irr1);
}
#endif

/* The following notice applies beyond this point in the file */

/*
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/isa/intr_machdep.c,v 1.29.2.5 2001/10/14 06:54:27 luigi Exp $
 *
 */

/*
 * Create and activate an interrupt handler descriptor data structure.
 *
 * The dev_instance pointer is required for resource management, and will
 * only be passed through to resource_claim().
 *
 * There will be functions that derive a driver and unit name from a
 * dev_instance variable, and those functions will be used to maintain the
 * interrupt counter label array referenced by systat and vmstat to report
 * device interrupt rates (->update_intrlabels).
 *
 * Add the interrupt handler descriptor data structure created by an
 * earlier call of create_intr() to the linked list for its irq.
 *
 * WARNING: This is an internal function and not to be used by device
 * drivers.  It is subject to change without notice.
 */

void *
inthand_add(const char *name, int irq, inthand2_t handler, void *arg,
	     int flags, lwkt_serialize_t serializer)
{
	int errcode = 0;
	void *id;

	if ((unsigned)irq >= ICU_LEN) {
		printf("create_intr: requested irq%d too high, limit is %d\n",
		       irq, ICU_LEN -1);
		return (NULL);
	}
	/*
	 * Register the interrupt, then setup the ICU
	 */
	id = register_int(irq, handler, arg, name, serializer, flags);

	if (id == NULL) {
		printf("Unable to install handler for %s\n", name);
		printf("\tdevice combination not supported on irq %d\n", irq);
		return(NULL);
	}

	crit_enter();
	if (count_registered_ints(irq) == 1) {
		if (machintr_vector_setup(irq, flags))
			errcode = -1;
	}
	crit_exit();

	/*
	 * Cleanup
	 */
	if (errcode != 0) {
		if (bootverbose) {
			printf("\tinthand_add(irq%d) failed, result=%d\n", 
			       irq, errcode);
		}
		unregister_int(id);
		id = NULL;
	}
	return (id);
}

/*
 * Deactivate and remove the interrupt handler descriptor data connected
 * created by an earlier call of intr_connect() from the linked list.
 *
 * Return the memory held by the interrupt handler descriptor data structure
 * to the system. Make sure, the handler is not actively used anymore, before.
 */
int
inthand_remove(void *id)
{
	int irq;

	if (id == NULL)
		return(-1);

	crit_enter();
	irq = get_registered_intr(id);
	if (unregister_int(id) == 0) {
		machintr_vector_teardown(irq);
	}
	crit_exit();
	return (0);
}

#ifdef SMP
/*
 * forward_fast_remote()
 *
 *	This function is called from the receiving end of an IPIQ when a
 *	remote cpu wishes to forward a fast interrupt to us.  All we have to
 *	do is set the interrupt pending and let the IPI's doreti deal with it.
 */
void
forward_fastint_remote(void *arg)
{
    int irq = (int)arg;
    struct mdglobaldata *gd = mdcpu;

    atomic_set_int_nonlocked(&gd->gd_fpending, 1 << irq);
    atomic_set_int_nonlocked(&gd->mi.gd_reqflags, RQF_INTPEND);
}

#endif
