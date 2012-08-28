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

#ifndef _ARCH_APIC_IOAPIC_ABI_H_
#define _ARCH_APIC_IOAPIC_ABI_H_

#ifndef _SYS_BUS_H_
#include <sys/bus.h>
#endif

/*
 * NOTE:
 * - Keep size of ioapic_irqinfo power of 2
 * - Update IOAPIC_IRQI_SZSHIFT after changing ioapic_irqinfo size
 */
struct ioapic_irqinfo {
	uint32_t	io_flags;	/* IOAPIC_IRQI_FLAG_ */
	int		io_idx;
	volatile void	*io_addr;
};
#define IOAPIC_IRQI_SZSHIFT	4

#define IOAPIC_IRQI_FLAG_LEVEL	0x1	/* default to edge trigger */
#define IOAPIC_IRQI_FLAG_MASKED	0x2

extern struct ioapic_irqinfo	ioapic_irqs[];

extern struct machintr_abi MachIntrABI_IOAPIC;

int	ioapic_conf_legacy_extint(int);
void	ioapic_set_legacy_irqmap(int, int, enum intr_trigger,
	    enum intr_polarity);
void	ioapic_fixup_legacy_irqmaps(void);

#endif	/* !_ARCH_APIC_IOAPIC_ABI_H_ */
