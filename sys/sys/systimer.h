/*
 * SYS/SYSTIMER.H
 * 
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/sys/systimer.h,v 1.13 2007/04/30 06:57:36 dillon Exp $
 */

#ifndef _SYS_SYSTIMER_H_
#define _SYS_SYSTIMER_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

/* XXX fix sys/kinfo.h */
#ifndef __BOOLEAN_T_DEFINED__
#define __BOOLEAN_T_DEFINED__
typedef	__boolean_t	boolean_t;
#endif

struct intrframe;

typedef __uint32_t	sysclock_t;
typedef int32_t		ssysclock_t;
typedef TAILQ_HEAD(systimerq, systimer) *systimerq_t;
typedef void (*systimer_func_t)(struct systimer *, int, struct intrframe *);

typedef struct systimer {
    TAILQ_ENTRY(systimer)	node;
    systimerq_t			queue;
    sysclock_t			time;		/* absolute time next intr */
    sysclock_t			periodic;	/* if non-zero */
    systimer_func_t		func;
    void			*data;
    int				flags;
    int				freq;		/* frequency if periodic */
    struct cputimer		*which;		/* which timer was used? */
    struct globaldata		*gd;		/* cpu owning structure */
} *systimer_t;

#define SYSTF_ONQUEUE		0x0001
#define SYSTF_IPIRUNNING	0x0002
#define SYSTF_NONQUEUED		0x0004

void systimer_intr_enable(void);
void systimer_intr(sysclock_t *, int, struct intrframe *);
void systimer_add(systimer_t);
void systimer_del(systimer_t);
void systimer_init_periodic(systimer_t, systimer_func_t, void *, int);
void systimer_init_periodic_nq(systimer_t, systimer_func_t, void *, int);
void systimer_adjust_periodic(systimer_t, int);
void systimer_init_oneshot(systimer_t, systimer_func_t, void *, int);

/*
 * cputimer interface.  This provides a free-running (non-interrupt) 
 * timebase for the system.  The cputimer
 *
 * These variables hold the fixed cputimer frequency, determining the
 * granularity of cputimer_count().
 *
 * Note that cputimer_count() always returns a full-width wrapping counter.
 *
 * The 64 bit versions are used for converting count values into uS or nS
 * as follows:
 *
 *	usec = (cputimer_freq64_usec * count) >> 32
 */

struct cputimer {
    SLIST_ENTRY(cputimer) next;
    const char	*name;
    int		pri;
    int		type;
    sysclock_t	(*count)(void);
    sysclock_t	(*fromhz)(int freq);
    sysclock_t	(*fromus)(int us);
    void	(*construct)(struct cputimer *, sysclock_t);
    void	(*destruct)(struct cputimer *);
    sysclock_t	freq;		/* in Hz */
    int64_t	freq64_usec;	/* in (1e6 << 32) / timer_freq */
    int64_t	freq64_nsec;	/* in (1e9 << 32) / timer_freq */
    sysclock_t	base;		/* (implementation dependant) */
};

extern struct cputimer *sys_cputimer;

#define CPUTIMER_DUMMY		0
#define CPUTIMER_8254_SEL1	1
#define CPUTIMER_8254_SEL2	2
#define CPUTIMER_ACPI		3
#define CPUTIMER_VKERNEL	4
#define CPUTIMER_HPET		5
#define CPUTIMER_GEODE		6
#define CPUTIMER_CS5536		7

#define CPUTIMER_PRI_DUMMY	-10
#define CPUTIMER_PRI_8254	0
#define CPUTIMER_PRI_ACPI	10
#define CPUTIMER_PRI_HPET	20
#define CPUTIMER_PRI_CS5536	30
#define CPUTIMER_PRI_GEODE	40
#define CPUTIMER_PRI_VKERNEL	200

void cputimer_select(struct cputimer *, int);
void cputimer_register(struct cputimer *);
void cputimer_deregister(struct cputimer *);
void cputimer_set_frequency(struct cputimer *, sysclock_t);
sysclock_t cputimer_default_fromhz(int);
sysclock_t cputimer_default_fromus(int);
void cputimer_default_construct(struct cputimer *, sysclock_t);
void cputimer_default_destruct(struct cputimer *);

