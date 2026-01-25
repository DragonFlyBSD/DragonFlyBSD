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

void	initializecpu(int cpu);

#endif /* !LOCORE */

#endif /* _KERNEL */

#endif /* _MACHINE_SMP_H_ */
