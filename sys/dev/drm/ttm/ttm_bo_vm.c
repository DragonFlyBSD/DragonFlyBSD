/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drm_vma_manager.h>
#include <linux/mm.h>
#include <linux/pfn_t.h>
#include <linux/rbtree.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/mem_encrypt.h>

#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_page2.h>

#define TTM_BO_VM_NUM_PREFAULT 16

static int ttm_bo_vm_fault_idle(struct ttm_buffer_object *bo,
				struct vm_area_struct *vma,
				struct vm_fault *vmf)
{
	int ret = 0;

	if (likely(!bo->moving))
		goto out_unlock;

	/*
	 * Quick non-stalling check for idle.
	 */
	if (dma_fence_is_signaled(bo->moving))
		goto out_clear;

	/*
	 * If possible, avoid waiting for GPU with mmap_sem
	 * held.
	 */
	if (vmf->flags & FAULT_FLAG_ALLOW_RETRY) {
		ret = VM_FAULT_RETRY;
		if (vmf->flags & FAULT_FLAG_RETRY_NOWAIT)
			goto out_unlock;

		ttm_bo_get(bo);
		up_read(&vmf->vma->vm_mm->mmap_sem);
		(void) dma_fence_wait(bo->moving, true);
		ttm_bo_unreserve(bo);
		ttm_bo_put(bo);
		goto out_unlock;
	}

	/*
	 * Ordinary wait.
	 */
	ret = dma_fence_wait(bo->moving, true);
	if (unlikely(ret != 0)) {
		ret = (ret != -ERESTARTSYS) ? VM_FAULT_SIGBUS :
			VM_FAULT_NOPAGE;
		goto out_unlock;
	}

out_clear:
	dma_fence_put(bo->moving);
	bo->moving = NULL;

out_unlock:
	return ret;
}

/*
 * Always unstall on unexpected vm_page alias, fatal bus fault.
 * Set to 0 to stall, set to positive count to unstall N times,
 * then stall again.
 */
static int drm_unstall = -1;
SYSCTL_INT(_debug, OID_AUTO, unstall, CTLFLAG_RW, &drm_unstall, 0, "");

static int ttm_bo_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	/* see ttm_bo_mmap_single() at end of this file */
	/* ttm_bo_vm_ops not currently used, no entry should occur */
	panic("ttm_bo_vm_fault");
