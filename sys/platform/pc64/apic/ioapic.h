/*
 * Copyright (c) 1996, by Steve Passe
 * Copyright (c) 2008 The DragonFly Project.
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
 * $FreeBSD: src/sys/i386/include/mpapic.h,v 1.14.2.2 2000/09/30 02:49:34 ps Exp $
 * $DragonFly: src/sys/platform/pc64/apic/mpapic.h,v 1.1 2008/08/29 17:07:12 dillon Exp $
 */

#ifndef _ARCH_APIC_IOAPIC_H_
#define _ARCH_APIC_IOAPIC_H_

#ifndef _SYS_BUS_H_
#include <sys/bus.h>
#endif

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

u_int	ioapic_read(volatile void *, int);
void	ioapic_write(volatile void *, int, u_int);

struct ioapic_enumerator {
	int	ioapic_prio;
	TAILQ_ENTRY(ioapic_enumerator) ioapic_link;
	int	(*ioapic_probe)(struct ioapic_enumerator *);
	void	(*ioapic_enumerate)(struct ioapic_enumerator *);
};

#define IOAPIC_ENUM_PRIO_MPTABLE	20
#define IOAPIC_ENUM_PRIO_MADT		40

void	ioapic_enumerator_register(struct ioapic_enumerator *);
void	ioapic_add(void *, int, int);
void	ioapic_intsrc(int, int, enum intr_trigger, enum intr_polarity);
void	*ioapic_gsi_ioaddr(int);
int	ioapic_gsi_pin(int);
void	ioapic_pin_setup(void *, int, int,
	    enum intr_trigger, enum intr_polarity, int);
void	ioapic_extpin_setup(void *, int, int);
int	ioapic_extpin_gsi(void);
int	ioapic_gsi(int, int);
void	*ioapic_map(vm_paddr_t);

extern int	ioapic_enable;

#endif	/* !_ARCH_APIC_IOAPIC_H_ */
