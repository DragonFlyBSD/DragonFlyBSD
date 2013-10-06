/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/i386/include/smp.h,v 1.50.2.5 2001/02/13 22:32:45 tegge Exp $
 */

#ifndef _MACHINE_SMP_H_
#define _MACHINE_SMP_H_

#ifdef _KERNEL

#ifndef LOCORE

/* XXX wrong header */
void	initializecpu(int cpu);

#endif	/* LOCORE */

#ifndef LOCORE

/*
 * For sending values to POST displays.
 * XXX FIXME: where does this really belong, isa.h/isa.c perhaps?
 */
extern int current_postcode;  /** XXX currently in mp_machdep.c */
#define POSTCODE(X)	current_postcode = (X), \
			outb(0x80, current_postcode)
#define POSTCODE_LO(X)	current_postcode &= 0xf0, \
			current_postcode |= ((X) & 0x0f), \
			outb(0x80, current_postcode)
#define POSTCODE_HI(X)	current_postcode &= 0x0f, \
			current_postcode |= (((X) << 4) & 0xf0), \
			outb(0x80, current_postcode)


#include <machine_base/apic/apicreg.h>
#include <machine/pcb.h>

/* global symbols in mpboot.S */
extern char			mptramp_start[];
extern char			mptramp_end[];
extern u_int32_t		mptramp_pagetables;

/* functions in mpboot.s */
void	bootMP			(void);

/* global data in apic_vector.s */
extern volatile cpumask_t	stopped_cpus;
extern volatile cpumask_t	started_cpus;

extern void (*cpustop_restartfunc) (void);

extern struct pcb		stoppcbs[];

/* functions in mp_machdep.c */
u_int	mp_bootaddress		(u_int);
void	mp_start		(void);
void	mp_announce		(void);
void	init_secondary		(void);
int	stop_cpus		(cpumask_t);
void	ap_init			(void);
int	restart_cpus		(cpumask_t);
void	forward_signal		(struct proc *);

#if defined(READY)
void	clr_io_apic_mask24	(int, u_int32_t);
void	set_io_apic_mask24	(int, u_int32_t);
#endif /* READY */

void	cpu_send_ipiq		(int);
int	cpu_send_ipiq_passive	(int);

/* global data in init_smp.c */
extern cpumask_t		smp_active_mask;

/* Detect CPU topology bits */
void detect_cpu_topology(void);

/* Interface functions for IDs calculation */
int get_chip_ID(int cpuid);
int get_core_number_within_chip(int cpuid);
int get_logical_CPU_number_within_core(int cpuid);

#include <machine_base/apic/lapic.h>
static __inline
int get_apicid_from_cpuid(int cpuid) {
	return CPUID_TO_APICID(cpuid);
}
static __inline
int get_cpuid_from_apicid(int apicid) {
	return APICID_TO_CPUID(apicid);
}

#endif /* !LOCORE */

#endif /* _KERNEL */
#endif /* _MACHINE_SMP_H_ */
