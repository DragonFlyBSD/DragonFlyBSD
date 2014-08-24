/*
 * Copyright (c) 1995 Terrence R. Lambert
 * All rights reserved.
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1992, 1993
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
 *	@(#)init_main.c	8.9 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/init_main.c,v 1.134.2.8 2003/06/06 20:21:32 tegge Exp $
 */

#include "opt_init_path.h"

#include <sys/param.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/sysent.h>
#include <sys/reboot.h>
#include <sys/sysproto.h>
#include <sys/vmmeter.h>
#include <sys/unistd.h>
#include <sys/malloc.h>
#include <sys/machintr.h>

#include <sys/refcount.h>
#include <sys/file2.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#include <machine/cpu.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>
#include <sys/user.h>
#include <sys/copyright.h>

int vfs_mountroot_devfs(void);

/* Components of the first process -- never freed. */
static struct session session0;
static struct pgrp pgrp0;
static struct sigacts sigacts0;
static struct filedesc filedesc0;
static struct plimit limit0;
static struct vmspace vmspace0;
struct proc *initproc;
struct proc proc0;
struct lwp lwp0;
struct thread thread0;

int cmask = CMASK;
u_int cpu_mi_feature;
cpumask_t usched_global_cpumask;
extern	struct user *proc0paddr;

int	boothowto = 0;		/* initialized so that it can be patched */
SYSCTL_INT(_debug, OID_AUTO, boothowto, CTLFLAG_RD, &boothowto, 0,
    "Reboot flags, from console subsystem");
SYSCTL_ULONG(_kern, OID_AUTO, usched_global_cpumask, CTLFLAG_RW,
    &usched_global_cpumask, 0, "global user scheduler cpumask");

/*
 * This ensures that there is at least one entry so that the sysinit_set
 * symbol is not undefined.  A subsystem ID of SI_SPECIAL_DUMMY is never
 * executed.
 */
SYSINIT(placeholder, SI_SPECIAL_DUMMY, SI_ORDER_ANY, NULL, NULL)

/*
 * The sysinit table itself.  Items are checked off as the are run.
 * If we want to register new sysinit types, add them to newsysinit.
 */
SET_DECLARE(sysinit_set, struct sysinit);
struct sysinit **sysinit, **sysinit_end;
struct sysinit **newsysinit, **newsysinit_end;


/*
 * Merge a new sysinit set into the current set, reallocating it if
 * necessary.  This can only be called after malloc is running.
 */
void
sysinit_add(struct sysinit **set, struct sysinit **set_end)
{
	struct sysinit **newset;
	struct sysinit **sipp;
	struct sysinit **xipp;
	int count;

	count = set_end - set;
	if (newsysinit)
		count += newsysinit_end - newsysinit;
	else
		count += sysinit_end - sysinit;
	newset = kmalloc(count * sizeof(*sipp), M_TEMP, M_WAITOK);
	xipp = newset;
	if (newsysinit) {
		for (sipp = newsysinit; sipp < newsysinit_end; sipp++)
			*xipp++ = *sipp;
	} else {
		for (sipp = sysinit; sipp < sysinit_end; sipp++)
			*xipp++ = *sipp;
	}
	for (sipp = set; sipp < set_end; sipp++)
		*xipp++ = *sipp;
	if (newsysinit)
		kfree(newsysinit, M_TEMP);
	newsysinit = newset;
	newsysinit_end = newset + count;
}

/*
 * Callbacks from machine-dependant startup code (e.g. init386) to set
 * up low level entities related to cpu #0's globaldata.
 *
 * Called from very low level boot code.
 */
void
mi_proc0init(struct globaldata *gd, struct user *proc0paddr)
{
	lwkt_init_thread(&thread0, proc0paddr, LWKT_THREAD_STACK, 0, gd);
	lwkt_set_comm(&thread0, "thread0");
	RB_INIT(&proc0.p_lwp_tree);
	spin_init(&proc0.p_spin, "iproc_proc0");
	lwkt_token_init(&proc0.p_token, "iproc");
	proc0.p_lasttid = 0;	/* +1 = next TID */
	lwp_rb_tree_RB_INSERT(&proc0.p_lwp_tree, &lwp0);
	lwp0.lwp_thread = &thread0;
	lwp0.lwp_proc = &proc0;
	proc0.p_usched = usched_init();
	CPUMASK_ASSALLONES(lwp0.lwp_cpumask);
	lwkt_token_init(&lwp0.lwp_token, "lwp_token");
	spin_init(&lwp0.lwp_spin, "iproc_lwp0");
	varsymset_init(&proc0.p_varsymset, NULL);
	thread0.td_flags |= TDF_RUNNING;
	thread0.td_proc = &proc0;
	thread0.td_lwp = &lwp0;
	thread0.td_switch = cpu_lwkt_switch;
	lwkt_schedule_self(curthread);
}

