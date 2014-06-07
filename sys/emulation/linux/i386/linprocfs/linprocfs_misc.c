/*
 * Copyright (c) 2000 Dag-Erling Coïdan Smørgrav
 * Copyright (c) 1999 Pierre Beyssac
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)procfs_status.c	8.4 (Berkeley) 6/15/94
 *
 * $FreeBSD: src/sys/i386/linux/linprocfs/linprocfs_misc.c,v 1.3.2.8 2001/06/25 19:46:47 pirzyk Exp $
 */

#include <sys/param.h>
#include <sys/blist.h>
#include <sys/kernel.h>
#include <sys/kinfo.h>
#include <sys/proc.h>
#include <sys/jail.h>
#include <sys/resourcevar.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/sbuf.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/swap_pager.h>
#include <sys/vmmeter.h>
#include <sys/exec.h>
#include <sys/devfs.h>

#include <machine/clock.h>
#include <machine/cputypes.h>
#include <machine/inttypes.h>
#include <machine/md_var.h>
#include <machine/vmparam.h>

#include "linprocfs.h"
#include "../linux.h"
#include "../../linux_ioctl.h"
#include "../../linux_mib.h"

/*
 * Various conversion macros
 */
#define T2J(x) (((x) * 100) / (stathz ? stathz : hz))	/* ticks to jiffies */
#define T2S(x) ((x) / (stathz ? stathz : hz))		/* ticks to seconds */
#define B2K(x) ((unsigned long)((x) >> 10))			/* bytes to kbytes */
#define P2B(x) ((x) << PAGE_SHIFT)			/* pages to bytes */
#define P2K(x) ((x) << (PAGE_SHIFT - 10))		/* pages to kbytes */

int
linprocfs_domeminfo(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		    struct uio *uio)
{
	char *ps;
	char psbuf[512];		/* XXX - conservative */
	unsigned long memtotal;		/* total memory in bytes */
	unsigned long memused;		/* used memory in bytes */
	unsigned long memfree;		/* free memory in bytes */
	unsigned long memshared;	/* shared memory ??? */
	unsigned long buffers, cached;	/* buffer / cache memory ??? */
	unsigned long long swaptotal;	/* total swap space in bytes */
	unsigned long long swapused;	/* used swap space in bytes */
	unsigned long long swapfree;	/* free swap space in bytes */
	int n;

	if (uio->uio_rw != UIO_READ)
		return (EOPNOTSUPP);

	memtotal = Maxmem * PAGE_SIZE;
	/*
	 * The correct thing here would be:
	 *
	memfree = vmstats.v_free_count * PAGE_SIZE;
	memused = memtotal - memfree;
	 *
	 * but it might mislead linux binaries into thinking there
	 * is very little memory left, so we cheat and tell them that
	 * all memory that isn't wired down is free.
	 */
	memused = vmstats.v_wire_count * PAGE_SIZE;
	memfree = memtotal - memused;
	if (swapblist == NULL) {
		swaptotal = 0;
		swapfree = 0;
	} else {
		swaptotal = swapblist->bl_blocks * 1024LL; /* XXX why 1024? */
		swapfree = (unsigned long long)swapblist->bl_root->u.bmu_avail * PAGE_SIZE;
	}
	swapused = swaptotal - swapfree;
	memshared = 0;

	for (n = 0; n < ncpus; ++n) {
		globaldata_t gd = globaldata_find(n);

		memshared += gd->gd_vmtotal.t_arm;
	}
	memshared *= PAGE_SIZE;

	/*
	 * We'd love to be able to write:
	 *
	buffers = bufspace;
	 *
	 * but bufspace is internal to vfs_bio.c and we don't feel
	 * like unstaticizing it just for linprocfs's sake.
	 */
	buffers = 0;
	cached = vmstats.v_cache_count * PAGE_SIZE;

	ps = psbuf;
	ps += ksprintf(ps,
		"        total:    used:    free:  shared: buffers:  cached:\n"
		"Mem:  %lu %lu %lu %lu %lu %lu\n"
		"Swap: %llu %llu %llu\n"
		"MemTotal: %9lu kB\n"
		"MemFree:  %9lu kB\n"
		"MemShared:%9lu kB\n"
		"Buffers:  %9lu kB\n"
		"Cached:   %9lu kB\n"
		"SwapTotal:%9lu kB\n"
		"SwapFree: %9lu kB\n",
		memtotal, memused, memfree, memshared, buffers, cached,
		swaptotal, swapused, swapfree,
		B2K(memtotal), B2K(memfree),
		B2K(memshared), B2K(buffers), B2K(cached),
		B2K(swaptotal), B2K(swapfree));

	return (uiomove_frombuf(psbuf, ps - psbuf, uio));
}

