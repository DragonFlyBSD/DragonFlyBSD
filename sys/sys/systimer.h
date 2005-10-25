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
 * $DragonFly: src/sys/sys/systimer.h,v 1.10 2005/10/25 17:26:58 dillon Exp $
 */

#ifndef _SYS_SYSTIMER_H_
#define _SYS_SYSTIMER_H_

struct intrframe;

typedef __uint32_t	sysclock_t;
typedef TAILQ_HEAD(systimerq, systimer) *systimerq_t;
typedef void (*systimer_func_t)(struct systimer *info);
typedef void (*systimer_func2_t)(struct systimer *info, struct intrframe *frame);

typedef struct systimer {
    TAILQ_ENTRY(systimer)	node;
    systimerq_t			queue;
    sysclock_t			time;		/* absolute time next intr */
    sysclock_t			periodic;	/* if non-zero */
    systimer_func2_t		func;
    void			*data;
    int				flags;
    struct globaldata		*gd;		/* cpu owning structure */
} *systimer_t;

#define SYSTF_ONQUEUE		0x0001
#define SYSTF_IPIRUNNING	0x0002
#define SYSTF_NONQUEUED		0x0004

void systimer_intr(sysclock_t *, int, struct intrframe *);
void systimer_add(systimer_t);
void systimer_del(systimer_t);
void systimer_init_periodic(systimer_t, void *, void *, int);
void systimer_init_periodic_nq(systimer_t, void *, void *, int);
void systimer_init_oneshot(systimer_t, void *, void *, int);

/*
 * cputimer interface.  This provides a free-running (non-interrupt) 
 * timebase for the system.  The cputimer
 *
 * These variables hold the fixed cputimer frequency, determining the
 * granularity of cputimer_count().  
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
    void	(*construct)(struct cputimer *cputimer, sysclock_t oldclock);
    void	(*destruct)(struct cputimer *cputimer);
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

#define CPUTIMER_PRI_DUMMY	-10
#define CPUTIMER_PRI_8254	0
#define CPUTIMER_PRI_ACPI	10

/*
 * note that cputimer_count() always returns a full-width wrapping counter.
 */
void cputimer_select(struct cputimer *, int);
void cputimer_register(struct cputimer *);
void cputimer_deregister(struct cputimer *);
void cputimer_set_frequency(struct cputimer *, int);
sysclock_t cputimer_default_fromhz(int);
sysclock_t cputimer_default_fromus(int);
void cputimer_default_construct(struct cputimer *, sysclock_t);
void cputimer_default_destruct(struct cputimer *);

void cputimer_intr_config(struct cputimer *);
void cputimer_intr_reload(sysclock_t);

#endif

