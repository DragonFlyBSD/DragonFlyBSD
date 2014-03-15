/*-
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software developed by the Computer Systems
 * Engineering group at Lawrence Berkeley Laboratory under DARPA contract
 * BG 91-66 and contributed to Berkeley.
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
 * $FreeBSD: src/lib/libkvm/kvm_proc.c,v 1.25.2.3 2002/08/24 07:27:46 kris Exp $
 *
 * @(#)kvm_proc.c	8.3 (Berkeley) 9/23/93
 */

/*
 * Proc traversal interface for kvm.  ps and w are (probably) the exclusive
 * users of this code, so we've factored it out into a separate module.
 * Thus, we keep this grunge out of the other kvm applications (i.e.,
 * most other applications are interested only in open/close/read/nlist).
 */

#include <sys/user.h>	/* MUST BE FIRST */
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <sys/stat.h>
#include <sys/globaldata.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/jail.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <nlist.h>
#include <kvm.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/swap_pager.h>

#include <sys/sysctl.h>

#include <limits.h>
#include <memory.h>
#include <paths.h>

#include "kvm_private.h"

#if used
static char *
kvm_readswap(kvm_t *kd, const struct proc *p, u_long va, u_long *cnt)
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
	/* XXX Stubbed out, our vm system is differnet */
	_kvm_err(kd, kd->program, "kvm_readswap not implemented");
	return(0);
#endif
}
#endif

#define KREAD(kd, addr, obj) \
	(kvm_read(kd, addr, (char *)(obj), sizeof(*obj)) != sizeof(*obj))
#define KREADSTR(kd, addr) \
	kvm_readstr(kd, (u_long)addr, NULL, NULL)

static struct kinfo_proc *
kinfo_resize_proc(kvm_t *kd, struct kinfo_proc *bp)
{
	if (bp < kd->procend)
		return bp;

	size_t pos = bp - kd->procend;
	size_t size = kd->procend - kd->procbase;

	if (size == 0)
		size = 8;
	else
		size *= 2;
	kd->procbase = _kvm_realloc(kd, kd->procbase, sizeof(*bp) * size);
	if (kd->procbase == NULL)
		return NULL;
	kd->procend = kd->procbase + size;
	bp = kd->procbase + pos;
	return bp;
}

/*
 * note: this function is also used by /usr/src/sys/kern/kern_kinfo.c as
 * compiled by userland.
 */
dev_t
dev2udev(cdev_t dev)
{
	if (dev == NULL)
		return NOUDEV;
	if ((dev->si_umajor & 0xffffff00) ||
	    (dev->si_uminor & 0x0000ff00)) {
		return NOUDEV;
	}
	return((dev->si_umajor << 8) | dev->si_uminor);
}

/*
 * Helper routine which traverses the left hand side of a red-black sub-tree.
 */
static uintptr_t
kvm_lwptraverse(kvm_t *kd, struct lwp *lwp, uintptr_t lwppos)
{
	for (;;) {
		if (KREAD(kd, lwppos, lwp)) {
			_kvm_err(kd, kd->program, "can't read lwp at %p",
				 (void *)lwppos);
			return ((uintptr_t)-1);
		}
		if (lwp->u.lwp_rbnode.rbe_left == NULL)
			break;
		lwppos = (uintptr_t)lwp->u.lwp_rbnode.rbe_left;
	}
	return(lwppos);
}

/*
 * Iterate LWPs in a process.
 *
 * The first lwp in a red-black tree is a left-side traversal of the tree.
 */
static uintptr_t
kvm_firstlwp(kvm_t *kd, struct lwp *lwp, struct proc *proc)
{
	return(kvm_lwptraverse(kd, lwp, (uintptr_t)proc->p_lwp_tree.rbh_root));
}

/*
 * If the current element is the left side of the parent the next element 
 * will be a left side traversal of the parent's right side.  If the parent
 * has no right side the next element will be the parent.
 *
 * If the current element is the right side of the parent the next element
 * is the parent.
 *
 * If the parent is NULL we are done.
 */