int
linprocfs_docpuinfo(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		    struct uio *uio)
{
	char *ps;
	char psbuf[8192];
	char hwmodel[128];
	size_t	modellen = sizeof(hwmodel);
	int mib[] = { CTL_HW, HW_MODEL };
	int class;
	int cpu;
        int i;
	int error;
#if 0
	extern char *cpu_model;		/* Yuck */
#endif
        /* We default the flags to include all non-conflicting flags,
           and the Intel versions of conflicting flags.  Note the space
           before each name; that is significant, and should be 
           preserved. */

        static char *flags[] = {
		"fpu",      "vme",     "de",       "pse",      "tsc",
		"msr",      "pae",     "mce",      "cx8",      "apic",
		"sep",      "sep",     "mtrr",     "pge",      "mca",
		"cmov",     "pat",     "pse36",    "pn",       "b19",
		"b20",      "b21",     "mmxext",   "mmx",      "fxsr",
		"xmm",      "b26",     "b27",      "b28",      "b29",
		"3dnowext", "3dnow"
	};

	if (uio->uio_rw != UIO_READ)
		return (EOPNOTSUPP);

	switch (cpu_class) {
	case CPUCLASS_286:
		class = 2;
		break;
	case CPUCLASS_386:
		class = 3;
		break;
	case CPUCLASS_486:
		class = 4;
		break;
	case CPUCLASS_586:
		class = 5;
		break;
	case CPUCLASS_686:
		class = 6;
		break;
	default:
                class = 0;
		break;
	}

	ps = psbuf;

	error = kernel_sysctl(mib, 2, hwmodel, &modellen, NULL, 0, NULL);
	if (error)
		strcpy(hwmodel, "unknown");

	for (cpu = 0; cpu < ncpus; cpu++) {
		ps += ksprintf(ps,
		    "processor\t: %d\n"
		    "vendor_id\t: %.20s\n"
		    "cpu family\t: %d\n"
		    "model\t\t: %d\n"
		    "model name\t: %s\n"
		    "stepping\t: %d\n",
		    cpu, cpu_vendor, class, cpu, hwmodel, cpu_id & 0xf);
	}

        ps += ksprintf(ps,
                        "flags\t\t:");

        if (cpu_vendor_id == CPU_VENDOR_AMD && (class < 6)) {
		flags[16] = "fcmov";
        }
        
        for (i = 0; i < 32; i++)
		if (cpu_feature & (1 << i))
			ps += ksprintf(ps, " %s", flags[i]);
	ps += ksprintf(ps, "\n");
        if (class >= 5) {
		ps += ksprintf(ps,
			"cpu MHz\t\t: %d.%02d\n"
			"bogomips\t: %d.%02d\n",
                        (int)((tsc_frequency + 4999) / 1000000),
                        (int)((tsc_frequency + 4999) / 10000) % 100,
                        (int)((tsc_frequency + 4999) / 1000000),
                        (int)((tsc_frequency + 4999) / 10000) % 100);
        }
        
	return (uiomove_frombuf(psbuf, ps - psbuf, uio));
}

