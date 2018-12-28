/*
 * Kernel interface to machine-dependent clock driver.
 * Garrett Wollman, September 1994.
 * This file is in the public domain.
 *
 * $FreeBSD: src/sys/i386/include/clock.h,v 1.38.2.1 2002/11/02 04:41:50 iwasaki Exp $
 */

#ifndef _MACHINE_CLOCK_H_
#define	_MACHINE_CLOCK_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#ifdef _KERNEL

#ifndef _SYS_SYSTIMER_H_
#include <sys/systimer.h>
#endif

typedef struct TOTALDELAY {
	int		us;
	int		started;
	sysclock_t	last_clock;
} TOTALDELAY;

#endif

typedef uint64_t tsc_uclock_t;
typedef int64_t	tsc_sclock_t;

#ifdef _KERNEL

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

/*
 * Driver to clock driver interface.
 */

int	CHECKTIMEOUT(TOTALDELAY *);
int	rtcin (int val);
int	acquire_timer2 (int mode);
int	release_timer2 (void);
int	sysbeep (int pitch, int period);
void	timer_restore (void);

/*
 * Allow registering timecounter initialization code.
 */
typedef struct timecounter_init {
	char *name;
	void (*configure)(void);
} timecounter_init_t;

#define TIMECOUNTER_INIT(name, config)			\
	static struct timecounter_init name##_timer = {	\
		#name, config				\
	};						\
	DATA_SET(timecounter_init_set, name##_timer);

#endif /* _KERNEL */

#endif /* !_MACHINE_CLOCK_H_ */
