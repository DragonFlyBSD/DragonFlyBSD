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

struct intrframe;

typedef __uint64_t	sysclock_t;
typedef int64_t		ssysclock_t;
typedef TAILQ_HEAD(systimerq, systimer) *systimerq_t;
typedef void (*systimer_func_t)(struct systimer *, int, struct intrframe *);

typedef struct systimer {
    TAILQ_ENTRY(systimer)	node;
    systimerq_t			queue;
    sysclock_t			time;		/* absolute time next intr */
    sysclock_t			periodic;	/* non-zero if periodic */
    systimer_func_t		func;
    void			*data;
    int				flags;
    int				us;		/* non-zero if one-shot */
    ssysclock_t			freq;		/* frequency if periodic */
    struct cputimer		*which;		/* which timer was used? */
    struct globaldata		*gd;		/* cpu owning structure */
} *systimer_t;

#define SYSTF_ONQUEUE		0x0001
#define SYSTF_IPIRUNNING	0x0002
#define SYSTF_NONQUEUED		0x0004
#define SYSTF_MSSYNC		0x0008		/* 1Khz coincident sync */
#define SYSTF_100KHZSYNC	0x0010		/* 100Khz coincident sync */
#define SYSTF_FIRST		0x0020		/* order first if coincident */
#define SYSTF_OFFSET50		0x0040		/* add 1/2 interval offset */
#define SYSTF_OFFSETCPU		0x0080		/* add cpu*periodic/ncpus */

#ifdef _KERNEL
void systimer_changed(void);
void systimer_intr_enable(void);
void systimer_intr(sysclock_t *, int, struct intrframe *);
void systimer_add(systimer_t);
void systimer_del(systimer_t);
void systimer_init_periodic(systimer_t, systimer_func_t, void *, int64_t);
void systimer_init_periodic_nq(systimer_t, systimer_func_t, void *, int64_t);
void systimer_init_periodic_nq1khz(systimer_t, systimer_func_t, void *, int64_t);
void systimer_init_periodic_nq100khz(systimer_t, systimer_func_t, void *, int64_t);
void systimer_init_periodic_flags(systimer_t, systimer_func_t, void *,
			int64_t, int);
void systimer_adjust_periodic(systimer_t, int64_t);
void systimer_init_oneshot(systimer_t, systimer_func_t, void *, int64_t);

/*
 * The cputimer interface.  This provides a free-running (non-interrupt)
 * and monotonically increasing timebase for the system.
 *
 * The cputimer structure holds the fixed cputimer frequency, determining
 * the granularity of sys_cputimer->count().
 *
 * Note that sys_cputimer->count() always returns a full-width wrapping
 * counter.
 *
 * The 64 bit versions of the frequency are used for converting count
 * values into uS or nS as follows:
 *
 *	usec = (sys_cputimer->freq64_usec * count) >> 32
 *
 * NOTE: If count > sys_cputimer->freq, above conversion may overflow.
 *
 * REQUIREMENT FOR CPUTIMER IMPLEMENTATION:
 *
 * - The values returned by count() must be MP synchronized.
 * - The values returned by count() must be stable under all situation,
 *   e.g. when the platform enters power saving mode.
 * - The values returned by count() must be monotonically increasing.
 */