static unsigned int
cpucnt(int offset)
{
    int i;
    int count = 0;

    for (i = 0; i < ncpus; ++i) {
	struct globaldata *gd = globaldata_find(i);
	count += *(unsigned int *)((char *)&gd->gd_cnt + offset);
    }
    return(count);
}

static int
linprocfs_domounts_callback(struct mount *mp, void *data)
{
	struct statfs *st;
	struct sbuf *sb = (struct sbuf *)data;
	char *to, *from, *fs;

	st = &mp->mnt_stat;

	from = st->f_mntfromname;
	to = st->f_mntonname;
	fs = st->f_fstypename;

	if (!strcmp(st->f_fstypename, "linprocfs"))
		fs = "proc";
	else if (!strcmp(st->f_fstypename, "ext2fs"))
		fs = "ext2";
	else if (!strcmp(st->f_fstypename, "msdos"))
		fs = "vfat";
	else if (!strcmp(st->f_fstypename, "msdosfs"))
		fs = "vfat";

	sbuf_printf(sb, "%s %s %s %s", from, to, fs,
	    st->f_flags & MNT_RDONLY ? "ro" : "rw");

#define OPT_ADD(name, flag) if (st->f_flags & (flag)) sbuf_printf(sb, "," name)
	OPT_ADD("sync",		MNT_SYNCHRONOUS);
	OPT_ADD("noexec",	MNT_NOEXEC);
	OPT_ADD("nosuid",	MNT_NOSUID);
	OPT_ADD("nodev",	MNT_NODEV);
	OPT_ADD("async",	MNT_ASYNC);
	OPT_ADD("suiddir",	MNT_SUIDDIR);
	OPT_ADD("nosymfollow",	MNT_NOSYMFOLLOW);
	OPT_ADD("noatime",	MNT_NOATIME);
#undef OPT_ADD

	sbuf_printf(sb, " 0 0\n");

	return 0;
}

int
linprocfs_domounts(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		    struct uio *uio)
{
	struct sbuf *sb;
	int error;

	sb = sbuf_new_auto();

	error = mountlist_scan(linprocfs_domounts_callback, sb, MNTSCAN_FORWARD);

	sbuf_finish(sb);
	if (error == 0)
		error = uiomove_frombuf(sbuf_data(sb), sbuf_len(sb), uio);
	sbuf_delete(sb);
	return (error);
}

int
linprocfs_dostat(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		 struct uio *uio)
{
        char *ps;
	char psbuf[8192];
	int cpu;

	ps = psbuf;
	ps += ksprintf(ps,
		      "cpu %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n",
		      T2J(cpu_time.cp_user),
		      T2J(cpu_time.cp_nice),
		      T2J(cpu_time.cp_sys),
		      T2J(cpu_time.cp_idle));

	for (cpu = 0; cpu < ncpus; cpu++) {
		ps += ksprintf(ps,
		      "cpu%d %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n",
		      cpu,
		      T2J(cputime_percpu[cpu].cp_user),
		      T2J(cputime_percpu[cpu].cp_nice),
		      T2J(cputime_percpu[cpu].cp_sys),
		      T2J(cputime_percpu[cpu].cp_idle));			
	}

	ps += ksprintf(ps,
		      "disk 0 0 0 0\n"
		      "page %u %u\n"
		      "swap %u %u\n"
		      "intr %u\n"
		      "ctxt %u\n"
		      "btime %ld\n",
		      cpucnt(offsetof(struct vmmeter, v_vnodepgsin)),
		      cpucnt(offsetof(struct vmmeter, v_vnodepgsout)),
		      cpucnt(offsetof(struct vmmeter, v_swappgsin)),
		      cpucnt(offsetof(struct vmmeter, v_swappgsout)),
		      cpucnt(offsetof(struct vmmeter, v_intr)),
		      cpucnt(offsetof(struct vmmeter, v_swtch)),
		      boottime.tv_sec);

	return (uiomove_frombuf(psbuf, ps - psbuf, uio));
}

