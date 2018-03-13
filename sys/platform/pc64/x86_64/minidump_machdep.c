/*-
 * Copyright (c) 2006 Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/amd64/amd64/minidump_machdep.c,v 1.10 2009/05/29 21:27:12 jamie Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/device.h>
#include <sys/globaldata.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/msgbuf.h>
#include <sys/kbio.h>
#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <machine/atomic.h>
#include <machine/elf.h>
#include <machine/globaldata.h>
#include <machine/md_var.h>
#include <machine/vmparam.h>
#include <machine/minidump.h>

CTASSERT(sizeof(struct kerneldumpheader) == 512);

/*
 * Don't touch the first SIZEOF_METADATA bytes on the dump device. This
 * is to protect us from metadata and to protect metadata from us.
 */
#define	SIZEOF_METADATA		(64*1024)

#define	MD_ALIGN(x)	(((off_t)(x) + PAGE_MASK) & ~PAGE_MASK)
#define	DEV_ALIGN(x)	roundup2((off_t)(x), DEV_BSIZE)

extern uint64_t KPDPphys;

uint64_t *vm_page_dump;
vm_offset_t vm_page_dump_size;

static struct kerneldumpheader kdh;
static off_t dumplo;

/* Handle chunked writes. */
static size_t fragsz;
static void *dump_va;
static size_t counter, progress;

CTASSERT(sizeof(*vm_page_dump) == 8);

static int
is_dumpable(vm_paddr_t pa)
{
	int i;

	for (i = 0; dump_avail[i].phys_beg || dump_avail[i].phys_end; ++i) {
		if (pa >= dump_avail[i].phys_beg && pa < dump_avail[i].phys_end)
			return (1);
	}
	return (0);
}

#define PG2MB(pgs) (((pgs) + (1 << 8) - 1) >> 8)

static int
blk_flush(struct dumperinfo *di)
{
	int error;

	if (fragsz == 0)
		return (0);

	error = dev_ddump(di->priv, dump_va, 0, dumplo, fragsz);
	dumplo += fragsz;
	fragsz = 0;
	return (error);
}

static int
blk_write(struct dumperinfo *di, char *ptr, vm_paddr_t pa, size_t sz)
{
	size_t len;
	int error, i, c;
	int max_iosize;

	error = 0;
	if ((sz & PAGE_MASK)) {
		kprintf("size not page aligned\n");
		return (EINVAL);
	}
	if (ptr != NULL && pa != 0) {
		kprintf("can't have both va and pa!\n");
		return (EINVAL);
	}
	if (pa != 0 && (((uintptr_t)pa) & PAGE_MASK) != 0) {
		kprintf("address not page aligned\n");
		return (EINVAL);
	}
	if (ptr != NULL) {
		/*
		 * If we're doing a virtual dump, flush any
		 * pre-existing pa pages
		 */
		error = blk_flush(di);
		if (error)
			return (error);
	}
	max_iosize = min(MAXPHYS, di->maxiosize);
	while (sz) {
		len = max_iosize - fragsz;
		if (len > sz)
			len = sz;
		counter += len;
		progress -= len;
		if (counter >> 24) {
			kprintf(" %ld", PG2MB(progress >> PAGE_SHIFT));
			counter &= (1<<24) - 1;
		}
		if (ptr) {
			/*kprintf("s");*/
			error = dev_ddump(di->priv, ptr, 0, dumplo, len);
			/* kprintf("t");*/
			if (error)
				return (error);
			dumplo += len;
			ptr += len;
			sz -= len;
		} else {
			for (i = 0; i < len; i += PAGE_SIZE) {
				dump_va = pmap_kenter_temporary(pa + i,
						(i + fragsz) >> PAGE_SHIFT);
			}
			smp_invltlb();
			fragsz += len;
			pa += len;
			sz -= len;
			if (fragsz == max_iosize) {
				error = blk_flush(di);
				if (error)
					return (error);
			}
		}
	}

	/* Check for user abort. */
	c = cncheckc();
	if (c == 0x03)
		return (ECANCELED);
	if (c != -1 && c != NOKEY)
		kprintf(" (CTRL-C to abort) ");

	return (0);
}

