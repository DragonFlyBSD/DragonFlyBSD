/*
 * (MPSAFE)
 *
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 *	@(#)device_pager.c	8.1 (Berkeley) 6/11/93
 * $FreeBSD: src/sys/vm/device_pager.c,v 1.46.2.1 2000/08/02 21:54:37 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/mman.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/thread2.h>
#include <sys/mutex2.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_zone.h>
#include <vm/vm_page2.h>

static void dev_pager_dealloc (vm_object_t);
static int dev_pager_getpage (vm_object_t, vm_page_t *, int);
static void dev_pager_putpages (vm_object_t, vm_page_t *, int, int, int *);
static boolean_t dev_pager_haspage (vm_object_t, vm_pindex_t);

/* list of device pager objects */
static TAILQ_HEAD(, vm_page) dev_freepages_list =
		TAILQ_HEAD_INITIALIZER(dev_freepages_list);
static MALLOC_DEFINE(M_FICTITIOUS_PAGES, "device-mapped pages",
		"Device mapped pages");

static vm_page_t dev_pager_getfake (vm_paddr_t, int);
static void dev_pager_putfake (vm_page_t);

struct pagerops devicepagerops = {
	dev_pager_dealloc,
	dev_pager_getpage,
	dev_pager_putpages,
	dev_pager_haspage
};

/* list of device pager objects */
static struct pagerlst dev_pager_object_list =
		TAILQ_HEAD_INITIALIZER(dev_pager_object_list);
/* protect list manipulation */
static struct mtx dev_pager_mtx = MTX_INITIALIZER("devpgr");

static int old_dev_pager_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *pg_color);
static void old_dev_pager_dtor(void *handle);
static int old_dev_pager_fault(vm_object_t object, vm_ooffset_t offset,
    int prot, vm_page_t *mres);

static struct cdev_pager_ops old_dev_pager_ops = {
	.cdev_pg_ctor = old_dev_pager_ctor,
	.cdev_pg_dtor = old_dev_pager_dtor,
	.cdev_pg_fault = old_dev_pager_fault
};

vm_object_t
cdev_pager_lookup(void *handle)
{
	vm_object_t object;
	mtx_lock(&dev_pager_mtx);
	object = vm_pager_object_lookup(&dev_pager_object_list, handle);
	mtx_unlock(&dev_pager_mtx);
	return (object);
}

vm_object_t
cdev_pager_allocate(void *handle, enum obj_type tp, struct cdev_pager_ops *ops,
	vm_ooffset_t size, vm_prot_t prot, vm_ooffset_t foff, struct ucred *cred)
{
	cdev_t dev;
	vm_object_t object;
	u_short color;

	/*
	 * Offset should be page aligned.
	 */
	if (foff & PAGE_MASK)
		return (NULL);

	size = round_page64(size);

	if (ops->cdev_pg_ctor(handle, size, prot, foff, cred, &color) != 0)
		return (NULL);

	/*
	 * Look up pager, creating as necessary.
	 */
	mtx_lock(&dev_pager_mtx);
	object = vm_pager_object_lookup(&dev_pager_object_list, handle);
	if (object == NULL) {
		/*
		 * Allocate object and associate it with the pager.
		 */
		object = vm_object_allocate_hold(tp,
						 OFF_TO_IDX(foff + size));
		object->handle = handle;
		object->un_pager.devp.ops = ops;
		object->un_pager.devp.dev = handle;
		TAILQ_INIT(&object->un_pager.devp.devp_pglist);

		/*
		 * handle is only a device for old_dev_pager_ctor.
		 */
		if (ops->cdev_pg_ctor == old_dev_pager_ctor) {
			dev = handle;
			dev->si_object = object;
		}

		TAILQ_INSERT_TAIL(&dev_pager_object_list, object,
		    pager_object_list);

		vm_object_drop(object);
	} else {
		/*
		 * Gain a reference to the object.
		 */
		vm_object_hold(object);
		vm_object_reference_locked(object);
		if (OFF_TO_IDX(foff + size) > object->size)
			object->size = OFF_TO_IDX(foff + size);
		vm_object_drop(object);
	}
	mtx_unlock(&dev_pager_mtx);

	return (object);
}

/*
 * No requirements.
 */
vm_object_t
dev_pager_alloc(void *handle, off_t size, vm_prot_t prot, off_t foff)
{
	return (cdev_pager_allocate(handle, OBJT_DEVICE, &old_dev_pager_ops,
	    size, prot, foff, NULL));
}

/* XXX */
void
cdev_pager_free_page(vm_object_t object, vm_page_t m)
{
	if (object->type == OBJT_MGTDEVICE) {
		KKASSERT((m->flags & PG_FICTITIOUS) != 0);
		pmap_page_protect(m, VM_PROT_NONE);
		vm_page_remove(m);
		vm_page_wakeup(m);
	} else if (object->type == OBJT_DEVICE) {
		TAILQ_REMOVE(&object->un_pager.devp.devp_pglist, m, pageq);
		dev_pager_putfake(m);
	}
}

/*
 * No requirements.
 */
