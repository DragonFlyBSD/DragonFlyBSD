/*-
 * Copyright (c) 2004 Marcel Moolenaar
 * Copyright (c) 2001 Doug Rabson
 * Copyright (c) 2016 The FreeBSD Foundation
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/amd64/amd64/efirt.c 307391 2016-10-16 06:07:43Z kib $
 */

#include <sys/param.h>
#include <sys/efi.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/globaldata.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <vm/vm_page2.h>

#include <machine/efi.h>
#include <machine/metadata.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <machine/vmparam.h>

static struct efi_systbl *efi_systbl;
static struct efi_cfgtbl *efi_cfgtbl;
static struct efi_rt *efi_runtime;

static int efi_status2err[25] = {
	0,		/* EFI_SUCCESS */
	ENOEXEC,	/* EFI_LOAD_ERROR */
	EINVAL,		/* EFI_INVALID_PARAMETER */
	ENOSYS,		/* EFI_UNSUPPORTED */
	EMSGSIZE, 	/* EFI_BAD_BUFFER_SIZE */
	EOVERFLOW,	/* EFI_BUFFER_TOO_SMALL */
	EBUSY,		/* EFI_NOT_READY */
	EIO,		/* EFI_DEVICE_ERROR */
	EROFS,		/* EFI_WRITE_PROTECTED */
	EAGAIN,		/* EFI_OUT_OF_RESOURCES */
	EIO,		/* EFI_VOLUME_CORRUPTED */
	ENOSPC,		/* EFI_VOLUME_FULL */
	ENXIO,		/* EFI_NO_MEDIA */
	ESTALE,		/* EFI_MEDIA_CHANGED */
	ENOENT,		/* EFI_NOT_FOUND */
	EACCES,		/* EFI_ACCESS_DENIED */
	ETIMEDOUT,	/* EFI_NO_RESPONSE */
	EADDRNOTAVAIL,	/* EFI_NO_MAPPING */
	ETIMEDOUT,	/* EFI_TIMEOUT */
	EDOOFUS,	/* EFI_NOT_STARTED */
	EALREADY,	/* EFI_ALREADY_STARTED */
	ECANCELED,	/* EFI_ABORTED */
	EPROTO,		/* EFI_ICMP_ERROR */
	EPROTO,		/* EFI_TFTP_ERROR */
	EPROTO		/* EFI_PROTOCOL_ERROR */
};

MALLOC_DEFINE(M_EFI, "efi", "EFI BIOS");

static int
efi_status_to_errno(efi_status status)
{
	u_long code;

	code = status & 0x3ffffffffffffffful;
	return (code < nitems(efi_status2err) ? efi_status2err[code] : EDOOFUS);
}

static struct lock efi_lock;
static struct lock resettodr_lock;
static mcontext_t efi_ctx;
static struct vmspace *efi_savevm;
static struct vmspace *efi_vmspace;
static vm_object_t efi_obj;
static struct efi_md *efi_map;
static int efi_ndesc;
static int efi_descsz;

static void
efi_destroy_1t1_map(void)
{
	vm_object_t obj;
	vm_page_t m;

	if ((obj = efi_obj) != NULL) {
		efi_obj = NULL;
		vm_object_hold(obj);
		vm_object_reference_locked(obj);	/* match deallocate */
	}
	if (efi_vmspace) {
		pmap_remove_pages(vmspace_pmap(efi_vmspace),
				  VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
		vm_map_remove(&efi_vmspace->vm_map,
			      VM_MIN_USER_ADDRESS,
			      VM_MAX_USER_ADDRESS);
		vmspace_rel(efi_vmspace);
		efi_vmspace = NULL;
	}
	if (obj) {
		while ((m = RB_ROOT(&obj->rb_memq)) != NULL) {
			vm_page_busy_wait(m, FALSE, "efipg");
			vm_page_unwire(m, 1);
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
			cdev_pager_free_page(obj, m);
			kfree(m, M_EFI);
		}
		vm_object_drop(obj);
		vm_object_deallocate(obj);
	}
}

static int
efi_pg_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
	    vm_ooffset_t foff, struct ucred *cred, u_short *color)
{
	*color = 0;
	return 0;
}

static void
efi_pg_dtor(void *handle)
{
}

static int
efi_pg_fault(vm_object_t obj, vm_ooffset_t offset, int prot, vm_page_t *mres)
{
	vm_page_t m;

	m = *mres;
	if ((m->flags & PG_FICTITIOUS) == 0) {
		*mres = NULL;
		vm_page_remove(m);
		vm_page_free(m);
		m = NULL;
	}
	if (m == NULL) {
		kprintf("efi_pg_fault: unmapped pg @%016jx\n", offset);
		return VM_PAGER_ERROR;
	}

	/*
	 * Shouldn't get hit, we pre-loaded all the pages.
	 */
	kprintf("efi_pg_fault: ok %p/%p @%016jx m=%016jx,%016jx\n",
		obj, efi_obj, offset, m->pindex, m->phys_addr);

	return VM_PAGER_OK;
}

