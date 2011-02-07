/*-
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD: src/sys/i386/include/apicvar.h,v 1.5 2003/11/14 22:21:30 peter Exp $
 * $DragonFly: src/sys/platform/pc64/apic/apicvar.h,v 1.1 2008/08/29 17:07:12 dillon Exp $
 */

#ifndef _MACHINE_APICVAR_H_
#define _MACHINE_APICVAR_H_

/*
 * Local && I/O APIC variable definitions.
 */

#define	LVT_LINT0	0
#define	LVT_LINT1	1
#define	LVT_TIMER	2
#define	LVT_ERROR	3
#define	LVT_PMC		4
#define	LVT_THERMAL	5
#define	LVT_MAX		LVT_THERMAL

#ifndef LOCORE

#define	APIC_IPI_DEST_SELF	-1
#define	APIC_IPI_DEST_ALL	-2
#define	APIC_IPI_DEST_OTHERS	-3

/*
 * An APIC enumerator is a psuedo bus driver that enumerates APIC's including
 * CPU's and I/O APIC's.
 */
struct apic_enumerator {
	const char *apic_name;
	int (*apic_probe)(void);
	int (*apic_probe_cpus)(void);
	int (*apic_setup_local)(void);
	int (*apic_setup_io)(void);
	SLIST_ENTRY(apic_enumerator) apic_next;
};

u_int	apic_irq_to_idt(u_int irq);
u_int	apic_idt_to_irq(u_int vector);
void	apic_register_enumerator(struct apic_enumerator *enumerator);
void	*ioapic_create(uintptr_t addr, int32_t id, int intbase);
int	ioapic_disable_pin(void *cookie, u_int pin);
void	ioapic_enable_mixed_mode(void);
int	ioapic_get_vector(void *cookie, u_int pin);
int	ioapic_next_logical_cluster(void);
void	ioapic_register(void *cookie);
int	ioapic_remap_vector(void *cookie, u_int pin, int vector);
int	ioapic_set_extint(void *cookie, u_int pin);
int	ioapic_set_nmi(void *cookie, u_int pin);
int	ioapic_set_polarity(void *cookie, u_int pin, char activehi);
int	ioapic_set_triggermode(void *cookie, u_int pin, char edgetrigger);
int	ioapic_set_smi(void *cookie, u_int pin);
void	lapic_create(u_int apic_id, int boot_cpu);
void	lapic_disable(void);
void	lapic_dump(const char *str);
void	lapic_enable_intr(u_int vector);
void	lapic_eoi(void);
int	lapic_id(void);
int	lapic_intr_pending(u_int vector);
void	lapic_ipi_raw(register_t icrlo, u_int dest);
void	lapic_ipi_vectored(u_int vector, int dest);
int	lapic_ipi_wait(int delay);
void	lapic_handle_intr(struct intrframe *frame);
void	lapic_set_logical_id(u_int apic_id, u_int cluster, u_int cluster_id);
int	lapic_set_lvt_mask(u_int apic_id, u_int lvt, u_char masked);
int	lapic_set_lvt_mode(u_int apic_id, u_int lvt, u_int32_t mode);
int	lapic_set_lvt_polarity(u_int apic_id, u_int lvt, u_char activehi);
int	lapic_set_lvt_triggermode(u_int apic_id, u_int lvt, u_char edgetrigger);
void	lapic_setup(void);

#endif /* !LOCORE */
#endif /* _MACHINE_APICVAR_H_ */
