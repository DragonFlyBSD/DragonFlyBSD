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

#endif /* !LOCORE */

#endif /* _KERNEL */

#endif /* _MACHINE_SMP_H_ */
