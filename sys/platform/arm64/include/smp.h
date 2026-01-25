/*
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 */

#ifndef _MACHINE_SMP_H_
#define _MACHINE_SMP_H_

#ifdef _KERNEL

#ifndef LOCORE

#include <machine/cpumask.h>

void	initializecpu(int cpu);
int	stop_cpus(cpumask_t);
int	restart_cpus(cpumask_t);
void	cpu_send_ipiq(int);
int	cpu_send_ipiq_passive(int);

extern volatile cpumask_t	stopped_cpus;

extern cpumask_t		smp_active_mask;

/*
 * CPU topology stub functions for arm64.
 * ARM64 does not use APIC IDs - provide simple stubs.
 */
static __inline int
get_cpuid_from_apicid(int apicid)
{
	return apicid;  /* 1:1 mapping on arm64 */
}

static __inline int
get_apicid_from_cpuid(int cpuid)
{
	return cpuid;   /* 1:1 mapping on arm64 */
}

/* CPU topology detection stubs - arm64 topology via devicetree/ACPI */
static __inline void
detect_cpu_topology(void)
{
	/* TODO: implement via MPIDR or ACPI PPTT */
}

static __inline int
get_chip_ID(int cpuid)
{
	return 0;       /* Single chip for now */
}

static __inline int
get_core_number_within_chip(int cpuid)
{
	return cpuid;   /* Each CPU is its own core for now */
}

static __inline int
get_logical_CPU_number_within_core(int cpuid)
{
	return 0;       /* No SMT assumed */
}

#endif /* !LOCORE */

#endif /* _KERNEL */

#endif /* _MACHINE_SMP_H_ */
