/*
 * (MPSAFE)
 *
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/imgact_aout.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/resourcevar.h>
#include <sys/sysent.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/inflate.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/resident.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <sys/sysref2.h>

static int exec_res_id = 0;

static TAILQ_HEAD(,vmresident) exec_res_list;

static MALLOC_DEFINE(M_EXEC_RES, "vmresident", "resident execs");

/* lockmgr lock for protecting the exec_res_list */
static struct lock exec_list_lock;

static void
vm_resident_init(void *__dummy)
{
	lockinit(&exec_list_lock, "vmres", 0, 0);
	TAILQ_INIT(&exec_res_list);
}
SYSINIT(vmres, SI_BOOT1_LOCK, SI_ORDER_ANY, vm_resident_init, 0);

static int
fill_xresident(struct vmresident *vr, struct xresident *in, struct thread *td)
{
	struct stat st;
	struct vnode *vrtmp;
	int error = 0;

	vrtmp = vr->vr_vnode;

	in->res_entry_addr = vr->vr_entry_addr;
	in->res_id = vr->vr_id;
	if (vrtmp) {
		char *freepath, *fullpath;
		error = vn_fullpath(td->td_proc, vrtmp, &fullpath, &freepath, 0);
		if (error != 0) {
			/* could not retrieve cached path, return zero'ed string */
			bzero(in->res_file, MAXPATHLEN);
			error = 0;
		} else {
			strlcpy(in->res_file, fullpath, sizeof(in->res_file));
			kfree(freepath, M_TEMP);
		}

		/* indicate that we are using the vnode */
		error = vget(vrtmp, LK_EXCLUSIVE);
		if (error)
			goto done;
	
		/* retrieve underlying stat information and release vnode */
		error = vn_stat(vrtmp, &st, td->td_ucred);
		vput(vrtmp);
		if (error)
			goto done;

		in->res_stat = st;
	}

done:
	if (error)
		kprintf("fill_xresident, error = %d\n", error);
	return (error);
}

static int
sysctl_vm_resident(SYSCTL_HANDLER_ARGS)
{
	struct vmresident *vmres;
	struct thread *td;
	int error;
	int count;

	/* only super-user should call this sysctl */
	td = req->td;
	if ((priv_check(td, PRIV_VM_RESIDENT)) != 0)
		return EPERM;

	error = count = 0;

	if (exec_res_id == 0)
	    return error;
	
	/* client queried for number of resident binaries */
	if (!req->oldptr)
	    return SYSCTL_OUT(req, 0, exec_res_id);

	lockmgr(&exec_list_lock, LK_SHARED);

	TAILQ_FOREACH(vmres, &exec_res_list, vr_link) {
		struct xresident xres;
		error = fill_xresident(vmres, &xres, td);
		if (error != 0)
			break;
		
		error = SYSCTL_OUT(req, (void *)&xres,
				sizeof(struct xresident));
		if (error != 0)
			break;
	}
	lockmgr(&exec_list_lock, LK_RELEASE);

	return (error);
}
SYSCTL_PROC(_vm, OID_AUTO, resident, CTLTYPE_OPAQUE|CTLFLAG_RD, 0, 0,
  sysctl_vm_resident, "S,xresident", "resident executables (sys/resident.h)");

int
exec_resident_imgact(struct image_params *imgp)
{
	struct vmresident *vmres;

	/*
	 * resident image activator
	 */
	lockmgr(&exec_list_lock, LK_SHARED);
	if ((vmres = imgp->vp->v_resident) == NULL) {
	    lockmgr(&exec_list_lock, LK_RELEASE);
	    return(-1);
	}
	atomic_add_int(&vmres->vr_refs, 1);
	lockmgr(&exec_list_lock, LK_RELEASE);

	/*
	 * We want to exec the new vmspace without holding the lock to
	 * improve concurrency.
	 */
	exec_new_vmspace(imgp, vmres->vr_vmspace);
	imgp->resident = 1;
	imgp->interpreted = 0;
	imgp->proc->p_sysent = vmres->vr_sysent;
	imgp->entry_addr = vmres->vr_entry_addr;
	atomic_subtract_int(&vmres->vr_refs, 1);

	return(0);
}

