/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vmmeter.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD: src/sys/sys/vmmeter.h,v 1.21.2.2 2002/10/10 19:28:21 dillon Exp $
 * $DragonFly: src/sys/vm/vm_page2.h,v 1.1 2003/07/03 17:24:04 dillon Exp $
 */

#ifndef _VM_VMPAGE2_H_
#define _VM_VMPAGE2_H_

#ifndef _SYS_VMMETER_H_
#include <sys/vmmeter.h>
#endif

#ifdef _KERNEL

/*
 * Return TRUE if we are under our reserved low-free-pages threshold
 */

static __inline 
int
vm_page_count_reserved(void)
{
    return (vmstats.v_free_reserved > 
	(vmstats.v_free_count + vmstats.v_cache_count));
}

/*
 * Return TRUE if we are under our severe low-free-pages threshold
 *
 * This routine is typically used at the user<->system interface to determine
 * whether we need to block in order to avoid a low memory deadlock.
 */

static __inline 
int
vm_page_count_severe(void)
{
    return (vmstats.v_free_severe >
	(vmstats.v_free_count + vmstats.v_cache_count));
}

/*
 * Return TRUE if we are under our minimum low-free-pages threshold.
 *
 * This routine is typically used within the system to determine whether
 * we can execute potentially very expensive code in terms of memory.  It
 * is also used by the pageout daemon to calculate when to sleep, when
 * to wake waiters up, and when (after making a pass) to become more
 * desparate.
 */

static __inline 
int
vm_page_count_min(void)
{
    return (vmstats.v_free_min >
	(vmstats.v_free_count + vmstats.v_cache_count));
}

/*
 * Return TRUE if we have not reached our free page target during
 * free page recovery operations.
 */

static __inline 
int
vm_page_count_target(void)
{
    return (vmstats.v_free_target >
	(vmstats.v_free_count + vmstats.v_cache_count));
}

/*
 * Return the number of pages we need to free-up or cache
 * A positive number indicates that we do not have enough free pages.
 */

static __inline 
int
vm_paging_target(void)
{
    return (
	(vmstats.v_free_target + vmstats.v_cache_min) - 
	(vmstats.v_free_count + vmstats.v_cache_count)
    );
}

/*
 * Return a positive number if the pagedaemon needs to be woken up.
 */

static __inline 
int
vm_paging_needed(void)
{
    return (
	(vmstats.v_free_reserved + vmstats.v_cache_min) >
	(vmstats.v_free_count + vmstats.v_cache_count)
    );
}

#endif	/* _KERNEL */
#endif