/*
 * System startup; initialize the world, create process 0, mount root
 * filesystem, and fork to create init and pagedaemon.  Most of the
 * hard work is done in the lower-level initialization routines including
 * startup(), which does memory initialization and autoconfiguration.
 *
 * This allows simple addition of new kernel subsystems that require
 * boot time initialization.  It also allows substitution of subsystem
 * (for instance, a scheduler, kernel profiler, or VM system) by object
 * module.  Finally, it allows for optional "kernel threads".
 */
void
mi_startup(void)
{
	struct sysinit *sip;		/* system initialization*/
	struct sysinit **sipp;		/* system initialization*/
	struct sysinit **xipp;		/* interior loop of sort*/
	struct sysinit *save;		/* bubble*/

	if (sysinit == NULL) {
		sysinit = SET_BEGIN(sysinit_set);
#if defined(__x86_64__) && defined(_KERNEL_VIRTUAL)
		/*
		 * XXX For whatever reason, on 64-bit vkernels
		 * the value of sysinit obtained from the
		 * linker set is wrong.
		 */
		if ((long)sysinit % 8 != 0) {
			kprintf("Fixing sysinit value...\n");
			sysinit = (void *)((long)(intptr_t)sysinit + 4);
		}
#endif
		sysinit_end = SET_LIMIT(sysinit_set);
	}
#if defined(__x86_64__) && defined(_KERNEL_VIRTUAL)
	KKASSERT((long)sysinit % 8 == 0);
#endif

restart:
	/*
	 * Perform a bubble sort of the system initialization objects by
	 * their subsystem (primary key) and order (secondary key).
	 */
	for (sipp = sysinit; sipp < sysinit_end; sipp++) {
		for (xipp = sipp + 1; xipp < sysinit_end; xipp++) {
			if ((*sipp)->subsystem < (*xipp)->subsystem ||
			     ((*sipp)->subsystem == (*xipp)->subsystem &&
			      (*sipp)->order <= (*xipp)->order))
				continue;	/* skip*/
			save = *sipp;
			*sipp = *xipp;
			*xipp = save;
		}
	}

	/*
	 * Traverse the (now) ordered list of system initialization tasks.
	 * Perform each task, and continue on to the next task.
	 *
	 * The last item on the list is expected to be the scheduler,
	 * which will not return.
	 */
	for (sipp = sysinit; sipp < sysinit_end; sipp++) {
		sip = *sipp;
		if (sip->subsystem == SI_SPECIAL_DUMMY)
			continue;	/* skip dummy task(s)*/

		if (sip->subsystem == SI_SPECIAL_DONE)
			continue;

#if 0
		if (bootverbose)
			kprintf("(%08x-%p)\n", sip->subsystem, sip->func);
#endif

		/* Call function */
		(*(sip->func))(sip->udata);

		/* Check off the one we're just done */
		sip->subsystem = SI_SPECIAL_DONE;

		/* Check if we've installed more sysinit items via KLD */
		if (newsysinit != NULL) {
			if (sysinit != SET_BEGIN(sysinit_set))
				kfree(sysinit, M_TEMP);
			sysinit = newsysinit;
			sysinit_end = newsysinit_end;
			newsysinit = NULL;
			newsysinit_end = NULL;
			goto restart;
		}
	}

	panic("Shouldn't get here!");
	/* NOTREACHED*/
}


/*
 ***************************************************************************
 ****
 **** The following SYSINIT's belong elsewhere, but have not yet
 **** been moved.
 ****
 ***************************************************************************
 */
static void
print_caddr_t(void *data)
{
	kprintf("%s", (char *)data);
}
SYSINIT(announce, SI_BOOT1_COPYRIGHT, SI_ORDER_FIRST, print_caddr_t, copyright)

/*
 * Leave the critical section that protected us from spurious interrupts
 * so device probes work.
 */
static void
leavecrit(void *dummy __unused)
{
	MachIntrABI.stabilize();
	cpu_enable_intr();
	MachIntrABI.cleanup();
	crit_exit();
	KKASSERT(!IN_CRITICAL_SECT(curthread));

	if (bootverbose)
		kprintf("Leaving critical section, allowing interrupts\n");
}
SYSINIT(leavecrit, SI_BOOT2_LEAVE_CRIT, SI_ORDER_ANY, leavecrit, NULL)

