/*-
 * Copyright (c) 2011 The FreeBSD Foundation
 * Copyright (c) 2014-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_page2.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <linux/err.h>
#include <linux/shmem_fs.h>

/*
 * This code is typically called with a normal VM object to access
 * data from a userspace shared memory mapping.  However, handle the
 * case where it might be called with OBJT_MGTDEVICE anyway.
 */
struct page *
shmem_read_mapping_page(vm_object_t object, vm_pindex_t pindex)
{
	vm_page_t m;
	int rv;

	VM_OBJECT_LOCK(object);
	if (object->type == OBJT_MGTDEVICE) {
		m = NULL;
		rv = vm_pager_get_page(object, pindex, &m, 1);
		if (m == NULL)
			return ERR_PTR(-ENOMEM);
		if (rv != VM_PAGER_OK) {
			vm_page_free(m);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		m = vm_page_grab(object, pindex,
				 VM_ALLOC_NORMAL | VM_ALLOC_RETRY);
		if (m->valid != VM_PAGE_BITS_ALL) {
			if (vm_pager_has_page(object, pindex)) {
				rv = vm_pager_get_page(object, pindex, &m, 1);
				m = vm_page_lookup(object, pindex);
				if (m == NULL)
					return ERR_PTR(-ENOMEM);
				if (rv != VM_PAGER_OK) {
					vm_page_free(m);
					return ERR_PTR(-ENOMEM);
				}
			} else {
				pmap_zero_page(VM_PAGE_TO_PHYS(m));
				m->valid = VM_PAGE_BITS_ALL;
				m->dirty = 0;
			}
		}
	}
	vm_page_wire(m);		/* put_page() undoes this */
	vm_page_wakeup(m);
	VM_OBJECT_UNLOCK(object);

	return (struct page *)m;
}

struct page *
shmem_read_mapping_page_gfp(struct vm_object *mapping,
    pgoff_t index, gfp_t gfp_mask)
{
	return shmem_read_mapping_page(mapping, index);
}

#include <linux/fs.h>

int
pagecache_write_begin(struct vm_object *obj, struct address_space *mapping,
    loff_t pos, unsigned len, unsigned flags, struct page **pagep, void **fsdata)
{
	*pagep = shmem_read_mapping_page(obj, OFF_TO_IDX(pos));

	return 0;
}

/* This is really shmem_write_end() for the i915 gem code */
int
pagecache_write_end(struct vm_object *obj, struct address_space *mapping,
    loff_t pos, unsigned len, unsigned copied, struct page *page, void *fsdata)
{
	set_page_dirty(page);
	put_page(page);

	return copied;
}

/*
 * userptr support
 */
long
get_user_pages(unsigned long start, unsigned long nr_pages,
	       unsigned int gup_flags, struct page **pages,
	       struct vm_area_struct **vmas)
{
	thread_t td;
	vm_page_t m;
	vm_map_t map;
	long i;
	int error;
	int busied;
	int fault_type = VM_PROT_READ;

	/* returning related vmas not yet supported */
	td = curthread;
	KKASSERT(vmas == NULL);
	KKASSERT(td->td_proc == NULL);
	map = &td->td_proc->p_vmspace->vm_map;

	if (gup_flags)
		fault_type |= VM_PROT_WRITE;

	error = 0;
	for (i = 0; i < nr_pages; ++i) {
		m = vm_fault_page(map, start + i * PAGE_SIZE,
				  fault_type, VM_FAULT_NORMAL,
				  &error, &busied);
		if (error)
			break;
		if (busied) {
			vm_page_wire(m);
		} else {
			vm_page_busy_wait(m, TRUE, "drmgup");
			vm_page_wire(m);
			vm_page_unhold(m);
		}
		vm_page_wakeup(m);
		pages[i] = (void *)m;
	}
	if (error) {
		while (--i >= 0) {
			put_page(pages[i]);
			pages[i] = NULL;
		}
		i = -error;
	}
	return i;
}

void
release_pages(struct page **pages, unsigned long nr_pages)
{
	while (nr_pages > 0) {
		--nr_pages;
		put_page(pages[nr_pages]);
		pages[nr_pages] = NULL;
	}
}
