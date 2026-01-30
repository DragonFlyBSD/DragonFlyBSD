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

#ifndef _SYS_DOMAINSET_H_
#define _SYS_DOMAINSET_H_

/* DragonFly domainset compatibility for LinuxKPI (FreeBSD NUMA) */

#include <sys/cpuset.h>

/* Domain set is just a cpuset in DragonFly (no NUMA support) */
typedef cpuset_t domainset_t;

#define DOMAINSET_NULL NULL
#define DOMAINSET_SIZE(n) sizeof(cpuset_t)
#define DOMAINSET_SET(n, p) CPU_SET(n, p)
#define DOMAINSET_CLR(n, p) CPU_CLR(n, p)
#define DOMAINSET_ISSET(n, p) CPU_ISSET(n, p)
#define DOMAINSET_COPY(f, t) CPU_COPY(f, t)
#define DOMAINSET_ZERO(p) CPU_ZERO(p)
#define DOMAINSET_FSET(p) CPU_FSET(p)
#define DOMAINSET_PCNT(p) CPU_COUNT(p)
#define DOMAINSET_SUB(s1, s2, d) CPU_SUB(s1, s2, d)
#define DOMAINSET_CMP(s1, s2) CPU_CMP(s1, s2)
#define DOMAINSET_VALID(mem, idx) 1

#define domainset_first(cpuset) CPU_FIRST(cpuset)
#define domainset_next(prev, cpuset) CPU_NEXT(prev, cpuset)

#define DOMAINSET_FORSUB(i, mask) CPU_FOREACH(i, mask)

/* Domain set allocation - stubs */
static __inline domainset_t *
domainset_alloc(void)
{
    domainset_t *ds;
    ds = (domainset_t *)kmalloc(sizeof(domainset_t), M_TEMP, M_WAITOK);
    if (ds)
        DOMAINSET_ZERO(ds);
    return ds;
}

static __inline void
domainset_free(domainset_t *ds)
{
    if (ds)
        kfree(ds, M_TEMP);
}

static __inline void
domainset_copy(domainset_t *src, domainset_t *dst)
{
    DOMAINSET_COPY(src, dst);
}

static __inline int
domainset_equal(domainset_t *a, domainset_t *b)
{
    return DOMAINSET_CMP(a, b) == 0;
}

static __inline int
domainset_empty(domainset_t *ds)
{
    return CPU_EMPTY(ds);
}

static __inline int
domainset_issingleton(domainset_t *ds)
{
    return CPU_SINGLETON(ds);
}

/* Fallback domain policy - stub */
#define DOMAINSET_RR 0
#define DOMAINSET_FIRSTFIT 1
#define DOMAINSET_FIXED 2
#define DOMAINSET_PREF 3
#define DOMAINSET_POLICY_MAX 4

static __inline int
domainset_first(domainset_t *ds)
{
    return domainset_first(ds);
}

static __inline int
domainset_firstvalid(int prefer, domainset_t *mask)
{
    return CPU_FIRST(mask);
}

static __inline int
domainset_getcpu(domainset_t *mask, int prefer, int policy)
{
    return CPU_FIRST(mask);
}

#endif /* _SYS_DOMAINSET_H_ */