#if 0
	struct ttm_buffer_object *bo = (struct ttm_buffer_object *)
	    vma->vm_private_data;
	struct ttm_bo_device *bdev = bo->bdev;
	unsigned long page_offset;
	unsigned long page_last;
	unsigned long pfn;
	struct ttm_tt *ttm = NULL;
	struct page *page;
	int ret;
	int i;
	unsigned long address = vmf->address;
	int retval = VM_FAULT_NOPAGE;
	struct ttm_mem_type_manager *man =
		&bdev->man[bo->mem.mem_type];
	struct vm_area_struct cvma;

	/*
	 * Work around locking order reversal in fault / nopfn
	 * between mmap_sem and bo_reserve: Perform a trylock operation
	 * for reserve, and if it fails, retry the fault after waiting
	 * for the buffer to become unreserved.
	 */
	ret = ttm_bo_reserve(bo, true, true, NULL);
	if (unlikely(ret != 0)) {
		if (ret != -EBUSY)
			return VM_FAULT_NOPAGE;

		if (vmf->flags & FAULT_FLAG_ALLOW_RETRY) {
			if (!(vmf->flags & FAULT_FLAG_RETRY_NOWAIT)) {
				ttm_bo_reference(bo);
				up_read(&vma->vm_mm->mmap_sem);
				(void) ttm_bo_wait_unreserved(bo);
				ttm_bo_unref(&bo);
			}

			return VM_FAULT_RETRY;
		}

		/*
		 * If we'd want to change locking order to
		 * mmap_sem -> bo::reserve, we'd use a blocking reserve here
		 * instead of retrying the fault...
		 */
		return VM_FAULT_NOPAGE;
	}

	/*
	 * Refuse to fault imported pages. This should be handled
	 * (if at all) by redirecting mmap to the exporter.
	 */
	if (bo->ttm && (bo->ttm->page_flags & TTM_PAGE_FLAG_SG)) {
		retval = VM_FAULT_SIGBUS;
		goto out_unlock;
	}

	if (bdev->driver->fault_reserve_notify) {
		ret = bdev->driver->fault_reserve_notify(bo);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
		case -ERESTARTSYS:
			retval = VM_FAULT_NOPAGE;
			goto out_unlock;
		default:
			retval = VM_FAULT_SIGBUS;
			goto out_unlock;
		}
	}

	/*
	 * Wait for buffer data in transit, due to a pipelined
	 * move.
	 */

	ret = ttm_bo_vm_fault_idle(bo, vma, vmf);
	if (unlikely(ret != 0)) {
		retval = ret;

		if (retval == VM_FAULT_RETRY &&
		    !(vmf->flags & FAULT_FLAG_RETRY_NOWAIT)) {
			/* The BO has already been unreserved. */
			return retval;
		}

		goto out_unlock;
	}

	ret = ttm_mem_io_lock(man, true);
	if (unlikely(ret != 0)) {
		retval = VM_FAULT_NOPAGE;
		goto out_unlock;
	}
	ret = ttm_mem_io_reserve_vm(bo);
	if (unlikely(ret != 0)) {
		retval = VM_FAULT_SIGBUS;
		goto out_io_unlock;
	}

	page_offset = ((address - vma->vm_start) >> PAGE_SHIFT) +
		vma->vm_pgoff - drm_vma_node_start(&bo->vma_node);
	page_last = vma_pages(vma) + vma->vm_pgoff -
		drm_vma_node_start(&bo->vma_node);

	if (unlikely(page_offset >= bo->num_pages)) {
		retval = VM_FAULT_SIGBUS;
		goto out_io_unlock;
	}

	/*
	 * Make a local vma copy to modify the page_prot member
	 * and vm_flags if necessary. The vma parameter is protected
	 * by mmap_sem in write mode.
	 */
	cvma = *vma;
	cvma.vm_page_prot = vm_get_page_prot(cvma.vm_flags);

	if (bo->mem.bus.is_iomem) {
		cvma.vm_page_prot = ttm_io_prot(bo->mem.placement,
						cvma.vm_page_prot);
	} else {
		struct ttm_operation_ctx ctx = {
			.interruptible = false,
			.no_wait_gpu = false,
			.flags = TTM_OPT_FLAG_FORCE_ALLOC

		};

		ttm = bo->ttm;
		cvma.vm_page_prot = ttm_io_prot(bo->mem.placement,
						cvma.vm_page_prot);

		/* Allocate all page at once, most common usage */
		if (ttm_tt_populate(ttm, &ctx)) {
			retval = VM_FAULT_OOM;
			goto out_io_unlock;
		}
	}

	/*
	 * Speculatively prefault a number of pages. Only error on
	 * first page.
	 */
	for (i = 0; i < TTM_BO_VM_NUM_PREFAULT; ++i) {
		if (bo->mem.bus.is_iomem)
			pfn = ((bo->mem.bus.base + bo->mem.bus.offset) >> PAGE_SHIFT) + page_offset;
		else {
			page = ttm->pages[page_offset];
			if (unlikely(!page && i == 0)) {
				retval = VM_FAULT_OOM;
				goto out_io_unlock;
			} else if (unlikely(!page)) {
				break;
			}
			page->mapping = vma->vm_file->f_mapping;
			page->index = drm_vma_node_start(&bo->vma_node) +
				page_offset;
			pfn = page_to_pfn(page);
		}

		if (vma->vm_flags & VM_MIXEDMAP)
			ret = vm_insert_mixed(&cvma, address,
					__pfn_to_pfn_t(pfn, PFN_DEV));
		else
			ret = vm_insert_pfn(&cvma, address, pfn);

		/*
		 * Somebody beat us to this PTE or prefaulting to
		 * an already populated PTE, or prefaulting error.
		 */

		if (unlikely((ret == -EBUSY) || (ret != 0 && i > 0)))
			break;
		else if (unlikely(ret != 0)) {
			retval =
			    (ret == -ENOMEM) ? VM_FAULT_OOM : VM_FAULT_SIGBUS;
			goto out_io_unlock;
		}

		address += PAGE_SIZE;
		if (unlikely(++page_offset >= page_last))
			break;
	}
out_io_unlock:
	ttm_mem_io_unlock(man);
out_unlock:
	ttm_bo_unreserve(bo);
	return retval;
#endif
}

