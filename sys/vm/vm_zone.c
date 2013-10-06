/*
 * (MPSAFE)
 *
 * Copyright (c) 1997, 1998 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice immediately at the beginning of the file, without modification,
 *	this list of conditions, and the following disclaimer.
 * 2. Absolutely no warranty of function or purpose is made by the author
 *	John S. Dyson.
 *
 * $FreeBSD: src/sys/vm/vm_zone.c,v 1.30.2.6 2002/10/10 19:50:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <sys/spinlock2.h>
#include <vm/vm_page2.h>

static MALLOC_DEFINE(M_ZONE, "ZONE", "Zone header");

#define	ZONE_ERROR_INVALID 0
#define	ZONE_ERROR_NOTFREE 1
#define	ZONE_ERROR_ALREADYFREE 2

#define ZONE_ROUNDING	32

#define	ZENTRY_FREE	0x12342378

int zone_burst = 32;

static void *zget(vm_zone_t z);

/*
 * Return an item from the specified zone.   This function is non-blocking for
 * ZONE_INTERRUPT zones.
 *
 * No requirements.
 */
void *
zalloc(vm_zone_t z)
{
	globaldata_t gd = mycpu;
	void *item;
	int n;

#ifdef INVARIANTS
	if (z == NULL)
		zerror(ZONE_ERROR_INVALID);
#endif
retry:
	/*
	 * Avoid spinlock contention by allocating from a per-cpu queue
	 */
	if (z->zfreecnt_pcpu[gd->gd_cpuid] > 0) {
		crit_enter_gd(gd);
		if (z->zfreecnt_pcpu[gd->gd_cpuid] > 0) {
			item = z->zitems_pcpu[gd->gd_cpuid];
#ifdef INVARIANTS
			KASSERT(item != NULL,
				("zitems_pcpu unexpectedly NULL"));
			if (((void **)item)[1] != (void *)ZENTRY_FREE)
				zerror(ZONE_ERROR_NOTFREE);
			((void **)item)[1] = NULL;
#endif
			z->zitems_pcpu[gd->gd_cpuid] = ((void **) item)[0];
			--z->zfreecnt_pcpu[gd->gd_cpuid];
			z->znalloc++;
			crit_exit_gd(gd);
			return item;
		}
		crit_exit_gd(gd);
	}

	/*
	 * Per-zone spinlock for the remainder.  Always load at least one
	 * item.
	 */
	spin_lock(&z->zlock);
	if (z->zfreecnt > z->zfreemin) {
		n = zone_burst;
		do {
			item = z->zitems;
#ifdef INVARIANTS
			KASSERT(item != NULL, ("zitems unexpectedly NULL"));
			if (((void **)item)[1] != (void *)ZENTRY_FREE)
				zerror(ZONE_ERROR_NOTFREE);
#endif
			z->zitems = ((void **)item)[0];
			z->zfreecnt--;
			((void **)item)[0] = z->zitems_pcpu[gd->gd_cpuid];
			z->zitems_pcpu[gd->gd_cpuid] = item;
			++z->zfreecnt_pcpu[gd->gd_cpuid];
		} while (--n > 0 && z->zfreecnt > z->zfreemin);
		spin_unlock(&z->zlock);
		goto retry;
	} else {
		spin_unlock(&z->zlock);
		item = zget(z);
		/*
		 * PANICFAIL allows the caller to assume that the zalloc()
		 * will always succeed.  If it doesn't, we panic here.
		 */
		if (item == NULL && (z->zflags & ZONE_PANICFAIL))
			panic("zalloc(%s) failed", z->zname);
	}
	return item;
}

/*
 * Free an item to the specified zone.   
 *
 * No requirements.
 */