/*
 * exec_sys_register(entry)
 *
 * Register ourselves for resident execution.  Only root (i.e. a process with
 * PRIV_VM_RESIDENT credentials) can do this.  This
 * will snapshot the vmspace and cause future exec's of the specified binary
 * to use the snapshot directly rather then load & relocate a new copy.
 *
 * MPALMOSTSAFE
 */
int
sys_exec_sys_register(struct exec_sys_register_args *uap)
{
    struct thread *td = curthread;
    struct vmresident *vmres;
    struct vnode *vp;
    struct proc *p;
    int error;

    p = td->td_proc;
    error = priv_check_cred(td->td_ucred, PRIV_VM_RESIDENT, 0);
    if (error)
	return(error);

    if ((vp = p->p_textvp) == NULL)
	return(ENOENT);

    lockmgr(&exec_list_lock, LK_EXCLUSIVE);

    if (vp->v_resident) {
	lockmgr(&exec_list_lock, LK_RELEASE);
	return(EEXIST);
    }

    vhold(vp);
    vmres = kmalloc(sizeof(*vmres), M_EXEC_RES, M_WAITOK | M_ZERO);
    vmres->vr_vnode = vp;
    vmres->vr_sysent = p->p_sysent;
    vmres->vr_id = ++exec_res_id;
    vmres->vr_entry_addr = (intptr_t)uap->entry;
    vmres->vr_vmspace = vmspace_fork(p->p_vmspace); /* XXX order */
    pmap_pinit2(vmspace_pmap(vmres->vr_vmspace));
    vp->v_resident = vmres;

    TAILQ_INSERT_TAIL(&exec_res_list, vmres, vr_link);
    lockmgr(&exec_list_lock, LK_RELEASE);

    return(0);
}

/*
 * exec_sys_unregister(id)
 *
 *	Unregister the specified id.  If an id of -1 is used unregister
 *	the registration associated with the current process.  An id of -2
 *	unregisters everything.
 *
 * MPALMOSTSAFE
 */
int
sys_exec_sys_unregister(struct exec_sys_unregister_args *uap)
{
    struct thread *td = curthread;
    struct vmresident *vmres;
    struct proc *p;
    int error;
    int id;
    int count;

    p = td->td_proc;
    error = priv_check_cred(td->td_ucred, PRIV_VM_RESIDENT, 0);
    if (error)
	return(error);

    /*
     * If id is -1, unregister ourselves
     */
    lockmgr(&exec_list_lock, LK_EXCLUSIVE);

    if ((id = uap->id) == -1 && p->p_textvp && p->p_textvp->v_resident)
	id = p->p_textvp->v_resident->vr_id;

    /*
     * Look for the registration
     */
    error = ENOENT;
    count = 0;

restart:
    TAILQ_FOREACH(vmres, &exec_res_list, vr_link) {
	if (id == -2 || vmres->vr_id == id) {
	    /*
	     * Check race against exec
	     */
	    if (vmres->vr_refs) {
		lockmgr(&exec_list_lock, LK_RELEASE);
		tsleep(vmres, 0, "vmres", 1);
		lockmgr(&exec_list_lock, LK_EXCLUSIVE);
		goto restart;
	    }

	    /*
	     * Remove it
	     */
	    TAILQ_REMOVE(&exec_res_list, vmres, vr_link);
	    if (vmres->vr_vnode) {
		vmres->vr_vnode->v_resident = NULL;
		vdrop(vmres->vr_vnode);
		vmres->vr_vnode = NULL;
	    }
	    if (vmres->vr_vmspace) {
		vmspace_rel(vmres->vr_vmspace);
		vmres->vr_vmspace = NULL;
	    }
	    kfree(vmres, M_EXEC_RES);
	    exec_res_id--;
	    error = 0;
	    ++count;
	    goto restart;
	}
    }
    lockmgr(&exec_list_lock, LK_RELEASE);

    if (error == 0)
	uap->sysmsg_result = count;
    return(error);
}
