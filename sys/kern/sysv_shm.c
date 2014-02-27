/*
 * Copyright (c) 1994 Adam Glass and Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Adam Glass and Charles
 *	Hannum.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_compat.h"
#include "opt_sysvipc.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/shm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysent.h>
#include <sys/jail.h>

#include <sys/mplock2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

static MALLOC_DEFINE(M_SHM, "shm", "SVID compatible shared memory segments");

static int shmget_allocate_segment (struct proc *p, struct shmget_args *uap, int mode);
static int shmget_existing (struct proc *p, struct shmget_args *uap, int mode, int segnum);

#define	SHMSEG_FREE     	0x0200
#define	SHMSEG_REMOVED  	0x0400
#define	SHMSEG_ALLOCATED	0x0800
#define	SHMSEG_WANTED		0x1000

static int shm_last_free, shm_committed, shmalloced;
int shm_nused;
static struct shmid_ds	*shmsegs;

struct shm_handle {
	/* vm_offset_t kva; */
	vm_object_t shm_object;
};

struct shmmap_state {
	vm_offset_t va;
	int shmid;
};

static void shm_deallocate_segment (struct shmid_ds *);
static int shm_find_segment_by_key (key_t);
static struct shmid_ds *shm_find_segment_by_shmid (int);
static int shm_delete_mapping (struct vmspace *vm, struct shmmap_state *);
static void shmrealloc (void);
static void shminit (void *);

/*
 * Tuneable values
 */
#ifndef SHMMIN
#define	SHMMIN	1
#endif
#ifndef SHMMNI
#define	SHMMNI	512
#endif
#ifndef SHMSEG
#define	SHMSEG	1024
#endif

struct	shminfo shminfo = {
	0,
	SHMMIN,
	SHMMNI,
	SHMSEG,
	0
};

static int shm_use_phys = 1;

TUNABLE_LONG("kern.ipc.shmmin", &shminfo.shmmin);
TUNABLE_LONG("kern.ipc.shmmni", &shminfo.shmmni);
TUNABLE_LONG("kern.ipc.shmseg", &shminfo.shmseg);
TUNABLE_LONG("kern.ipc.shmmaxpgs", &shminfo.shmall);
TUNABLE_INT("kern.ipc.shm_use_phys", &shm_use_phys);

SYSCTL_LONG(_kern_ipc, OID_AUTO, shmmax, CTLFLAG_RW, &shminfo.shmmax, 0,
    "Max shared memory segment size");
SYSCTL_LONG(_kern_ipc, OID_AUTO, shmmin, CTLFLAG_RW, &shminfo.shmmin, 0,
    "Min shared memory segment size");
SYSCTL_LONG(_kern_ipc, OID_AUTO, shmmni, CTLFLAG_RD, &shminfo.shmmni, 0,
    "Max number of shared memory identifiers");
SYSCTL_LONG(_kern_ipc, OID_AUTO, shmseg, CTLFLAG_RW, &shminfo.shmseg, 0,
    "Max shared memory segments per process");
SYSCTL_LONG(_kern_ipc, OID_AUTO, shmall, CTLFLAG_RW, &shminfo.shmall, 0,
    "Max pages of shared memory");
SYSCTL_INT(_kern_ipc, OID_AUTO, shm_use_phys, CTLFLAG_RW, &shm_use_phys, 0,
    "Use phys pager allocation instead of swap pager allocation");

static int
shm_find_segment_by_key(key_t key)
{
	int i;

	for (i = 0; i < shmalloced; i++) {
		if ((shmsegs[i].shm_perm.mode & SHMSEG_ALLOCATED) &&
		    shmsegs[i].shm_perm.key == key)
			return i;
	}
	return -1;
}

static struct shmid_ds *
shm_find_segment_by_shmid(int shmid)
{
	int segnum;
	struct shmid_ds *shmseg;

	segnum = IPCID_TO_IX(shmid);
	if (segnum < 0 || segnum >= shmalloced)
		return NULL;
	shmseg = &shmsegs[segnum];
	if ((shmseg->shm_perm.mode & (SHMSEG_ALLOCATED | SHMSEG_REMOVED))
	    != SHMSEG_ALLOCATED ||
	    shmseg->shm_perm.seq != IPCID_TO_SEQ(shmid)) {
		return NULL;
	}
	return shmseg;
}

