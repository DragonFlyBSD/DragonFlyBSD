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
 * $DragonFly: src/sys/emulation/linux/i386/linux_machdep.c,v 1.11 2003/11/13 04:04:42 daver Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/sysproto.h>
#include <sys/unistd.h>

#include <machine/frame.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/sysarch.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include "linux.h"
#include "linux_proto.h"
#include "../linux_ipc.h"
#include "../linux_signal.h"
#include "../linux_util.h"

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

int
linux_execve(struct linux_execve_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	struct image_args exec_args;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(execve))
		printf(ARGS(execve, "%s"), path);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_LOCKLEAF | CNP_FOLLOW | CNP_SAVENAME,
	    UIO_SYSSPACE, path, td);

	error = exec_copyin_args(&exec_args, path, PATH_SYSSPACE,
	    args->argp, args->envp);
	if (error) {
		linux_free_path(&path);
		return (error);
	}

	error = kern_execve(&nd, &exec_args);

	/*
	 * The syscall result is returned in registers to the new program.
	 * Linux will register %edx as an atexit function and we must be
	 * sure to set it to 0.  XXX
	 */
	if (error == 0)
		args->sysmsg_result64 = 0;

	exec_free_args(&exec_args);
	linux_free_path(&path);
	return(error);
}

struct l_ipc_kludge {
	struct l_msgbuf *msgp;
	l_long msgtyp;
};

int
linux_ipc(struct linux_ipc_args *args)
{
	int error = 0;

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
			return (error);
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
		a.msgflg = args->arg3;
		a.sysmsg_lresult = 0;
		if ((args->what >> 16) == 0) {
			struct l_ipc_kludge tmp;
			int error;

			if (args->ptr == NULL)
				return (EINVAL);
			error = copyin((caddr_t)args->ptr, &tmp, sizeof(tmp));
			if (error)
				return (error);
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
	return(error);
}

int
linux_old_select(struct linux_old_select_args *args)
{
	struct l_old_select_argv linux_args;
	struct linux_select_args newsel;
	int error;

#ifdef DEBUG
	if (ldebug(old_select))
		printf(ARGS(old_select, "%p"), args->ptr);
#endif

	error = copyin((caddr_t)args->ptr, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	newsel.sysmsg_result = 0;
	newsel.nfds = linux_args.nfds;
	newsel.readfds = linux_args.readfds;
	newsel.writefds = linux_args.writefds;
	newsel.exceptfds = linux_args.exceptfds;
	newsel.timeout = linux_args.timeout;
	error = linux_select(&newsel);
	args->sysmsg_result = newsel.sysmsg_result;
	return(error);
}

int
linux_fork(struct linux_fork_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(fork))
		printf(ARGS(fork, ""));
#endif

	if ((error = fork((struct fork_args *)args)) != 0)
		return (error);

	if (args->sysmsg_result == 1)
		args->sysmsg_result = 0;
	return (0);
}

int
linux_vfork(struct linux_vfork_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(vfork))
		printf(ARGS(vfork, ""));
#endif

	if ((error = vfork((struct vfork_args *)args)) != 0)
		return (error);
	/* Are we the child? */
	if (args->sysmsg_result == 1)
		args->sysmsg_result = 0;
	return (0);
}

#define CLONE_VM	0x100
#define CLONE_FS	0x200
#define CLONE_FILES	0x400
#define CLONE_SIGHAND	0x800
#define CLONE_PID	0x1000