/* ttm_bo_vm_ops not currently used, no entry should occur */
static void ttm_bo_vm_open(struct vm_area_struct *vma)
{
	struct ttm_buffer_object *bo =
	    (struct ttm_buffer_object *)vma->vm_private_data;

#if 0
	WARN_ON(bo->bdev->dev_mapping != vma->vm_file->f_mapping);
#endif

	ttm_bo_get(bo);
}

/* ttm_bo_vm_ops not currently used, no entry should occur */
static void ttm_bo_vm_close(struct vm_area_struct *vma)
{
	struct ttm_buffer_object *bo = (struct ttm_buffer_object *)vma->vm_private_data;

	ttm_bo_put(bo);
	vma->vm_private_data = NULL;
}

static int ttm_bo_vm_access_kmap(struct ttm_buffer_object *bo,
				 unsigned long offset,
				 uint8_t *buf, int len, int write)
{
	unsigned long page = offset >> PAGE_SHIFT;
	unsigned long bytes_left = len;
	int ret;

	/* Copy a page at a time, that way no extra virtual address
	 * mapping is needed
	 */
	offset -= page << PAGE_SHIFT;
	do {
		unsigned long bytes = min(bytes_left, PAGE_SIZE - offset);
		struct ttm_bo_kmap_obj map;
		void *ptr;
		bool is_iomem;

		ret = ttm_bo_kmap(bo, page, 1, &map);
		if (ret)
			return ret;

		ptr = (uint8_t *)ttm_kmap_obj_virtual(&map, &is_iomem) + offset;
		WARN_ON_ONCE(is_iomem);
		if (write)
			memcpy(ptr, buf, bytes);
		else
			memcpy(buf, ptr, bytes);
		ttm_bo_kunmap(&map);

		page++;
		buf += bytes;
		bytes_left -= bytes;
		offset = 0;
	} while (bytes_left);

	return len;
}

static int ttm_bo_vm_access(struct vm_area_struct *vma, unsigned long addr,
			    void *buf, int len, int write)
{
	unsigned long offset = (addr) - vma->vm_start;
	struct ttm_buffer_object *bo = vma->vm_private_data;
	int ret;

	if (len < 1 || (offset + len) >> PAGE_SHIFT > bo->num_pages)
		return -EIO;

	ret = ttm_bo_reserve(bo, true, false, NULL);
	if (ret)
		return ret;

	switch (bo->mem.mem_type) {
	case TTM_PL_SYSTEM:
		if (unlikely(bo->ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)) {
			ret = ttm_tt_swapin(bo->ttm);
			if (unlikely(ret != 0))
				return ret;
		}
		/* fall through */
	case TTM_PL_TT:
		ret = ttm_bo_vm_access_kmap(bo, offset, buf, len, write);
		break;
	default:
		if (bo->bdev->driver->access_memory)
			ret = bo->bdev->driver->access_memory(
				bo, offset, buf, len, write);
		else
			ret = -EIO;
	}

	ttm_bo_unreserve(bo);

	return ret;
}

static const struct vm_operations_struct ttm_bo_vm_ops = {
	.fault = ttm_bo_vm_fault,
	.open = ttm_bo_vm_open,
	.close = ttm_bo_vm_close,
	.access = ttm_bo_vm_access
};

static struct ttm_buffer_object *ttm_bo_vm_lookup(struct ttm_bo_device *bdev,
						  unsigned long offset,
						  unsigned long pages)
{
	struct drm_vma_offset_node *node;
	struct ttm_buffer_object *bo = NULL;

	drm_vma_offset_lock_lookup(&bdev->vma_manager);

	node = drm_vma_offset_lookup_locked(&bdev->vma_manager, offset, pages);
	if (likely(node)) {
		bo = container_of(node, struct ttm_buffer_object, vma_node);
		if (!kref_get_unless_zero(&bo->kref))
			bo = NULL;
	}

	drm_vma_offset_unlock_lookup(&bdev->vma_manager);

	if (!bo)
		pr_err("Could not find buffer object to map\n");

	return bo;
}

int ttm_bo_mmap(struct file *filp, struct vm_area_struct *vma,
		struct ttm_bo_device *bdev)
{
	struct ttm_bo_driver *driver;
	struct ttm_buffer_object *bo;
	int ret;

	bo = ttm_bo_vm_lookup(bdev, vma->vm_pgoff, vma_pages(vma));
	if (unlikely(!bo))
		return -EINVAL;

