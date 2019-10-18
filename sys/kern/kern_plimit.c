/*
 * Copyright (c) 2006,2017,2018 The DragonFly Project.  All rights reserved.
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
/*
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_resource.c	8.5 (Berkeley) 1/21/94
 */
#include <sys/resource.h>
#include <sys/spinlock.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/file.h>
#include <sys/lockf.h>
#include <sys/kern_syscall.h>
#include <sys/malloc.h>

#include <vm/vm_param.h>
#include <vm/vm.h>
#include <vm/vm_map.h>

#include <machine/pmap.h>

#include <sys/spinlock2.h>

static MALLOC_DEFINE(M_PLIMIT, "plimit", "resource limits");

static void plimit_copy(struct plimit *olimit, struct plimit *nlimit);

static __inline
struct plimit *
readplimits(struct proc *p)
{
	thread_t td = curthread;
	struct plimit *limit;

	limit = td->td_limit;
	if (limit != p->p_limit) {
		spin_lock_shared(&p->p_spin);
		limit = p->p_limit;
		atomic_add_int(&limit->p_refcnt, 1);
		spin_unlock_shared(&p->p_spin);
		if (td->td_limit)
			plimit_free(td->td_limit);
		td->td_limit = limit;
	}
	return limit;
}

/*
 * Initialize proc0's plimit structure.  All later plimit structures
 * are inherited through fork.
 */
void
plimit_init0(struct plimit *limit)
{
	int i;
	rlim_t lim;

	for (i = 0; i < RLIM_NLIMITS; ++i) {
		limit->pl_rlimit[i].rlim_cur = RLIM_INFINITY;
		limit->pl_rlimit[i].rlim_max = RLIM_INFINITY;
	}
	limit->pl_rlimit[RLIMIT_NOFILE].rlim_cur = maxfiles;
	limit->pl_rlimit[RLIMIT_NOFILE].rlim_max = maxfiles;
	limit->pl_rlimit[RLIMIT_NPROC].rlim_cur = maxproc;
	limit->pl_rlimit[RLIMIT_NPROC].rlim_max = maxproc;
	lim = ptoa((rlim_t)vmstats.v_free_count);
	limit->pl_rlimit[RLIMIT_RSS].rlim_max = lim;
	limit->pl_rlimit[RLIMIT_MEMLOCK].rlim_max = lim;
	limit->pl_rlimit[RLIMIT_MEMLOCK].rlim_cur = lim / 3;
	limit->p_cpulimit = RLIM_INFINITY;
	limit->p_refcnt = 1;
	spin_init(&limit->p_spin, "plimitinit");
}

/*
 * Return a plimit for use by a new forked process given the one
 * contained in the parent process.
 */
struct plimit *
plimit_fork(struct proc *p1)
{
	struct plimit *olimit = p1->p_limit;
	struct plimit *nlimit;
	uint32_t count;

	/*
	 * Try to share the parent's plimit structure.  If we cannot, make
	 * a copy.
	 *
	 * NOTE: (count) value is field prior to increment.
	 */
	count = atomic_fetchadd_int(&olimit->p_refcnt, 1);
	cpu_ccfence();
	if (count & PLIMITF_EXCLUSIVE) {
		if ((count & PLIMITF_MASK) == 1 && p1->p_nthreads == 1) {
			atomic_clear_int(&olimit->p_refcnt, PLIMITF_EXCLUSIVE);
		} else {
			nlimit = kmalloc(sizeof(*nlimit), M_PLIMIT, M_WAITOK);
			plimit_copy(olimit, nlimit);
			olimit = nlimit;
		}
	}
	return olimit;
}

/*
 * This routine is called when a new LWP is created for a process.  We
 * must force exclusivity to ensure that p->p_limit remains stable.
 *
 * LWPs share the same process structure so this does not bump refcnt.
 */
void
plimit_lwp_fork(struct proc *p)
{
	struct plimit *olimit = p->p_limit;
	struct plimit *nlimit;
	uint32_t count;

	count = olimit->p_refcnt;
	cpu_ccfence();
	if ((count & PLIMITF_EXCLUSIVE) == 0) {
		if (count != 1) {
			nlimit = kmalloc(sizeof(*nlimit), M_PLIMIT, M_WAITOK);
			plimit_copy(olimit, nlimit);
			p->p_limit = nlimit;
			plimit_free(olimit);
			olimit = nlimit;
		}
		atomic_set_int(&olimit->p_refcnt, PLIMITF_EXCLUSIVE);
	}
}

