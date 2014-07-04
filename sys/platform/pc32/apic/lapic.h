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
 * $FreeBSD: src/sys/i386/include/mpapic.h,v 1.14.2.2 2000/09/30 02:49:34 ps Exp $
 */

#ifndef _ARCH_APIC_LAPIC_H_
#define _ARCH_APIC_LAPIC_H_

#include <machine_base/apic/apicreg.h>

/*
 * APIC ID <-> CPU ID mapping macros
 */
#define CPUID_TO_APICID(cpu_id)		(cpu_id_to_apic_id[(cpu_id)])
#define APICID_TO_CPUID(apic_id)	(apic_id_to_cpu_id[(apic_id)])

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

struct lapic_enumerator {
	int	lapic_prio;
	TAILQ_ENTRY(lapic_enumerator) lapic_link;
	int	(*lapic_probe)(struct lapic_enumerator *);
	int	(*lapic_enumerate)(struct lapic_enumerator *);
};

#define LAPIC_ENUM_PRIO_MPTABLE		20
#define LAPIC_ENUM_PRIO_MADT		40

extern volatile lapic_t		*lapic;
extern int			cpu_id_to_apic_id[];
extern int			apic_id_to_cpu_id[];
extern int			lapic_enable;

void	apic_dump(char*);
void	lapic_init(boolean_t);
void	lapic_set_cpuid(int, int);
int	lapic_config(void);
void	lapic_enumerator_register(struct lapic_enumerator *);
void	set_apic_timer(int);
int	get_apic_timer_frequency(void);
int	read_apic_timer(void);
void	u_sleep(int);

void	lapic_map(vm_paddr_t);
int	lapic_unused_apic_id(int);
void	lapic_fixup_noioapic(void);

#ifndef _MACHINE_SMP_H_
#include <machine/smp.h>
#endif

int	apic_ipi(int, int, int);
void	selected_apic_ipi(cpumask_t, int, int);
void	single_apic_ipi(int, int, int);
int	single_apic_ipi_passive(int, int, int);

/*
 * Send an IPI INTerrupt containing 'vector' to all CPUs EXCEPT myself
 */
static __inline int
all_but_self_ipi(int vector)
{
	if (CPUMASK_ISUP(smp_active_mask))
		return 0;
	return apic_ipi(APIC_DEST_ALLESELF, vector, APIC_DELMODE_FIXED);
}

#endif /* _ARCH_APIC_LAPIC_H_ */