/*
 * Interrupt cputimer interface.
 *
 * Interrupt cputimers are normally one shot timers which will
 * generate interrupt upon expiration.
 *
 * initclock -- Called at SI_BOOT2_CLOCKREG, SI_ORDER_SECOND.  The
 *              interrupt timer could deregister itself here, if it
 *              is not the selected system interrupt cputimer.  Before
 *              this function is called, 'enable' and 'reload' will
 *              not be called.
 * enable    -- Enable interrupt.  It is called by each CPU.  It is
 *              only called once during boot.  Before this function
 *              is called, 'reload' will not be called.
 * reload    -- Called by each CPU when it wants to to reprogram the
 *              one shot timer expiration time.  The reload value is
 *              measured in sys_cputimer->freq.
 * config    -- Setup the interrupt cputimer according to the passed
 *              in non-interrupt cputimer.  It will be called when
 *              sys_cputimer's frequency is changed or when sys_cputimer
 *              itself is changed.  It is also called when this interrupt
 *              cputimer gets registered.
 * restart   -- Start the possibly stalled interrupt cputimer immediately.
 *              Do fixup if necessary.
 * pmfixup   -- Called after ACPI power management is enabled.
 */
struct cputimer_intr {
	sysclock_t	freq;
	void		(*reload)
			(struct cputimer_intr *, sysclock_t);
	void		(*enable)
			(struct cputimer_intr *);
	void		(*config)
			(struct cputimer_intr *, const struct cputimer *);
	void		(*restart)
			(struct cputimer_intr *);
	void		(*pmfixup)
			(struct cputimer_intr *);
	void		(*initclock)
			(struct cputimer_intr *, boolean_t);
	SLIST_ENTRY(cputimer_intr) next;
	const char	*name;
	int		type;	/* CPUTIMER_INTR_ */
	int		prio;	/* CPUTIMER_INTR_PRIO_ */
	uint32_t	caps;	/* CPUTIMER_INTR_CAP_ */
};

#define CPUTIMER_INTR_8254		0
#define CPUTIMER_INTR_LAPIC		1
#define CPUTIMER_INTR_VKERNEL		2

/* NOTE: Keep the new values less than CPUTIMER_INTR_PRIO_MAX */
#define CPUTIMER_INTR_PRIO_8254		0
#define CPUTIMER_INTR_PRIO_LAPIC	10
#define CPUTIMER_INTR_PRIO_VKERNEL	20
#define CPUTIMER_INTR_PRIO_MAX		1000

#define CPUTIMER_INTR_CAP_NONE		0
#define CPUTIMER_INTR_CAP_PS		0x1	/* works during powersaving */

/*
 * Interrupt cputimer implementation interfaces
 *
 * NOTE:
 * cputimer_intr_deregister() is _not_ allowed to be called
 * with the currently selected interrupt cputimer.
 */
void cputimer_intr_register(struct cputimer_intr *);
void cputimer_intr_deregister(struct cputimer_intr *);
int  cputimer_intr_select(struct cputimer_intr *, int);

/*
 * Interrupt cputimer implementation helper functions
 *
 * default_enable    -- NOP
 * default_restart   -- reload(0)
 * default_config    -- NOP
 * default_pmfixup   -- NOP
 * default_initclock -- NOP
 */
void cputimer_intr_default_enable(struct cputimer_intr *);
void cputimer_intr_default_restart(struct cputimer_intr *);
void cputimer_intr_default_config(struct cputimer_intr *,
				  const struct cputimer *);
void cputimer_intr_default_pmfixup(struct cputimer_intr *);
void cputimer_intr_default_initclock(struct cputimer_intr *, boolean_t);

/*
 * Interrupt cputimer external interfaces
 */
void cputimer_intr_enable(void);
void cputimer_intr_pmfixup(void);
void cputimer_intr_config(const struct cputimer *);
void cputimer_intr_reload(sysclock_t);
void cputimer_intr_restart(void);
int  cputimer_intr_select_caps(uint32_t);

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#endif	/* !_SYS_SYSTIMER_H_ */
