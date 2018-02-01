/*-
 * Copyright (c) 2006 Peter Wemm
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * AMD64 machine dependent routines for kvm and minidumps.
 */

#include <sys/user.h>	   /* MUST BE FIRST */
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/fnv_hash.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <nlist.h>
#include <kvm.h>

#include <vm/vm.h>
#include <vm/vm_param.h>

#include <machine/elf.h>
#include <machine/cpufunc.h>
#include <machine/minidump.h>

#include <limits.h>

#include "kvm_private.h"

struct hpte {
	struct hpte *next;
	vm_paddr_t pa;
	int64_t off;
};

#define HPT_SIZE 1024

/* minidump must be the first item! */
struct vmstate {
	int minidump;		/* 1 = minidump mode */
	int pgtable;		/* pagetable mode */
	void *hpt_head[HPT_SIZE];
	uint64_t *bitmap;
	uint64_t *ptemap;
	uint64_t kernbase;
	uint64_t dmapbase;
	uint64_t dmapend;
	uint64_t bitmapsize;
};

static void
hpt_insert(kvm_t *kd, vm_paddr_t pa, int64_t off)
{
	struct hpte *hpte;
	uint32_t fnv = FNV1_32_INIT;

	fnv = fnv_32_buf(&pa, sizeof(pa), fnv);
	fnv &= (HPT_SIZE - 1);
	hpte = malloc(sizeof(*hpte));
	hpte->pa = pa;
	hpte->off = off;
	hpte->next = kd->vmst->hpt_head[fnv];
	kd->vmst->hpt_head[fnv] = hpte;
}

static int64_t
hpt_find(kvm_t *kd, vm_paddr_t pa)
{
	struct hpte *hpte;
	uint32_t fnv = FNV1_32_INIT;

	fnv = fnv_32_buf(&pa, sizeof(pa), fnv);
	fnv &= (HPT_SIZE - 1);
	for (hpte = kd->vmst->hpt_head[fnv]; hpte != NULL; hpte = hpte->next) {
		if (pa == hpte->pa)
			return (hpte->off);
	}
	return (-1);
}

static int
inithash(kvm_t *kd, uint64_t *base, int len, off_t off)
{
	uint64_t idx;
	uint64_t bit, bits;
	vm_paddr_t pa;

	for (idx = 0; idx < len / sizeof(*base); idx++) {
		bits = base[idx];
		while (bits) {
			bit = bsfq(bits);
			bits &= ~(1ul << bit);
			pa = (idx * sizeof(*base) * NBBY + bit) * PAGE_SIZE;
			hpt_insert(kd, pa, off);
			off += PAGE_SIZE;
		}
	}
	return (off);
}

void
_kvm_minidump_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	if (vm->bitmap)
		free(vm->bitmap);
	if (vm->ptemap)
		free(vm->ptemap);
	free(vm);
	kd->vmst = NULL;
}

static int _kvm_minidump_init_hdr1(kvm_t *kd, struct vmstate *vmst,
			struct minidumphdr1 *hdr);
static int _kvm_minidump_init_hdr2(kvm_t *kd, struct vmstate *vmst,
			struct minidumphdr2 *hdr);

int
_kvm_minidump_initvtop(kvm_t *kd)
{
	struct vmstate *vmst;
	int error;
	union {
		struct minidumphdr1 hdr1;
		struct minidumphdr2 hdr2;
	} u;

	vmst = _kvm_malloc(kd, sizeof(*vmst));
	if (vmst == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate vm");
		return (-1);
	}
	kd->vmst = vmst;
	bzero(vmst, sizeof(*vmst));
	vmst->minidump = 1;

	if (pread(kd->pmfd, &u, sizeof(u), 0) != sizeof(u)) {
		_kvm_err(kd, kd->program, "cannot read dump header");
		return (-1);
	}
	if (strncmp(MINIDUMP1_MAGIC, u.hdr1.magic, sizeof(u.hdr1.magic)) == 0 &&
	    u.hdr1.version == MINIDUMP1_VERSION) {
		error = _kvm_minidump_init_hdr1(kd, vmst, &u.hdr1);
	} else
	if (strncmp(MINIDUMP2_MAGIC, u.hdr1.magic, sizeof(u.hdr1.magic)) == 0 &&
	    u.hdr2.version == MINIDUMP2_VERSION) {
		error = _kvm_minidump_init_hdr2(kd, vmst, &u.hdr2);
	} else {
		_kvm_err(kd, kd->program, "not a minidump for this platform");
		error = -1;
	}
	return error;
}

