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

#ifndef _SYS_KDB_H_
#define _SYS_KDB_H_

/* DragonFly kdb compatibility for LinuxKPI */

/* KDB trap reasons */
#define KDB_WHY_UNSET 0
#define KDB_WHY_BOOT 1
#define KDB_WHY_PANIC 2
#define KDB_WHY_KDB 3
#define KDB_WHY_TRAP 4
#define KDB_WHY_SYSCTL 5
#define KDB_WHY_WATCHDOG 6

/* Debugger active check - stub */
static __inline int
kdb_active(void)
{
    return 0; /* DragonFly doesn't have kdb_active equivalent */
}

/* Enter debugger - stub */
static __inline void
kdb_enter(const char *why, const char *msg)
{
    /* DragonFly uses different debug mechanism */
}

/* KDB system call - stub */
static __inline int
sys_kdb_enter(void)
{
    return 0;
}

#endif /* _SYS_KDB_H_ */