int
linprocfs_douptime(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		   struct uio *uio)
{
	char *ps;
	char psbuf[64];
	struct timeval tv;

	getmicrouptime(&tv);
	ps = psbuf;
	ps += ksprintf(ps, "%ld.%02ld %"PRIu64".%02"PRIu64"\n",
		      tv.tv_sec, tv.tv_usec / 10000,
		      T2S(cpu_time.cp_idle), T2J(cpu_time.cp_idle) % 100);
	return (uiomove_frombuf(psbuf, ps - psbuf, uio));
}

int
linprocfs_doversion(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		    struct uio *uio)
{
        char *ps;
	size_t xlen;

	ps = version; /* XXX not entirely correct */
	for (xlen = 0; ps[xlen] != '\n'; ++xlen)
		/* nothing */ ;
	++xlen;
	return (uiomove_frombuf(ps, xlen, uio));
}

#define B2P(x) ((x) >> PAGE_SHIFT)			/* bytes to pages */
int
linprocfs_dostatm(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		    struct uio *uio)
{
	char *ps, psbuf[1024];
	struct kinfo_proc kp;

	lwkt_gettoken(&p->p_token);
	fill_kinfo_proc(p, &kp);

	ps = psbuf;
	ps += ksprintf(ps, "%d", p->p_pid);
#define PS_ADD(name, fmt, arg) ps += ksprintf(ps, " " fmt, arg)
	PS_ADD("",	"%ju",	B2P((uintmax_t)(kp.kp_vm_tsize + kp.kp_vm_dsize + kp.kp_vm_ssize)));
	PS_ADD("",	"%ju",	(uintmax_t)kp.kp_vm_rssize);
	PS_ADD("",	"%ju",	(uintmax_t)0); /* XXX */
	PS_ADD("",	"%ju",	(uintmax_t)kp.kp_vm_tsize);
	PS_ADD("",	"%ju",	(uintmax_t)kp.kp_vm_dsize);
	PS_ADD("",	"%ju",	(uintmax_t)kp.kp_vm_ssize);
	PS_ADD("",	"%ju",	(uintmax_t)0); /* XXX */
#undef	PS_ADD
	ps += ksprintf(ps, "\n");
	lwkt_reltoken(&p->p_token);

	return (uiomove_frombuf(psbuf, ps - psbuf, uio));
}