void
zfree(vm_zone_t z, void *item)
{
	globaldata_t gd = mycpu;
	int zmax;

	/*
	 * Avoid spinlock contention by freeing into a per-cpu queue
	 */
	if ((zmax = z->zmax) != 0)
		zmax = zmax / ncpus / 16;
	if (zmax < 64)
		zmax = 64;

	if (z->zfreecnt_pcpu[gd->gd_cpuid] < zmax) {
		crit_enter_gd(gd);
		((void **)item)[0] = z->zitems_pcpu[gd->gd_cpuid];
#ifdef INVARIANTS
		if (((void **)item)[1] == (void *)ZENTRY_FREE)
			zerror(ZONE_ERROR_ALREADYFREE);
		((void **)item)[1] = (void *)ZENTRY_FREE;
#endif
		z->zitems_pcpu[gd->gd_cpuid] = item;
		++z->zfreecnt_pcpu[gd->gd_cpuid];
		crit_exit_gd(gd);
		return;
	}

	/*
	 * Per-zone spinlock for the remainder.
	 */
	spin_lock(&z->zlock);
	((void **)item)[0] = z->zitems;
#ifdef INVARIANTS
	if (((void **)item)[1] == (void *)ZENTRY_FREE)
		zerror(ZONE_ERROR_ALREADYFREE);
	((void **)item)[1] = (void *)ZENTRY_FREE;
#endif
	z->zitems = item;
	z->zfreecnt++;
	spin_unlock(&z->zlock);
}

/*
 * This file comprises a very simple zone allocator.  This is used
 * in lieu of the malloc allocator, where needed or more optimal.
 *
 * Note that the initial implementation of this had coloring, and
 * absolutely no improvement (actually perf degradation) occurred.
 *
 * Note also that the zones are type stable.  The only restriction is
 * that the first two longwords of a data structure can be changed
 * between allocations.  Any data that must be stable between allocations
 * must reside in areas after the first two longwords.
 *
 * zinitna, zinit, zbootinit are the initialization routines.
 * zalloc, zfree, are the allocation/free routines.
 */

LIST_HEAD(zlist, vm_zone) zlist = LIST_HEAD_INITIALIZER(zlist);
static int sysctl_vm_zone(SYSCTL_HANDLER_ARGS);
static int zone_kmem_pages, zone_kern_pages;
static long zone_kmem_kvaspace;

/*
 * Create a zone, but don't allocate the zone structure.  If the
 * zone had been previously created by the zone boot code, initialize
 * various parts of the zone code.
 *
 * If waits are not allowed during allocation (e.g. during interrupt
 * code), a-priori allocate the kernel virtual space, and allocate
 * only pages when needed.
 *
 * Arguments:
 * z		pointer to zone structure.
 * obj		pointer to VM object (opt).
 * name		name of zone.
 * size		size of zone entries.
 * nentries	number of zone entries allocated (only ZONE_INTERRUPT.)
 * flags	ZONE_INTERRUPT -- items can be allocated at interrupt time.
 * zalloc	number of pages allocated when memory is needed.
 *
 * Note that when using ZONE_INTERRUPT, the size of the zone is limited
 * by the nentries argument.  The size of the memory allocatable is
 * unlimited if ZONE_INTERRUPT is not set.
 *
 * No requirements.
 */
