/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
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
 * 3. Neither the name of the University nor the names of its contributors
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
 */
/*
 * This file contains an aggregated module marked:
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 * See the notice for details.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/bus.h>
#include <bus/isa/isavar.h>

#include <machine_base/icu/icu_var.h>
#include <machine_base/isa/isa_intr.h>

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
isa_nmi(int cd)
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
 * Fill in default interrupt table (in case of spurious interrupt
 * during configuration of kernel, setup interrupt control unit
 */
void
isa_defaultirq(void)
{
	icu_definit();
}

intrmask_t
isa_irq_pending(void)
{
	return icu_irq_pending();
}