static void
shm_deallocate_segment(struct shmid_ds *shmseg)
{
	struct shm_handle *shm_handle;
	size_t size;

	shm_handle = shmseg->shm_internal;
	vm_object_deallocate(shm_handle->shm_object);
	kfree((caddr_t)shm_handle, M_SHM);
	shmseg->shm_internal = NULL;
	size = round_page(shmseg->shm_segsz);
	shm_committed -= btoc(size);
	shm_nused--;
	shmseg->shm_perm.mode = SHMSEG_FREE;
}

static int
shm_delete_mapping(struct vmspace *vm, struct shmmap_state *shmmap_s)
{
	struct shmid_ds *shmseg;
	int segnum, result;
	size_t size;

	segnum = IPCID_TO_IX(shmmap_s->shmid);
	shmseg = &shmsegs[segnum];
	size = round_page(shmseg->shm_segsz);
	result = vm_map_remove(&vm->vm_map, shmmap_s->va, shmmap_s->va + size);
	if (result != KERN_SUCCESS)
		return EINVAL;
	shmmap_s->shmid = -1;
	shmseg->shm_dtime = time_second;
	if ((--shmseg->shm_nattch <= 0) &&
	    (shmseg->shm_perm.mode & SHMSEG_REMOVED)) {
		shm_deallocate_segment(shmseg);
		shm_last_free = segnum;
	}
	return 0;
}

/*
 * MPALMOSTSAFE
 */
int
sys_shmdt(struct shmdt_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct shmmap_state *shmmap_s;
	long i;
	int error;

	if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);

	get_mplock();
	shmmap_s = (struct shmmap_state *)p->p_vmspace->vm_shm;
	if (shmmap_s == NULL) {
		error = EINVAL;
		goto done;
	}
	for (i = 0; i < shminfo.shmseg; i++, shmmap_s++) {
		if (shmmap_s->shmid != -1 &&
		    shmmap_s->va == (vm_offset_t)uap->shmaddr)
			break;
	}
	if (i == shminfo.shmseg)
		error = EINVAL;
	else
		error = shm_delete_mapping(p->p_vmspace, shmmap_s);
done:
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_shmat(struct shmat_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error, flags;
	long i;
	struct shmid_ds *shmseg;
	struct shmmap_state *shmmap_s = NULL;
	struct shm_handle *shm_handle;
	vm_offset_t attach_va;
	vm_prot_t prot;
	vm_size_t size;
	vm_size_t align;
	int rv;

	if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);

	get_mplock();
again:
	shmmap_s = (struct shmmap_state *)p->p_vmspace->vm_shm;
	if (shmmap_s == NULL) {
		size = shminfo.shmseg * sizeof(struct shmmap_state);
		shmmap_s = kmalloc(size, M_SHM, M_WAITOK);
		for (i = 0; i < shminfo.shmseg; i++)
			shmmap_s[i].shmid = -1;
		if (p->p_vmspace->vm_shm != NULL) {
			kfree(shmmap_s, M_SHM);
			goto again;
		}
		p->p_vmspace->vm_shm = (caddr_t)shmmap_s;
	}
	shmseg = shm_find_segment_by_shmid(uap->shmid);
	if (shmseg == NULL) {
		error = EINVAL;
		goto done;
	}
	error = ipcperm(p, &shmseg->shm_perm,
			(uap->shmflg & SHM_RDONLY) ? IPC_R : IPC_R|IPC_W);
	if (error)
		goto done;
	for (i = 0; i < shminfo.shmseg; i++) {
		if (shmmap_s->shmid == -1)
			break;
		shmmap_s++;
	}
	if (i >= shminfo.shmseg) {
		error = EMFILE;
		goto done;
	}
	size = round_page(shmseg->shm_segsz);
#ifdef VM_PROT_READ_IS_EXEC
	prot = VM_PROT_READ | VM_PROT_EXECUTE;
#else
	prot = VM_PROT_READ;
