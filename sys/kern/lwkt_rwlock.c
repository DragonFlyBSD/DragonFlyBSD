/*
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
 * Implements simple shared/exclusive locks using LWKT. 
 *
 * $DragonFly: src/sys/kern/Attic/lwkt_rwlock.c,v 1.6 2004/07/16 05:51:10 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>

/*
 * NOTE! called from low level boot, we cannot do anything fancy.
 */
void
lwkt_rwlock_init(lwkt_rwlock_t lock)
{
    lwkt_wait_init(&lock->rw_wait);
    lock->rw_owner = NULL;
    lock->rw_count = 0;
    lock->rw_requests = 0;
}

void
lwkt_rwlock_uninit(lwkt_rwlock_t lock)
{
    /* empty */
}

void
lwkt_exlock(lwkt_rwlock_t lock, const char *wmesg)
{
    lwkt_tokref ilock;
    int gen;

    lwkt_gettoken(&ilock, &lock->rw_token);
    gen = lock->rw_wait.wa_gen;
    while (lock->rw_owner != curthread) {
	if (lock->rw_owner == NULL && lock->rw_count == 0) {
	    lock->rw_owner = curthread;
	    break;
	}
	++lock->rw_requests;
	lwkt_block(&lock->rw_wait, wmesg, &gen);
	--lock->rw_requests;
    }
    ++lock->rw_count;
    lwkt_reltoken(&ilock);
}

void
lwkt_shlock(lwkt_rwlock_t lock, const char *wmesg)
{
    lwkt_tokref ilock;
    int gen;

    lwkt_gettoken(&ilock, &lock->rw_token);
    gen = lock->rw_wait.wa_gen;
    while (lock->rw_owner != NULL) {
	++lock->rw_requests;
	lwkt_block(&lock->rw_wait, wmesg, &gen);
	--lock->rw_requests;
    }
    ++lock->rw_count;
    lwkt_reltoken(&ilock);
}

void
lwkt_exunlock(lwkt_rwlock_t lock)
{
    lwkt_tokref ilock;

    lwkt_gettoken(&ilock, &lock->rw_token);
    KASSERT(lock->rw_owner != NULL, ("lwkt_exunlock: shared lock"));
    KASSERT(lock->rw_owner == curthread, ("lwkt_exunlock: not owner"));
    if (--lock->rw_count == 0) {
	lock->rw_owner = NULL;
	if (lock->rw_requests)
	    lwkt_signal(&lock->rw_wait, 1);
    }
    lwkt_reltoken(&ilock);
}

void
lwkt_shunlock(lwkt_rwlock_t lock)
{
    lwkt_tokref ilock;

    lwkt_gettoken(&ilock, &lock->rw_token);
    KASSERT(lock->rw_owner == NULL, ("lwkt_shunlock: exclusive lock"));
    if (--lock->rw_count == 0) {
	if (lock->rw_requests)
	    lwkt_signal(&lock->rw_wait, 1);
    }
    lwkt_reltoken(&ilock);
}