struct cputimer {
    SLIST_ENTRY(cputimer) next;
    const char	*name;
    int		pri;
    int		type;
    sysclock_t	(*count)(void);
    sysclock_t	(*fromhz)(int64_t freq);
    sysclock_t	(*fromus)(int64_t us);
    void	(*construct)(struct cputimer *, sysclock_t);
    void	(*destruct)(struct cputimer *);
    sysclock_t	sync_base;	/* periodic synchronization base */
    sysclock_t	base;		/* (implementation dependant) */
    sysclock_t	freq;		/* in Hz */
    int64_t	freq64_usec;	/* in (1e6 << 32) / freq */
    int64_t	freq64_nsec;	/* in (1e9 << 32) / freq */
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
#define CPUTIMER_TSC		8
#define CPUTIMER_VMM		9
#define CPUTIMER_VMM1		10
#define CPUTIMER_VMM2		11

#define CPUTIMER_PRI_DUMMY	-10
#define CPUTIMER_PRI_8254	0
#define CPUTIMER_PRI_ACPI	10
#define CPUTIMER_PRI_HPET	20
#define CPUTIMER_PRI_CS5536	30
#define CPUTIMER_PRI_GEODE	40
#define CPUTIMER_PRI_VKERNEL	200
#define CPUTIMER_PRI_TSC	250
#define CPUTIMER_PRI_VMM	1000
#define CPUTIMER_PRI_VMM_HI	2000

void cputimer_select(struct cputimer *, int);
void cputimer_register(struct cputimer *);
void cputimer_deregister(struct cputimer *);
void cputimer_set_frequency(struct cputimer *, sysclock_t);
sysclock_t cputimer_default_fromhz(int64_t);
sysclock_t cputimer_default_fromus(int64_t);
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
 * pcpuhand  -- Per-cpu handler (could be NULL).
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
	void		(*pcpuhand)
			(struct cputimer_intr *);
	SLIST_ENTRY(cputimer_intr) next;
	const char	*name;
	int		type;	/* CPUTIMER_INTR_ */
	int		prio;	/* CPUTIMER_INTR_PRIO_ */
	uint32_t	caps;	/* CPUTIMER_INTR_CAP_ */
	void		*priv;	/* private data */
};

#define CPUTIMER_INTR_8254		0
#define CPUTIMER_INTR_LAPIC		1
#define CPUTIMER_INTR_VKERNEL		2
#define CPUTIMER_INTR_VMM		3

/* NOTE: Keep the new values less than CPUTIMER_INTR_PRIO_MAX */
#define CPUTIMER_INTR_PRIO_8254		0
#define CPUTIMER_INTR_PRIO_LAPIC	10
#define CPUTIMER_INTR_PRIO_VKERNEL	20
#define CPUTIMER_INTR_PRIO_VMM		500
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
int  cputimer_intr_powersave_addreq(void);
void cputimer_intr_powersave_remreq(void);

/*
 * The cpucounter interface.
 *
 * REQUIREMENT FOR CPUCOUNTER IMPLEMENTATION:
 *
 * - The values returned by count() must be MP synchronized, if
 *   CPUCOUNTER_FLAG_MPSYNC is set on 'flags'.
 * - The values returned by count() must be stable under all situation,
 *   e.g. when the platform enters power saving mode.
 * - The values returned by count() must be monotonically increasing.
 */
struct cpucounter {
	uint64_t	freq;
	uint64_t	(*count)(void);
	uint16_t	flags;		/* CPUCOUNTER_FLAG_ */
	uint16_t	prio;		/* CPUCOUNTER_PRIO_ */
	uint16_t	type;		/* CPUCOUNTER_ */
	uint16_t	reserved;
	SLIST_ENTRY(cpucounter) link;
} __cachealign;

#define CPUCOUNTER_FLAG_MPSYNC		0x0001

#define CPUCOUNTER_DUMMY		0
#define CPUCOUNTER_TSC			1
#define CPUCOUNTER_VMM			2
#define CPUCOUNTER_VMM1			3
#define CPUCOUNTER_VMM2			4

#define CPUCOUNTER_PRIO_DUMMY		0
#define CPUCOUNTER_PRIO_TSC		50
#define CPUCOUNTER_PRIO_VMM		100
#define CPUCOUNTER_PRIO_VMM_HI		150

void cpucounter_register(struct cpucounter *);
const struct cpucounter *cpucounter_find_pcpu(void);
const struct cpucounter *cpucounter_find(void);
#endif	/* _KERNEL */

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#endif	/* !_SYS_SYSTIMER_H_ */