#endif
	if ((uap->shmflg & SHM_RDONLY) == 0)
		prot |= VM_PROT_WRITE;
	flags = MAP_ANON | MAP_SHARED;
	if (uap->shmaddr) {
		flags |= MAP_FIXED;
		if (uap->shmflg & SHM_RND) {
			attach_va = (vm_offset_t)uap->shmaddr & ~(SHMLBA-1);
		} else if (((vm_offset_t)uap->shmaddr & (SHMLBA-1)) == 0) {
			attach_va = (vm_offset_t)uap->shmaddr;
		} else {
			error = EINVAL;
			goto done;
		}
	} else {
		/*
		 * This is just a hint to vm_map_find() about where to put it.
		 */
		attach_va = round_page((vm_offset_t)p->p_vmspace->vm_taddr +
				       maxtsiz + maxdsiz);
	}

	/*
	 * Handle alignment.  For large memory maps it is possible
	 * that the MMU can optimize the page table so align anything
	 * that is a multiple of SEG_SIZE to SEG_SIZE.
	 */
	if ((flags & MAP_FIXED) == 0 && (size & SEG_MASK) == 0)
		align = SEG_SIZE;
	else
		align = PAGE_SIZE;

	shm_handle = shmseg->shm_internal;
	vm_object_hold(shm_handle->shm_object);
	vm_object_chain_wait(shm_handle->shm_object, 0);
	vm_object_reference_locked(shm_handle->shm_object);
	rv = vm_map_find(&p->p_vmspace->vm_map, 
			 shm_handle->shm_object, 0,
			 &attach_va,
			 size, align,
			 ((flags & MAP_FIXED) ? 0 : 1), 
			 VM_MAPTYPE_NORMAL,
			 prot, prot,
			 0);
	vm_object_drop(shm_handle->shm_object);
	if (rv != KERN_SUCCESS) {
                vm_object_deallocate(shm_handle->shm_object);
		error = ENOMEM;
		goto done;
	}
	vm_map_inherit(&p->p_vmspace->vm_map,
		       attach_va, attach_va + size, VM_INHERIT_SHARE);

	KKASSERT(shmmap_s->shmid == -1);
	shmmap_s->va = attach_va;
	shmmap_s->shmid = uap->shmid;
	shmseg->shm_lpid = p->p_pid;
	shmseg->shm_atime = time_second;
	shmseg->shm_nattch++;
	uap->sysmsg_resultp = (void *)attach_va;
	error = 0;
done:
	rel_mplock();
	return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_shmctl(struct shmctl_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct shmid_ds inbuf;
	struct shmid_ds *shmseg;

	if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);

	get_mplock();
	shmseg = shm_find_segment_by_shmid(uap->shmid);
	if (shmseg == NULL) {
		error = EINVAL;
		goto done;
	}

	switch (uap->cmd) {
	case IPC_STAT:
		error = ipcperm(p, &shmseg->shm_perm, IPC_R);
		if (error == 0)
			error = copyout(shmseg, uap->buf, sizeof(inbuf));
		break;
	case IPC_SET:
		error = ipcperm(p, &shmseg->shm_perm, IPC_M);
		if (error == 0)
			error = copyin(uap->buf, &inbuf, sizeof(inbuf));
		if (error == 0) {
			shmseg->shm_perm.uid = inbuf.shm_perm.uid;
			shmseg->shm_perm.gid = inbuf.shm_perm.gid;
			shmseg->shm_perm.mode =
			    (shmseg->shm_perm.mode & ~ACCESSPERMS) |
			    (inbuf.shm_perm.mode & ACCESSPERMS);
			shmseg->shm_ctime = time_second;
		}
		break;
	case IPC_RMID:
		error = ipcperm(p, &shmseg->shm_perm, IPC_M);
		if (error == 0) {
			shmseg->shm_perm.key = IPC_PRIVATE;
			shmseg->shm_perm.mode |= SHMSEG_REMOVED;
			if (shmseg->shm_nattch <= 0) {
				shm_deallocate_segment(shmseg);
				shm_last_free = IPCID_TO_IX(uap->shmid);
			}
		}
		break;
#if 0
	case SHM_LOCK:
	case SHM_UNLOCK:
#endif
	default:
		error = EINVAL;
		break;
	}
done:
	rel_mplock();
	return error;
}

static int
shmget_existing(struct proc *p, struct shmget_args *uap, int mode, int segnum)
{
	struct shmid_ds *shmseg;
	int error;

	shmseg = &shmsegs[segnum];
	if (shmseg->shm_perm.mode & SHMSEG_REMOVED) {
		/*
		 * This segment is in the process of being allocated.  Wait
		 * until it's done, and look the key up again (in case the
		 * allocation failed or it was freed).
		 */
		shmseg->shm_perm.mode |= SHMSEG_WANTED;
		error = tsleep((caddr_t)shmseg, PCATCH, "shmget", 0);
		if (error)
			return error;
		return EAGAIN;
	}
	if ((uap->shmflg & (IPC_CREAT | IPC_EXCL)) == (IPC_CREAT | IPC_EXCL))
		return EEXIST;
	error = ipcperm(p, &shmseg->shm_perm, mode);
	if (error)
		return error;
	if (uap->size && uap->size > shmseg->shm_segsz)
		return EINVAL;
	uap->sysmsg_result = IXSEQ_TO_IPCID(segnum, shmseg->shm_perm);
	return 0;
}

