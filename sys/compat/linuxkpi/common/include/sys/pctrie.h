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

#ifndef _SYS_PCTRIE_H_
#define _SYS_PCTRIE_H_

/* DragonFly pctrie compatibility for LinuxKPI (FreeBSD page counter trie) */

#include <sys/types.h>

/* Page counter trie node - minimal implementation */
struct pctrie_node {
    struct pctrie_node *pn_child[2];
    uintptr_t pn_value;
};

/* Page counter trie head */
struct pctrie {
    struct pctrie_node *pt_root;
};

typedef struct pctrie pctrie_t;

/* pctrie functions - stubs */
static __inline void
pctrie_init(struct pctrie *pt)
{
    pt->pt_root = NULL;
}

static __inline int
pctrie_insert(struct pctrie *pt, uint64_t index, void *value)
{
    return 0; /* Stub */
}

static __inline void *
pctrie_lookup(struct pctrie *pt, uint64_t index)
{
    return NULL; /* Stub */
}

static __inline void *
pctrie_remove(struct pctrie *pt, uint64_t index)
{
    return NULL; /* Stub */
}

static __inline void
pctrie_clean(struct pctrie *pt)
{
    /* Stub */
}

/* Page counter type alias */
typedef struct pctrie pagecount_t;

static __inline void
pagecount_init(pagecount_t *pc)
{
    pctrie_init(pc);
}

static __inline uint64_t
pagecount_read(pagecount_t *pc, uint64_t index)
{
    void *val = pctrie_lookup(pc, index);
    return (val != NULL) ? (uint64_t)(uintptr_t)val : 0;
}

static __inline int
pagecount_add(pagecount_t *pc, uint64_t index, int64_t delta)
{
    return 0; /* Stub */
}

#endif /* _SYS_PCTRIE_H_ */