int
linux_clone(struct linux_clone_args *args)
{
	int error, ff = RFPROC;
	struct proc *p2;
	int exit_signal;
	vm_offset_t start;
	struct rfork_args rf_args;

#ifdef DEBUG
	if (ldebug(clone)) {
		printf(ARGS(clone, "flags %x, stack %x"), 
		    (unsigned int)args->flags, (unsigned int)args->stack);
		if (args->flags & CLONE_PID)
			printf(LMSG("CLONE_PID not yet supported"));
	}
#endif

	if (!args->stack)
		return (EINVAL);

	exit_signal = args->flags & 0x000000ff;
	if (exit_signal >= LINUX_NSIG)
		return (EINVAL);

	if (exit_signal <= LINUX_SIGTBLSZ)
		exit_signal = linux_to_bsd_signal[_SIG_IDX(exit_signal)];

	/* RFTHREAD probably not necessary here, but it shouldn't hurt */
	ff |= RFTHREAD;

	if (args->flags & CLONE_VM)
		ff |= RFMEM;
	if (args->flags & CLONE_SIGHAND)
		ff |= RFSIGSHARE;
	if (!(args->flags & CLONE_FILES))
		ff |= RFFDG;

	error = 0;
	start = 0;

	rf_args.flags = ff;
	rf_args.sysmsg_result = 0;
	if ((error = rfork(&rf_args)) != 0)
		return (error);
	args->sysmsg_result = rf_args.sysmsg_result;

	p2 = pfind(rf_args.sysmsg_result);
	if (p2 == NULL)
		return (ESRCH);

	p2->p_sigparent = exit_signal;
	p2->p_md.md_regs->tf_esp = (unsigned int)args->stack;

#ifdef DEBUG
	if (ldebug(clone))
		printf(LMSG("clone: successful rfork to %ld"),
		    (long)p2->p_pid);
#endif

	return (0);
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

int
linux_mmap(struct linux_mmap_args *args)
{
	struct proc *p = curproc;
	struct mmap_args bsd_args;
	int error;
	struct l_mmap_argv linux_args;

	error = copyin((caddr_t)args->ptr, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

#ifdef DEBUG
	if (ldebug(mmap))
		printf(ARGS(mmap, "%p, %d, %d, 0x%08x, %d, %d"),
		    (void *)linux_args.addr, linux_args.len, linux_args.prot,
		    linux_args.flags, linux_args.fd, linux_args.pos);
#endif

	bsd_args.flags = 0;
	bsd_args.sysmsg_resultp = NULL;
	if (linux_args.flags & LINUX_MAP_SHARED)
		bsd_args.flags |= MAP_SHARED;
	if (linux_args.flags & LINUX_MAP_PRIVATE)
		bsd_args.flags |= MAP_PRIVATE;
	if (linux_args.flags & LINUX_MAP_FIXED)
		bsd_args.flags |= MAP_FIXED;
	if (linux_args.flags & LINUX_MAP_ANON)
		bsd_args.flags |= MAP_ANON;
	else
		bsd_args.flags |= MAP_NOSYNC;
	if (linux_args.flags & LINUX_MAP_GROWSDOWN) {
		bsd_args.flags |= MAP_STACK;

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
		bsd_args.addr = linux_args.addr + linux_args.len;

		if (bsd_args.addr > p->p_vmspace->vm_maxsaddr) {
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
		if (linux_args.len > STACK_SIZE - GUARD_SIZE)
			bsd_args.len = linux_args.len;
		else
			bsd_args.len  = STACK_SIZE - GUARD_SIZE;

		/* This gives us a new BOS.  If we're using VM_STACK, then
		 * mmap will just map the top SGROWSIZ bytes, and let
		 * the stack grow down to the limit at BOS.  If we're
		 * not using VM_STACK we map the full stack, since we
		 * don't have a way to autogrow it.
		 */
		bsd_args.addr -= bsd_args.len;
	} else {
		bsd_args.addr = linux_args.addr;
		bsd_args.len  = linux_args.len;
	}

	bsd_args.prot = linux_args.prot | PROT_READ;	/* always required */
	if (linux_args.flags & LINUX_MAP_ANON)
		bsd_args.fd = -1;
	else
		bsd_args.fd = linux_args.fd;
	bsd_args.pos = linux_args.pos;
	bsd_args.pad = 0;

#ifdef DEBUG
	if (ldebug(mmap))
		printf("-> (%p, %d, %d, 0x%08x, %d, %d)\n",
		    (void *)bsd_args.addr, bsd_args.len, bsd_args.prot,
		    bsd_args.flags, bsd_args.fd, (int)bsd_args.pos);
#endif

	error = mmap(&bsd_args);
	args->sysmsg_resultp = bsd_args.sysmsg_resultp;
	return(error);
}

int
linux_pipe(struct linux_pipe_args *args)
{
	int error;
	int reg_edx;
	struct pipe_args bsd_args;

#ifdef DEBUG
	if (ldebug(pipe))
		printf(ARGS(pipe, "*"));
#endif

	reg_edx = args->sysmsg_fds[1];
	error = pipe(&bsd_args);
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

int
linux_ioperm(struct linux_ioperm_args *args)
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
	error = sysarch(&sa);
	args->sysmsg_resultp = sa.sysmsg_resultp;
	return(error);
}

int
linux_iopl(struct linux_iopl_args *args)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;

	KKASSERT(p);

	if (args->level < 0 || args->level > 3)
		return (EINVAL);
	if ((error = suser(td)) != 0)
		return (error);
	if (securelevel > 0)
		return (EPERM);
	p->p_md.md_regs->tf_eflags = (p->p_md.md_regs->tf_eflags & ~PSL_IOPL) |
	    (args->level * (PSL_IOPL / 3));
	return (0);
}

int
linux_modify_ldt(struct linux_modify_ldt_args *uap)
{
	int error;
	caddr_t sg;
	struct sysarch_args args;
	struct i386_ldt_args *ldt;
	struct l_descriptor ld;
	union descriptor *desc;

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
		args.sysmsg_result = 0;
		error = sysarch(&args);
		uap->sysmsg_result = args.sysmsg_result *
					    sizeof(union descriptor);
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
		args.sysmsg_result = 0;
		error = sysarch(&args);
		uap->sysmsg_result = args.sysmsg_result;
		break;
	default:
		error = EINVAL;
		break;
	}

	if (error == EOPNOTSUPP) {
		printf("linux: modify_ldt needs kernel option USER_LDT\n");
		error = ENOSYS;
	}

	return (error);
}

int
linux_sigaction(struct linux_sigaction_args *args)
{
	l_osigaction_t osa;
	l_sigaction_t linux_act, linux_oact;
	struct sigaction act, oact;
	int error;

#ifdef DEBUG
	if (ldebug(sigaction))
		printf(ARGS(sigaction, "%d, %p, %p"),
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

	error = kern_sigaction(args->sig, args->nsa ? &act : NULL,
	    args->osa ? &oact : NULL);

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
 */
int
linux_sigsuspend(struct linux_sigsuspend_args *args)
{
	l_sigset_t linux_mask;
	sigset_t mask;
	int error;

#ifdef DEBUG
	if (ldebug(sigsuspend))
		printf(ARGS(sigsuspend, "%08lx"), (unsigned long)args->mask);
#endif

	LINUX_SIGEMPTYSET(mask);
	mask.__bits[0] = args->mask;
	linux_to_bsd_sigset(&linux_mask, &mask);

	error = kern_sigsuspend(&mask);

	return(error);
}

int
linux_rt_sigsuspend(struct linux_rt_sigsuspend_args *uap)
{
	l_sigset_t linux_mask;
	sigset_t mask;
	int error;

#ifdef DEBUG
	if (ldebug(rt_sigsuspend))
		printf(ARGS(rt_sigsuspend, "%p, %d"),
		    (void *)uap->newset, uap->sigsetsize);
#endif

	if (uap->sigsetsize != sizeof(l_sigset_t))
		return (EINVAL);

	error = copyin(uap->newset, &linux_mask, sizeof(l_sigset_t));
	if (error)
		return (error);

	linux_to_bsd_sigset(&linux_mask, &mask);

	error = kern_sigsuspend(&mask);

	return(error);
}

int
linux_pause(struct linux_pause_args *args)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	sigset_t mask;
	int error;

#ifdef DEBUG
	if (ldebug(pause))
		printf(ARGS(pause, ""));
#endif

	mask = p->p_sigmask;

	error = kern_sigsuspend(&mask);

	return(error);
}

int
linux_sigaltstack(struct linux_sigaltstack_args *uap)
{
	stack_t ss, oss;
	l_stack_t linux_ss;
	int error;

#ifdef DEBUG
	if (ldebug(sigaltstack))
		printf(ARGS(sigaltstack, "%p, %p"), uap->uss, uap->uoss);
#endif

	if (uap->uss) {
		error = copyin(uap->uss, &linux_ss, sizeof(l_stack_t));
		if (error)
			return (error);

		ss.ss_sp = linux_ss.ss_sp;
		ss.ss_size = linux_ss.ss_size;
		ss.ss_flags = linux_to_bsd_sigaltstack(linux_ss.ss_flags);
	}

	error = kern_sigaltstack(uap->uss ? &ss : NULL,
	    uap->uoss ? &oss : NULL);

	if (error == 0 && uap->uoss) {
		linux_ss.ss_sp = oss.ss_sp;
		linux_ss.ss_size = oss.ss_size;
		linux_ss.ss_flags = bsd_to_linux_sigaltstack(oss.ss_flags);
		error = copyout(&linux_ss, uap->uoss, sizeof(l_stack_t));
	}

	return (error);
}
