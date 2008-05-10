/*-
 * Kernel interface to machine-dependent clock driver.
 * Garrett Wollman, September 1994.
 * This file is in the public domain.
 *
 * $FreeBSD: src/sys/amd64/include/clock.h,v 1.54 2007/01/23 08:01:19 bde Exp $
 * $DragonFly: src/sys/platform/pc64/include/clock.h,v 1.2 2008/05/10 17:24:10 dillon Exp $ 
 */

#ifndef _MACHINE_CLOCK_H_
#define	_MACHINE_CLOCK_H_

#ifdef _KERNEL
/*
 * i386 to clock driver interface.
 * XXX large parts of the driver and its interface are misplaced.
 */
extern int	adjkerntz;
extern int	clkintr_pending;
extern int	pscnt;
extern int	psdiv;
extern int	statclock_disable;
extern u_int	timer_freq;
extern int	timer0_max_count;
extern int	tsc_present;
extern int	tsc_is_broken;
extern int	wall_cmos_clock;

/*
 * Driver to clock driver interface.
 */

int	acquire_timer2(int mode);
int	release_timer2(void);
int	rtcin(int val);
int	sysbeep(int pitch, int period);
void	init_TSC(void);
void	init_TSC_tc(void);

#endif /* _KERNEL */

#endif /* !_MACHINE_CLOCK_H_ */