/*
 * This is called after the threading system is up and running,
 * including the softclock, clock interrupts, and SMP.
 */
static void
tsleepworks(void *dummy __unused)
{
	tsleep_now_works = 1;
}
SYSINIT(tsleepworks, SI_BOOT2_FINISH_SMP, SI_ORDER_SECOND, tsleepworks, NULL)

/*
 * This is called after devices have configured.  Tell the kernel we are
 * no longer in cold boot.
 */
static void
endofcoldboot(void *dummy __unused)
{
	cold = 0;
}
SYSINIT(endofcoldboot, SI_SUB_ISWARM, SI_ORDER_ANY, endofcoldboot, NULL)

/*
 ***************************************************************************
 ****
 **** The two following SYSINT's are proc0 specific glue code.  I am not
 **** convinced that they can not be safely combined, but their order of
 **** operation has been maintained as the same as the original init_main.c
 **** for right now.
 ****
 **** These probably belong in init_proc.c or kern_proc.c, since they
 **** deal with proc0 (the fork template process).
 ****
 ***************************************************************************
 */
/* ARGSUSED*/
static void
proc0_init(void *dummy __unused)
{
	struct proc *p;
	struct lwp *lp;

	p = &proc0;
	lp = &lwp0;

	/*
	 * Initialize osrel
	 */
	p->p_osrel = osreldate;

	/*
	 * Initialize process and pgrp structures.
	 */
	procinit();

	/*
	 * additional VM structures
	 */
	vm_init2();

	/*
	 * Create process 0 (the swapper).
	 */
	procinsertinit(p);
	pgrpinsertinit(&pgrp0);
	LIST_INIT(&pgrp0.pg_members);
	lwkt_token_init(&pgrp0.pg_token, "pgrp0");
	refcount_init(&pgrp0.pg_refs, 1);
	lockinit(&pgrp0.pg_lock, "pgwt0", 0, 0);
	LIST_INSERT_HEAD(&pgrp0.pg_members, p, p_pglist);

	pgrp0.pg_session = &session0;
	session0.s_count = 1;
	session0.s_leader = p;
	sessinsertinit(&session0);

	pgref(&pgrp0);
	p->p_pgrp = &pgrp0;

	p->p_sysent = &aout_sysvec;

	p->p_flags = P_SYSTEM;
	p->p_stat = SACTIVE;
	lp->lwp_stat = LSRUN;
	p->p_nice = NZERO;
	p->p_rtprio.type = RTP_PRIO_NORMAL;
	p->p_rtprio.prio = 0;
	lp->lwp_rtprio = p->p_rtprio;

	p->p_peers = NULL;
	p->p_leader = p;

	bcopy("swapper", p->p_comm, sizeof ("swapper"));
	bcopy("swapper", thread0.td_comm, sizeof ("swapper"));

	/* Create credentials. */
	p->p_ucred = crget();
	p->p_ucred->cr_ruidinfo = uifind(0);
	p->p_ucred->cr_ngroups = 1;	/* group 0 */
	p->p_ucred->cr_uidinfo = uifind(0);
	thread0.td_ucred = crhold(p->p_ucred);	/* bootstrap fork1() */

	/* Don't jail it */
	p->p_ucred->cr_prison = NULL;

	/* Create sigacts. */
	p->p_sigacts = &sigacts0;
	refcount_init(&p->p_sigacts->ps_refcnt, 1);

	/* Initialize signal state for process 0. */
	siginit(p);

	/* Create the file descriptor table. */
	fdinit_bootstrap(p, &filedesc0, cmask);

	/* Create the limits structures. */
	plimit_init0(&limit0);
	p->p_limit = &limit0;

	/* Allocate a prototype map so we have something to fork. */
	pmap_pinit0(vmspace_pmap(&vmspace0));
	p->p_vmspace = &vmspace0;
	lp->lwp_vmspace = p->p_vmspace;
	vmspace_initrefs(&vmspace0);
	vm_map_init(&vmspace0.vm_map,
		    round_page(VM_MIN_USER_ADDRESS),
		    trunc_page(VM_MAX_USER_ADDRESS),
		    vmspace_pmap(&vmspace0));

	kqueue_init(&lwp0.lwp_kqueue, &filedesc0);

	/*
	 * Charge root for one process.
	 */
	(void)chgproccnt(p->p_ucred->cr_uidinfo, 1, 0);
	vm_init_limits(p);
}
SYSINIT(p0init, SI_BOOT2_PROC0, SI_ORDER_FIRST, proc0_init, NULL)

static int proc0_post_callback(struct proc *p, void *data __unused);

