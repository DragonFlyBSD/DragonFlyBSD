/*-
 * Copyright (c) 2026 The DragonFly Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_RWLOCK_H_
#define _SYS_RWLOCK_H_

/* DragonFly rwlock compatibility for LinuxKPI */

#include <sys/param.h>

/* DragonFly uses lockmgr for rwlocks, but provides a simpler rwlock API */
typedef struct rwlock {
    struct lock lk;
} rwlock_t;

/* RW lock operations - map to DragonFly's lockmgr */
#define rw_init(rw, name) lockinit(&(rw)->lk, name, 0, LK_CANRECURSE)
#define rw_destroy(rw) lockuninit(&(rw)->lk)
#define rw_wlock(rw) lockmgr(&(rw)->lk, LK_EXCLUSIVE)
#define rw_wunlock(rw) lockmgr(&(rw)->lk, LK_RELEASE)
#define rw_rlock(rw) lockmgr(&(rw)->lk, LK_SHARED)
#define rw_runlock(rw) lockmgr(&(rw)->lk, LK_RELEASE)
#define rw_try_wlock(rw) (lockmgr(&(rw)->lk, LK_EXCLUSIVE|LK_NOWAIT) == 0)
#define rw_try_rlock(rw) (lockmgr(&(rw)->lk, LK_SHARED|LK_NOWAIT) == 0)
#define rw_wowned(rw) lockstatus(&(rw)->lk, LK_EXCLUSIVE)
#define rw_assert(rw, what) 

/* RW lock assertions */
#define RA_WLOCKED 1
#define RA_RLOCKED 2
#define RA_UNLOCKED 3

#endif /* _SYS_RWLOCK_H_ */