#define P2K(x) ((x) << (PAGE_SHIFT - 10))		/* pages to kbytes */
int
linprocfs_doprocstat(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		     struct uio *uio)
{
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;
	vm_offset_t start, end;
	char *ps, psbuf[1024];
	struct kinfo_proc kp;

	lwkt_gettoken(&p->p_token);
	fill_kinfo_proc(p, &kp);

	start = 0;
	end = 0;
	vm_map_lock_read(map);
	for (entry = map->header.next; entry != &map->header;
		entry = entry->next) {
		if (entry->maptype != VM_MAPTYPE_NORMAL &&
		    entry->maptype != VM_MAPTYPE_VPAGETABLE) {
			continue;
		}
		/* Assuming that text is the first entry */
		start = entry->start;
		end = entry->end;
	}
	vm_map_unlock_read(map);

	ps = psbuf;
	ps += ksprintf(ps, "%d", p->p_pid);
#define PS_ADD(name, fmt, arg) ps += ksprintf(ps, " " fmt, arg)
	PS_ADD("comm",		"(%s)",	p->p_comm);
	PS_ADD("statr",		"%c",	'0'); /* XXX */
	PS_ADD("ppid",		"%d",	p->p_pptr ? p->p_pptr->p_pid : 0);
	PS_ADD("pgrp",		"%d",	p->p_pgid);
	PS_ADD("session",	"%d",	p->p_session->s_sid);
	PS_ADD("tty",		"%d",	0); /* XXX */
	PS_ADD("tpgid",		"%d",	kp.kp_tpgid); /* XXX */
	PS_ADD("flags",		"%u",	0); /* XXX */
	PS_ADD("minflt",	"%lu",	kp.kp_ru.ru_minflt); /* XXX */
	PS_ADD("cminflt",	"%lu",	kp.kp_cru.ru_minflt); /* XXX */
	PS_ADD("majflt",	"%lu",	kp.kp_ru.ru_majflt); /* XXX */
	PS_ADD("cmajflt",	"%lu",	kp.kp_cru.ru_majflt); /* XXX */
	PS_ADD("utime",		"%d",	T2J(tvtohz_high(&kp.kp_ru.ru_utime))); /* XXX */
	PS_ADD("stime",		"%d",	T2J(tvtohz_high(&kp.kp_ru.ru_stime))); /* XXX */
	PS_ADD("cutime",	"%d",	T2J(tvtohz_high(&kp.kp_cru.ru_utime))); /* XXX */
	PS_ADD("cstime",	"%d",	T2J(tvtohz_high(&kp.kp_cru.ru_stime))); /* XXX */
	PS_ADD("priority",	"%d",	0); /* XXX */
	PS_ADD("nice",		"%d",	kp.kp_nice);
	PS_ADD("timeout",	"%u",	0); /* XXX */
	PS_ADD("itrealvalue",	"%u",	0); /* XXX */
	PS_ADD("starttime",	"%d",	T2J(tvtohz_high(&kp.kp_start))); /* XXX */
	PS_ADD("vsize",		"%ju",	P2K((uintmax_t)(kp.kp_vm_tsize + kp.kp_vm_dsize + kp.kp_vm_ssize))); /* XXX: not sure */
	PS_ADD("rss",		"%ju",	(uintmax_t)kp.kp_vm_rssize); /* XXX */
	PS_ADD("rlim",		"%lu",	kp.kp_ru.ru_maxrss); /* XXX */
	PS_ADD("startcode",	"%lu",	start); /* XXX */
	PS_ADD("endcode",	"%lu",	end); /* XXX */
	PS_ADD("startstack",	"%lu",	(u_long)p->p_vmspace->vm_minsaddr); /* XXX */
	PS_ADD("kstkesp",	"%u",	0); /* XXX */
	PS_ADD("kstkeip",	"%u",	0); /* XXX */
	PS_ADD("signal",	"%d",	0); /* XXX */
	PS_ADD("blocked",	"%d",	0); /* XXX */
	PS_ADD("sigignore",	"%d",	0); /* XXX */
	PS_ADD("sigcatch",	"%d",	0); /* XXX */
	PS_ADD("wchan",		"%u",	0); /* XXX */
	PS_ADD("nswap",		"%lu",	kp.kp_ru.ru_nswap); /* XXX */
	PS_ADD("cnswap",	"%lu",	kp.kp_cru.ru_nswap); /* XXX */
	PS_ADD("exitsignal",	"%d",	0); /* XXX */
	PS_ADD("processor",	"%u",	kp.kp_lwp.kl_cpuid); /* XXX */
	PS_ADD("rt_priority",	"%u",	0); /* XXX */ /* >= 2.5.19 */
	PS_ADD("policy",	"%u",	kp.kp_nice); /* XXX */ /* >= 2.5.19 */
#undef PS_ADD
	ps += ksprintf(ps, "\n");
	lwkt_reltoken(&p->p_token);
	
	return (uiomove_frombuf(psbuf, ps - psbuf, uio));
}

/*
 * Map process state to descriptive letter. Note that this does not
 * quite correspond to what Linux outputs, but it's close enough.
 */
static char *state_str[] = {
	"? (unknown)",
	"I (idle)",
	"R (running)",
	"T (stopped)",
	"Z (zombie)",
	"S (sleeping)",
	"W (waiting)",
	"M (mutex)"
};