static
int
_kvm_minidump_init_hdr1(kvm_t *kd, struct vmstate *vmst,
			struct minidumphdr1 *hdr)
{
	off_t off;

	/* Skip header and msgbuf */
	off = PAGE_SIZE + round_page(hdr->msgbufsize);

	vmst->bitmap = _kvm_malloc(kd, hdr->bitmapsize);
	if (vmst->bitmap == NULL) {
		_kvm_err(kd, kd->program,
			 "cannot allocate %jd bytes for bitmap",
			 (intmax_t)hdr->bitmapsize);
		return (-1);
	}
	if (pread(kd->pmfd, vmst->bitmap, hdr->bitmapsize, off) !=
	    hdr->bitmapsize) {
		_kvm_err(kd, kd->program,
			 "cannot read %jd bytes for page bitmap",
			 (intmax_t)hdr->bitmapsize);
		return (-1);
	}
	off += round_page(vmst->bitmapsize);

	vmst->ptemap = _kvm_malloc(kd, hdr->ptesize);
	if (vmst->ptemap == NULL) {
		_kvm_err(kd, kd->program,
			 "cannot allocate %jd bytes for ptemap",
			 (intmax_t)hdr->ptesize);
		return (-1);
	}
	if (pread(kd->pmfd, vmst->ptemap, hdr->ptesize, off) !=
	    hdr->ptesize) {
		_kvm_err(kd, kd->program,
			 "cannot read %jd bytes for ptemap",
			 (intmax_t)hdr->ptesize);
		return (-1);
	}
	off += hdr->ptesize;

	vmst->kernbase = hdr->kernbase;
	vmst->dmapbase = hdr->dmapbase;
	vmst->dmapend = hdr->dmapend;
	vmst->bitmapsize = hdr->bitmapsize;
	vmst->pgtable = 0;

	/* build physical address hash table for sparse pages */
	inithash(kd, vmst->bitmap, hdr->bitmapsize, off);

	return (0);
}

static
int
_kvm_minidump_init_hdr2(kvm_t *kd, struct vmstate *vmst,
			struct minidumphdr2 *hdr)
{
	off_t off;

	/* Skip header and msgbuf */
	off = PAGE_SIZE + round_page(hdr->msgbufsize);

	vmst->bitmap = _kvm_malloc(kd, hdr->bitmapsize);
	if (vmst->bitmap == NULL) {
		_kvm_err(kd, kd->program,
			 "cannot allocate %jd bytes for bitmap",
			 (intmax_t)hdr->bitmapsize);
		return (-1);
	}
	if (pread(kd->pmfd, vmst->bitmap, hdr->bitmapsize, off) !=
	    hdr->bitmapsize) {
		_kvm_err(kd, kd->program,
			 "cannot read %jd bytes for page bitmap",
			 (intmax_t)hdr->bitmapsize);
		return (-1);
	}
	off += round_page(hdr->bitmapsize);

	vmst->ptemap = _kvm_malloc(kd, hdr->ptesize);
	if (vmst->ptemap == NULL) {
		_kvm_err(kd, kd->program,
			 "cannot allocate %jd bytes for ptemap",
			 (intmax_t)hdr->ptesize);
		return (-1);
	}
	if (pread(kd->pmfd, vmst->ptemap, hdr->ptesize, off) !=
	    hdr->ptesize) {
		_kvm_err(kd, kd->program,
			 "cannot read %jd bytes for ptemap",
			 (intmax_t)hdr->ptesize);
		return (-1);
	}
	off += hdr->ptesize;

	vmst->kernbase = hdr->kernbase;
	vmst->dmapbase = hdr->dmapbase;
	vmst->bitmapsize = hdr->bitmapsize;
	vmst->pgtable = 1;

	/* build physical address hash table for sparse pages */
	inithash(kd, vmst->bitmap, hdr->bitmapsize, off);

	return (0);
}

