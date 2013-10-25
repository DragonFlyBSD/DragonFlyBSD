/*-
 * Copyright (c) 2000 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * $FreeBSD: src/sys/i386/linux/linux_machdep.c,v 1.6.2.4 2001/11/05 19:08:23 marcel Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/nlookup.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/ptrace.h>
#include <sys/sysproto.h>
#include <sys/thread2.h>
#include <sys/unistd.h>
#include <sys/wait.h>

#include <machine/frame.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/sysarch.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/mplock2.h>

#include "linux.h"
#include "linux_proto.h"
#include "../linux_ipc.h"
#include "../linux_signal.h"
#include "../linux_util.h"
#include "../linux_emuldata.h"

struct l_descriptor {
	l_uint		entry_number;
	l_ulong		base_addr;
	l_uint		limit;
	l_uint		seg_32bit:1;
	l_uint		contents:2;
	l_uint		read_exec_only:1;
	l_uint		limit_in_pages:1;
	l_uint		seg_not_present:1;
	l_uint		useable:1;
};

struct l_old_select_argv {
	l_int		nfds;
	l_fd_set	*readfds;
	l_fd_set	*writefds;
	l_fd_set	*exceptfds;
	struct l_timeval	*timeout;
};

int
linux_to_bsd_sigaltstack(int lsa)
{
	int bsa = 0;

	if (lsa & LINUX_SS_DISABLE)
		bsa |= SS_DISABLE;
	if (lsa & LINUX_SS_ONSTACK)
		bsa |= SS_ONSTACK;
	return (bsa);
}

int
bsd_to_linux_sigaltstack(int bsa)
{
	int lsa = 0;

	if (bsa & SS_DISABLE)
		lsa |= LINUX_SS_DISABLE;
	if (bsa & SS_ONSTACK)
		lsa |= LINUX_SS_ONSTACK;
	return (lsa);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_execve(struct linux_execve_args *args)
{
	struct nlookupdata nd;
	struct image_args exec_args;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(execve))
		kprintf(ARGS(execve, "%s"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	bzero(&exec_args, sizeof(exec_args));
	if (error == 0) {
		error = exec_copyin_args(&exec_args, path, PATH_SYSSPACE,
					args->argp, args->envp);
	}
	if (error == 0)
		error = kern_execve(&nd, &exec_args);
	nlookup_done(&nd);

	/*
	 * The syscall result is returned in registers to the new program.
	 * Linux will register %edx as an atexit function and we must be
	 * sure to set it to 0.  XXX
	 */
	if (error == 0) {
		args->sysmsg_result64 = 0;
		if (curproc->p_sysent == &elf_linux_sysvec)
   		  	error = emuldata_init(curproc, NULL, 0);
	}

	exec_free_args(&exec_args);
	linux_free_path(&path);

	if (error < 0) {
		/* We hit a lethal error condition.  Let's die now. */
		exit1(W_EXITCODE(0, SIGABRT));
		/* NOTREACHED */
	}
	rel_mplock();

	return(error);
}

struct l_ipc_kludge {
	struct l_msgbuf *msgp;
	l_long msgtyp;
};

/*
 * MPALMOSTSAFE
 */