static int
shmget_allocate_segment(struct proc *p, struct shmget_args *uap, int mode)
{
	int i, segnum, shmid;
	size_t size;
	struct ucred *cred = p->p_ucred;
	struct shmid_ds *shmseg;
	struct shm_handle *shm_handle;

	if (uap->size < shminfo.shmmin || uap->size > shminfo.shmmax)
		return EINVAL;
	if (shm_nused >= shminfo.shmmni) /* any shmids left? */
		return ENOSPC;
	size = round_page(uap->size);
	if (shm_committed + btoc(size) > shminfo.shmall)
		return ENOMEM;
	if (shm_last_free < 0) {
		shmrealloc();	/* maybe expand the shmsegs[] array */
		for (i = 0; i < shmalloced; i++) {
			if (shmsegs[i].shm_perm.mode & SHMSEG_FREE)
				break;
		}
		if (i == shmalloced)
			return ENOSPC;
		segnum = i;
	} else  {
		segnum = shm_last_free;
		shm_last_free = -1;
	}
	shmseg = &shmsegs[segnum];
	/*
	 * In case we sleep in malloc(), mark the segment present but deleted
	 * so that noone else tries to create the same key.
	 */
	shmseg->shm_perm.mode = SHMSEG_ALLOCATED | SHMSEG_REMOVED;
	shmseg->shm_perm.key = uap->key;
	shmseg->shm_perm.seq = (shmseg->shm_perm.seq + 1) & 0x7fff;
	shm_handle = kmalloc(sizeof(struct shm_handle), M_SHM, M_WAITOK);
	shmid = IXSEQ_TO_IPCID(segnum, shmseg->shm_perm);
	
	/*
	 * We make sure that we have allocated a pager before we need
	 * to.
	 */
	if (shm_use_phys) {
		shm_handle->shm_object =
		   phys_pager_alloc(NULL, size, VM_PROT_DEFAULT, 0);
	} else {
		shm_handle->shm_object =
		   swap_pager_alloc(NULL, size, VM_PROT_DEFAULT, 0);
	}
	vm_object_clear_flag(shm_handle->shm_object, OBJ_ONEMAPPING);
	vm_object_set_flag(shm_handle->shm_object, OBJ_NOSPLIT);

	shmseg->shm_internal = shm_handle;
	shmseg->shm_perm.cuid = shmseg->shm_perm.uid = cred->cr_uid;
	shmseg->shm_perm.cgid = shmseg->shm_perm.gid = cred->cr_gid;
	shmseg->shm_perm.mode = (shmseg->shm_perm.mode & SHMSEG_WANTED) |
	    (mode & ACCESSPERMS) | SHMSEG_ALLOCATED;
	shmseg->shm_segsz = uap->size;
	shmseg->shm_cpid = p->p_pid;
	shmseg->shm_lpid = shmseg->shm_nattch = 0;
	shmseg->shm_atime = shmseg->shm_dtime = 0;
	shmseg->shm_ctime = time_second;
	shm_committed += btoc(size);
	shm_nused++;

	/*
	 * If a physical mapping is desired and we have a ton of free pages
	 * we pre-allocate the pages here in order to avoid on-the-fly
	 * allocation later.  This has a big effect on database warm-up
	 * times since DFly supports concurrent page faults coming from the
	 * same VM object for pages which already exist.
	 *
	 * This can hang the kernel for a while so only do it if shm_use_phys
	 * is set to 2 or higher.
	 */
	if (shm_use_phys > 1) {
		vm_pindex_t pi, pmax;
		vm_page_t m;

		pmax = round_page(shmseg->shm_segsz) >> PAGE_SHIFT;
		vm_object_hold(shm_handle->shm_object);
		if (pmax > vmstats.v_free_count)
			pmax = vmstats.v_free_count;
		for (pi = 0; pi < pmax; ++pi) {
			m = vm_page_grab(shm_handle->shm_object, pi,
					 VM_ALLOC_SYSTEM | VM_ALLOC_NULL_OK |
					 VM_ALLOC_ZERO);
			if (m == NULL)
				break;
			vm_pager_get_page(shm_handle->shm_object, &m, 1);
			vm_page_activate(m);
			vm_page_wakeup(m);
			lwkt_yield();
		}
		vm_object_drop(shm_handle->shm_object);
	}

	if (shmseg->shm_perm.mode & SHMSEG_WANTED) {
		/*
		 * Somebody else wanted this key while we were asleep.  Wake
		 * them up now.
		 */
		shmseg->shm_perm.mode &= ~SHMSEG_WANTED;
		wakeup((caddr_t)shmseg);
	}
	uap->sysmsg_result = shmid;
	return 0;
}