static struct cdev_pager_ops efi_pager_ops = {
	.cdev_pg_fault	= efi_pg_fault,
	.cdev_pg_ctor	= efi_pg_ctor,
	.cdev_pg_dtor	= efi_pg_dtor
};

static bool
efi_create_1t1_map(struct efi_md *map, int ndesc, int descsz)
{
	vm_page_t m;
	struct efi_md *p;
	int i;
	int count;
	int result;

	efi_map = map;
	efi_ndesc = ndesc;
	efi_descsz = descsz;

	/*
	 * efi_obj is ref'd by cdev_pager_allocate
	 */
	efi_vmspace = vmspace_alloc(VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	pmap_pinit2(vmspace_pmap(efi_vmspace));
	efi_obj = cdev_pager_allocate(NULL, OBJT_MGTDEVICE, &efi_pager_ops,
				  VM_MAX_USER_ADDRESS,
				  VM_PROT_READ | VM_PROT_WRITE,
				  0, proc0.p_ucred);
	vm_object_hold(efi_obj);

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(&efi_vmspace->vm_map);
	result = vm_map_insert(&efi_vmspace->vm_map, &count, efi_obj, NULL,
			      0, NULL,
			      0, VM_MAX_USER_ADDRESS,
			      VM_MAPTYPE_NORMAL,
			      VM_SUBSYS_EFI,
			      VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
			      VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
			      0);
	vm_map_unlock(&efi_vmspace->vm_map);
	if (result != KERN_SUCCESS)
		goto fail;

	for (i = 0, p = map;
	     i < ndesc; i++, p = efi_next_descriptor(p, descsz)) {
		vm_offset_t va;
		uint64_t idx;
		int mode;

		if ((p->md_attr & EFI_MD_ATTR_RT) == 0)
			continue;
		if (p->md_virt != NULL) {
			if (bootverbose)
				kprintf("EFI Runtime entry %d is mapped\n", i);
			goto fail;
		}
		if ((p->md_phys & EFI_PAGE_MASK) != 0) {
			if (bootverbose)
				kprintf("EFI Runtime entry %d is not aligned\n",
				    i);
			goto fail;
		}
		if (p->md_phys + p->md_pages * EFI_PAGE_SIZE < p->md_phys ||
		    p->md_phys + p->md_pages * EFI_PAGE_SIZE >=
		    VM_MAX_USER_ADDRESS) {
			kprintf("EFI Runtime entry %d is not in mappable for RT:"
			    "base %#016jx %#jx pages\n",
			    i, (uintmax_t)p->md_phys,
			    (uintmax_t)p->md_pages);
			goto fail;
		}

		if ((p->md_attr & EFI_MD_ATTR_WB) != 0)
			mode = VM_MEMATTR_WRITE_BACK;
		else if ((p->md_attr & EFI_MD_ATTR_WT) != 0)
			mode = VM_MEMATTR_WRITE_THROUGH;
		else if ((p->md_attr & EFI_MD_ATTR_WC) != 0)
			mode = VM_MEMATTR_WRITE_COMBINING;
		else if ((p->md_attr & EFI_MD_ATTR_WP) != 0)
			mode = VM_MEMATTR_WRITE_PROTECTED;
		else if ((p->md_attr & EFI_MD_ATTR_UC) != 0)
			mode = VM_MEMATTR_UNCACHEABLE;
		else {
			if (bootverbose)
				kprintf("EFI Runtime entry %d mapping "
				    "attributes unsupported\n", i);
			mode = VM_MEMATTR_UNCACHEABLE;
		}

		if (bootverbose) {
			kprintf("efirt: map %016jx-%016jx\n",
				p->md_phys,
				p->md_phys + IDX_TO_OFF(p->md_pages));
		}

		for (va = p->md_phys, idx = 0; idx < p->md_pages; idx++,
		    va += PAGE_SIZE) {
			m = kmalloc(sizeof(*m), M_EFI, M_WAITOK | M_ZERO);
			/*m->flags |= PG_WRITEABLE;*/
			vm_page_initfake(m, va, mode);	/* va is phys addr */
			m->valid = VM_PAGE_BITS_ALL;
			m->dirty = m->valid;
			vm_page_insert(m, efi_obj, OFF_TO_IDX(va));
			vm_page_wakeup(m);
		}
	}
	vm_object_drop(efi_obj);
	vm_map_entry_release(count);

	return true;

fail:
	vm_object_drop(efi_obj);
	vm_map_entry_release(count);
	efi_destroy_1t1_map();

	return false;
}

/*
 * Create an environment for the EFI runtime code call.  The most
 * important part is creating the required 1:1 physical->virtual
 * mappings for the runtime segments.  To do that, we manually create
 * page table which unmap userspace but gives correct kernel mapping.
 * The 1:1 mappings for runtime segments usually occupy low 4G of the
 * physical address map.
 *
 * The 1:1 mappings were chosen over the SetVirtualAddressMap() EFI RT
 * service, because there are some BIOSes which fail to correctly
 * relocate itself on the call, requiring both 1:1 and virtual
 * mapping.  As result, we must provide 1:1 mapping anyway, so no
 * reason to bother with the virtual map, and no need to add a
 * complexity into loader.
 *
 * The fpu_kern_enter() call allows firmware to use FPU, as mandated
 * by the specification.  In particular, CR0.TS bit is cleared.  Also
 * it enters critical section, giving us neccessary protection against
 * context switch.
 *
 * There is no need to disable interrupts around the change of %cr3,
 * the kernel mappings are correct, while we only grabbed the
 * userspace portion of VA.  Interrupts handlers must not access
 * userspace.  Having interrupts enabled fixes the issue with
 * firmware/SMM long operation, which would negatively affect IPIs,
 * esp. TLB shootdown requests.
 *
 * We must disable SMAP (aka smap_open()) operation to access the
 * direct map as it will likely be using userspace addresses.
 */
static int
efi_enter(void)
{
	thread_t td = curthread;

	if (efi_runtime == NULL)
		return (ENXIO);
	lockmgr(&efi_lock, LK_EXCLUSIVE);
	efi_savevm = td->td_lwp->lwp_vmspace;
	pmap_setlwpvm(td->td_lwp, efi_vmspace);
	npxpush(&efi_ctx);
	cpu_invltlb();
	smap_smep_disable();

	return (0);
}

static void
efi_leave(void)
{
	thread_t td = curthread;

	smap_smep_enable();
	pmap_setlwpvm(td->td_lwp, efi_savevm);
	npxpop(&efi_ctx);
	cpu_invltlb();
	efi_savevm = NULL;
	lockmgr(&efi_lock, LK_RELEASE);
}

static int
efi_init(void)
{
	struct efi_map_header *efihdr;
	struct efi_md *map;
	caddr_t kmdp;
	size_t efisz;

	lockinit(&efi_lock, "efi", 0, LK_CANRECURSE);
	lockinit(&resettodr_lock, "efitodr", 0, LK_CANRECURSE);

	if (efi_systbl_phys == 0) {
		if (bootverbose)
			kprintf("EFI systbl not available\n");
		return (ENXIO);
	}
	efi_systbl = (struct efi_systbl *)PHYS_TO_DMAP(efi_systbl_phys);
	if (efi_systbl->st_hdr.th_sig != EFI_SYSTBL_SIG) {
		efi_systbl = NULL;
		if (bootverbose)
			kprintf("EFI systbl signature invalid\n");
		return (ENXIO);
	}
	efi_cfgtbl = (efi_systbl->st_cfgtbl == 0) ? NULL :
	    (struct efi_cfgtbl *)efi_systbl->st_cfgtbl;
	if (efi_cfgtbl == NULL) {
		if (bootverbose)
			kprintf("EFI config table is not present\n");
	}

	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	efihdr = (struct efi_map_header *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_MAP);
	if (efihdr == NULL) {
		if (bootverbose)
			kprintf("EFI map is not present\n");
		return (ENXIO);
	}
	efisz = (sizeof(struct efi_map_header) + 0xf) & ~0xf;
	map = (struct efi_md *)((uint8_t *)efihdr + efisz);
	if (efihdr->descriptor_size == 0)
		return (ENOMEM);

	if (!efi_create_1t1_map(map, efihdr->memory_size /
	    efihdr->descriptor_size, efihdr->descriptor_size)) {
		if (bootverbose)
			kprintf("EFI cannot create runtime map\n");
		return (ENOMEM);
	}

	efi_runtime = (efi_systbl->st_rt == 0) ? NULL :
			(struct efi_rt *)efi_systbl->st_rt;
	if (efi_runtime == NULL) {
		if (bootverbose)
			kprintf("EFI runtime services table is not present\n");
		efi_destroy_1t1_map();
		return (ENXIO);
	}

	return (0);
}

static void
efi_uninit(void)
{
	efi_destroy_1t1_map();

	efi_systbl = NULL;
	efi_cfgtbl = NULL;
	efi_runtime = NULL;

	lockuninit(&efi_lock);
	lockuninit(&resettodr_lock);
}

int
efi_get_table(struct uuid *uuid, void **ptr)
{
	struct efi_cfgtbl *ct;
	u_long count;

	if (efi_cfgtbl == NULL)
		return (ENXIO);
	count = efi_systbl->st_entries;
	ct = efi_cfgtbl;
	while (count--) {
		if (!bcmp(&ct->ct_uuid, uuid, sizeof(*uuid))) {
			*ptr = (void *)PHYS_TO_DMAP(ct->ct_data);
			return (0);
		}
		ct++;
	}
	return (ENOENT);
}

char SaveCode[1024];

int
efi_get_time_locked(struct efi_tm *tm)
{
	efi_status status;
	int error;

	KKASSERT(lockowned(&resettodr_lock) != 0);
	error = efi_enter();
	if (error != 0)
		return (error);
	status = efi_runtime->rt_gettime(tm, NULL);
	efi_leave();
	error = efi_status_to_errno(status);

	return (error);
}

int
efi_get_time(struct efi_tm *tm)
{
	int error;

	if (efi_runtime == NULL)
		return (ENXIO);
	lockmgr(&resettodr_lock, LK_EXCLUSIVE);
	error = efi_get_time_locked(tm);
	lockmgr(&resettodr_lock, LK_RELEASE);

	return (error);
}

int
efi_reset_system(void)
{
	int error;

	error = efi_enter();
	if (error != 0)
		return (error);
	efi_runtime->rt_reset(EFI_RESET_WARM, 0, 0, NULL);
	efi_leave();
	return (EIO);
}

int
efi_set_time_locked(struct efi_tm *tm)
{
	efi_status status;
	int error;

	KKASSERT(lockowned(&resettodr_lock) != 0);
	error = efi_enter();
	if (error != 0)
		return (error);
	status = efi_runtime->rt_settime(tm);
	efi_leave();
	error = efi_status_to_errno(status);
	return (error);
}

int
efi_set_time(struct efi_tm *tm)
{
	int error;

	if (efi_runtime == NULL)
		return (ENXIO);
	lockmgr(&resettodr_lock, LK_EXCLUSIVE);
	error = efi_set_time_locked(tm);
	lockmgr(&resettodr_lock, LK_RELEASE);
	return (error);
}

int
efi_var_get(efi_char *name, struct uuid *vendor, uint32_t *attrib,
    size_t *datasize, void *data)
{
	efi_status status;
	int error;

	error = efi_enter();
	if (error != 0)
		return (error);
	status = efi_runtime->rt_getvar(name, vendor, attrib, datasize, data);
	efi_leave();
	error = efi_status_to_errno(status);
	return (error);
}

int
efi_var_nextname(size_t *namesize, efi_char *name, struct uuid *vendor)
{
	efi_status status;
	int error;

	error = efi_enter();
	if (error != 0)
		return (error);
	status = efi_runtime->rt_scanvar(namesize, name, vendor);
	efi_leave();
	error = efi_status_to_errno(status);
	return (error);
}

int
efi_var_set(efi_char *name, struct uuid *vendor, uint32_t attrib,
    size_t datasize, void *data)
{
	efi_status status;
	int error;

	error = efi_enter();
	if (error != 0)
		return (error);
	status = efi_runtime->rt_setvar(name, vendor, attrib, datasize, data);
	efi_leave();
	error = efi_status_to_errno(status);
	return (error);
}

static int
efirt_modevents(module_t m, int event, void *arg __unused)
{

	switch (event) {
	case MOD_LOAD:
		return (efi_init());

	case MOD_UNLOAD:
		efi_uninit();
		return (0);

	case MOD_SHUTDOWN:
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t efirt_moddata = {
	.name = "efirt",
	.evhand = efirt_modevents,
	.priv = NULL,
};

DECLARE_MODULE(efirt, efirt_moddata, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(efirt, 1);


/* XXX debug stuff */
static int
efi_time_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct efi_tm tm;
	int error, val;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	error = efi_get_time(&tm);
	if (error == 0) {
		uprintf("EFI reports: Year %d Month %d Day %d Hour %d Min %d "
		    "Sec %d\n", tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour,
		    tm.tm_min, tm.tm_sec);
	}
	return (error);
}

SYSCTL_PROC(_debug, OID_AUTO, efi_time, CTLTYPE_INT | CTLFLAG_RW, NULL, 0,
	    efi_time_sysctl_handler, "I", "");