	driver = bo->bdev->driver;
	if (unlikely(!driver->verify_access)) {
		ret = -EPERM;
		goto out_unref;
	}
	ret = driver->verify_access(bo, filp);
	if (unlikely(ret != 0))
		goto out_unref;

	vma->vm_ops = &ttm_bo_vm_ops;

	/*
	 * Note: We're transferring the bo reference to
	 * vma->vm_private_data here.
	 */

	vma->vm_private_data = bo;

	/*
	 * We'd like to use VM_PFNMAP on shared mappings, where
	 * (vma->vm_flags & VM_SHARED) != 0, for performance reasons,
	 * but for some reason VM_PFNMAP + x86 PAT + write-combine is very
	 * bad for performance. Until that has been sorted out, use
	 * VM_MIXEDMAP on all mappings. See freedesktop.org bug #75719
	 */
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
out_unref:
	ttm_bo_put(bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_mmap);

int ttm_fbdev_mmap(struct vm_area_struct *vma, struct ttm_buffer_object *bo)
{
	if (vma->vm_pgoff != 0)
		return -EACCES;

	ttm_bo_get(bo);

	vma->vm_ops = &ttm_bo_vm_ops;
	vma->vm_private_data = bo;
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_flags |= VM_IO | VM_DONTEXPAND;
	return 0;
}
EXPORT_SYMBOL(ttm_fbdev_mmap);

/*
 * DragonFlyBSD Interface
 */

#include "opt_vm.h"

/*
 * NOTE: This code is fragile.  This code can only be entered with *mres
 *	 not NULL when *mres is a placeholder page allocated by the kernel.
 */
static int
ttm_bo_vm_fault_dfly(vm_object_t vm_obj, vm_ooffset_t offset,
		     int prot, vm_page_t *mres)
{
	struct ttm_buffer_object *bo = vm_obj->handle;
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_tt *ttm = NULL;
	vm_page_t m;
	int ret;
	int retval = VM_PAGER_OK;
	struct ttm_mem_type_manager *man =
		&bdev->man[bo->mem.mem_type];
	struct vm_area_struct cvma;

/*
   The Linux code expects to receive these arguments:
   - struct vm_area_struct *vma
   - struct vm_fault *vmf
*/
#ifdef __DragonFly__
	struct vm_area_struct vmas;
	struct vm_area_struct *vma = &vmas;
	struct vm_fault vmfs;
	struct vm_fault *vmf = &vmfs;

	memset(vma, 0, sizeof(*vma));
	memset(vmf, 0, sizeof(*vmf));

	vma->vm_mm = current->mm;
	vmf->vma = vma;
#endif

	vm_object_pip_add(vm_obj, 1);

	/*
	 * OBJT_MGTDEVICE does not pre-allocate the page.
	 */
	KKASSERT(*mres == NULL);

retry:
	/* The Linux page fault handler acquires mmap_sem */
	down_read(&vma->vm_mm->mmap_sem);

	m = NULL;

	/*
	 * Work around locking order reversal in fault / nopfn
	 * between mmap_sem and bo_reserve: Perform a trylock operation
	 * for reserve, and if it fails, retry the fault after waiting
	 * for the buffer to become unreserved.
	 */
	ret = ttm_bo_reserve(bo, true, true, NULL);
	if (unlikely(ret != 0)) {
		if (ret != -EBUSY) {
			retval = VM_PAGER_ERROR;
			goto out_unlock2;
		}

		if (vmf->flags & FAULT_FLAG_ALLOW_RETRY || 1) {
			if (!(vmf->flags & FAULT_FLAG_RETRY_NOWAIT)) {
				up_read(&vma->vm_mm->mmap_sem);
				(void) ttm_bo_wait_unreserved(bo);
			}

#ifndef __DragonFly__
			return VM_FAULT_RETRY;
#else
			up_read(&vma->vm_mm->mmap_sem);
			lwkt_yield();
			goto retry;
#endif
		}

		/*
		 * If we'd want to change locking order to
		 * mmap_sem -> bo::reserve, we'd use a blocking reserve here
		 * instead of retrying the fault...
		 */
		retval = VM_PAGER_ERROR;
		goto out_unlock2;
	}

	if (bdev->driver->fault_reserve_notify) {
		ret = bdev->driver->fault_reserve_notify(bo);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
			lwkt_yield();
			/* fall through */
		case -ERESTARTSYS:
		case -EINTR:
			retval = VM_PAGER_ERROR;
			goto out_unlock1;
		default:
			retval = VM_PAGER_ERROR;
			goto out_unlock1;
		}
	}

