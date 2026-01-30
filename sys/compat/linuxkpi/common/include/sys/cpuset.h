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

#ifndef _SYS_CPUSET_H_
#define _SYS_CPUSET_H_

/* DragonFly cpuset compatibility for LinuxKPI */

#include <sys/types.h>

/* Maximum number of CPUs - DragonFly supports up to 256 */
#define CPU_MAXSIZE 256

/* Define cpuset as a bitmap */
typedef long cpuset_t;

/* Basic cpuset operations */
#define CPU_SETSIZE CPU_MAXSIZE
#define CPU_ZERO(cpuset) (*(cpuset) = 0)
#define CPU_SET(cpu, cpuset) (*(cpuset) |= (1L << (cpu)))
#define CPU_CLR(cpu, cpuset) (*(cpuset) &= ~(1L << (cpu)))
#define CPU_ISSET(cpu, cpuset) ((*(cpuset) & (1L << (cpu))) != 0)
#define CPU_COPY(src, dst) (*(dst) = *(src))
#define CPU_FSET(cpuset) (*(cpuset) = (~0L))
#define CPU_COUNT(cpuset) __builtin_popcountl(*(cpuset))
#define CPU_SUB(s1, s2, d) (*(d) = (*(s1) & ~*(s2)))
#define CPU_CMP(s1, s2) ((*(s1) != *(s2)) ? 1 : 0)

#define CPU_FOREACH(cpu, cpuset) \
    for ((cpu) = 0; (cpu) < ncpus; (cpu)++) \
        if (CPU_ISSET(cpu, cpuset))

#define CPU_EMPTY(cpuset) (*(cpuset) == 0)
#define CPU_SINGLETON(cpuset) (__builtin_popcountl(*(cpuset)) == 1)
#define CPU_FIRST(cpuset) __builtin_ctzl(*(cpuset))
#define CPU_NEXT(cpu, cpuset) ({ \
    long __m = (*(cpuset) & (~0L << ((cpu) + 1))); \
    (__m == 0) ? -1 : __builtin_ctzl(__m); \
})

/* For compatibility with FreeBSD's setfirst */
#define setfirst(cpuset) CPU_FIRST(cpuset)

/* Internal cpuset representation */
#ifndef _CPU_SET_T_DEFINED
#define _CPU_SET_T_DEFINED
typedef cpuset_t cpu_set_t;
#endif

/* CPU set affinity functions - stubs for now */
static __inline int
cpuset_getaffinity(int level, pid_t id, size_t setsize, cpuset_t *mask)
{
    *mask = ~0L;
    return 0;
}

static __inline int
cpuset_setaffinity(int level, pid_t id, size_t setsize, const cpuset_t *mask)
{
    return 0;
}

/* Deprecated/obsolete cpuset functions - stubs */
static __inline int
cpuset_getthread(pid_t pid, pid_t tid, cpuset_t *mask)
{
    *mask = ~0L;
    return 0;
}

static __inline int
cpuset_setthread(pid_t pid, pid_t tid, const cpuset_t *mask)
{
    return 0;
}

/* CPU set formatting/parsing - stubs */
static __inline int
cpusetobj_strprint(char *buf, size_t bufsize, const cpuset_t *mask)
{
    return snprintf(buf, bufsize, "0x%lx", (unsigned long)*mask);
}

static __inline int
cpusetobj_strscan(cpuset_t *mask, const char *buf)
{
    unsigned long val;
    if (sscanf(buf, "0x%lx", &val) == 1 || sscanf(buf, "%lx", &val) == 1) {
        *mask = (cpuset_t)val;
        return 0;
    }
    return -1;
}

#endif /* _SYS_CPUSET_H_ */