int
linprocfs_doprocstatus(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		       struct uio *uio)
{
	char *ps, psbuf[1024];
	char *state;
	int i;

	ps = psbuf;

	lwkt_gettoken(&p->p_token);
	if (p->p_stat > NELEM(state_str))
		state = state_str[0];
	else
		state = state_str[(int)p->p_stat];

#define PS_ADD ps += ksprintf
	PS_ADD(ps, "Name:\t%s\n",	  p->p_comm); /* XXX escape */
	PS_ADD(ps, "State:\t%s\n",	  state);

	/*
	 * Credentials
	 */
	PS_ADD(ps, "Pid:\t%d\n",	  p->p_pid);
	PS_ADD(ps, "PPid:\t%d\n",	  p->p_pptr ? p->p_pptr->p_pid : 0);
	PS_ADD(ps, "Uid:\t%d %d %d %d\n", p->p_ucred->cr_ruid,
		                          p->p_ucred->cr_uid,
		                          p->p_ucred->cr_svuid,
		                          /* FreeBSD doesn't have fsuid */
		                          p->p_ucred->cr_uid);
	PS_ADD(ps, "Gid:\t%d %d %d %d\n", p->p_ucred->cr_rgid,
		                          p->p_ucred->cr_gid,
		                          p->p_ucred->cr_svgid,
		                          /* FreeBSD doesn't have fsgid */
		                          p->p_ucred->cr_gid);
	PS_ADD(ps, "Groups:\t");
	for (i = 0; i < p->p_ucred->cr_ngroups; i++)
		PS_ADD(ps, "%d ", p->p_ucred->cr_groups[i]);
	PS_ADD(ps, "\n");
	
	/*
	 * Memory
	 */
	PS_ADD(ps, "VmSize:\t%8lu kB\n",  B2K(p->p_vmspace->vm_map.size));
	PS_ADD(ps, "VmLck:\t%8u kB\n",    P2K(0)); /* XXX */
	/* XXX vm_rssize seems to always be zero, how can this be? */
	PS_ADD(ps, "VmRss:\t%8u kB\n",    P2K(p->p_vmspace->vm_rssize));
	PS_ADD(ps, "VmData:\t%8u kB\n",   P2K(p->p_vmspace->vm_dsize));
	PS_ADD(ps, "VmStk:\t%8u kB\n",    P2K(p->p_vmspace->vm_ssize));
	PS_ADD(ps, "VmExe:\t%8u kB\n",    P2K(p->p_vmspace->vm_tsize));
	PS_ADD(ps, "VmLib:\t%8u kB\n",    P2K(0)); /* XXX */

	/*
	 * Signal masks
	 *
	 * We support up to 128 signals, while Linux supports 32,
	 * but we only define 32 (the same 32 as Linux, to boot), so
	 * just show the lower 32 bits of each mask. XXX hack.
	 *
	 * NB: on certain platforms (Sparc at least) Linux actually
	 * supports 64 signals, but this code is a long way from
	 * running on anything but i386, so ignore that for now.
	 */
	PS_ADD(ps, "SigPnd:\t%08x\n",	  p->p_siglist.__bits[0]);
	PS_ADD(ps, "SigBlk:\t%08x\n",	  0); /* XXX */
	PS_ADD(ps, "SigIgn:\t%08x\n",	  p->p_sigignore.__bits[0]);
	PS_ADD(ps, "SigCgt:\t%08x\n",	  p->p_sigcatch.__bits[0]);
	
	/*
	 * Linux also prints the capability masks, but we don't have
	 * capabilities yet, and when we do get them they're likely to
	 * be meaningless to Linux programs, so we lie. XXX
	 */
	PS_ADD(ps, "CapInh:\t%016x\n",	  0);
	PS_ADD(ps, "CapPrm:\t%016x\n",	  0);
	PS_ADD(ps, "CapEff:\t%016x\n",	  0);
#undef PS_ADD
	lwkt_reltoken(&p->p_token);
	
	return (uiomove_frombuf(psbuf, ps - psbuf, uio));
}

int
linprocfs_doloadavg(struct proc *curp, struct proc *p,
		    struct pfsnode *pfs, struct uio *uio)
{
	char *ps, psbuf[512];