int
zinitna(vm_zone_t z, vm_object_t obj, char *name, int size,
	int nentries, int flags, int zalloc)
{
	size_t totsize;

	/*
	 * Only zones created with zinit() are destroyable.
	 */
	if (z->zflags & ZONE_DESTROYABLE)
		panic("zinitna: can't create destroyable zone");

	/*
	 * NOTE: We can only adjust zsize if we previously did not
	 * 	 use zbootinit().
	 */
	if ((z->zflags & ZONE_BOOT) == 0) {
		z->zsize = (size + ZONE_ROUNDING - 1) & ~(ZONE_ROUNDING - 1);
		spin_init(&z->zlock);
		z->zfreecnt = 0;
		z->ztotal = 0;
		z->zmax = 0;
		z->zname = name;
		z->znalloc = 0;
		z->zitems = NULL;

		lwkt_gettoken(&vm_token);
		LIST_INSERT_HEAD(&zlist, z, zlink);
		lwkt_reltoken(&vm_token);

		bzero(z->zitems_pcpu, sizeof(z->zitems_pcpu));
		bzero(z->zfreecnt_pcpu, sizeof(z->zfreecnt_pcpu));
	}

	z->zkmvec = NULL;
	z->zkmcur = z->zkmmax = 0;
	z->zflags |= flags;

	/*
	 * If we cannot wait, allocate KVA space up front, and we will fill
	 * in pages as needed.  This is particularly required when creating
	 * an allocation space for map entries in kernel_map, because we
	 * do not want to go into a recursion deadlock with 
	 * vm_map_entry_reserve().
	 */
	if (z->zflags & ZONE_INTERRUPT) {
		totsize = round_page((size_t)z->zsize * nentries);
		atomic_add_long(&zone_kmem_kvaspace, totsize);

		z->zkva = kmem_alloc_pageable(&kernel_map, totsize);
		if (z->zkva == 0) {
			LIST_REMOVE(z, zlink);
			return 0;
		}

		z->zpagemax = totsize / PAGE_SIZE;
		if (obj == NULL) {
			z->zobj = vm_object_allocate(OBJT_DEFAULT, z->zpagemax);
		} else {
			z->zobj = obj;
			_vm_object_allocate(OBJT_DEFAULT, z->zpagemax, obj);
			vm_object_drop(obj);
		}
		z->zallocflag = VM_ALLOC_SYSTEM | VM_ALLOC_INTERRUPT |
				VM_ALLOC_NORMAL | VM_ALLOC_RETRY;
		z->zmax += nentries;
	} else {
		z->zallocflag = VM_ALLOC_NORMAL | VM_ALLOC_SYSTEM;
		z->zmax = 0;
	}


	if (z->zsize > PAGE_SIZE)
		z->zfreemin = 1;
	else
		z->zfreemin = PAGE_SIZE / z->zsize;

	z->zpagecount = 0;
	if (zalloc)
		z->zalloc = zalloc;
	else
		z->zalloc = 1;

	/*
	 * Populate the interrrupt zone at creation time rather than
	 * on first allocation, as this is a potentially long operation.
	 */
	if (z->zflags & ZONE_INTERRUPT) {
		void *buf;

		buf = zget(z);
		zfree(z, buf);
	}

	return 1;
}

/*
 * Subroutine same as zinitna, except zone data structure is allocated
 * automatically by malloc.  This routine should normally be used, except
 * in certain tricky startup conditions in the VM system -- then
 * zbootinit and zinitna can be used.  Zinit is the standard zone
 * initialization call.
 *
 * No requirements.
 */
vm_zone_t
zinit(char *name, int size, int nentries, int flags, int zalloc)
{
	vm_zone_t z;

	z = (vm_zone_t) kmalloc(sizeof (struct vm_zone), M_ZONE, M_NOWAIT);
	if (z == NULL)
		return NULL;

	z->zflags = 0;
	if (zinitna(z, NULL, name, size, nentries,
	            flags & ~ZONE_DESTROYABLE, zalloc) == 0) {
		kfree(z, M_ZONE);
		return NULL;
	}

	if (flags & ZONE_DESTROYABLE)
		z->zflags |= ZONE_DESTROYABLE;

	return z;
}

/*
 * Initialize a zone before the system is fully up.  This routine should
 * only be called before full VM startup.
 *
 * Called from the low level boot code only.
 */