/* ARGSUSED*/
static void
proc0_post(void *dummy __unused)
{
	struct timespec ts;

	/*
	 * Now we can look at the time, having had a chance to verify the
	 * time from the file system.  Pretend that proc0 started now.
	 */
	allproc_scan(proc0_post_callback, NULL);

	/*
	 * Give the ``random'' number generator a thump.
	 * XXX: Does read_random() contain enough bits to be used here ?
	 */
	nanotime(&ts);
	skrandom(ts.tv_sec ^ ts.tv_nsec);
}

static int
proc0_post_callback(struct proc *p, void *data __unused)
{
	microtime(&p->p_start);
	return(0);
}

SYSINIT(p0post, SI_SUB_PROC0_POST, SI_ORDER_FIRST, proc0_post, NULL)

/*
 ***************************************************************************
 ****
 **** The following SYSINIT's and glue code should be moved to the
 **** respective files on a per subsystem basis.
 ****
 ***************************************************************************
 */


/*
 ***************************************************************************
 ****
 **** The following code probably belongs in another file, like
 **** kern/init_init.c.
 ****
 ***************************************************************************
 */

/*
 * List of paths to try when searching for "init".
 */
static char init_path[MAXPATHLEN] =
#ifdef	INIT_PATH
    __XSTRING(INIT_PATH);
#else
    "/sbin/init:/sbin/oinit:/sbin/init.bak";
#endif
SYSCTL_STRING(_kern, OID_AUTO, init_path, CTLFLAG_RD, init_path, 0, "");

/*
 * Shutdown timeout of init(8).
 * Unused within kernel, but used to control init(8), hence do not remove.
 */
#ifndef INIT_SHUTDOWN_TIMEOUT
#define INIT_SHUTDOWN_TIMEOUT 120
#endif
static int init_shutdown_timeout = INIT_SHUTDOWN_TIMEOUT;
SYSCTL_INT(_kern, OID_AUTO, init_shutdown_timeout,
	CTLFLAG_RW, &init_shutdown_timeout, 0, "Shutdown timeout of init(8). "
	"Unused within kernel, but used to control init(8)");

/*
 * Start the initial user process; try exec'ing each pathname in init_path.
 * The program is invoked with one argument containing the boot flags.
 */