	/*
	 * Wait for buffer data in transit, due to a pipelined
	 * move.
	 */
	ret = ttm_bo_vm_fault_idle(bo, vma, vmf);
	if (unlikely(ret != 0)) {
		retval = VM_PAGER_ERROR;
		goto out_unlock1;
	}

	ret = ttm_mem_io_lock(man, true);
	if (unlikely(ret != 0)) {
		retval = VM_PAGER_ERROR;
		goto out_unlock1;
	}
	ret = ttm_mem_io_reserve_vm(bo);
	if (unlikely(ret != 0)) {
		retval = VM_PAGER_ERROR;
		goto out_io_unlock1;
	}
	if (unlikely(OFF_TO_IDX(offset) >= bo->num_pages)) {
		retval = VM_PAGER_ERROR;
		goto out_io_unlock1;
	}

	/*
	 * Lookup the real page.
	 *
	 * Strictly, we're not allowed to modify vma->vm_page_prot here,
	 * since the mmap_sem is only held in read mode. However, we
	 * modify only the caching bits of vma->vm_page_prot and
	 * consider those bits protected by
	 * the bo->mutex, as we should be the only writers.
	 * There shouldn't really be any readers of these bits except
	 * within vm_insert_mixed()? fork?
	 *
	 * TODO: Add a list of vmas to the bo, and change the
	 * vma->vm_page_prot when the object changes caching policy, with
	 * the correct locks held.
	 */

	/*
	 * Make a local vma copy to modify the page_prot member
	 * and vm_flags if necessary. The vma parameter is protected
	 * by mmap_sem in write mode.
	 */
	cvma = *vma;
#if 0
	cvma.vm_page_prot = vm_get_page_prot(cvma.vm_flags);
#else
	cvma.vm_page_prot = 0;
#endif

	if (bo->mem.bus.is_iomem) {
#ifdef __DragonFly__
		m = vm_phys_fictitious_to_vm_page(bo->mem.bus.base +
						  bo->mem.bus.offset + offset);
		pmap_page_set_memattr(m, ttm_io_prot(bo->mem.placement, 0));
#endif
		cvma.vm_page_prot = ttm_io_prot(bo->mem.placement,
						cvma.vm_page_prot);
	} else {
		ttm = bo->ttm;
		cvma.vm_page_prot = ttm_io_prot(bo->mem.placement,
						cvma.vm_page_prot);

		/* Allocate all page at once, most common usage */
		struct ttm_operation_ctx ctx = {
			.interruptible = false,
			.no_wait_gpu = false,
			.flags = TTM_OPT_FLAG_FORCE_ALLOC

		};
		if (ttm_tt_populate(ttm, &ctx)) {
			retval = VM_PAGER_ERROR;
			goto out_io_unlock1;
		}

		m = (struct vm_page *)ttm->pages[OFF_TO_IDX(offset)];
		if (unlikely(!m)) {
			retval = VM_PAGER_ERROR;
			goto out_io_unlock1;
		}
		pmap_page_set_memattr(m,
		    (bo->mem.placement & TTM_PL_FLAG_CACHED) ?
		    VM_MEMATTR_WRITE_BACK : ttm_io_prot(bo->mem.placement, 0));
	}

	if (vm_page_busy_try(m, FALSE)) {
		kprintf("r");
		vm_page_sleep_busy(m, FALSE, "ttmvmf");
		ttm_mem_io_unlock(man);
		ttm_bo_unreserve(bo);
		up_read(&vma->vm_mm->mmap_sem);
		goto retry;
	}

	/*
	 * Return our fake page BUSYd.  Do not index it into the VM object.
	 * The caller will enter it into the pmap.
	 */
	m->valid = VM_PAGE_BITS_ALL;
	*mres = m;

out_io_unlock1:
	ttm_mem_io_unlock(man);
out_unlock1:
	ttm_bo_unreserve(bo);
out_unlock2:
	vm_object_pip_wakeup(vm_obj);
	return (retval);
}

