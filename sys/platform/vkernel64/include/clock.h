/*
 * Kernel interface to machine-dependent clock driver.
 * Garrett Wollman, September 1994.
 * This file is in the public domain.
 *
 * $FreeBSD: src/sys/i386/include/clock.h,v 1.38.2.1 2002/11/02 04:41:50 iwasaki Exp $
 */

#ifndef _MACHINE_CLOCK_H_
#define	_MACHINE_CLOCK_H_

#ifdef _KERNEL

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

typedef uint64_t tsc_uclock_t;
typedef int64_t tsc_sclock_t;

/*
 * x86 to clock driver interface.
 * XXX large parts of the driver and its interface are misplaced.
 */
extern int	adjkerntz;
extern int	disable_rtc_set;
extern int	tsc_present;
extern int	tsc_invariant;
extern int	tsc_mpsync;
extern int	wall_cmos_clock;
extern tsc_uclock_t tsc_frequency;
extern tsc_uclock_t tsc_oneus_approx;	/* do not use for fine calc, min 1 */

#endif /* _KERNEL */

#endif /* !_MACHINE_CLOCK_H_ */
