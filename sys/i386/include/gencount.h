/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 * $DragonFly: src/sys/i386/include/Attic/gencount.h,v 1.1 2004/12/08 23:19:51 joerg Exp $
 */

#ifndef _MACHINE_GENCOUNT_H
#define	_MACHINE_GENCOUNT_H

#ifndef _KERNEL
#error "no user-servicable parts inside"
#endif

#include <sys/types.h>
#include <sys/thread2.h>

typedef struct {
	uint32_t low, high;
} gencount_t;

/* not interrupt-safe */
static __inline void
gencount_init(gencount_t *gencnt)
{
	gencnt->low = 0;
	gencnt->high = 0;
}

/* interrupt-safe */
static __inline void
gencount_inc(gencount_t *gencnt)
{
	crit_enter();
	if (++gencnt->high == 0)
		++gencnt->low;
	crit_exit();
}

/* interrupt-safe */
static __inline gencount_t
gencount_get(gencount_t *gencnt)
{
	volatile gencount_t *vgencnt;
	gencount_t ret;

	vgencnt = gencnt;
        ret.low = vgencnt->low;
	ret.high = vgencnt->high;
	if (ret.low != vgencnt->low) {
		/*
		 * Choose an arbitrary time between when we started
		 * and when we finished. Since blocking can occur
		 * at any time, this has to be save.
		 */
		ret.low++;
		ret.high = 0;
	}

	return(ret);
}

/* not interrupt-safe */
static __inline int
gencount_cmp(gencount_t *gencnt1, gencount_t *gencnt2)
{
	if (gencnt1->low > gencnt2->low)
		return(1);
	else if (gencnt1->low < gencnt1->low)
		return(-1);
	else if (gencnt1->high > gencnt2->high)
		return(1);
	else if (gencnt1->high < gencnt2->high)
		return(1);
	else
		return(0);
}

#endif
