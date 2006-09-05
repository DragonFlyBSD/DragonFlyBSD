/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * $FreeBSD: src/sys/kern/kern_resource.c,v 1.55.2.5 2001/11/03 01:41:08 ps Exp $
 * $DragonFly: src/sys/kern/kern_plimit.c,v 1.2 2006/09/05 00:55:45 dillon Exp $
 */

#include <sys/resource.h>
#include <sys/spinlock.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/lockf.h>
#include <sys/kern_syscall.h>

#include <vm/vm_param.h>
#include <vm/vm.h>
#include <vm/vm_map.h>

#include <machine/pmap.h>

#include <sys/spinlock2.h>

static struct plimit *plimit_copy(struct plimit *olimit);
static void plimit_exclusive(struct plimit **limitp);

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
	spin_init(&limit->p_spin);
}

/*
 * Return a plimit for use by a new forked process given the one
 * contained in the parent process.  p_exclusive will be set if
 * the parent process has more then one thread, requiring a copy.
 * Otherwise we can just inherit a reference.
 *
 * MPSAFE
 */
struct plimit *
plimit_fork(struct plimit *limit)
{
	if (limit->p_exclusive) {
		limit = plimit_copy(limit);
	} else {
		spin_lock_wr(&limit->p_spin);
		++limit->p_refcnt;
		spin_unlock_wr(&limit->p_spin);
	}
	return(limit);
}

/*
 * This routine is called when the second LWP is created for a process.
 * The process's limit structure must be made exclusive and a copy is
 * made if necessary.
 *
 * If the refcnt is 1 only the LWPs associated with the caller's process
 * have access to the structure, and since all we do is set the exclusive
 * but we don't need a spinlock.
 *
 * MPSAFE
 */
static
void
plimit_exclusive(struct plimit **limitp)
{
	struct plimit *olimit = *limitp;
	struct plimit *nlimit;

	if (olimit->p_refcnt == 1) {
		olimit->p_exclusive = 1;
	} else {
		nlimit = plimit_copy(olimit);
		nlimit->p_exclusive = 1;
		*limitp = nlimit;
		spin_lock_wr(&olimit->p_spin);
		if (--olimit->p_refcnt == 0) {
			spin_unlock_wr(&olimit->p_spin);
			kfree(olimit, M_SUBPROC);
		} else {
			spin_unlock_wr(&olimit->p_spin);
		}
	}
}

/*
 * Return a copy of the passed plimit.  The returned copy will have a refcnt
 * of 1 and p_exclusive will be cleared.
 *
 * MPSAFE
 */
static
struct plimit *
plimit_copy(struct plimit *olimit)
{
	struct plimit *nlimit;

	nlimit = kmalloc(sizeof(struct plimit), M_SUBPROC, M_WAITOK);

	spin_lock_rd(&olimit->p_spin);
	*nlimit = *olimit;
	spin_unlock_rd(&olimit->p_spin);

	spin_init(&nlimit->p_spin);
	nlimit->p_refcnt = 1;
	nlimit->p_exclusive = 0;
	return (nlimit);
}

/*
 * This routine is called to fixup a proces's p_limit structure prior
 * to it being modified.  If index >= 0 the specified modified is also
 * made.
 *
 * A limit structure is potentially shared only if the process has exactly
 * one LWP, otherwise it is guarenteed to be exclusive and no copy needs
 * to be made.  This means that we can safely replace *limitp in the copy
 * case.
 *
 * We call plimit_exclusive() to do all the hard work, but the result does
 * not actually have to be exclusive since the original was not, so just
 * clear p_exclusive afterwords.
 *
 * MPSAFE
 */
void
plimit_modify(struct plimit **limitp, int index, struct rlimit *rlim)
{
	struct plimit *limit = *limitp;

	if (limit->p_exclusive == 0 && limit->p_refcnt > 1) {
		plimit_exclusive(limitp);
		limit = *limitp;
		limit->p_exclusive = 0;
	}
	if (index >= 0) {
		spin_lock_wr(&limit->p_spin);
		limit->pl_rlimit[index] = *rlim;
		spin_unlock_wr(&limit->p_spin);
	}
}