static uintptr_t
kvm_nextlwp(kvm_t *kd, uintptr_t lwppos, struct lwp *lwp, struct proc *proc)
{
	uintptr_t nextpos;

	nextpos = (uintptr_t)lwp->u.lwp_rbnode.rbe_parent;
	if (nextpos) {
		if (KREAD(kd, nextpos, lwp)) {
			_kvm_err(kd, kd->program, "can't read lwp at %p",
				 (void *)lwppos);
			return ((uintptr_t)-1);
		}
		if (lwppos == (uintptr_t)lwp->u.lwp_rbnode.rbe_left) {
			/*
			 * If we had gone down the left side the next element
			 * is a left hand traversal of the parent's right
			 * side, or the parent itself if there is no right
			 * side.
			 */
			lwppos = (uintptr_t)lwp->u.lwp_rbnode.rbe_right;
			if (lwppos)
				nextpos = kvm_lwptraverse(kd, lwp, lwppos);
		} else {
			/*
			 * If we had gone down the right side the next
			 * element is the parent.
			 */
			/* nextpos = nextpos */
		}
	}
	return(nextpos);
}

/*
 * Read proc's from memory file into buffer bp, which has space to hold
 * at most maxcnt procs.
 */
static int
kvm_proclist(kvm_t *kd, int what, int arg, struct proc *p,
	     struct kinfo_proc *bp)
{
	struct pgrp pgrp;
	struct pgrp tpgrp;
	struct globaldata gdata;
	struct session sess;
	struct session tsess;
	struct tty tty;
	struct proc proc;
	struct ucred ucred;
	struct thread thread;
	struct proc pproc;
	struct cdev cdev;
	struct vmspace vmspace;
	struct prison prison;
	struct sigacts sigacts;
	struct lwp lwp;
	uintptr_t lwppos;
	int count;
	char *wmesg;

	count = 0;