	ps = psbuf;
	ps += ksprintf(ps, "%d.%02d %d.%02d %d.%02d %d/%d %d\n",
	    (int)(averunnable.ldavg[0] / averunnable.fscale),
	    (int)(averunnable.ldavg[0] * 100 / averunnable.fscale % 100),
	    (int)(averunnable.ldavg[1] / averunnable.fscale),
	    (int)(averunnable.ldavg[1] * 100 / averunnable.fscale % 100),
	    (int)(averunnable.ldavg[2] / averunnable.fscale),
	    (int)(averunnable.ldavg[2] * 100 / averunnable.fscale % 100),
	    1,                      /* number of running tasks */
	    -1,                     /* number of tasks */
	    1         /* The last pid, just kidding */
	);
	return(uiomove_frombuf(psbuf, ps - psbuf, uio));
}

int
linprocfs_donetdev(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		   struct uio *uio)
{
	struct sbuf *sb;
	char ifname[16]; /* XXX LINUX_IFNAMSIZ */
	struct ifnet *ifp;
	int error;

	sb = sbuf_new_auto();

	sbuf_printf(sb, "%6s|%58s|%s\n%6s|%58s|%58s\n",
	    "Inter-", "   Receive", "  Transmit", " face",
	    "bytes    packets errs drop fifo frame compressed",
	    "bytes    packets errs drop fifo frame compressed");

	crit_enter();
	TAILQ_FOREACH(ifp, &ifnet, if_link) {
		linux_ifname(ifp, ifname, sizeof ifname);
		sbuf_printf(sb, "%6.6s:", ifname);
		sbuf_printf(sb, "%8lu %7lu %4lu %4lu %4lu %5lu %10lu %9lu ",
		    0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL);
		sbuf_printf(sb, "%8lu %7lu %4lu %4lu %4lu %5lu %7lu %10lu\n",
		    0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL);
	}
	crit_exit();
	sbuf_finish(sb);
	error = uiomove_frombuf(sbuf_data(sb), sbuf_len(sb), uio);
	sbuf_delete(sb);
	return (error);
}

static void
linprocfs_dodevices_callback(char *name, cdev_t dev, bool is_alias, void *arg)
{
	struct sbuf *sb = arg;

	sbuf_printf(sb, "%3d %s\n", dev->si_umajor, name);
}

int
linprocfs_dodevices(struct proc *curp, struct proc *p, struct pfsnode *pfs,
    struct uio *uio)
{
	struct sbuf *sb;
	int error;

	sb = sbuf_new_auto();
	sbuf_printf(sb, "Character devices:\n");
	devfs_scan_callback(linprocfs_dodevices_callback, sb);
	sbuf_printf(sb, "\nBlock devices:\n");
	sbuf_finish(sb);
	error = uiomove_frombuf(sbuf_data(sb), sbuf_len(sb), uio);
	sbuf_delete(sb);
	return (error);
}

int
linprocfs_doosrelease(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		   struct uio *uio)
{
	char *osrelease;

	osrelease = linux_get_osrelease(curthread);
	return(uiomove_frombuf(osrelease, strlen(osrelease)+1, uio));
}

int
linprocfs_doostype(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		   struct uio *uio)
{
	char *osname;

	osname = linux_get_osname(curthread);
	return(uiomove_frombuf(osname, strlen(osname)+1, uio));
}

int
linprocfs_dopidmax(struct proc *curp, struct proc *p, struct pfsnode *pfs,
		   struct uio *uio)
{
	char buf[32];

	ksnprintf(buf, sizeof(buf), "%d", PID_MAX);
	return(uiomove_frombuf(buf, strlen(buf)+1, uio));
}

int
linprocfs_domaps(struct proc *curp, struct proc *p, struct pfsnode *pfs,
	     struct uio *uio)
{
	int error;
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;
	vm_ooffset_t off = 0;
	struct sbuf *sb;
	char *name = "", *freename = NULL;
	struct vnode *vp;
	struct vattr vat;
	ino_t ino;

