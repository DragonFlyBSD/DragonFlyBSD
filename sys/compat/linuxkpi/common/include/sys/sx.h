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

#ifndef _SYS_SX_H_
#define _SYS_SX_H_

/* DragonFly sx (shared/exclusive lock) compatibility for LinuxKPI */

#include <sys/param.h>

/* sx lock - simple shared/exclusive lock using DragonFly's lockmgr */
typedef struct sx {
    struct lock lk;
} sx_t;

/* sx lock operations - map to DragonFly's lockmgr */
#define sx_init(sx, name) lockinit(&(sx)->lk, name, 0, LK_CANRECURSE)
#define sx_destroy(sx) lockuninit(&(sx)->lk)
#define sx_xlock(sx) lockmgr(&(sx)->lk, LK_EXCLUSIVE)
#define sx_xunlock(sx) lockmgr(&(sx)->lk, LK_RELEASE)
#define sx_slock(sx) lockmgr(&(sx)->lk, LK_SHARED)
#define sx_sunlock(sx) lockmgr(&(sx)->lk, LK_RELEASE)
#define sx_try_xlock(sx) (lockmgr(&(sx)->lk, LK_EXCLUSIVE|LK_NOWAIT) == 0)
#define sx_try_slock(sx) (lockmgr(&(sx)->lk, LK_SHARED|LK_NOWAIT) == 0)
#define sx_xlocked(sx) lockstatus(&(sx)->lk, LK_EXCLUSIVE)
#define sx_assert(sx, what)

/* sx lock assertions */
#define SA_XLOCKED 1
#define SA_SLOCKED 2
#define SA_UNLOCKED 3

#endif /* _SYS_SX_H_ */
