/*-
 * Copyright (C) 2003 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2001,2003 Daniel Eischen <deischen@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $DragonFly: src/lib/libthread_xu/arch/i386/i386/pthread_md.c,v 1.1 2005/02/01 12:38:27 davidxu Exp $
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <machine/cpufunc.h>
#include <machine/segments.h>
#include <machine/sysarch.h>

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifndef __DragonFly__
#include "rtld_tls.h"
#endif

#include "pthread_md.h"

#ifdef __DragonFly__

#define LDT_ENTRIES 8192
#define LDT_WORDS   (8192/sizeof(unsigned int))
#define LDT_RESERVED NLDT

static unsigned int ldt_mask[LDT_WORDS];
static int initialized = 0;

static void	initialize(void);

static void
initialize(void)
{
	int i, j;

	memset(ldt_mask, 0xFF, sizeof(ldt_mask));
	/* Reserve system predefined LDT entries */
	for (i = 0; i < LDT_RESERVED; ++i) {
		j = i / 32;
		ldt_mask[j] &= ~(1 << (i % 32));
	}
	initialized = 1;
}

static u_int
alloc_ldt_entry(void)
{
	u_int i, j, index;
	
	index = 0;
	for (i = 0; i < LDT_WORDS; ++i) {
		if (ldt_mask[i] != 0) {
			j = bsfl(ldt_mask[i]);
			ldt_mask[i] &= ~(1 << j);
			index = i * 32 + j;
			break;
		}
	}
	return (index);
}

static void
free_ldt_entry(u_int index)
{
	u_int i, j;

	if (index < LDT_RESERVED || index >= LDT_ENTRIES)
		return;
	i = index / 32;
	j = index % 32;
	ldt_mask[i] |= (1 << j);
}

#endif

struct tcb *
_tcb_ctor(struct pthread *thread, int initial)
{
#ifndef COMPAT_32BIT
	union descriptor ldt;
#endif
	struct tcb *tcb;

#ifndef __DragonFly__
	void *oldtls;

	if (initial)
		__asm __volatile("movl %%gs:0, %0" : "=r" (oldtls));
	else
		oldtls = NULL;

	tcb = _rtld_allocate_tls(oldtls, sizeof(struct tcb), 16);
#else
	if (!initialized)
		initialize();
	if ((tcb = malloc(sizeof(struct tcb))) == NULL)
		return (NULL);
	/* tcb_self and tcb_dtv should assigned by rtld tls code. */
	tcb->tcb_self = tcb;
	tcb->tcb_dtv = NULL;
	if ((tcb->tcb_ldt = alloc_ldt_entry()) == 0) {
		free(tcb);
		return (NULL);
	}
#endif

	if (tcb) {
		tcb->tcb_thread = thread;
#ifndef COMPAT_32BIT
		ldt.sd.sd_hibase = (unsigned int)tcb >> 24;
		ldt.sd.sd_lobase = (unsigned int)tcb & 0xFFFFFF;
		ldt.sd.sd_hilimit = (sizeof(struct tcb) >> 16) & 0xF;
		ldt.sd.sd_lolimit = sizeof(struct tcb) & 0xFFFF;
		ldt.sd.sd_type = SDT_MEMRWA;
		ldt.sd.sd_dpl = SEL_UPL;
		ldt.sd.sd_p = 1;
		ldt.sd.sd_xx = 0;
		ldt.sd.sd_def32 = 1;
		ldt.sd.sd_gran = 0;	/* no more than 1M */

#ifdef __DragonFly__
		if (i386_set_ldt(tcb->tcb_ldt, &ldt, 1) == -1) { 
			free_ldt_entry(tcb->tcb_ldt);
			free(tcb);
			tcb = NULL;
		}
#else
		tcb->tcb_ldt = i386_set_ldt(LDT_AUTO_ALLOC, &ldt, 1);
		if (tcb->tcb_ldt < 0) {
			free(tcb);
			tcb = NULL;
		}
#endif
#endif
	}
	return (tcb);
}

void
_tcb_dtor(struct tcb *tcb)
{
#ifdef __DragonFly__
	if (tcb->tcb_ldt > 0) {
		free_ldt_entry(tcb->tcb_ldt);
		free(tcb);	
	}
#else
#ifndef COMPAT_32BIT
	if (tcb->tcb_ldt > 0)
		i386_set_ldt(tcb->tcb_ldt, NULL, 1);
#endif
	_rtld_free_tls(tcb, sizeof(struct tcb), 16);
#endif
}