/*
 * This routine is called to fixup a process's p_limit structure prior
 * to it being modified.  If index >= 0 the specified modification is also
 * made.
 *
 * This routine must make the limit structure exclusive.  If we are threaded,
 * the structure will already be exclusive.  A later fork will convert it
 * back to copy-on-write if possible.
 *
 * We can count on p->p_limit being stable since if we had created any
 * threads it will have already been made exclusive.
 */
void
plimit_modify(struct proc *p, int index, struct rlimit *rlim)
{
	struct plimit *olimit;
	struct plimit *nlimit;
	uint32_t count;

	/*
	 * Make exclusive
	 */
	olimit = p->p_limit;
	count = olimit->p_refcnt;
	cpu_ccfence();
	if ((count & PLIMITF_EXCLUSIVE) == 0) {
		if (count != 1) {
			nlimit = kmalloc(sizeof(*nlimit), M_PLIMIT, M_WAITOK);
			plimit_copy(olimit, nlimit);
			p->p_limit = nlimit;
			plimit_free(olimit);
			olimit = nlimit;
		}
		atomic_set_int(&olimit->p_refcnt, PLIMITF_EXCLUSIVE);
	}

	/*
	 * Make modification
	 */
	if (index >= 0) {
		if (p->p_nthreads == 1) {
			p->p_limit->pl_rlimit[index] = *rlim;
		} else {
			spin_lock(&olimit->p_spin);
			p->p_limit->pl_rlimit[index].rlim_cur = rlim->rlim_cur;
			p->p_limit->pl_rlimit[index].rlim_max = rlim->rlim_max;
			spin_unlock(&olimit->p_spin);
		}
	}
}

/*
 * Destroy a process's plimit structure.
 */
void
plimit_free(struct plimit *limit)
{
	uint32_t count;

	count = atomic_fetchadd_int(&limit->p_refcnt, -1);

	if ((count & ~PLIMITF_EXCLUSIVE) == 1) {
		limit->p_refcnt = -999;
		kfree(limit, M_PLIMIT);
	}
}

/*
 * Modify a resource limit (from system call)
 */
int
kern_setrlimit(u_int which, struct rlimit *limp)
{
        struct proc *p = curproc;
	struct plimit *limit;
        struct rlimit *alimp;
        int error;

        if (which >= RLIM_NLIMITS)
                return (EINVAL);

	/*
	 * We will be modifying a resource, make a copy if necessary.
	 */
	plimit_modify(p, -1, NULL);
	limit = p->p_limit;
        alimp = &limit->pl_rlimit[which];

        /*
         * Preserve historical bugs by treating negative limits as unsigned.
         */
        if (limp->rlim_cur < 0)
                limp->rlim_cur = RLIM_INFINITY;
        if (limp->rlim_max < 0)
                limp->rlim_max = RLIM_INFINITY;

	spin_lock(&limit->p_spin);
        if (limp->rlim_cur > alimp->rlim_max ||
            limp->rlim_max > alimp->rlim_max) {
		spin_unlock(&limit->p_spin);
                error = priv_check_cred(p->p_ucred, PRIV_PROC_SETRLIMIT, 0);
                if (error)
                        return (error);
	} else {
		spin_unlock(&limit->p_spin);
	}
        if (limp->rlim_cur > limp->rlim_max)
                limp->rlim_cur = limp->rlim_max;

        switch (which) {
        case RLIMIT_CPU:
		spin_lock(&limit->p_spin);
                if (limp->rlim_cur > RLIM_INFINITY / (rlim_t)1000000)
                        limit->p_cpulimit = RLIM_INFINITY;
                else
                        limit->p_cpulimit = (rlim_t)1000000 * limp->rlim_cur;
		spin_unlock(&limit->p_spin);
                break;
        case RLIMIT_DATA:
                if (limp->rlim_cur > maxdsiz)
                        limp->rlim_cur = maxdsiz;
                if (limp->rlim_max > maxdsiz)
                        limp->rlim_max = maxdsiz;
                break;

        case RLIMIT_STACK:
                if (limp->rlim_cur > maxssiz)
                        limp->rlim_cur = maxssiz;
                if (limp->rlim_max > maxssiz)
                        limp->rlim_max = maxssiz;
                /*
                 * Stack is allocated to the max at exec time with only
                 * "rlim_cur" bytes accessible.  If stack limit is going
                 * up make more accessible, if going down make inaccessible.
                 */
		spin_lock(&limit->p_spin);
                if (limp->rlim_cur != alimp->rlim_cur) {
                        vm_offset_t addr;
                        vm_size_t size;
                        vm_prot_t prot;

                        if (limp->rlim_cur > alimp->rlim_cur) {
                                prot = VM_PROT_ALL;
                                size = limp->rlim_cur - alimp->rlim_cur;
                                addr = USRSTACK - limp->rlim_cur;
                        } else {
                                prot = VM_PROT_NONE;
                                size = alimp->rlim_cur - limp->rlim_cur;
                                addr = USRSTACK - alimp->rlim_cur;
                        }
			spin_unlock(&limit->p_spin);
                        addr = trunc_page(addr);
                        size = round_page(size);
                        vm_map_protect(&p->p_vmspace->vm_map,
				       addr, addr+size, prot, FALSE);
                } else {
			spin_unlock(&limit->p_spin);
		}
                break;

        case RLIMIT_NOFILE:
                if (limp->rlim_cur > maxfilesperproc)
                        limp->rlim_cur = maxfilesperproc;
                if (limp->rlim_max > maxfilesperproc)
                        limp->rlim_max = maxfilesperproc;
                break;

        case RLIMIT_NPROC:
                if (limp->rlim_cur > maxprocperuid)
                        limp->rlim_cur = maxprocperuid;
                if (limp->rlim_max > maxprocperuid)
                        limp->rlim_max = maxprocperuid;
                if (limp->rlim_cur < 1)
                        limp->rlim_cur = 1;
                if (limp->rlim_max < 1)
                        limp->rlim_max = 1;
                break;
        case RLIMIT_POSIXLOCKS:
                if (limp->rlim_cur > maxposixlocksperuid)
                        limp->rlim_cur = maxposixlocksperuid;
                if (limp->rlim_max > maxposixlocksperuid)
                        limp->rlim_max = maxposixlocksperuid;
                break;
        }
	spin_lock(&limit->p_spin);
        *alimp = *limp;
	spin_unlock(&limit->p_spin);
        return (0);
}