static void
start_init(void *dummy, struct trapframe *frame)
{
	vm_offset_t addr;
	struct execve_args args;
	int options, error;
	char *var, *path, *next, *s;
	char *ucp, **uap, *arg0, *arg1;
	struct proc *p;
	struct lwp *lp;
	struct mount *mp;
	struct vnode *vp;
	char *env;

        /*
	 * This is passed in by the bootloader
         */
	env = kgetenv("kernelname");
	if (env != NULL)
		strlcpy(kernelname, env, sizeof(kernelname));

	/*
	 * The MP lock is not held on entry.  We release it before
	 * returning to userland.
	 */
	get_mplock();
	p = curproc;

	lp = ONLY_LWP_IN_PROC(p);

	/* Get the vnode for '/'.  Set p->p_fd->fd_cdir to reference it. */
	mp = mountlist_boot_getfirst();
	if (VFS_ROOT(mp, &vp))
		panic("cannot find root vnode");
	if (mp->mnt_ncmountpt.ncp == NULL) {
		cache_allocroot(&mp->mnt_ncmountpt, mp, vp);
		cache_unlock(&mp->mnt_ncmountpt);	/* leave ref intact */
	}
	p->p_fd->fd_cdir = vp;
	vref(p->p_fd->fd_cdir);
	p->p_fd->fd_rdir = vp;
	vref(p->p_fd->fd_rdir);
	vfs_cache_setroot(vp, cache_hold(&mp->mnt_ncmountpt));
	vn_unlock(vp);			/* leave ref intact */
	cache_copy(&mp->mnt_ncmountpt, &p->p_fd->fd_ncdir);
	cache_copy(&mp->mnt_ncmountpt, &p->p_fd->fd_nrdir);

	kprintf("Mounting devfs\n");
	vfs_mountroot_devfs();

	/*
	 * Need just enough stack to hold the faked-up "execve()" arguments.
	 */
	addr = trunc_page(USRSTACK - PAGE_SIZE);
	error = vm_map_find(&p->p_vmspace->vm_map, NULL, 0, &addr,
			    PAGE_SIZE, PAGE_SIZE,
			    FALSE, VM_MAPTYPE_NORMAL,
			    VM_PROT_ALL, VM_PROT_ALL,
			    0);
	if (error)
		panic("init: couldn't allocate argument space");
	p->p_vmspace->vm_maxsaddr = (caddr_t)addr;
	p->p_vmspace->vm_ssize = 1;

	if ((var = kgetenv("init_path")) != NULL) {
		strncpy(init_path, var, sizeof init_path);
		init_path[sizeof init_path - 1] = 0;
	}

	for (path = init_path; *path != '\0'; path = next) {
		while (*path == ':')
			path++;
		if (*path == '\0')
			break;
		for (next = path; *next != '\0' && *next != ':'; next++)
			/* nothing */ ;
		if (bootverbose)
			kprintf("start_init: trying %.*s\n", (int)(next - path),
			    path);
			
		/*
		 * Move out the boot flag argument.
		 */
		options = 0;
		ucp = (char *)USRSTACK;
		(void)subyte(--ucp, 0);		/* trailing zero */
		if (boothowto & RB_SINGLE) {
			(void)subyte(--ucp, 's');
			options = 1;
		}
#ifdef notyet
                if (boothowto & RB_FASTBOOT) {
			(void)subyte(--ucp, 'f');
			options = 1;
		}
#endif

#ifdef BOOTCDROM
		(void)subyte(--ucp, 'C');
		options = 1;
#endif
		if (options == 0)
			(void)subyte(--ucp, '-');
		(void)subyte(--ucp, '-');		/* leading hyphen */
		arg1 = ucp;

		/*
		 * Move out the file name (also arg 0).
		 */
		(void)subyte(--ucp, 0);
		for (s = next - 1; s >= path; s--)
			(void)subyte(--ucp, *s);
		arg0 = ucp;

		/*
		 * Move out the arg pointers.
		 */
		uap = (char **)((intptr_t)ucp & ~(sizeof(intptr_t)-1));
		(void)suword((caddr_t)--uap, (long)0);	/* terminator */
		(void)suword((caddr_t)--uap, (long)(intptr_t)arg1);
		(void)suword((caddr_t)--uap, (long)(intptr_t)arg0);

		/*
		 * Point at the arguments.
		 */
		args.fname = arg0;
		args.argv = uap;
		args.envv = NULL;

		/*
		 * Now try to exec the program.  If can't for any reason
		 * other than it doesn't exist, complain.
		 *
		 * Otherwise, return via fork_trampoline() all the way
		 * to user mode as init!
		 *
		 * WARNING!  We may have been moved to another cpu after
		 * acquiring the current user process designation.  The
		 * MP lock will migrate with us though so we still have to
		 * release it.
		 */
		if ((error = sys_execve(&args)) == 0) {
			rel_mplock();
			lp->lwp_proc->p_usched->acquire_curproc(lp);
			return;
		}
		if (error != ENOENT)
			kprintf("exec %.*s: error %d\n", (int)(next - path), 
			    path, error);
	}
	kprintf("init: not found in path %s\n", init_path);
	panic("no init");
}

/*
 * Like kthread_create(), but runs in it's own address space.
 * We do this early to reserve pid 1.
 *
 * Note special case - do not make it runnable yet.  Other work
 * in progress will change this more.
 */
static void
create_init(const void *udata __unused)
{
	int error;
	struct lwp *lp;

	crit_enter();
	error = fork1(&lwp0, RFFDG | RFPROC, &initproc);
	if (error)
		panic("cannot fork init: %d", error);
	initproc->p_flags |= P_SYSTEM;
	lp = ONLY_LWP_IN_PROC(initproc);
	cpu_set_fork_handler(lp, start_init, NULL);
	crit_exit();
}
SYSINIT(init, SI_SUB_CREATE_INIT, SI_ORDER_FIRST, create_init, NULL)

/*
 * Make it runnable now.
 */
static void
kick_init(const void *udata __unused)
{
	start_forked_proc(&lwp0, initproc);
}
SYSINIT(kickinit, SI_SUB_KTHREAD_INIT, SI_ORDER_FIRST, kick_init, NULL)

/*
 * Machine independant globaldata initialization
 *
 * WARNING!  Called from early boot, 'mycpu' may not work yet.
 */
void
mi_gdinit(struct globaldata *gd, int cpuid)
{
	TAILQ_INIT(&gd->gd_systimerq);
	gd->gd_sysid_alloc = cpuid;	/* prime low bits for cpu lookup */
	gd->gd_cpuid = cpuid;
	CPUMASK_ASSBIT(gd->gd_cpumask, cpuid);
	lwkt_gdinit(gd);
	vm_map_entry_reserve_cpu_init(gd);
	sleep_gdinit(gd);
	ATOMIC_CPUMASK_ORBIT(usched_global_cpumask, cpuid);
}