/*
 * Destroy a process's plimit structure.
 *
 * MPSAFE
 */
void
plimit_free(struct plimit **limitp)
{
	struct plimit *limit;

	if ((limit = *limitp) != NULL) {
		*limitp = NULL;

		spin_lock_wr(&limit->p_spin);
		if (--limit->p_refcnt == 0) {
			spin_unlock_wr(&limit->p_spin);
			kfree(limit, M_SUBPROC);
		} else {
			spin_unlock_wr(&limit->p_spin);
		}
	}
}

/*
 * Modify a resource limit (from system call)
 *
 * MPSAFE
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
	plimit_modify(&p->p_limit, -1, NULL);
	limit = p->p_limit;
        alimp = &limit->pl_rlimit[which];

        /*
         * Preserve historical bugs by treating negative limits as unsigned.
         */
        if (limp->rlim_cur < 0)
                limp->rlim_cur = RLIM_INFINITY;
        if (limp->rlim_max < 0)
                limp->rlim_max = RLIM_INFINITY;

	spin_lock_rd(&limit->p_spin);
        if (limp->rlim_cur > alimp->rlim_max ||
            limp->rlim_max > alimp->rlim_max) {
		spin_unlock_rd(&limit->p_spin);
                if ((error = suser_cred(p->p_ucred, PRISON_ROOT)))
                        return (error);
	} else {
		spin_unlock_rd(&limit->p_spin);
	}
        if (limp->rlim_cur > limp->rlim_max)
                limp->rlim_cur = limp->rlim_max;

        switch (which) {
        case RLIMIT_CPU:
		spin_lock_wr(&limit->p_spin);
                if (limp->rlim_cur > RLIM_INFINITY / (rlim_t)1000000)
                        limit->p_cpulimit = RLIM_INFINITY;
                else
                        limit->p_cpulimit = (rlim_t)1000000 * limp->rlim_cur;
		spin_unlock_wr(&limit->p_spin);
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
		spin_lock_rd(&limit->p_spin);
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
			spin_unlock_rd(&limit->p_spin);
                        addr = trunc_page(addr);
                        size = round_page(size);
                        (void) vm_map_protect(&p->p_vmspace->vm_map,
                                              addr, addr+size, prot, FALSE);
                } else {
			spin_unlock_rd(&limit->p_spin);
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
	spin_lock_wr(&limit->p_spin);
        *alimp = *limp;
	spin_unlock_wr(&limit->p_spin);
        return (0);
}

/*
 * The rlimit indexed by which is returned in the second argument.
 *
 * MPSAFE
 */
int
kern_getrlimit(u_int which, struct rlimit *limp)
{
	struct proc *p = curproc;
	struct plimit *limit;

        if (which >= RLIM_NLIMITS)
                return (EINVAL);

	limit = p->p_limit;
	spin_lock_rd(&limit->p_spin);
        *limp = p->p_rlimit[which];
	spin_unlock_rd(&limit->p_spin);
        return (0);
}

/*
 * Determine if the cpu limit has been reached and return an operations
 * code for the caller to perform.
 *
 * MPSAFE
 */
int
plimit_testcpulimit(struct plimit *limit, u_int64_t ttime)
{
	struct rlimit *rlim;
	int mode;

	mode = PLIMIT_TESTCPU_OK;
	if (limit->p_cpulimit != RLIM_INFINITY) {
		spin_lock_rd(&limit->p_spin);
		if (ttime > limit->p_cpulimit) {
			rlim = &limit->pl_rlimit[RLIMIT_CPU];
			if (ttime / (rlim_t)1000000 >= rlim->rlim_max + 5) {
				mode = PLIMIT_TESTCPU_KILL;
			} else {
				mode = PLIMIT_TESTCPU_XCPU;
			}
		}
		spin_unlock_rd(&limit->p_spin);
	}
	return(mode);
}

