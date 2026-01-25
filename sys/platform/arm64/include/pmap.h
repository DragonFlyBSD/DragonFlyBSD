/*
 * Copyright (c) 1991 Regents of the University of California.
 * Copyright (c) 2026 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#ifndef _MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

struct pmap;
struct vm_page;
struct vm_object;

struct md_page {
	long interlock_count;
	long writeable_count_unused;
};

#define MD_PAGE_FREEABLE(m)	\
	(((m)->flags & (PG_MAPPED | PG_WRITEABLE)) == 0)

struct md_object {
	void *dummy_unused;
};

struct pmap_statistics {
	long resident_count;
	long wired_count;
};

typedef struct pmap_statistics *pmap_statistics_t;

struct pmap {
	struct pmap_statistics pm_stats;
};

typedef struct pmap *pmap_t;

#define pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)
#define pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)
#define pmap_resident_tlnw_count(pmap)	\
	((pmap)->pm_stats.resident_count - (pmap)->pm_stats.wired_count)

#ifdef _KERNEL
extern struct pmap *kernel_pmap;
extern char *ptvmmap;

void	pmap_release(struct pmap *pmap);
void	pmap_page_set_memattr(struct vm_page *m, vm_memattr_t ma);

/*
 * ARM64 does not emulate AD bits in software; the hardware handles them.
 */
static __inline int
pmap_emulate_ad_bits(pmap_t pmap __unused) {
	return (0);
}
#endif /* _KERNEL */

#endif /* !_MACHINE_PMAP_H_ */