int
sys_linux_ipc(struct linux_ipc_args *args)
{
	int error = 0;

	get_mplock();

	switch (args->what & 0xFFFF) {
	case LINUX_SEMOP: {
		struct linux_semop_args a;

		a.semid = args->arg1;
		a.tsops = args->ptr;
		a.nsops = args->arg2;
		a.sysmsg_lresult = 0;
		error = linux_semop(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_SEMGET: {
		struct linux_semget_args a;

		a.key = args->arg1;
		a.nsems = args->arg2;
		a.semflg = args->arg3;
		a.sysmsg_lresult = 0;
		error = linux_semget(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_SEMCTL: {
		struct linux_semctl_args a;
		int error;

		a.semid = args->arg1;
		a.semnum = args->arg2;
		a.cmd = args->arg3;
		a.sysmsg_lresult = 0;
		error = copyin((caddr_t)args->ptr, &a.arg, sizeof(a.arg));
		if (error)
			break;
		error = linux_semctl(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_MSGSND: {
		struct linux_msgsnd_args a;

		a.msqid = args->arg1;
		a.msgp = args->ptr;
		a.msgsz = args->arg2;
		a.msgflg = args->arg3;
		a.sysmsg_lresult = 0;
		error = linux_msgsnd(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_MSGRCV: {
		struct linux_msgrcv_args a;

		a.msqid = args->arg1;
		a.msgsz = args->arg2;
		if (a.msgsz < 0) {
			error = EINVAL;
			break;
		}
		a.msgflg = args->arg3;
		a.sysmsg_lresult = 0;
		if ((args->what >> 16) == 0) {
			struct l_ipc_kludge tmp;
			int error;

			if (args->ptr == NULL) {
				error = EINVAL;
				break;
			}
			error = copyin((caddr_t)args->ptr, &tmp, sizeof(tmp));
			if (error)
				break;
			a.msgp = tmp.msgp;
			a.msgtyp = tmp.msgtyp;
		} else {
			a.msgp = args->ptr;
			a.msgtyp = args->arg5;
		}
		error = linux_msgrcv(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_MSGGET: {
		struct linux_msgget_args a;

		a.key = args->arg1;
		a.msgflg = args->arg2;
		a.sysmsg_lresult = 0;
		error = linux_msgget(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_MSGCTL: {
		struct linux_msgctl_args a;

		a.msqid = args->arg1;
		a.cmd = args->arg2;
		a.buf = args->ptr;
		a.sysmsg_lresult = 0;
		error = linux_msgctl(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_SHMAT: {
		struct linux_shmat_args a;

		a.shmid = args->arg1;
		a.shmaddr = args->ptr;
		a.shmflg = args->arg2;
		a.raddr = (l_ulong *)args->arg3;
		a.sysmsg_lresult = 0;
		error = linux_shmat(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_SHMDT: {
		struct linux_shmdt_args a;

		a.shmaddr = args->ptr;
		a.sysmsg_lresult = 0;
		error = linux_shmdt(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_SHMGET: {
		struct linux_shmget_args a;

		a.key = args->arg1;
		a.size = args->arg2;
		a.shmflg = args->arg3;
		a.sysmsg_lresult = 0;
		error = linux_shmget(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	case LINUX_SHMCTL: {
		struct linux_shmctl_args a;

		a.shmid = args->arg1;
		a.cmd = args->arg2;
		a.buf = args->ptr;
		a.sysmsg_lresult = 0;
		error = linux_shmctl(&a);
		args->sysmsg_lresult = a.sysmsg_lresult;
		break;
	}
	default:
		error = EINVAL;
		break;
	}
	rel_mplock();
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_old_select(struct linux_old_select_args *args)
{
	struct l_old_select_argv linux_args;
	struct linux_select_args newsel;
	int error;

#ifdef DEBUG
	if (ldebug(old_select))
		kprintf(ARGS(old_select, "%p"), args->ptr);
#endif

	error = copyin((caddr_t)args->ptr, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	newsel.sysmsg_iresult = 0;
	newsel.nfds = linux_args.nfds;
	newsel.readfds = linux_args.readfds;
	newsel.writefds = linux_args.writefds;
	newsel.exceptfds = linux_args.exceptfds;
	newsel.timeout = linux_args.timeout;
	error = sys_linux_select(&newsel);
	args->sysmsg_iresult = newsel.sysmsg_iresult;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_fork(struct linux_fork_args *args)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p2;
	int error;

	get_mplock();
	error = fork1(lp, RFFDG | RFPROC | RFPGLOCK, &p2);
	if (error == 0) {
		emuldata_init(curproc, p2, 0);

		start_forked_proc(lp, p2);
		args->sysmsg_fds[0] = p2->p_pid;
		args->sysmsg_fds[1] = 0;
	}
	rel_mplock();

	/* Are we the child? */
	if (args->sysmsg_iresult == 1)
		args->sysmsg_iresult = 0;

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_exit_group(struct linux_exit_group_args *args)
{
	struct linux_emuldata *em, *e;
	struct proc *p;
	int rval;

	rval = args->rval;
	EMUL_LOCK();

	em = emuldata_get(curproc);

	if (em->s->refs == 1) {
		EMUL_UNLOCK();
		exit1(W_EXITCODE(rval, 0));
		/* NOTREACHED */
		return (0);
	}
	KKASSERT(em->proc == curproc);
	em->flags |= EMUL_DIDKILL;
	em->s->flags |= LINUX_LES_INEXITGROUP;
	em->s->xstat = W_EXITCODE(rval, 0);

	LIST_REMOVE(em, threads);
	LIST_INSERT_HEAD(&em->s->threads, em, threads);

	while ((e = LIST_NEXT(em, threads)) != NULL) {
		LIST_REMOVE(em, threads);
		LIST_INSERT_AFTER(e, em, threads);
		if ((e->flags & EMUL_DIDKILL) == 0) {
			e->flags |= EMUL_DIDKILL;
			p = e->proc;
			PHOLD(p);
			ksignal(p, SIGKILL);
			PRELE(p);
		}
	}

	EMUL_UNLOCK();
	exit1(W_EXITCODE(rval, 0));
	/* NOTREACHED */

	return (0);
}

/*
 * MPSAFE
 */
int
sys_linux_vfork(struct linux_vfork_args *args)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p2;
	int error;

	get_mplock();
	error = fork1(lp, RFFDG | RFPROC | RFPPWAIT | RFMEM | RFPGLOCK, &p2);
	if (error == 0) {
		emuldata_init(curproc, p2, 0);

		start_forked_proc(lp, p2);
		args->sysmsg_fds[0] = p2->p_pid;
		args->sysmsg_fds[1] = 0;
	}
	rel_mplock();

	if (args->sysmsg_iresult == 1)
		args->sysmsg_iresult = 0;

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_clone(struct linux_clone_args *args)
{
	struct segment_descriptor *desc;
	struct l_user_desc info;
	int idx;
	int a[2];

	struct lwp *lp = curthread->td_lwp;
	int error, ff = RFPROC;
	struct proc *p2 = NULL;
	int exit_signal;

	exit_signal = args->flags & 0x000000ff;
	if (exit_signal >= LINUX_NSIG)
		return (EINVAL);
	if (exit_signal <= LINUX_SIGTBLSZ)
		exit_signal = linux_to_bsd_signal[_SIG_IDX(exit_signal)];

	if (args->flags & LINUX_CLONE_VM)
		ff |= RFMEM;
	if (args->flags & LINUX_CLONE_SIGHAND)
		ff |= RFSIGSHARE;
	if (!(args->flags & (LINUX_CLONE_FILES | LINUX_CLONE_FS)))
		ff |= RFFDG;
	if ((args->flags & 0xffffff00) == LINUX_THREADING_FLAGS)
		ff |= RFTHREAD;
	if (args->flags & LINUX_CLONE_VFORK)
		ff |= RFPPWAIT;
	if (args->flags & LINUX_CLONE_PARENT_SETTID) {
		if (args->parent_tidptr == NULL)
 			return (EINVAL);
	}

	error = 0;

	get_mplock();
	error = fork1(lp, ff | RFPGLOCK, &p2);
	if (error) {
		rel_mplock();
		return error;
	}

	args->sysmsg_fds[0] = p2 ? p2->p_pid : 0;
	args->sysmsg_fds[1] = 0;
	
	if (args->flags & (LINUX_CLONE_PARENT | LINUX_CLONE_THREAD)) {
		lwkt_gettoken(&curproc->p_token);
		while (p2->p_pptr != curproc->p_pptr)
			proc_reparent(p2, curproc->p_pptr);
		lwkt_reltoken(&curproc->p_token);
	}

	emuldata_init(curproc, p2, args->flags);
	linux_proc_fork(p2, curproc, args->child_tidptr);
	/*
	 * XXX: this can't happen, p2 is never NULL, or else we'd have
	 *	other problems, too (see p2->p_sigparent == ...,
	 *	linux_proc_fork and emuldata_init.
	 */
	if (p2 == NULL) {
		error = ESRCH;
	} else {
		if (args->flags & LINUX_CLONE_PARENT_SETTID) {
			error = copyout(&p2->p_pid, args->parent_tidptr, sizeof(p2->p_pid));
		}
	}

	p2->p_sigparent = exit_signal;
	if (args->stack) {
		ONLY_LWP_IN_PROC(p2)->lwp_md.md_regs->tf_esp =
					(unsigned long)args->stack;
	}

	if (args->flags & LINUX_CLONE_SETTLS) {
		error = copyin((void *)curthread->td_lwp->lwp_md.md_regs->tf_esi, &info, sizeof(struct l_user_desc));
		if (error) {
			kprintf("copyin of tf_esi to info failed\n");
		} else {
			idx = info.entry_number;
			/*
			 * We understand both our own entries such as the ones
			 * we provide on linux_set_thread_area, as well as the
			 * linux-type entries 6-8.
			 */
			if ((idx < 6 || idx > 8) && (idx < GTLS_START)) {
				kprintf("LINUX_CLONE_SETTLS, invalid idx requested: %d\n", idx);
				goto out;
			}
			if (idx < GTLS_START) {
				idx -= 6;
			} else {
#if 0 /* was SMP */
				idx -= (GTLS_START + mycpu->gd_cpuid * NGDT);
#endif
				idx -= GTLS_START;
			}
			KKASSERT(idx >= 0);

			a[0] = LINUX_LDT_entry_a(&info);
			a[1] = LINUX_LDT_entry_b(&info);
			if (p2) {
				desc = &FIRST_LWP_IN_PROC(p2)->lwp_thread->td_tls.tls[idx];
				memcpy(desc, &a, sizeof(a));
			} else {
				kprintf("linux_clone... we don't have a p2\n");
			}
		}
	}
out:
	if (p2)
		start_forked_proc(lp, p2);

	rel_mplock();
#ifdef DEBUG
	if (ldebug(clone))
		kprintf(LMSG("clone: successful rfork to %ld"),
		    (long)p2->p_pid);
#endif

	return (error);
}

/* XXX move */
struct l_mmap_argv {
	l_caddr_t	addr;
	l_int		len;
	l_int		prot;
	l_int		flags;
	l_int		fd;
	l_int		pos;
};

#define STACK_SIZE  (2 * 1024 * 1024)
#define GUARD_SIZE  (4 * PAGE_SIZE)

/*
 * MPALMOSTSAFE
 */
static int
linux_mmap_common(caddr_t linux_addr, size_t linux_len, int linux_prot,
		  int linux_flags, int linux_fd, off_t pos, void **res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	caddr_t addr;
	void *new;
	int error, flags, len, prot, fd;

	flags = 0;
	if (linux_flags & LINUX_MAP_SHARED)
		flags |= MAP_SHARED;
	if (linux_flags & LINUX_MAP_PRIVATE)
		flags |= MAP_PRIVATE;
	if (linux_flags & LINUX_MAP_FIXED)
		flags |= MAP_FIXED;
	if (linux_flags & LINUX_MAP_ANON) {
		flags |= MAP_ANON;
	} else {
		flags |= MAP_NOSYNC;
	}

	lwkt_gettoken(&curproc->p_vmspace->vm_map.token);

	if (linux_flags & LINUX_MAP_GROWSDOWN) {
		flags |= MAP_STACK;
		/* The linux MAP_GROWSDOWN option does not limit auto
		 * growth of the region.  Linux mmap with this option
		 * takes as addr the inital BOS, and as len, the initial
		 * region size.  It can then grow down from addr without
		 * limit.  However, linux threads has an implicit internal
		 * limit to stack size of STACK_SIZE.  Its just not
		 * enforced explicitly in linux.  But, here we impose
		 * a limit of (STACK_SIZE - GUARD_SIZE) on the stack
		 * region, since we can do this with our mmap.
		 *
		 * Our mmap with MAP_STACK takes addr as the maximum
		 * downsize limit on BOS, and as len the max size of
		 * the region.  It them maps the top SGROWSIZ bytes,
		 * and autgrows the region down, up to the limit
		 * in addr.
		 *
		 * If we don't use the MAP_STACK option, the effect
		 * of this code is to allocate a stack region of a
		 * fixed size of (STACK_SIZE - GUARD_SIZE).
		 */

		/* This gives us TOS */
		addr = linux_addr + linux_len;

		if (addr > p->p_vmspace->vm_maxsaddr) {
			/* Some linux apps will attempt to mmap
			 * thread stacks near the top of their
			 * address space.  If their TOS is greater
			 * than vm_maxsaddr, vm_map_growstack()
			 * will confuse the thread stack with the
			 * process stack and deliver a SEGV if they
			 * attempt to grow the thread stack past their
			 * current stacksize rlimit.  To avoid this,
			 * adjust vm_maxsaddr upwards to reflect
			 * the current stacksize rlimit rather
			 * than the maximum possible stacksize.
			 * It would be better to adjust the
			 * mmap'ed region, but some apps do not check
			 * mmap's return value.
			 */
			p->p_vmspace->vm_maxsaddr = (char *)USRSTACK -
			    p->p_rlimit[RLIMIT_STACK].rlim_cur;
		}

		/* This gives us our maximum stack size */
		if (linux_len > STACK_SIZE - GUARD_SIZE) {
			len = linux_len;
		} else {
			len = STACK_SIZE - GUARD_SIZE;
		}
		/* This gives us a new BOS.  If we're using VM_STACK, then
		 * mmap will just map the top SGROWSIZ bytes, and let
		 * the stack grow down to the limit at BOS.  If we're
		 * not using VM_STACK we map the full stack, since we
		 * don't have a way to autogrow it.
		 */
		addr -= len;
	} else {
		addr = linux_addr;
		len = linux_len;
	}

	prot = linux_prot;

	if (prot & (PROT_READ | PROT_WRITE | PROT_EXEC))
		prot |= PROT_READ | PROT_EXEC;

	if (linux_flags & LINUX_MAP_ANON) {
		fd = -1;
	} else {
		fd = linux_fd;
	}
	
#ifdef DEBUG
	if (ldebug(mmap) || ldebug(mmap2))
		kprintf("-> (%p, %d, %d, 0x%08x, %d, %lld)\n",
		    addr, len, prot, flags, fd, pos);
#endif
	error = kern_mmap(curproc->p_vmspace, addr, len,
			  prot, flags, fd, pos, &new);

	lwkt_reltoken(&curproc->p_vmspace->vm_map.token);

	if (error == 0)
		*res = new;
	return (error);
}

/*
 * MPSAFE
 */
int
sys_linux_mmap(struct linux_mmap_args *args)
{
	struct l_mmap_argv linux_args;
	int error;

	error = copyin((caddr_t)args->ptr, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

#ifdef DEBUG
	if (ldebug(mmap))
		kprintf(ARGS(mmap, "%p, %d, %d, 0x%08x, %d, %d"),
		    (void *)linux_args.addr, linux_args.len, linux_args.prot,
		    linux_args.flags, linux_args.fd, linux_args.pos);
#endif
	error = linux_mmap_common(linux_args.addr, linux_args.len,
	    linux_args.prot, linux_args.flags, linux_args.fd,
	    linux_args.pos, &args->sysmsg_resultp);
#ifdef DEBUG
	if (ldebug(mmap))
		kprintf("-> %p\n", args->sysmsg_resultp);
#endif
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_mmap2(struct linux_mmap2_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(mmap2))
		kprintf(ARGS(mmap2, "%p, %d, %d, 0x%08x, %d, %d"),
		    (void *)args->addr, args->len, args->prot, args->flags,
		    args->fd, args->pgoff);
#endif
	error = linux_mmap_common((void *)args->addr, args->len, args->prot,
	    args->flags, args->fd, args->pgoff * PAGE_SIZE,
	    &args->sysmsg_resultp);
#ifdef DEBUG
	if (ldebug(mmap2))
		kprintf("-> %p\n", args->sysmsg_resultp);
#endif
	return (error);
}

/*
 * MPSAFE
 */
int
sys_linux_pipe(struct linux_pipe_args *args)
{
	int error;
	int reg_edx;
	struct pipe_args bsd_args;

#ifdef DEBUG
	if (ldebug(pipe))
		kprintf(ARGS(pipe, "*"));
#endif

	reg_edx = args->sysmsg_fds[1];
	error = sys_pipe(&bsd_args);
	if (error) {
		args->sysmsg_fds[1] = reg_edx;
		return (error);
	}

	error = copyout(bsd_args.sysmsg_fds, args->pipefds, 2*sizeof(int));
	if (error) {
		args->sysmsg_fds[1] = reg_edx;
		return (error);
	}

	args->sysmsg_fds[1] = reg_edx;
	args->sysmsg_fds[0] = 0;
	return (0);
}

/*
 * XXX: Preliminary
 */
int
sys_linux_pipe2(struct linux_pipe2_args *args)
{
	struct thread *td = curthread;
	int error;
	int reg_edx;
	struct pipe_args bsd_args;
	union fcntl_dat dat;

	reg_edx = args->sysmsg_fds[1];
	error = sys_pipe(&bsd_args);
	if (error) {
		args->sysmsg_fds[1] = reg_edx;
		return (error);
	}

//	if (args->flags & LINUX_O_CLOEXEC) {
//	}

	if (args->flags & LINUX_O_NONBLOCK) {
		dat.fc_flags = O_NONBLOCK;
		kern_fcntl(bsd_args.sysmsg_fds[0], F_SETFL, &dat, td->td_ucred);
		kern_fcntl(bsd_args.sysmsg_fds[1], F_SETFL, &dat, td->td_ucred);
	}

	error = copyout(bsd_args.sysmsg_fds, args->pipefds, 2*sizeof(int));
	if (error) {
		args->sysmsg_fds[1] = reg_edx;
		return (error);
	}

	args->sysmsg_fds[1] = reg_edx;
	args->sysmsg_fds[0] = 0;
	return (0);
}

/*
 * MPSAFE
 */
int
sys_linux_ioperm(struct linux_ioperm_args *args)
{
	struct sysarch_args sa;
	struct i386_ioperm_args *iia;
	caddr_t sg;
	int error;

	sg = stackgap_init();
	iia = stackgap_alloc(&sg, sizeof(struct i386_ioperm_args));
	iia->start = args->start;
	iia->length = args->length;
	iia->enable = args->enable;
	sa.sysmsg_resultp = NULL;
	sa.op = I386_SET_IOPERM;
	sa.parms = (char *)iia;
	error = sys_sysarch(&sa);
	args->sysmsg_resultp = sa.sysmsg_resultp;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_iopl(struct linux_iopl_args *args)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	int error;

	if (args->level < 0 || args->level > 3)
		return (EINVAL);
	if ((error = priv_check(td, PRIV_ROOT)) != 0)
		return (error);
	if (securelevel > 0)
		return (EPERM);
	lp->lwp_md.md_regs->tf_eflags =
	    (lp->lwp_md.md_regs->tf_eflags & ~PSL_IOPL) |
	    (args->level * (PSL_IOPL / 3));
	return (0);
}

/*
 * MPSAFE
 */
int
sys_linux_modify_ldt(struct linux_modify_ldt_args *uap)
{
	int error;
	caddr_t sg;
	struct sysarch_args args;
	struct i386_ldt_args *ldt;
	struct l_descriptor ld;
	union descriptor *desc;
	int size, written;

	sg = stackgap_init();

	if (uap->ptr == NULL)
		return (EINVAL);

	switch (uap->func) {
	case 0x00: /* read_ldt */
		ldt = stackgap_alloc(&sg, sizeof(*ldt));
		ldt->start = 0;
		ldt->descs = uap->ptr;
		ldt->num = uap->bytecount / sizeof(union descriptor);
		args.op = I386_GET_LDT;
		args.parms = (char*)ldt;
		args.sysmsg_iresult = 0;
		error = sys_sysarch(&args);
		uap->sysmsg_iresult = args.sysmsg_iresult *
				      sizeof(union descriptor);
		break;
	case 0x02: /* read_default_ldt = 0 */
		size = 5*sizeof(struct l_desc_struct);
		if (size > uap->bytecount)
			size = uap->bytecount;
		for (written = error = 0; written < size && error == 0; written++)
			error = subyte((char *)uap->ptr + written, 0);
		uap->sysmsg_iresult = written;
		break;
	case 0x01: /* write_ldt */
	case 0x11: /* write_ldt */
		if (uap->bytecount != sizeof(ld))
			return (EINVAL);

		error = copyin(uap->ptr, &ld, sizeof(ld));
		if (error)
			return (error);

		ldt = stackgap_alloc(&sg, sizeof(*ldt));
		desc = stackgap_alloc(&sg, sizeof(*desc));
		ldt->start = ld.entry_number;
		ldt->descs = desc;
		ldt->num = 1;
		desc->sd.sd_lolimit = (ld.limit & 0x0000ffff);
		desc->sd.sd_hilimit = (ld.limit & 0x000f0000) >> 16;
		desc->sd.sd_lobase = (ld.base_addr & 0x00ffffff);
		desc->sd.sd_hibase = (ld.base_addr & 0xff000000) >> 24;
		desc->sd.sd_type = SDT_MEMRO | ((ld.read_exec_only ^ 1) << 1) |
			(ld.contents << 2);
		desc->sd.sd_dpl = 3;
		desc->sd.sd_p = (ld.seg_not_present ^ 1);
		desc->sd.sd_xx = 0;
		desc->sd.sd_def32 = ld.seg_32bit;
		desc->sd.sd_gran = ld.limit_in_pages;
		args.op = I386_SET_LDT;
		args.parms = (char*)ldt;
		args.sysmsg_iresult = 0;
		error = sys_sysarch(&args);
		uap->sysmsg_iresult = args.sysmsg_iresult;
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_sigaction(struct linux_sigaction_args *args)
{
	l_osigaction_t osa;
	l_sigaction_t linux_act, linux_oact;
	struct sigaction act, oact;
	int error, sig;

#ifdef DEBUG
	if (ldebug(sigaction))
		kprintf(ARGS(sigaction, "%d, %p, %p"),
		    args->sig, (void *)args->nsa, (void *)args->osa);
#endif

	if (args->nsa) {
		error = copyin(args->nsa, &osa, sizeof(l_osigaction_t));
		if (error)
			return (error);
		linux_act.lsa_handler = osa.lsa_handler;
		linux_act.lsa_flags = osa.lsa_flags;
		linux_act.lsa_restorer = osa.lsa_restorer;
		LINUX_SIGEMPTYSET(linux_act.lsa_mask);
		linux_act.lsa_mask.__bits[0] = osa.lsa_mask;
		linux_to_bsd_sigaction(&linux_act, &act);
	}

	if (args->sig <= LINUX_SIGTBLSZ)
		sig = linux_to_bsd_signal[_SIG_IDX(args->sig)];
	else
		sig = args->sig;

	get_mplock();
	error = kern_sigaction(sig, args->nsa ? &act : NULL,
			       args->osa ? &oact : NULL);
	rel_mplock();

	if (args->osa != NULL && !error) {
		bsd_to_linux_sigaction(&oact, &linux_oact);
		osa.lsa_handler = linux_oact.lsa_handler;
		osa.lsa_flags = linux_oact.lsa_flags;
		osa.lsa_restorer = linux_oact.lsa_restorer;
		osa.lsa_mask = linux_oact.lsa_mask.__bits[0];
		error = copyout(&osa, args->osa, sizeof(l_osigaction_t));
	}
	return (error);
}

/*
 * Linux has two extra args, restart and oldmask.  We dont use these,
 * but it seems that "restart" is actually a context pointer that
 * enables the signal to happen with a different register set.
 *
 * MPALMOSTSAFE
 */
int
sys_linux_sigsuspend(struct linux_sigsuspend_args *args)
{
	l_sigset_t linux_mask;
	sigset_t mask;
	int error;

#ifdef DEBUG
	if (ldebug(sigsuspend))
		kprintf(ARGS(sigsuspend, "%08lx"), (unsigned long)args->mask);
#endif

	LINUX_SIGEMPTYSET(mask);
	mask.__bits[0] = args->mask;
	linux_to_bsd_sigset(&linux_mask, &mask);

	get_mplock();
	error = kern_sigsuspend(&mask);
	rel_mplock();

	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_rt_sigsuspend(struct linux_rt_sigsuspend_args *uap)
{
	l_sigset_t linux_mask;
	sigset_t mask;
	int error;

#ifdef DEBUG
	if (ldebug(rt_sigsuspend))
		kprintf(ARGS(rt_sigsuspend, "%p, %d"),
		    (void *)uap->newset, uap->sigsetsize);
#endif

	if (uap->sigsetsize != sizeof(l_sigset_t))
		return (EINVAL);

	error = copyin(uap->newset, &linux_mask, sizeof(l_sigset_t));
	if (error)
		return (error);

	linux_to_bsd_sigset(&linux_mask, &mask);

	get_mplock();
	error = kern_sigsuspend(&mask);
	rel_mplock();

	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_pause(struct linux_pause_args *args)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	sigset_t mask;
	int error;

#ifdef DEBUG
	if (ldebug(pause))
		kprintf(ARGS(pause, ""));
#endif

	mask = lp->lwp_sigmask;

	get_mplock();
	error = kern_sigsuspend(&mask);
	rel_mplock();

	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_sigaltstack(struct linux_sigaltstack_args *uap)
{
	stack_t ss, oss;
	l_stack_t linux_ss;
	int error;

#ifdef DEBUG
	if (ldebug(sigaltstack))
		kprintf(ARGS(sigaltstack, "%p, %p"), uap->uss, uap->uoss);
#endif

	if (uap->uss) {
		error = copyin(uap->uss, &linux_ss, sizeof(l_stack_t));
		if (error)
			return (error);

		ss.ss_sp = linux_ss.ss_sp;
		ss.ss_size = linux_ss.ss_size;
		ss.ss_flags = linux_to_bsd_sigaltstack(linux_ss.ss_flags);
	}

	get_mplock();
	error = kern_sigaltstack(uap->uss ? &ss : NULL,
				 uap->uoss ? &oss : NULL);
	rel_mplock();

	if (error == 0 && uap->uoss) {
		linux_ss.ss_sp = oss.ss_sp;
		linux_ss.ss_size = oss.ss_size;
		linux_ss.ss_flags = bsd_to_linux_sigaltstack(oss.ss_flags);
		error = copyout(&linux_ss, uap->uoss, sizeof(l_stack_t));
	}

	return (error);
}

int
sys_linux_set_thread_area(struct linux_set_thread_area_args *args)
{
	struct segment_descriptor *desc;
	struct l_user_desc info;
	int error;
	int idx;
	int a[2];
	int i;

	error = copyin(args->desc, &info, sizeof(struct l_user_desc));
	if (error)
		return (EFAULT);

#ifdef DEBUG
	if (ldebug(set_thread_area))
	   	kprintf(ARGS(set_thread_area, "%i, %x, %x, %i, %i, %i, %i, %i, %i\n"),
		      info.entry_number,
      		      info.base_addr,
      		      info.limit,
      		      info.seg_32bit,
		      info.contents,
      		      info.read_exec_only,
      		      info.limit_in_pages,
      		      info.seg_not_present,
      		      info.useable);
#endif

	idx = info.entry_number;
	if (idx != -1 && (idx < 6 || idx > 8))
		return (EINVAL);

	if (idx == -1) {
		/* -1 means finding the first free TLS entry */
		for (i = 0; i < NGTLS; i++) {
			/*
			 * try to determine if the TLS entry is empty by looking
			 * at the lolimit entry.
			 */
			if (curthread->td_tls.tls[idx].sd_lolimit == 0) {
				idx = i;
				break;
			}
		}

		if (idx == -1) {
			/*
			 * By now we should have an index. If not, it means
			 * that no entry is free, so return ESRCH.
			 */
			return (ESRCH);
		}
	} else {
		/* translate the index from Linux to ours */
		idx -= 6;
		KKASSERT(idx >= 0);
	}

	/* Tell the caller about the allocated entry number */
#if 0 /* was SMP */
	info.entry_number = GTLS_START + mycpu->gd_cpuid * NGDT + idx;
#endif
	info.entry_number = GTLS_START + idx;


	error = copyout(&info, args->desc, sizeof(struct l_user_desc));
	if (error)
		return (error);

	if (LINUX_LDT_empty(&info)) {
		a[0] = 0;
		a[1] = 0;
	} else {
		a[0] = LINUX_LDT_entry_a(&info);
		a[1] = LINUX_LDT_entry_b(&info);
	}

	/*
	 * Update the TLS and the TLS entries in the GDT, but hold a critical
	 * section as required by set_user_TLS().
	 */
	crit_enter();
	desc = &curthread->td_tls.tls[idx];
	memcpy(desc, &a, sizeof(a));
	set_user_TLS();
	crit_exit();

	return (0);
}

int
sys_linux_get_thread_area(struct linux_get_thread_area_args *args)
{
	struct segment_descriptor *sd;
	struct l_desc_struct desc;
	struct l_user_desc info;
	int error;
	int idx;

#ifdef DEBUG
	if (ldebug(get_thread_area))
		kprintf(ARGS(get_thread_area, "%p"), args->desc);
#endif

	error = copyin(args->desc, &info, sizeof(struct l_user_desc));
	if (error)
		return (EFAULT);
		
	idx = info.entry_number;
	if ((idx < 6 || idx > 8) && (idx < GTLS_START)) {
		kprintf("sys_linux_get_thread_area, invalid idx requested: %d\n", idx);
		return (EINVAL);
	}

	memset(&info, 0, sizeof(info));

	/* translate the index from Linux to ours */
	info.entry_number = idx;
	if (idx < GTLS_START) {
		idx -= 6;
	} else {
#if 0 /* was SMP */
		idx -= (GTLS_START + mycpu->gd_cpuid * NGDT);
#endif
		idx -= GTLS_START;

	}
	KKASSERT(idx >= 0);

	sd = &curthread->td_tls.tls[idx];
	memcpy(&desc, sd, sizeof(desc));
	info.base_addr = LINUX_GET_BASE(&desc);
	info.limit = LINUX_GET_LIMIT(&desc);
	info.seg_32bit = LINUX_GET_32BIT(&desc);
	info.contents = LINUX_GET_CONTENTS(&desc);
	info.read_exec_only = !LINUX_GET_WRITABLE(&desc);
	info.limit_in_pages = LINUX_GET_LIMIT_PAGES(&desc);
	info.seg_not_present = !LINUX_GET_PRESENT(&desc);
	info.useable = LINUX_GET_USEABLE(&desc);

	error = copyout(&info, args->desc, sizeof(struct l_user_desc));
	if (error)
		return (EFAULT);

	return (0);
}
