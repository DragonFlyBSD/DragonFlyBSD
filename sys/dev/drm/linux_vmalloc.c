/*
 * Copyright (c) 2017-2019 François Tigeot <ftigeot@wolfpond.org>
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/queue.h>
#include <vm/vm_extern.h>

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/mm.h>

struct vmap {
	void *addr;
	int npages;
	SLIST_ENTRY(vmap) vm_vmaps;
};

struct lock vmap_lock = LOCK_INITIALIZER("dlvml", 0, LK_CANRECURSE);

SLIST_HEAD(vmap_list_head, vmap) vmap_list = SLIST_HEAD_INITIALIZER(vmap_list);

/* vmap: map an array of pages into virtually contiguous space */
void *
vmap(struct page **pages, unsigned int count,
	unsigned long flags, pgprot_t prot)
{
	struct vmap *vmp;
	vm_offset_t off;
	size_t size;

	vmp = kmalloc(sizeof(struct vmap), M_DRM, M_WAITOK | M_ZERO);

	size = count * PAGE_SIZE;
	off = kmem_alloc_nofault(&kernel_map, size,
				 VM_SUBSYS_DRM_VMAP, PAGE_SIZE);
	if (off == 0)
		return (NULL);

	vmp->addr = (void *)off;
	vmp->npages = count;
	pmap_qenter(off, (struct vm_page **)pages, count);
	lockmgr(&vmap_lock, LK_EXCLUSIVE);
	SLIST_INSERT_HEAD(&vmap_list, vmp, vm_vmaps);
	lockmgr(&vmap_lock, LK_RELEASE);

	return (void *)off;
}

void
vunmap(const void *addr)
{
	struct vmap *vmp, *tmp_vmp;
	size_t size;

	SLIST_FOREACH_MUTABLE(vmp, &vmap_list, vm_vmaps, tmp_vmp) {
		if (vmp->addr == addr) {
			size = vmp->npages * PAGE_SIZE;

			pmap_qremove((vm_offset_t)addr, vmp->npages);
			kmem_free(&kernel_map, (vm_offset_t)addr, size);
			goto found;
		}
	}

found:
	lockmgr(&vmap_lock, LK_EXCLUSIVE);
	SLIST_REMOVE(&vmap_list, vmp, vmap, vm_vmaps);
	lockmgr(&vmap_lock, LK_RELEASE);
	kfree(vmp);
}

int
is_vmalloc_addr(const void *x)
{
	struct vmap *vmp, *tmp_vmp;

	SLIST_FOREACH_MUTABLE(vmp, &vmap_list, vm_vmaps, tmp_vmp) {
		if (vmp->addr == x)
			return 1;
	}

	return false;
}

void *
vmalloc(unsigned long size)
{
	return kmalloc(size, M_DRM, M_WAITOK);
}

void *
vzalloc(unsigned long size)
{
	return kmalloc(size, M_DRM, M_WAITOK | M_ZERO);
}

/* allocate zeroed virtually contiguous memory for userspace */
void *
vmalloc_user(unsigned long size)
{
	return kmalloc(size, M_DRM, M_WAITOK | M_ZERO);
}

void
vfree(const void *addr)
{
	void *nc_addr;

	memcpy(&nc_addr, &addr, sizeof(void *));
	kfree(nc_addr);
}

void *
kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (n == 0)
		return NULL;

	if (n > SIZE_MAX / size)
		return NULL;

	return kmalloc(n * size, M_DRM, flags);
}
