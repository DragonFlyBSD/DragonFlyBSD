/*
 * Kernel interface to machine-dependent clock driver.
 * Garrett Wollman, September 1994.
 * This file is in the public domain.
 *
 * $FreeBSD: src/sys/i386/include/clock.h,v 1.38.2.1 2002/11/02 04:41:50 iwasaki Exp $
 * $DragonFly: src/sys/i386/include/Attic/clock.h,v 1.4 2003/08/26 21:42:18 rob Exp $
 */

#ifndef _MACHINE_CLOCK_H_
#define	_MACHINE_CLOCK_H_

#ifdef _KERNEL
/*
 * i386 to clock driver interface.
 * XXX large parts of the driver and its interface are misplaced.
 */
extern int	adjkerntz;
extern int	disable_rtc_set;
extern int	statclock_disable;
extern u_int	timer_freq;
extern int	timer0_max_count;
extern u_int	tsc_freq;
extern int	tsc_is_broken;
extern int	wall_cmos_clock;
#ifdef APIC_IO
extern int	apic_8254_intr;
#endif

/*
 * Driver to clock driver interface.
 */
struct clockframe;

int	acquire_timer0 (int rate,
			    void (*function)(struct clockframe *frame));
int	acquire_timer2 (int mode);
int	release_timer0 (void);
int	release_timer2 (void);
#ifndef PC98
int	rtcin (int val);
#else
int	acquire_timer1 (int mode);
int	release_timer1 (void);
#endif
int	sysbeep (int pitch, int period);
void	timer_restore (void);

#endif /* _KERNEL */

#endif /* !_MACHINE_CLOCK_H_ */