static void
dev_pager_dealloc(vm_object_t object)
{
	vm_page_t m;

	mtx_lock(&dev_pager_mtx);
        object->un_pager.devp.ops->cdev_pg_dtor(object->un_pager.devp.dev);

	TAILQ_REMOVE(&dev_pager_object_list, object, pager_object_list);

	if (object->type == OBJT_DEVICE) {
		/*
		 * Free up our fake pages.
		 */
		while ((m = TAILQ_FIRST(&object->un_pager.devp.devp_pglist)) !=
		       NULL) {
			TAILQ_REMOVE(&object->un_pager.devp.devp_pglist,
				     m, pageq);
			dev_pager_putfake(m);
		}
	}
	mtx_unlock(&dev_pager_mtx);
}

/*
 * No requirements.
 *
 * WARNING! Do not obtain dev_pager_mtx here, doing so will cause a
 *	    deadlock in DRMs VM paging code.
 */
static int
dev_pager_getpage(vm_object_t object, vm_page_t *mpp, int seqaccess)
{
	vm_page_t page;
	int error;

	page = *mpp;

	error = object->un_pager.devp.ops->cdev_pg_fault(
			object, IDX_TO_OFF(page->pindex),
			PROT_READ, mpp);

	return (error);
}

/*
 * No requirements.
 */
static void
dev_pager_putpages(vm_object_t object, vm_page_t *m,
		   int count, int sync, int *rtvals)
{
	panic("dev_pager_putpage called");
}

/*
 * No requirements.
 */
static boolean_t
dev_pager_haspage(vm_object_t object, vm_pindex_t pindex)
{
	return (TRUE);
}

/*
 * The caller must hold dev_pager_mtx
 */
static vm_page_t
dev_pager_getfake(vm_paddr_t paddr, int pat_mode)
{
	vm_page_t m;

	m = kmalloc(sizeof(*m), M_FICTITIOUS_PAGES, M_WAITOK|M_ZERO);

	pmap_page_init(m);

	m->flags = PG_BUSY | PG_FICTITIOUS;
	m->valid = VM_PAGE_BITS_ALL;
	m->dirty = 0;
	m->busy = 0;
	m->queue = PQ_NONE;
	m->object = NULL;

	m->wire_count = 1;
	m->hold_count = 0;
	m->phys_addr = paddr;
	m->pat_mode = pat_mode;

	return (m);
}

/*
 * Synthesized VM pages must be structurally stable for lockless lookups to
 * work properly.
 *
 * The caller must hold dev_pager_mtx
 */
static void
dev_pager_putfake(vm_page_t m)
{
	if (!(m->flags & PG_FICTITIOUS))
		panic("dev_pager_putfake: bad page");
	KKASSERT(m->object == NULL);
	KKASSERT(m->hold_count == 0);
	kfree(m, M_FICTITIOUS_PAGES);
}

static int
old_dev_pager_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred,  u_short *color)
{
	unsigned int npages;
	vm_offset_t off;
	cdev_t dev;

	dev = handle;

	/*
	 * Check that the specified range of the device allows the desired
	 * protection.
	 *
	 * XXX assumes VM_PROT_* == PROT_*
	 */
	npages = OFF_TO_IDX(size);
	for (off = foff; npages--; off += PAGE_SIZE) {
		if (dev_dmmap(dev, off, (int)prot, NULL) == -1)
			return (EINVAL);
	}

	return (0);
}

static void old_dev_pager_dtor(void *handle)
{
	cdev_t dev;

	dev = handle;
	if (dev != NULL) {
		KKASSERT(dev->si_object);
		dev->si_object = NULL;
	}
}

static int old_dev_pager_fault(vm_object_t object, vm_ooffset_t offset,
    int prot, vm_page_t *mres)
{
	vm_paddr_t paddr;
	vm_page_t page;
	vm_offset_t pidx = OFF_TO_IDX(offset);
	cdev_t dev;

	page = *mres;
	dev = object->handle;

	paddr = pmap_phys_address(
		    dev_dmmap(dev, offset, prot, NULL));
	KASSERT(paddr != -1,("dev_pager_getpage: map function returns error"));
	KKASSERT(object->type == OBJT_DEVICE);

	if (page->flags & PG_FICTITIOUS) {
		/*
		 * If the passed in reqpage page is already a fake page,
		 * update it with the new physical address.
		 */
		page->phys_addr = paddr;
		page->valid = VM_PAGE_BITS_ALL;
	} else {
		/*
		 * Replace the passed in reqpage page with our own fake page
		 * and free up all the original pages.
		 */
		page = dev_pager_getfake(paddr, object->memattr);
		TAILQ_INSERT_TAIL(&object->un_pager.devp.devp_pglist,
				  page, pageq);
		vm_object_hold(object);
		vm_page_free(*mres);
		if (vm_page_insert(page, object, pidx) == FALSE) {
			panic("dev_pager_getpage: page (%p,%016jx) exists",
			      object, (uintmax_t)pidx);
		}
		vm_object_drop(object);
	}

	return (VM_PAGER_OK);
}