static int
ttm_bo_vm_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
	       vm_ooffset_t foff, struct ucred *cred, u_short *color)
{

	/*
	 * On Linux, a reference to the buffer object is acquired here.
	 * The reason is that this function is not called when the
	 * mmap() is initialized, but only when a process forks for
	 * instance. Therefore on Linux, the reference on the bo is
	 * acquired either in ttm_bo_mmap() or ttm_bo_vm_open(). It's
	 * then released in ttm_bo_vm_close().
	 *
	 * Here, this function is called during mmap() intialization.
	 * Thus, the reference acquired in ttm_bo_mmap_single() is
	 * sufficient.
	 */
	*color = 0;
	return (0);
}

static void
ttm_bo_vm_dtor(void *handle)
{
	struct ttm_buffer_object *bo = handle;

	ttm_bo_unref(&bo);
}

static struct cdev_pager_ops ttm_pager_ops = {
	.cdev_pg_fault = ttm_bo_vm_fault_dfly,
	.cdev_pg_ctor = ttm_bo_vm_ctor,
	.cdev_pg_dtor = ttm_bo_vm_dtor
};

/*
 * Called from drm_drv.c
 *
 * *offset - object offset in bytes
 * size	   - map size in bytes
 *
 * We setup a dummy vma (for now) and call ttm_bo_mmap().  Then we setup
 * our own VM object and dfly ops.  Note that the ops supplied by
 * ttm_bo_mmap() are not currently used.
 */
int
ttm_bo_mmap_single(struct file *fp, struct drm_device *dev,
		   vm_ooffset_t *offset, vm_size_t size,
		   struct vm_object **obj_res, int nprot)
{
	struct ttm_bo_device *bdev = dev->drm_ttm_bdev;
	struct ttm_buffer_object *bo;
	struct vm_object *vm_obj;
	struct vm_area_struct vma;
	int ret;

	*obj_res = NULL;

	bzero(&vma, sizeof(vma));
	vma.vm_start = *offset;		/* bdev-relative offset */
	vma.vm_end = vma.vm_start + size;
	vma.vm_pgoff = vma.vm_start >> PAGE_SHIFT;
	/* vma.vm_page_prot */
	/* vma.vm_flags */

	/*
	 * Call the linux-ported code to do the work, and on success just
	 * setup our own VM object and ignore what the linux code did other
	 * then supplying us the 'bo'.
	 */
	ret = ttm_bo_mmap(fp, &vma, bdev);

	if (ret == 0) {
		bo = vma.vm_private_data;
		vm_obj = cdev_pager_allocate(bo, OBJT_MGTDEVICE,
					     &ttm_pager_ops,
					     size, nprot, 0,
					     curthread->td_ucred);
		if (vm_obj) {
			*obj_res = vm_obj;
			*offset = 0;		/* object-relative offset */
		} else {
			ttm_bo_unref(&bo);
			ret = EINVAL;
		}
	}
	return ret;
}
EXPORT_SYMBOL(ttm_bo_mmap_single);

#ifdef __DragonFly__
void ttm_bo_release_mmap(struct ttm_buffer_object *bo);

void
ttm_bo_release_mmap(struct ttm_buffer_object *bo)
{
	vm_object_t vm_obj;

	vm_obj = cdev_pager_lookup(bo);
	if (vm_obj == NULL)
		return;

	VM_OBJECT_LOCK(vm_obj);
#if 1
	vm_object_page_remove(vm_obj, 0, 0, false);
#else
	/*
	 * XXX REMOVED
	 *
	 * We no longer manage the vm pages inside the MGTDEVICE
	 * objects.
	 */
	vm_page_t m;
	int i;

	for (i = 0; i < bo->num_pages; i++) {
		m = vm_page_lookup_busy_wait(vm_obj, i, TRUE, "ttm_unm");
		if (m == NULL)
			continue;
		cdev_pager_free_page(vm_obj, m);
	}
#endif
	VM_OBJECT_UNLOCK(vm_obj);

	vm_object_deallocate(vm_obj);
}
#endif

#if 0
int ttm_fbdev_mmap(struct vm_area_struct *vma, struct ttm_buffer_object *bo)
{
	if (vma->vm_pgoff != 0)
		return -EACCES;

	vma->vm_ops = &ttm_bo_vm_ops;
	vma->vm_private_data = ttm_bo_reference(bo);
	vma->vm_flags |= VM_IO | VM_MIXEDMAP | VM_DONTEXPAND;
	return 0;
}
EXPORT_SYMBOL(ttm_fbdev_mmap);
#endif