void
zbootinit(vm_zone_t z, char *name, int size, void *item, int nitems)
{
	int i;

	bzero(z->zitems_pcpu, sizeof(z->zitems_pcpu));
	bzero(z->zfreecnt_pcpu, sizeof(z->zfreecnt_pcpu));

	z->zname = name;
	z->zsize = size;
	z->zpagemax = 0;
	z->zobj = NULL;
	z->zflags = ZONE_BOOT;
	z->zfreemin = 0;
	z->zallocflag = 0;
	z->zpagecount = 0;
	z->zalloc = 0;
	z->znalloc = 0;
	spin_init(&z->zlock);

	bzero(item, (size_t)nitems * z->zsize);
	z->zitems = NULL;
	for (i = 0; i < nitems; i++) {
		((void **)item)[0] = z->zitems;
#ifdef INVARIANTS
		((void **)item)[1] = (void *)ZENTRY_FREE;
#endif
		z->zitems = item;
		item = (uint8_t *)item + z->zsize;
	}
	z->zfreecnt = nitems;
	z->zmax = nitems;
	z->ztotal = nitems;

	lwkt_gettoken(&vm_token);
	LIST_INSERT_HEAD(&zlist, z, zlink);
	lwkt_reltoken(&vm_token);
}

/*
 * Release all resources owned by zone created with zinit().
 *
 * No requirements.
 */
void
zdestroy(vm_zone_t z)
{
	vm_page_t m;
	int i;

	if (z == NULL)
		panic("zdestroy: null zone");
	if ((z->zflags & ZONE_DESTROYABLE) == 0)
		panic("zdestroy: undestroyable zone");

	lwkt_gettoken(&vm_token);
	LIST_REMOVE(z, zlink);
	lwkt_reltoken(&vm_token);

	/*
	 * Release virtual mappings, physical memory and update sysctl stats.
	 */
	if (z->zflags & ZONE_INTERRUPT) {
		/*
		 * Pages mapped via pmap_kenter() must be removed from the
		 * kernel_pmap() before calling kmem_free() to avoid issues
		 * with kernel_pmap.pm_stats.resident_count.
		 */
		pmap_qremove(z->zkva, z->zpagemax);
		vm_object_hold(z->zobj);
		for (i = 0; i < z->zpagecount; ++i) {
			m = vm_page_lookup_busy_wait(z->zobj, i, TRUE, "vmzd");
			vm_page_unwire(m, 0);
			vm_page_free(m);
		}

		/*
		 * Free the mapping.
		 */
		kmem_free(&kernel_map, z->zkva,
			  (size_t)z->zpagemax * PAGE_SIZE);
		atomic_subtract_long(&zone_kmem_kvaspace,
				     (size_t)z->zpagemax * PAGE_SIZE);

		/*
		 * Free the backing object and physical pages.
		 */
		vm_object_deallocate(z->zobj);
		vm_object_drop(z->zobj);
		atomic_subtract_int(&zone_kmem_pages, z->zpagecount);
	} else {
		for (i=0; i < z->zkmcur; i++) {
			kmem_free(&kernel_map, z->zkmvec[i],
				  (size_t)z->zalloc * PAGE_SIZE);
			atomic_subtract_int(&zone_kern_pages, z->zalloc);
		}
		if (z->zkmvec != NULL)
			kfree(z->zkmvec, M_ZONE);
	}

	spin_uninit(&z->zlock);
	kfree(z, M_ZONE);
}


/*
 * void *zalloc(vm_zone_t zone) --
 *	Returns an item from a specified zone.  May not be called from a
 *	FAST interrupt or IPI function.
 *
 * void zfree(vm_zone_t zone, void *item) --
 *	Frees an item back to a specified zone.  May not be called from a
 *	FAST interrupt or IPI function.
 */

/*
 * Internal zone routine.  Not to be called from external (non vm_zone) code.
 *
 * No requirements.
 */
