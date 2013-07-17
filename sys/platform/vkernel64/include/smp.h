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

/* global data in apic_vector.s */
extern volatile cpumask_t	stopped_cpus;
extern int			optcpus;	/* from main() */
extern int			vkernel_b_arg;	/* arg from main() */
extern int			vkernel_B_arg;	/* arg from main() */

void	mp_start		(void);
void	mp_announce		(void);
int	stop_cpus		(cpumask_t);
void	ap_init			(void);
int	restart_cpus		(cpumask_t);
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

/* Assume that APICID = CPUID for virtual processors */
#define get_cpuid_from_apicid(cpuid) cpuid
#define get_apicid_from_cpuid(cpuid) cpuid

#endif /* !LOCORE */

#endif /* _KERNEL */
#endif /* _MACHINE_SMP_H_ */