	if (uio->uio_rw != UIO_READ)
		return (EOPNOTSUPP);

	sb = sbuf_new_auto();

	error = 0;
	vm_map_lock_read(map);
	for (entry = map->header.next;
		((uio->uio_resid > 0) && (entry != &map->header));
		entry = entry->next) {
		vm_object_t obj, tobj, lobj;
		vm_offset_t ostart;
		name = "";
		freename = NULL;
		ino = 0;
		if (entry->maptype != VM_MAPTYPE_NORMAL &&
		    entry->maptype != VM_MAPTYPE_VPAGETABLE) {
			continue;
		}
		/*
		 * Use map->hint as a poor man's ripout detector.
		 */
		map->hint = entry;
		ostart = entry->start;

		/*
		 * Find the bottom-most object, leaving the base object
		 * and the bottom-most object held (but only one hold
		 * if they happen to be the same).
		 */
		obj = entry->object.vm_object;
		if (obj)
			vm_object_hold(obj);

                lobj = obj;
                while (lobj && (tobj = lobj->backing_object) != NULL) {
			KKASSERT(tobj != obj);
                        vm_object_hold(tobj);
                        if (tobj == lobj->backing_object) {
				if (lobj != obj) {
					vm_object_lock_swap();
					vm_object_drop(lobj);
				}
                                lobj = tobj;
                        } else {
                                vm_object_drop(tobj);
                        }
                }

		if (lobj) {
			off = IDX_TO_OFF(lobj->size);
			if (lobj->type == OBJT_VNODE) {
				vp = lobj->handle;
				if (vp)
					vref(vp);
			} else {
				vp = NULL;
			}
			
			if (vp) {
				vn_fullpath(curproc, vp, &name, &freename, 1);
				vn_lock(vp, LK_SHARED | LK_RETRY);
				VOP_GETATTR(vp, &vat);
				ino = vat.va_fileid;
				vput(vp);
			}
		}
		if (freename == NULL) {
			if (entry->eflags & MAP_ENTRY_STACK)
				name = "[stack]";
		}

		if (lobj != obj)
			vm_object_drop(lobj);
		if (obj)
			vm_object_drop(obj);

		/*
		 * We cannot safely hold the map locked while accessing
		 * userspace as a VM fault might recurse the locked map.
		 */
		vm_map_unlock_read(map);

		/*
		 * format:
		 *  start-end access offset major:minor inode [.text file]
		 */
		error = sbuf_printf(sb,
		    "%08lx-%08lx %s%s%s%s %08llx %02x:%02x %llu%s%s\n",
		    (u_long)entry->start, (u_long)entry->end,
		    (entry->protection & VM_PROT_READ)?"r":"-",
		    (entry->protection & VM_PROT_WRITE)?"w":"-",
		    (entry->protection & VM_PROT_EXECUTE)?"x":"-",
		    "p",
		    off,	/* offset */
		    0,		/* major */
		    0,		/* minor */
		    ino,	/* inode */
		    (name && *name) ? "     " : "",
		    name ? name : "");
		if (error == -1)
			error = ENOMEM;
		if (freename)
			kfree(freename, M_TEMP);

		vm_map_lock_read(map);
		if (error)
			break;

		/*
		 * We use map->hint as a poor man's ripout detector.  If
		 * it does not match the entry we set it to prior to
		 * unlocking the map the entry MIGHT now be stale.  In
		 * this case we do an expensive lookup to find our place
		 * in the iteration again.
		 */
		if (map->hint != entry) {
			vm_map_entry_t reentry;
		
			vm_map_lookup_entry(map, ostart, &reentry);
			entry = reentry;
		}
	}
	vm_map_unlock_read(map);

	sbuf_finish(sb);
	if (error == 0)
		error = uiomove_frombuf(sbuf_data(sb) + uio->uio_offset,
		    sbuf_len(sb) - uio->uio_offset, uio);
	sbuf_delete(sb);
	return error;
}