static void *
zget(vm_zone_t z)
{
	int i;
	vm_page_t m;
	int nitems;
	int npages;
	int savezpc;
	size_t nbytes;
	size_t noffset;
	void *item;

	if (z == NULL)
		panic("zget: null zone");

	if (z->zflags & ZONE_INTERRUPT) {
		/*
		 * Interrupt zones do not mess with the kernel_map, they
		 * simply populate an existing mapping.
		 *
		 * First reserve the required space.
		 */
		vm_object_hold(z->zobj);
		noffset = (size_t)z->zpagecount * PAGE_SIZE;
		noffset -= noffset % z->zsize;
		savezpc = z->zpagecount;
		if (z->zpagecount + z->zalloc > z->zpagemax)
			z->zpagecount = z->zpagemax;
		else
			z->zpagecount += z->zalloc;
		item = (char *)z->zkva + noffset;
		npages = z->zpagecount - savezpc;
		nitems = ((size_t)(savezpc + npages) * PAGE_SIZE - noffset) /
			 z->zsize;
		atomic_add_int(&zone_kmem_pages, npages);

		/*
		 * Now allocate the pages.  Note that we can block in the
		 * loop, so we've already done all the necessary calculations
		 * and reservations above.
		 */
		for (i = 0; i < npages; ++i) {
			vm_offset_t zkva;

			m = vm_page_alloc(z->zobj, savezpc + i, z->zallocflag);
			KKASSERT(m != NULL);
			/* note: z might be modified due to blocking */

			KKASSERT(m->queue == PQ_NONE);
			m->valid = VM_PAGE_BITS_ALL;
			vm_page_wire(m);
			vm_page_wakeup(m);

			zkva = z->zkva + (size_t)(savezpc + i) * PAGE_SIZE;
			pmap_kenter(zkva, VM_PAGE_TO_PHYS(m));
			bzero((void *)zkva, PAGE_SIZE);
		}
		vm_object_drop(z->zobj);
	} else if (z->zflags & ZONE_SPECIAL) {
		/*
		 * The special zone is the one used for vm_map_entry_t's.
		 * We have to avoid an infinite recursion in 
		 * vm_map_entry_reserve() by using vm_map_entry_kreserve()
		 * instead.  The map entries are pre-reserved by the kernel
		 * by vm_map_entry_reserve_cpu_init().
		 */
		nbytes = (size_t)z->zalloc * PAGE_SIZE;

		item = (void *)kmem_alloc3(&kernel_map, nbytes, KM_KRESERVE);

		/* note: z might be modified due to blocking */
		if (item != NULL) {
			zone_kern_pages += z->zalloc;	/* not MP-safe XXX */
			bzero(item, nbytes);
		} else {
			nbytes = 0;
		}
		nitems = nbytes / z->zsize;
	} else {
		/*
		 * Otherwise allocate KVA from the kernel_map.
		 */
		nbytes = (size_t)z->zalloc * PAGE_SIZE;

		item = (void *)kmem_alloc3(&kernel_map, nbytes, 0);

		/* note: z might be modified due to blocking */
		if (item != NULL) {
			zone_kern_pages += z->zalloc;	/* not MP-safe XXX */
			bzero(item, nbytes);

			if (z->zflags & ZONE_DESTROYABLE) {
				if (z->zkmcur == z->zkmmax) {
					z->zkmmax =
						z->zkmmax==0 ? 1 : z->zkmmax*2;
					z->zkmvec = krealloc(z->zkmvec,
					    z->zkmmax * sizeof(z->zkmvec[0]),
					    M_ZONE, M_WAITOK);
				}
				z->zkmvec[z->zkmcur++] = (vm_offset_t)item;
			}
		} else {
			nbytes = 0;
		}
		nitems = nbytes / z->zsize;
	}

	spin_lock(&z->zlock);
	z->ztotal += nitems;
	/*
	 * Save one for immediate allocation
	 */
	if (nitems != 0) {
		nitems -= 1;
		for (i = 0; i < nitems; i++) {
			((void **)item)[0] = z->zitems;
#ifdef INVARIANTS
			((void **)item)[1] = (void *)ZENTRY_FREE;
#endif
			z->zitems = item;
			item = (uint8_t *)item + z->zsize;
		}
		z->zfreecnt += nitems;
		z->znalloc++;
	} else if (z->zfreecnt > 0) {
		item = z->zitems;
		z->zitems = ((void **)item)[0];
#ifdef INVARIANTS
		if (((void **)item)[1] != (void *)ZENTRY_FREE)
			zerror(ZONE_ERROR_NOTFREE);
		((void **) item)[1] = NULL;
#endif
		z->zfreecnt--;
		z->znalloc++;
	} else {
		item = NULL;
	}
	spin_unlock(&z->zlock);

	/*
	 * A special zone may have used a kernel-reserved vm_map_entry.  If
	 * so we have to be sure to recover our reserve so we don't run out.
	 * We will panic if we run out.
	 */
	if (z->zflags & ZONE_SPECIAL)
		vm_map_entry_reserve(0);

	return item;
}