/*
 * MPALMOSTSAFE
 */
int
sys_shmget(struct shmget_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int segnum, mode, error;

	if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);

	mode = uap->shmflg & ACCESSPERMS;
	get_mplock();

	if (uap->key != IPC_PRIVATE) {
	again:
		segnum = shm_find_segment_by_key(uap->key);
		if (segnum >= 0) {
			error = shmget_existing(p, uap, mode, segnum);
			if (error == EAGAIN)
				goto again;
			goto done;
		}
		if ((uap->shmflg & IPC_CREAT) == 0) {
			error = ENOENT;
			goto done;
		}
	}
	error = shmget_allocate_segment(p, uap, mode);
done:
	rel_mplock();
	return (error);
}

void
shmfork(struct proc *p1, struct proc *p2)
{
	struct shmmap_state *shmmap_s;
	size_t size;
	int i;

	get_mplock();
	size = shminfo.shmseg * sizeof(struct shmmap_state);
	shmmap_s = kmalloc(size, M_SHM, M_WAITOK);
	bcopy((caddr_t)p1->p_vmspace->vm_shm, (caddr_t)shmmap_s, size);
	p2->p_vmspace->vm_shm = (caddr_t)shmmap_s;
	for (i = 0; i < shminfo.shmseg; i++, shmmap_s++) {
		if (shmmap_s->shmid != -1)
			shmsegs[IPCID_TO_IX(shmmap_s->shmid)].shm_nattch++;
	}
	rel_mplock();
}

void
shmexit(struct vmspace *vm)
{
	struct shmmap_state *base, *shm;
	int i;

	if ((base = (struct shmmap_state *)vm->vm_shm) != NULL) {
		vm->vm_shm = NULL;
		get_mplock();
		for (i = 0, shm = base; i < shminfo.shmseg; i++, shm++) {
			if (shm->shmid != -1)
				shm_delete_mapping(vm, shm);
		}
		kfree(base, M_SHM);
		rel_mplock();
	}
}

static void
shmrealloc(void)
{
	int i;
	struct shmid_ds *newsegs;

	if (shmalloced >= shminfo.shmmni)
		return;

	newsegs = kmalloc(shminfo.shmmni * sizeof(*newsegs), M_SHM, M_WAITOK);
	for (i = 0; i < shmalloced; i++)
		bcopy(&shmsegs[i], &newsegs[i], sizeof(newsegs[0]));
	for (; i < shminfo.shmmni; i++) {
		shmsegs[i].shm_perm.mode = SHMSEG_FREE;
		shmsegs[i].shm_perm.seq = 0;
	}
	kfree(shmsegs, M_SHM);
	shmsegs = newsegs;
	shmalloced = shminfo.shmmni;
}

static void
shminit(void *dummy)
{
	int i;

	/*
	 * If not overridden by a tunable set the maximum shm to
	 * 2/3 of main memory.
	 */
	if (shminfo.shmall == 0)
		shminfo.shmall = (size_t)vmstats.v_page_count * 2 / 3;

	shminfo.shmmax = shminfo.shmall * PAGE_SIZE;
	shmalloced = shminfo.shmmni;
	shmsegs = kmalloc(shmalloced * sizeof(shmsegs[0]), M_SHM, M_WAITOK);
	for (i = 0; i < shmalloced; i++) {
		shmsegs[i].shm_perm.mode = SHMSEG_FREE;
		shmsegs[i].shm_perm.seq = 0;
	}
	shm_last_free = 0;
	shm_nused = 0;
	shm_committed = 0;
}
SYSINIT(sysv_shm, SI_SUB_SYSV_SHM, SI_ORDER_FIRST, shminit, NULL);