static int
_kvm_minidump_vatop(kvm_t *kd, u_long va, off_t *pa)
{
	struct vmstate *vm;
	u_long offset;
	pt_entry_t pte;
	u_long pteindex;
	u_long a;
	off_t ofs;

	vm = kd->vmst;
	offset = va & (PAGE_SIZE - 1);

	if (va >= vm->kernbase) {
		switch (vm->pgtable) {
		case 0:
			/*
			 * Page tables are specifically dumped (old style)
			 */
			pteindex = (va - vm->kernbase) >> PAGE_SHIFT;
			pte = vm->ptemap[pteindex];
			if (((u_long)pte & X86_PG_V) == 0) {
				_kvm_err(kd, kd->program,
					 "_kvm_vatop: pte not valid");
				goto invalid;
			}
			a = pte & PG_FRAME;
			break;
		case 1:
			/*
			 * Kernel page table pages are included in the
			 * sparse map.  We only dump the contents of
			 * the PDs (zero-filling any empty entries).
			 *
			 * Index of PD entry in PDP & PDP in PML4E together.
			 *
			 * First shift by 30 (1GB) - gives us an index
			 * into PD entries.  We do not PDP entries in the
			 * PML4E, so there are 512 * 512 PD entries possible.
			 */
			pteindex = (va >> PDPSHIFT) & (512 * 512 - 1);
			pte = vm->ptemap[pteindex];
			if ((pte & X86_PG_V) == 0) {
				_kvm_err(kd, kd->program,
					 "_kvm_vatop: pd not valid");
				goto invalid;
			}
			if (pte & X86_PG_PS) {		/* 1GB pages */
				pte += va & (1024 * 1024 * 1024 - 1);
				goto shortcut;
			}
			ofs = hpt_find(kd, pte & PG_FRAME);
			if (ofs == -1) {
				_kvm_err(kd, kd->program,
					 "_kvm_vatop: no phys page for pd");
				goto invalid;
			}

			/*
			 * Index of PT entry in PD
			 */
			pteindex = (va >> PDRSHIFT) & 511;
			if (pread(kd->pmfd, &pte, sizeof(pte),
			      ofs + pteindex * sizeof(pte)) != sizeof(pte)) {
				_kvm_err(kd, kd->program,
					 "_kvm_vatop: pd lookup not valid");
				goto invalid;
			}
			if ((pte & X86_PG_V) == 0) {
				_kvm_err(kd, kd->program,
					 "_kvm_vatop: pt not valid");
				goto invalid;
			}
			if (pte & X86_PG_PS) {		/* 2MB pages */
				pte += va & (2048 * 1024 - 1);
				goto shortcut;
			}
			ofs = hpt_find(kd, pte & PG_FRAME);
			if (ofs == -1) {
				_kvm_err(kd, kd->program,
					 "_kvm_vatop: no phys page for pt");
				goto invalid;
			}

			/*
			 * Index of pte entry in PT
			 */
			pteindex = (va >> PAGE_SHIFT) & 511;
			if (pread(kd->pmfd, &pte, sizeof(pte),
			      ofs + pteindex * sizeof(pte)) != sizeof(pte)) {
				_kvm_err(kd, kd->program,
					 "_kvm_vatop: pte lookup not valid");
				goto invalid;
			}

			/*
			 * Calculate end page
			 */
shortcut:
			a = pte & PG_FRAME;
			break;
		default:
			_kvm_err(kd, kd->program,
				 "_kvm_vatop: bad pgtable mode ");
			goto invalid;
		}
		ofs = hpt_find(kd, a);
		if (ofs == -1) {
			_kvm_err(kd, kd->program, "_kvm_vatop: physical address 0x%lx not in minidump", a);
			goto invalid;
		}
		*pa = ofs + offset;
		return (PAGE_SIZE - offset);
	} else if (va >= vm->dmapbase && va < vm->dmapend) {
		a = (va - vm->dmapbase) & ~PAGE_MASK;
		ofs = hpt_find(kd, a);
		if (ofs == -1) {
			_kvm_err(kd, kd->program, "_kvm_vatop: direct map address 0x%lx not in minidump", va);
			goto invalid;
		}
		*pa = ofs + offset;
		return (PAGE_SIZE - offset);
	} else {
		_kvm_err(kd, kd->program, "_kvm_vatop: virtual address 0x%lx not minidumped", va);
		goto invalid;
	}

invalid:
	_kvm_err(kd, 0, "invalid address (0x%lx)", va);
	return (0);
}

int
_kvm_minidump_kvatop(kvm_t *kd, u_long va, off_t *pa)
{
	if (kvm_ishost(kd)) {
		_kvm_err(kd, 0, "kvm_vatop called in live kernel!");
		return((off_t)0);
	}

	return (_kvm_minidump_vatop(kd, va, pa));
}