/*
 * No requirements.
 */
static int
sysctl_vm_zone(SYSCTL_HANDLER_ARGS)
{
	int error=0;
	vm_zone_t curzone;
	char tmpbuf[128];
	char tmpname[14];

	ksnprintf(tmpbuf, sizeof(tmpbuf),
	    "\nITEM            SIZE     LIMIT    USED    FREE  REQUESTS\n");
	error = SYSCTL_OUT(req, tmpbuf, strlen(tmpbuf));
	if (error)
		return (error);

	lwkt_gettoken(&vm_token);
	LIST_FOREACH(curzone, &zlist, zlink) {
		int i;
		int n;
		int len;
		int offset;
		int freecnt;

		len = strlen(curzone->zname);
		if (len >= (sizeof(tmpname) - 1))
			len = (sizeof(tmpname) - 1);
		for(i = 0; i < sizeof(tmpname) - 1; i++)
			tmpname[i] = ' ';
		tmpname[i] = 0;
		memcpy(tmpname, curzone->zname, len);
		tmpname[len] = ':';
		offset = 0;
		if (curzone == LIST_FIRST(&zlist)) {
			offset = 1;
			tmpbuf[0] = '\n';
		}
		freecnt = curzone->zfreecnt;
		for (n = 0; n < ncpus; ++n)
			freecnt += curzone->zfreecnt_pcpu[n];

		ksnprintf(tmpbuf + offset, sizeof(tmpbuf) - offset,
			"%s %6.6u, %8.8u, %6.6u, %6.6u, %8.8u\n",
			tmpname, curzone->zsize, curzone->zmax,
			(curzone->ztotal - freecnt),
			freecnt, curzone->znalloc);

		len = strlen((char *)tmpbuf);
		if (LIST_NEXT(curzone, zlink) == NULL)
			tmpbuf[len - 1] = 0;

		error = SYSCTL_OUT(req, tmpbuf, len);

		if (error)
			break;
	}
	lwkt_reltoken(&vm_token);
	return (error);
}

#if defined(INVARIANTS)

/*
 * Debugging only.
 */
void
zerror(int error)
{
	char *msg;

	switch (error) {
	case ZONE_ERROR_INVALID:
		msg = "zone: invalid zone";
		break;
	case ZONE_ERROR_NOTFREE:
		msg = "zone: entry not free";
		break;
	case ZONE_ERROR_ALREADYFREE:
		msg = "zone: freeing free entry";
		break;
	default:
		msg = "zone: invalid error";
		break;
	}
	panic("%s", msg);
}
#endif

SYSCTL_OID(_vm, OID_AUTO, zone, CTLTYPE_STRING|CTLFLAG_RD, \
	NULL, 0, sysctl_vm_zone, "A", "Zone Info");

SYSCTL_INT(_vm, OID_AUTO, zone_kmem_pages,
	CTLFLAG_RD, &zone_kmem_pages, 0, "Number of interrupt safe pages allocated by zone");
SYSCTL_INT(_vm, OID_AUTO, zone_burst,
	CTLFLAG_RW, &zone_burst, 0, "Burst from depot to pcpu cache");
SYSCTL_LONG(_vm, OID_AUTO, zone_kmem_kvaspace,
	CTLFLAG_RD, &zone_kmem_kvaspace, 0, "KVA space allocated by zone");
SYSCTL_INT(_vm, OID_AUTO, zone_kern_pages,
	CTLFLAG_RD, &zone_kern_pages, 0, "Number of non-interrupt safe pages allocated by zone");
