/*
 * SYS/SYSTIMER.H
 *
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/systimer.h,v 1.1 2004/01/30 05:42:18 dillon Exp $
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

void systimer_intr(sysclock_t *time, struct intrframe *frame);
void systimer_add(systimer_t info);
void systimer_del(systimer_t info);
void systimer_init_periodic(systimer_t info, void *func, void *data, int hz);
void systimer_init_oneshot(systimer_t info, void *func, void *data, int us);


/*
 * note that cputimer_count() always returns a full-width wrapping counter.
 */
sysclock_t cputimer_count(void);
sysclock_t cputimer_fromhz(int freq);
sysclock_t cputimer_fromus(int us);
void cputimer_intr_reload(sysclock_t clock);

/*
 * These variables hold the fixed cputimer frequency, determining the
 * granularity of cputimer_count().  
 *
 * The 64 bit versions are used for converting count values into uS or nS
 * as follows:
 *
 *	usec = (cputimer_freq64_usec * count) >> 32
 */
extern sysclock_t cputimer_freq;	/* in Hz */
extern int64_t cputimer_freq64_usec;	/* in (1e6 << 32) / timer_freq */
extern int64_t cputimer_freq64_nsec;	/* in (1e9 << 32) / timer_freq */

#endif