/*
 * The rlimit indexed by which is returned in the second argument.
 */
int
kern_getrlimit(u_int which, struct rlimit *limp)
{
	struct proc *p = curproc;
	struct plimit *limit;

	/*
	 * p is NULL when kern_getrlimit is called from a
	 * kernel thread. In this case as the calling proc
	 * isn't available we just skip the limit check.
	 */
	if (p == NULL)
		return 0;

        if (which >= RLIM_NLIMITS)
                return (EINVAL);

	limit = readplimits(p);
        *limp = limit->pl_rlimit[which];

        return (0);
}

/*
 * Determine if the cpu limit has been reached and return an operations
 * code for the caller to perform.
 */
int
plimit_testcpulimit(struct proc *p, u_int64_t ttime)
{
	struct plimit *limit;
	struct rlimit *rlim;
	int mode;

	limit = readplimits(p);

	/*
	 * Initial tests without the spinlock.  This is the fast path.
	 * Any 32/64 bit glitches will fall through and retest with
	 * the spinlock.
	 */
	if (limit->p_cpulimit == RLIM_INFINITY)
		return(PLIMIT_TESTCPU_OK);
	if (ttime <= limit->p_cpulimit)
		return(PLIMIT_TESTCPU_OK);

	if (ttime > limit->p_cpulimit) {
		rlim = &limit->pl_rlimit[RLIMIT_CPU];
		if (ttime / (rlim_t)1000000 >= rlim->rlim_max + 5)
			mode = PLIMIT_TESTCPU_KILL;
		else
			mode = PLIMIT_TESTCPU_XCPU;
	} else {
		mode = PLIMIT_TESTCPU_OK;
	}

	return(mode);
}

/*
 * Helper routine to copy olimit to nlimit and initialize nlimit for
 * use.  nlimit's reference count will be set to 1 and its exclusive bit
 * will be cleared.
 */
static
void
plimit_copy(struct plimit *olimit, struct plimit *nlimit)
{
	*nlimit = *olimit;

	spin_init(&nlimit->p_spin, "plimitcopy");
	nlimit->p_refcnt = 1;
}

/*
 * This routine returns the value of a resource, downscaled based on
 * the processes fork depth and chroot depth (up to 50%).  This mechanism
 * is designed to prevent run-aways from blowing up unrelated processes
 * running under the same UID.
 *
 * NOTE: Currently only applicable to RLIMIT_NPROC.  We could also limit
 *	 file descriptors but we shouldn't have to as these are allocated
 *	 dynamically.
 */
u_int64_t
plimit_getadjvalue(int i)
{
	struct proc *p = curproc;
	struct plimit *limit;
	uint64_t v;
	uint32_t depth;

	limit = p->p_limit;
	v = limit->pl_rlimit[i].rlim_cur;
	if (i == RLIMIT_NPROC) {
		/*
		 * 10% per chroot (around 1/3% per fork depth), with a
		 * maximum of 50% downscaling of the resource limit.
		 */
		depth = p->p_depth;
		if (depth > 32 * 5)
			depth = 32 * 5;
		v -= v * depth / 320;
	}
	return v;
}