/* A fake page table page, to avoid having to handle both 4K and 2M pages */
static pt_entry_t fakept[NPTEPG];

void
minidumpsys(struct dumperinfo *di)
{
	uint64_t dumpsize;
	uint64_t ptesize;
	vm_offset_t va;
	vm_offset_t kern_end;
	int error;
	uint64_t bits;
	uint64_t *pdp, *pd, *pt, pa;
	int i, j, k, bit;
	int kpdp, klo, khi;
	int lpdp = -1;
	long lpdpttl = 0;
	struct minidumphdr2 mdhdr;
	struct mdglobaldata *md;

	cnpoll(TRUE);
	counter = 0;

	/*
	 * minidump page table format is an array of PD entries (1GB pte's),
	 * representing the entire user and kernel virtual address space
	 * (256TB).
	 *
	 * However, we will only dump the KVM portion of this space.  And we
	 * only copy the PDP pages for direct access, the PD and PT pages
	 * will be included in the dump as part of the physical map.
	 */
	ptesize = NPML4EPG * NPDPEPG * 8;

	/*
	 * Walk page table pages, set bits in vm_page_dump.
	 *
	 * NOTE: kernel_vm_end can actually be below KERNBASE.
	 * 	 Just use KvaEnd.  Also note that loops which go
	 *	 all the way to the end of the address space might
	 *	 overflow the loop variable.
	 */
	md = (struct mdglobaldata *)globaldata_find(0);

	kern_end = KvaEnd;
	if (kern_end < (vm_offset_t)&(md[ncpus]))
		kern_end = (vm_offset_t)&(md[ncpus]);

	pdp = (uint64_t *)PHYS_TO_DMAP(KPDPphys);
	for (va = VM_MIN_KERNEL_ADDRESS; va < kern_end; va += NBPDR) {
		/*
		 * The loop probably overflows a 64-bit int due to NBPDR.
		 */
		if (va < VM_MIN_KERNEL_ADDRESS)
			break;

		/*
		 * KPDPphys[] is relative to VM_MIN_KERNEL_ADDRESS. It
		 * contains NKPML4E PDP pages (so we can get to all kernel
		 * PD entries from this array).
		 */
		i = ((va - VM_MIN_KERNEL_ADDRESS) >> PDPSHIFT) &
		    (NPML4EPG * NPDPEPG - 1);
		if (i != lpdp) {
			lpdp = i;
			lpdpttl = 0;
		}

		/*
		 * Calculate the PD index in the PDP.  Each PD represents 1GB.
		 * KVA space can cover multiple PDP pages.  The PDP array
		 * has been initialized for the entire kernel address space.
		 *
		 * We include the PD entries in the PDP in the dump
		 */
		i = ((va - VM_MIN_KERNEL_ADDRESS) >> PDPSHIFT) &
		    (NPML4EPG * NPDPEPG - 1);
		if ((pdp[i] & kernel_pmap.pmap_bits[PG_V_IDX]) == 0)
			continue;

		/*
		 * Add the PD page from the PDP to the dump
		 */
		dump_add_page(pdp[i] & PG_FRAME);
		lpdpttl += PAGE_SIZE;

		pd = (uint64_t *)PHYS_TO_DMAP(pdp[i] & PG_FRAME);
		j = ((va >> PDRSHIFT) & ((1ul << NPDEPGSHIFT) - 1));
		if ((pd[j] & (kernel_pmap.pmap_bits[PG_PS_IDX] | kernel_pmap.pmap_bits[PG_V_IDX])) ==
		    (kernel_pmap.pmap_bits[PG_PS_IDX] | kernel_pmap.pmap_bits[PG_V_IDX]))  {
			/* This is an entire 2M page. */
			lpdpttl += PAGE_SIZE * NPTEPG;
			pa = pd[j] & PG_PS_FRAME;
			for (k = 0; k < NPTEPG; k++) {
				if (is_dumpable(pa))
					dump_add_page(pa);
				pa += PAGE_SIZE;
			}
			continue;
		}
		if ((pd[j] & kernel_pmap.pmap_bits[PG_V_IDX]) ==
		    kernel_pmap.pmap_bits[PG_V_IDX]) {
			/*
			 * Add the PT page from the PD to the dump (it is no
			 * longer included in the ptemap.
			 */
			dump_add_page(pd[j] & PG_FRAME);
			lpdpttl += PAGE_SIZE;

			/* set bit for each valid page in this 2MB block */
			pt = (uint64_t *)PHYS_TO_DMAP(pd[j] & PG_FRAME);
			for (k = 0; k < NPTEPG; k++) {
				if ((pt[k] & kernel_pmap.pmap_bits[PG_V_IDX]) == kernel_pmap.pmap_bits[PG_V_IDX]) {
					pa = pt[k] & PG_FRAME;
					lpdpttl += PAGE_SIZE;
					if (is_dumpable(pa))
						dump_add_page(pa);
				}
			}
		} else {
			/* nothing, we're going to dump a null page */
		}
	}

	/* Calculate dump size. */
	dumpsize = ptesize;
	dumpsize += round_page(msgbufp->msg_size);
	dumpsize += round_page(vm_page_dump_size);

	for (i = 0; i < vm_page_dump_size / sizeof(*vm_page_dump); i++) {
		bits = vm_page_dump[i];
		while (bits) {
			bit = bsfq(bits);
			pa = (((uint64_t)i * sizeof(*vm_page_dump) * NBBY) + bit) * PAGE_SIZE;
			/* Clear out undumpable pages now if needed */
			if (is_dumpable(pa)) {
				dumpsize += PAGE_SIZE;
			} else {
				dump_drop_page(pa);
			}
			bits &= ~(1ul << bit);
		}
	}
	dumpsize += PAGE_SIZE;

	/* Determine dump offset on device. */
	if (di->mediasize < SIZEOF_METADATA + dumpsize + sizeof(kdh) * 2) {
		error = ENOSPC;
		goto fail;
	}
	dumplo = di->mediaoffset + di->mediasize - dumpsize;
	dumplo -= sizeof(kdh) * 2;
	progress = dumpsize;

	/* Initialize mdhdr */
	bzero(&mdhdr, sizeof(mdhdr));
	strcpy(mdhdr.magic, MINIDUMP2_MAGIC);
	mdhdr.version = MINIDUMP2_VERSION;
	mdhdr.msgbufsize = msgbufp->msg_size;
	mdhdr.bitmapsize = vm_page_dump_size;
	mdhdr.ptesize = ptesize;
	mdhdr.kernbase = VM_MIN_KERNEL_ADDRESS;
	mdhdr.dmapbase = DMAP_MIN_ADDRESS;
	mdhdr.dmapend = DMAP_MAX_ADDRESS;

	mkdumpheader(&kdh, KERNELDUMPMAGIC, KERNELDUMP_AMD64_VERSION,
		     dumpsize, di->blocksize);

	kprintf("Physical memory: %jd MB\n", (intmax_t)ptoa(physmem) / 1048576);
	kprintf("Dumping %jd MB:", (intmax_t)dumpsize >> 20);

	/* Dump leader */
	error = dev_ddump(di->priv, &kdh, 0, dumplo, sizeof(kdh));
	if (error)
		goto fail;
	dumplo += sizeof(kdh);

	/* Dump my header */
	bzero(fakept, sizeof(fakept));
	bcopy(&mdhdr, fakept, sizeof(mdhdr));
	error = blk_write(di, (char *)fakept, 0, PAGE_SIZE);
	if (error)
		goto fail;

	/* Dump msgbuf up front */
	error = blk_write(di, (char *)msgbufp->msg_ptr, 0, round_page(msgbufp->msg_size));
	if (error)
		goto fail;

	/* Dump bitmap */
	error = blk_write(di, (char *)vm_page_dump, 0, round_page(vm_page_dump_size));
	if (error)
		goto fail;

	/*
	 * Dump a full PDP array for the entire KVM space, user and kernel.
	 * This is 512*512 1G PD entries (512*512*8 = 2MB).
	 *
	 * The minidump only dumps PD entries related to KVA space.  Also
	 * note that pdp[] (aka KPDPphys[]) only covers VM_MIN_KERNEL_ADDRESS
	 * to VM_MAX_KERNEL_ADDRESS.
	 *
	 * The actual KPDPphys[] array covers a KVA space starting at KVA
	 * KPDPPHYS_KVA.
	 *
	 * By dumping a PDP[] array of PDs representing the entire virtual
	 * address space we can expand what we dump in the future.
	 */
	pdp = (uint64_t *)PHYS_TO_DMAP(KPDPphys);
	kpdp = (KPDPPHYS_KVA >> PDPSHIFT) &
		    (NPML4EPG * NPDPEPG - 1);
	klo = (int)(VM_MIN_KERNEL_ADDRESS >> PDPSHIFT) &
		    (NPML4EPG * NPDPEPG - 1);
	khi = (int)(VM_MAX_KERNEL_ADDRESS >> PDPSHIFT) &
		    (NPML4EPG * NPDPEPG - 1);

	for (i = 0; i < NPML4EPG * NPDPEPG; ++i) {
		if (i < klo || i > khi) {
			fakept[i & (NPDPEPG - 1)] = 0;
		} else {
			fakept[i & (NPDPEPG - 1)] = pdp[i - kpdp];
		}
		if ((i & (NPDPEPG - 1)) == (NPDPEPG - 1)) {
			error = blk_write(di, (char *)fakept, 0, PAGE_SIZE);
			if (error)
				goto fail;
			error = blk_flush(di);
			if (error)
				goto fail;
		}
	}

	/* Dump memory chunks */
	/* XXX cluster it up and use blk_dump() */
	for (i = 0; i < vm_page_dump_size / sizeof(*vm_page_dump); i++) {
		bits = vm_page_dump[i];
		while (bits) {
			bit = bsfq(bits);
			pa = (((uint64_t)i * sizeof(*vm_page_dump) * NBBY) + bit) * PAGE_SIZE;
			error = blk_write(di, 0, pa, PAGE_SIZE);
			if (error)
				goto fail;
			bits &= ~(1ul << bit);
		}
	}

	error = blk_flush(di);
	if (error)
		goto fail;

	/* Dump trailer */
	error = dev_ddump(di->priv, &kdh, 0, dumplo, sizeof(kdh));
	if (error)
		goto fail;
	dumplo += sizeof(kdh);

	/* Signal completion, signoff and exit stage left. */
	dev_ddump(di->priv, NULL, 0, 0, 0);
	kprintf("\nDump complete\n");
	cnpoll(FALSE);
	return;

 fail:
	cnpoll(FALSE);
	if (error < 0)
		error = -error;

	if (error == ECANCELED)
		kprintf("\nDump aborted\n");
	else if (error == ENOSPC)
		kprintf("\nDump failed. Partition too small.\n");
	else
		kprintf("\n** DUMP FAILED (ERROR %d) **\n", error);
}

void
dump_add_page(vm_paddr_t pa)
{
	int idx, bit;

	pa >>= PAGE_SHIFT;
	idx = pa >> 6;		/* 2^6 = 64 */
	bit = pa & 63;
	atomic_set_long(&vm_page_dump[idx], 1ul << bit);
}

void
dump_drop_page(vm_paddr_t pa)
{
	int idx, bit;

	pa >>= PAGE_SHIFT;
	idx = pa >> 6;		/* 2^6 = 64 */
	bit = pa & 63;
	atomic_clear_long(&vm_page_dump[idx], 1ul << bit);
}