	for (; p != NULL; p = proc.p_list.le_next) {
		if (KREAD(kd, (u_long)p, &proc)) {
			_kvm_err(kd, kd->program, "can't read proc at %p", p);
			return (-1);
		}
		if (KREAD(kd, (u_long)proc.p_ucred, &ucred)) {
			_kvm_err(kd, kd->program, "can't read ucred at %p",
				 proc.p_ucred);
			return (-1);
		}
		proc.p_ucred = &ucred;

		switch(what & ~KERN_PROC_FLAGMASK) {

		case KERN_PROC_PID:
			if (proc.p_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_UID:
			if (ucred.cr_uid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_RUID:
			if (ucred.cr_ruid != (uid_t)arg)
				continue;
			break;
		}

		if (KREAD(kd, (u_long)proc.p_pgrp, &pgrp)) {
			_kvm_err(kd, kd->program, "can't read pgrp at %p",
				 proc.p_pgrp);
			return (-1);
		}
		proc.p_pgrp = &pgrp;
		if (proc.p_pptr) {
		  if (KREAD(kd, (u_long)proc.p_pptr, &pproc)) {
			_kvm_err(kd, kd->program, "can't read pproc at %p",
				 proc.p_pptr);
			return (-1);
		  }
		  proc.p_pptr = &pproc;
		}

		if (proc.p_sigacts) {
			if (KREAD(kd, (u_long)proc.p_sigacts, &sigacts)) {
				_kvm_err(kd, kd->program,
					 "can't read sigacts at %p",
					 proc.p_sigacts);
				return (-1);
			}
			proc.p_sigacts = &sigacts;
		}

		if (KREAD(kd, (u_long)pgrp.pg_session, &sess)) {
			_kvm_err(kd, kd->program, "can't read session at %p",
				pgrp.pg_session);
			return (-1);
		}
		pgrp.pg_session = &sess;

		if ((proc.p_flags & P_CONTROLT) && sess.s_ttyp != NULL) {
			if (KREAD(kd, (u_long)sess.s_ttyp, &tty)) {
				_kvm_err(kd, kd->program,
					 "can't read tty at %p", sess.s_ttyp);
				return (-1);
			}
			sess.s_ttyp = &tty;
			if (tty.t_dev != NULL) {
				if (KREAD(kd, (u_long)tty.t_dev, &cdev))
					tty.t_dev = NULL;
				else
					tty.t_dev = &cdev;
			}
			if (tty.t_pgrp != NULL) {
				if (KREAD(kd, (u_long)tty.t_pgrp, &tpgrp)) {
					_kvm_err(kd, kd->program,
						 "can't read tpgrp at %p",
						tty.t_pgrp);
					return (-1);
				}
				tty.t_pgrp = &tpgrp;
			}
			if (tty.t_session != NULL) {
				if (KREAD(kd, (u_long)tty.t_session, &tsess)) {
					_kvm_err(kd, kd->program,
						 "can't read tsess at %p",
						tty.t_session);
					return (-1);
				}
				tty.t_session = &tsess;
			}
		}

		if (KREAD(kd, (u_long)proc.p_vmspace, &vmspace)) {
			_kvm_err(kd, kd->program, "can't read vmspace at %p",
				 proc.p_vmspace);
			return (-1);
		}
		proc.p_vmspace = &vmspace;

		if (ucred.cr_prison != NULL) {
			if (KREAD(kd, (u_long)ucred.cr_prison, &prison)) {
				_kvm_err(kd, kd->program, "can't read prison at %p",
					 ucred.cr_prison);
				return (-1);
			}
			ucred.cr_prison = &prison;
		}

		switch (what & ~KERN_PROC_FLAGMASK) {

		case KERN_PROC_PGRP:
			if (proc.p_pgrp->pg_id != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_TTY:
			if ((proc.p_flags & P_CONTROLT) == 0 ||
			    dev2udev(proc.p_pgrp->pg_session->s_ttyp->t_dev)
					!= (dev_t)arg)
				continue;
			break;
		}

		if ((bp = kinfo_resize_proc(kd, bp)) == NULL)
			return (-1);
		fill_kinfo_proc(&proc, bp);
		bp->kp_paddr = (uintptr_t)p;

		lwppos = kvm_firstlwp(kd, &lwp, &proc);
		if (lwppos == 0) {
			bp++;		/* Just export the proc then */
			count++;
		}
		while (lwppos && lwppos != (uintptr_t)-1) {
			if (p != lwp.lwp_proc) {
				_kvm_err(kd, kd->program, "lwp has wrong parent");
				return (-1);
			}
			lwp.lwp_proc = &proc;
			if (KREAD(kd, (u_long)lwp.lwp_thread, &thread)) {
				_kvm_err(kd, kd->program, "can't read thread at %p",
				    lwp.lwp_thread);
				return (-1);
			}
			lwp.lwp_thread = &thread;

			if (thread.td_gd) {
				if (KREAD(kd, (u_long)thread.td_gd, &gdata)) {
					_kvm_err(kd, kd->program, "can't read"
						  " gd at %p",
						  thread.td_gd);
					return(-1);
				}
				thread.td_gd = &gdata;
			}
			if (thread.td_wmesg) {
				wmesg = (void *)KREADSTR(kd, thread.td_wmesg);
				if (wmesg == NULL) {
					_kvm_err(kd, kd->program, "can't read"
						  " wmesg %p",
						  thread.td_wmesg);
					return(-1);
				}
				thread.td_wmesg = wmesg;
			} else {
				wmesg = NULL;
			}

			if ((bp = kinfo_resize_proc(kd, bp)) == NULL)
				return (-1);
			fill_kinfo_proc(&proc, bp);
			fill_kinfo_lwp(&lwp, &bp->kp_lwp);
			bp->kp_paddr = (uintptr_t)p;
			bp++;
			count++;
			if (wmesg)
				free(wmesg);
			if ((what & KERN_PROC_FLAG_LWP) == 0)
				break;
			lwppos = kvm_nextlwp(kd, lwppos, &lwp, &proc);
		}
		if (lwppos == (uintptr_t)-1)
			return(-1);
	}
	return (count);
}

/*
 * Build proc info array by reading in proc list from a crash dump.
 * We reallocate kd->procbase as necessary.
 */
static int
kvm_deadprocs(kvm_t *kd, int what, int arg, u_long a_allproc,
	      int allproc_hsize)
{
	struct kinfo_proc *bp;
	struct proc *p;
	struct proclist **pl;
	int cnt, partcnt, n;
	u_long nextoff;

	cnt = partcnt = 0;
	nextoff = 0;

	/*
	 * Dynamically allocate space for all the elements of the
	 * allprocs array and KREAD() them.
	 */
	pl = _kvm_malloc(kd, allproc_hsize * sizeof(struct proclist *));
	for (n = 0; n < allproc_hsize; n++) {
		pl[n] = _kvm_malloc(kd, sizeof(struct proclist));
		nextoff = a_allproc + (n * sizeof(struct proclist));
		if (KREAD(kd, (u_long)nextoff, pl[n])) {
			_kvm_err(kd, kd->program, "can't read proclist at 0x%lx",
				a_allproc);
			return (-1);
		}

		/* Ignore empty proclists */
		if (LIST_EMPTY(pl[n]))
			continue;

		bp = kd->procbase + cnt;
		p = pl[n]->lh_first;
		partcnt = kvm_proclist(kd, what, arg, p, bp);
		if (partcnt < 0) {
			free(pl[n]);
			return (partcnt);
		}

		cnt += partcnt;
		free(pl[n]);
	}

	return (cnt);
}

struct kinfo_proc *
kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt)
{
	int mib[4], st, nprocs, allproc_hsize;
	int miblen = ((op & ~KERN_PROC_FLAGMASK) == KERN_PROC_ALL) ? 3 : 4;
	size_t size;

	if (kd->procbase != 0) {
		free((void *)kd->procbase);
		/*
		 * Clear this pointer in case this call fails.  Otherwise,
		 * kvm_close() will free it again.
		 */
		kd->procbase = 0;
	}
	if (kvm_ishost(kd)) {
		size = 0;
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = op;
		mib[3] = arg;
		st = sysctl(mib, miblen, NULL, &size, NULL, 0);
		if (st == -1) {
			_kvm_syserr(kd, kd->program, "kvm_getprocs");
			return (0);
		}
		do {
			size += size / 10;
			kd->procbase = (struct kinfo_proc *)
			    _kvm_realloc(kd, kd->procbase, size);
			if (kd->procbase == 0)
				return (0);
			st = sysctl(mib, miblen, kd->procbase, &size, NULL, 0);
		} while (st == -1 && errno == ENOMEM);
		if (st == -1) {
			_kvm_syserr(kd, kd->program, "kvm_getprocs");
			return (0);
		}
		if (size % sizeof(struct kinfo_proc) != 0) {
			_kvm_err(kd, kd->program,
				"proc size mismatch (%zd total, %zd chunks)",
				size, sizeof(struct kinfo_proc));
			return (0);
		}
		nprocs = size / sizeof(struct kinfo_proc);
	} else {
		struct nlist nl[4], *p;

		nl[0].n_name = "_nprocs";
		nl[1].n_name = "_allprocs";
		nl[2].n_name = "_allproc_hsize";
		nl[3].n_name = 0;

		if (kvm_nlist(kd, nl) != 0) {
			for (p = nl; p->n_type != 0; ++p)
				;
			_kvm_err(kd, kd->program,
				 "%s: no such symbol", p->n_name);
			return (0);
		}
		if (KREAD(kd, nl[0].n_value, &nprocs)) {
			_kvm_err(kd, kd->program, "can't read nprocs");
			return (0);
		}
		if (KREAD(kd, nl[2].n_value, &allproc_hsize)) {
			_kvm_err(kd, kd->program, "can't read allproc_hsize");
			return (0);
		}
		nprocs = kvm_deadprocs(kd, op, arg, nl[1].n_value,
				      allproc_hsize);
#ifdef notdef
		size = nprocs * sizeof(struct kinfo_proc);
		(void)realloc(kd->procbase, size);
#endif
	}
	*cnt = nprocs;
	return (kd->procbase);
}

void
_kvm_freeprocs(kvm_t *kd)
{
	if (kd->procbase) {
		free(kd->procbase);
		kd->procbase = 0;
	}
}

void *
_kvm_realloc(kvm_t *kd, void *p, size_t n)
{
	void *np = (void *)realloc(p, n);

	if (np == NULL) {
		free(p);
		_kvm_err(kd, kd->program, "out of memory");
	}
	return (np);
}

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/*
 * Read in an argument vector from the user address space of process pid.
 * addr if the user-space base address of narg null-terminated contiguous
 * strings.  This is used to read in both the command arguments and
 * environment strings.  Read at most maxcnt characters of strings.
 */
static char **
kvm_argv(kvm_t *kd, pid_t pid, u_long addr, int narg, int maxcnt)
{
	char *np, *cp, *ep, *ap;
	u_long oaddr = -1;
	int len, cc;
	char **argv;

	/*
	 * Check that there aren't an unreasonable number of agruments,
	 * and that the address is in user space.
	 */
	if (narg > 512 || 
	    addr < VM_MIN_USER_ADDRESS || addr >= VM_MAX_USER_ADDRESS) {
		return (0);
	}

	/*
	 * kd->argv : work space for fetching the strings from the target 
	 *            process's space, and is converted for returning to caller
	 */
	if (kd->argv == 0) {
		/*
		 * Try to avoid reallocs.
		 */
		kd->argc = MAX(narg + 1, 32);
		kd->argv = (char **)_kvm_malloc(kd, kd->argc *
						sizeof(*kd->argv));
		if (kd->argv == 0)
			return (0);
	} else if (narg + 1 > kd->argc) {
		kd->argc = MAX(2 * kd->argc, narg + 1);
		kd->argv = (char **)_kvm_realloc(kd, kd->argv, kd->argc *
						sizeof(*kd->argv));
		if (kd->argv == 0)
			return (0);
	}
	/*
	 * kd->argspc : returned to user, this is where the kd->argv
	 *              arrays are left pointing to the collected strings.
	 */
	if (kd->argspc == 0) {
		kd->argspc = (char *)_kvm_malloc(kd, PAGE_SIZE);
		if (kd->argspc == 0)
			return (0);
		kd->arglen = PAGE_SIZE;
	}
	/*
	 * kd->argbuf : used to pull in pages from the target process.
	 *              the strings are copied out of here.
	 */
	if (kd->argbuf == 0) {
		kd->argbuf = (char *)_kvm_malloc(kd, PAGE_SIZE);
		if (kd->argbuf == 0)
			return (0);
	}

	/* Pull in the target process'es argv vector */
	cc = sizeof(char *) * narg;
	if (kvm_uread(kd, pid, addr, (char *)kd->argv, cc) != cc)
		return (0);
	/*
	 * ap : saved start address of string we're working on in kd->argspc
	 * np : pointer to next place to write in kd->argspc
	 * len: length of data in kd->argspc
	 * argv: pointer to the argv vector that we are hunting around the
	 *       target process space for, and converting to addresses in
	 *       our address space (kd->argspc).
	 */
	ap = np = kd->argspc;
	argv = kd->argv;
	len = 0;
	/*
	 * Loop over pages, filling in the argument vector.
	 * Note that the argv strings could be pointing *anywhere* in
	 * the user address space and are no longer contiguous.
	 * Note that *argv is modified when we are going to fetch a string
	 * that crosses a page boundary.  We copy the next part of the string
	 * into to "np" and eventually convert the pointer.
	 */
	while (argv < kd->argv + narg && *argv != NULL) {

		/* get the address that the current argv string is on */
		addr = (u_long)*argv & ~(PAGE_SIZE - 1);

		/* is it the same page as the last one? */
		if (addr != oaddr) {
			if (kvm_uread(kd, pid, addr, kd->argbuf, PAGE_SIZE) !=
			    PAGE_SIZE)
				return (0);
			oaddr = addr;
		}

		/* offset within the page... kd->argbuf */
		addr = (u_long)*argv & (PAGE_SIZE - 1);

		/* cp = start of string, cc = count of chars in this chunk */
		cp = kd->argbuf + addr;
		cc = PAGE_SIZE - addr;

		/* dont get more than asked for by user process */
		if (maxcnt > 0 && cc > maxcnt - len)
			cc = maxcnt - len;

		/* pointer to end of string if we found it in this page */
		ep = memchr(cp, '\0', cc);
		if (ep != NULL)
			cc = ep - cp + 1;
		/*
		 * at this point, cc is the count of the chars that we are
		 * going to retrieve this time. we may or may not have found
		 * the end of it.  (ep points to the null if the end is known)
		 */

		/* will we exceed the malloc/realloced buffer? */
		if (len + cc > kd->arglen) {
			size_t off;
			char **pp;
			char *op = kd->argspc;

			kd->arglen *= 2;
			kd->argspc = (char *)_kvm_realloc(kd, kd->argspc,
							  kd->arglen);
			if (kd->argspc == 0)
				return (0);
			/*
			 * Adjust argv pointers in case realloc moved
			 * the string space.
			 */
			off = kd->argspc - op;
			for (pp = kd->argv; pp < argv; pp++)
				*pp += off;
			ap += off;
			np += off;
		}
		/* np = where to put the next part of the string in kd->argspc*/
		/* np is kinda redundant.. could use "kd->argspc + len" */
		memcpy(np, cp, cc);
		np += cc;	/* inc counters */
		len += cc;

		/*
		 * if end of string found, set the *argv pointer to the
		 * saved beginning of string, and advance. argv points to
		 * somewhere in kd->argv..  This is initially relative
		 * to the target process, but when we close it off, we set
		 * it to point in our address space.
		 */
		if (ep != NULL) {
			*argv++ = ap;
			ap = np;
		} else {
			/* update the address relative to the target process */
			*argv += cc;
		}

		if (maxcnt > 0 && len >= maxcnt) {
			/*
			 * We're stopping prematurely.  Terminate the
			 * current string.
			 */
			if (ep == NULL) {
				*np = '\0';
				*argv++ = ap;
			}
			break;
		}
	}
	/* Make sure argv is terminated. */
	*argv = NULL;
	return (kd->argv);
}

static void
ps_str_a(struct ps_strings *p, u_long *addr, int *n)
{
	*addr = (u_long)p->ps_argvstr;
	*n = p->ps_nargvstr;
}

static void
ps_str_e(struct ps_strings *p, u_long *addr, int *n)
{
	*addr = (u_long)p->ps_envstr;
	*n = p->ps_nenvstr;
}

/*
 * Determine if the proc indicated by p is still active.
 * This test is not 100% foolproof in theory, but chances of
 * being wrong are very low.
 */
static int
proc_verify(kvm_t *kd, const struct kinfo_proc *p)
{
	struct kinfo_proc kp;
	int mib[4];
	size_t len;
	int error;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;
	mib[3] = p->kp_pid;

	len = sizeof(kp);
	error = sysctl(mib, 4, &kp, &len, NULL, 0);
	if (error)
		return (0);

	error = (p->kp_pid == kp.kp_pid &&
	    (kp.kp_stat != SZOMB || p->kp_stat == SZOMB));
	return (error);
}

static char **
kvm_doargv(kvm_t *kd, const struct kinfo_proc *kp, int nchr,
	   void (*info)(struct ps_strings *, u_long *, int *))
{
	char **ap;
	u_long addr;
	int cnt;
	static struct ps_strings arginfo;
	static u_long ps_strings;
	size_t len;

	if (ps_strings == 0) {
		len = sizeof(ps_strings);
		if (sysctlbyname("kern.ps_strings", &ps_strings, &len, NULL,
		    0) == -1)
			ps_strings = PS_STRINGS;
	}

	/*
	 * Pointers are stored at the top of the user stack.
	 */
	if (kp->kp_stat == SZOMB ||
	    kvm_uread(kd, kp->kp_pid, ps_strings, (char *)&arginfo,
		      sizeof(arginfo)) != sizeof(arginfo))
		return (0);

	(*info)(&arginfo, &addr, &cnt);
	if (cnt == 0)
		return (0);
	ap = kvm_argv(kd, kp->kp_pid, addr, cnt, nchr);
	/*
	 * For live kernels, make sure this process didn't go away.
	 */
	if (ap != NULL && (kvm_ishost(kd) || kvm_isvkernel(kd)) &&
	    !proc_verify(kd, kp))
		ap = NULL;
	return (ap);
}

/*
 * Get the command args.  This code is now machine independent.
 */
char **
kvm_getargv(kvm_t *kd, const struct kinfo_proc *kp, int nchr)
{
	int oid[4];
	int i;
	size_t bufsz;
	static unsigned long buflen;
	static char *buf, *p;
	static char **bufp;
	static int argc;

	if (!kvm_ishost(kd)) { /* XXX: vkernels */
		_kvm_err(kd, kd->program,
		    "cannot read user space from dead kernel");
		return (0);
	}

	if (!buflen) {
		bufsz = sizeof(buflen);
		i = sysctlbyname("kern.ps_arg_cache_limit", 
		    &buflen, &bufsz, NULL, 0);
		if (i == -1) {
			buflen = 0;
		} else {
			buf = malloc(buflen);
			if (buf == NULL)
				buflen = 0;
			argc = 32;
			bufp = malloc(sizeof(char *) * argc);
		}
	}
	if (buf != NULL) {
		oid[0] = CTL_KERN;
		oid[1] = KERN_PROC;
		oid[2] = KERN_PROC_ARGS;
		oid[3] = kp->kp_pid;
		bufsz = buflen;
		i = sysctl(oid, 4, buf, &bufsz, 0, 0);
		if (i == 0 && bufsz > 0) {
			i = 0;
			p = buf;
			do {
				bufp[i++] = p;
				p += strlen(p) + 1;
				if (i >= argc) {
					argc += argc;
					bufp = realloc(bufp,
					    sizeof(char *) * argc);
				}
			} while (p < buf + bufsz);
			bufp[i++] = NULL;
			return (bufp);
		}
	}
	if (kp->kp_flags & P_SYSTEM)
		return (NULL);
	return (kvm_doargv(kd, kp, nchr, ps_str_a));
}

char **
kvm_getenvv(kvm_t *kd, const struct kinfo_proc *kp, int nchr)
{
	return (kvm_doargv(kd, kp, nchr, ps_str_e));
}

/*
 * Read from user space.  The user context is given by pid.
 */
ssize_t
kvm_uread(kvm_t *kd, pid_t pid, u_long uva, char *buf, size_t len)
{
	char *cp;
	char procfile[MAXPATHLEN];
	ssize_t amount;
	int fd;

	if (!kvm_ishost(kd)) { /* XXX: vkernels */
		_kvm_err(kd, kd->program,
		    "cannot read user space from dead kernel");
		return (0);
	}

	sprintf(procfile, "/proc/%d/mem", pid);
	fd = open(procfile, O_RDONLY, 0);
	if (fd < 0) {
		_kvm_err(kd, kd->program, "cannot open %s", procfile);
		close(fd);
		return (0);
	}

	cp = buf;
	while (len > 0) {
		errno = 0;
		if (lseek(fd, (off_t)uva, 0) == -1 && errno != 0) {
			_kvm_err(kd, kd->program, "invalid address (%lx) in %s",
			    uva, procfile);
			break;
		}
		amount = read(fd, cp, len);
		if (amount < 0) {
			_kvm_syserr(kd, kd->program, "error reading %s",
			    procfile);
			break;
		}
		if (amount == 0) {
			_kvm_err(kd, kd->program, "EOF reading %s", procfile);
			break;
		}
		cp += amount;
		uva += amount;
		len -= amount;
	}

	close(fd);
	return ((ssize_t)(cp - buf));
}
