/*
 * Copyright (c) 1980, 1986, 1989, 1993
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
 *	@(#)param.c	8.3 (Berkeley) 8/20/94
 * $FreeBSD: src/sys/kern/subr_param.c,v 1.42.2.10 2002/03/09 21:05:47 silby Exp $
 */

#include "opt_param.h"
#include "opt_maxusers.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <vm/pmap.h>
#include <machine/vmparam.h>

/*
 * System parameter formulae.
 */

#ifndef HZ
#define	HZ 100
#endif
#define	NPROC (20 + 16 * maxusers)
#ifndef NBUF
#define NBUF 0
#endif
#ifndef MAXFILES
#define	MAXFILES (maxproc * 16)
#endif
#ifndef MAXPOSIXLOCKSPERUID
#define MAXPOSIXLOCKSPERUID (maxusers * 64) /* Should be a safe value */
#endif

static int sysctl_kern_vmm_guest(SYSCTL_HANDLER_ARGS);

int	hz;
int	stathz;
int	profhz;
int	ustick;				/* tick interval in microseconds */
int	nstick;				/* tick interval in nanoseconds */
int	maxusers;			/* base tunable */
int	maxproc;			/* maximum # of processes */
int	maxprocperuid;			/* max # of procs per user */
int	maxfiles;			/* system wide open files limit */
int	maxfilesrootres;		/* descriptors reserved for root use */
int	minfilesperproc;		/* per-proc min open files (safety) */
int	maxfilesperproc;		/* per-proc open files limit */
int	maxfilesperuser;		/* per-user open files limit */
int	maxposixlocksperuid;		/* max # POSIX locks per uid */
int	ncallout;			/* maximum # of timer events */
int	mbuf_wait = 32;			/* mbuf sleep time in ticks */
long	nbuf;
long	nswbuf;
long	maxswzone;			/* max swmeta KVA storage */
long	maxbcache;			/* max buffer cache KVA storage */
enum vmm_guest_type vmm_guest = VMM_GUEST_NONE;	/* Running as VM guest? */
u_quad_t	maxtsiz;			/* max text size */
u_quad_t	dfldsiz;			/* initial data size limit */
u_quad_t	maxdsiz;			/* max data size */
u_quad_t	dflssiz;			/* initial stack size limit */
u_quad_t	maxssiz;			/* max stack size */
u_quad_t	sgrowsiz;			/* amount to grow stack */

SYSCTL_PROC(_kern, OID_AUTO, vmm_guest, CTLFLAG_RD | CTLTYPE_STRING,
    NULL, 0, sysctl_kern_vmm_guest, "A",
    "Virtual machine guest type");

/*
 * These have to be allocated somewhere; allocating
 * them here forces loader errors if this file is omitted
 * (if they've been externed everywhere else; hah!).
 */
struct	buf *swbuf;

struct vmm_bname {
	const char *str;
	enum vmm_guest_type type;
};

static struct vmm_bname vmm_bnames[] = {
	{ "QEMU",	VMM_GUEST_QEMU },	/* QEMU */
	{ "Plex86",	VMM_GUEST_PLEX86 },	/* Plex86 */
	{ "Bochs",	VMM_GUEST_BOCHS },	/* Bochs */
	{ "Xen",	VMM_GUEST_XEN },	/* Xen */
	{ "BHYVE",	VMM_GUEST_BHYVE },	/* bhyve */
	{ "Seabios",	VMM_GUEST_KVM},		/* KVM */
	{ NULL, 0 }
};

static struct vmm_bname vmm_pnames[] = {
	{ "VMware Virtual Platform",	VMM_GUEST_VMWARE },	/* VMWare VM */
	{ "Virtual Machine",		VMM_GUEST_VPC },	/* M$ VirtualPC */
	{ "VirtualBox",			VMM_GUEST_VBOX },	/* Sun VirtualBox */
	{ "Parallels Virtual Platform",	VMM_GUEST_PARALLELS },	/* Parallels VM */
	{ "KVM",			VMM_GUEST_KVM },	/* KVM */
	{ NULL, 0 }
};

static const char *const vmm_guest_sysctl_names[] = {
	"none",
	"qemu",
	"plex86",
	"bochs",
	"xen",
	"bhyve",
	"kvm",
	"vmware",
	"vpc",
	"vbox",
	"parallels",
	"vkernel",
	"unknown",
	NULL
};
CTASSERT(NELEM(vmm_guest_sysctl_names) - 1 == VMM_GUEST_LAST);

/*
 * Detect known Virtual Machine hosts by inspecting the emulated BIOS.
 */
enum vmm_guest_type
detect_virtual(void)
{
	char *sysenv;
	int i;

	sysenv = kgetenv("smbios.bios.vendor");
	if (sysenv != NULL) {
		for (i = 0; vmm_bnames[i].str != NULL; i++)
			if (strcmp(sysenv, vmm_bnames[i].str) == 0) {
				kfreeenv(sysenv);
				return (vmm_bnames[i].type);
			}
		kfreeenv(sysenv);
	}
	sysenv = kgetenv("smbios.system.product");
	if (sysenv != NULL) {
		for (i = 0; vmm_pnames[i].str != NULL; i++)
			if (strcmp(sysenv, vmm_pnames[i].str) == 0) {
				kfreeenv(sysenv);
				return (vmm_pnames[i].type);
			}
		kfreeenv(sysenv);
	}
	return (VMM_GUEST_NONE);
}

/*
 * Boot time overrides that are not scaled against main memory
 */
void
init_param1(void)
{
	hz = HZ;
	TUNABLE_INT_FETCH("kern.hz", &hz);
	stathz = hz * 128 / 100;
	profhz = stathz;
	ustick = 1000000 / hz;
	nstick = 1000000000 / hz;
	/* can adjust 30ms in 60s */
	ntp_default_tick_delta = howmany(30000000, 60 * hz);

#ifdef VM_SWZONE_SIZE_MAX
	maxswzone = VM_SWZONE_SIZE_MAX;
#endif
	TUNABLE_LONG_FETCH("kern.maxswzone", &maxswzone);
#ifdef VM_BCACHE_SIZE_MAX
	maxbcache = VM_BCACHE_SIZE_MAX;
#endif
	TUNABLE_LONG_FETCH("kern.maxbcache", &maxbcache);
	maxtsiz = MAXTSIZ;
	TUNABLE_QUAD_FETCH("kern.maxtsiz", &maxtsiz);
	dfldsiz = DFLDSIZ;
	TUNABLE_QUAD_FETCH("kern.dfldsiz", &dfldsiz);
	maxdsiz = MAXDSIZ;
	TUNABLE_QUAD_FETCH("kern.maxdsiz", &maxdsiz);
	dflssiz = DFLSSIZ;
	TUNABLE_QUAD_FETCH("kern.dflssiz", &dflssiz);
	maxssiz = MAXSSIZ;
	TUNABLE_QUAD_FETCH("kern.maxssiz", &maxssiz);
	sgrowsiz = SGROWSIZ;
	TUNABLE_QUAD_FETCH("kern.sgrowsiz", &sgrowsiz);
}

/*
 * Boot time overrides that are scaled against main memory
 */
void
init_param2(int physpages)
{
	size_t limsize;

	/*
	 * Calculate manually becaus the VM page queues / system is not set up yet
	 */
	limsize = (size_t)physpages * PAGE_SIZE;
	if (limsize > KvaSize)
		limsize = KvaSize;
	limsize /= 1024 * 1024;		/* smaller of KVM or physmem in MB */

	/* Base parameters */
	maxusers = MAXUSERS;
	TUNABLE_INT_FETCH("kern.maxusers", &maxusers);
	if (maxusers == 0) {
		maxusers = limsize / 8;		/* ~384 per 3G */
		if (maxusers < 32)
			maxusers = 32;
		/* no upper limit */
	}

	/*
	 * The following can be overridden after boot via sysctl.  Note:
	 * unless overriden, these macros are ultimately based on maxusers.
	 *
	 * Limit maxproc so that kmap entries cannot be exhausted by
	 * processes.
	 */
	maxproc = NPROC;
	TUNABLE_INT_FETCH("kern.maxproc", &maxproc);
	if (maxproc < 32)
		maxproc = 32;
	if (maxproc > limsize * 21)
		maxproc = limsize * 21;

	/*
	 * Maximum number of open files
	 */
	maxfiles = MAXFILES;
	TUNABLE_INT_FETCH("kern.maxfiles", &maxfiles);
	if (maxfiles < 128)
		maxfiles = 128;

	/*
	 * Limit file descriptors so no single user can exhaust the
	 * system.
	 *
	 * WARNING: Do not set minfilesperproc too high or the user
	 *	    can exhaust the system with a combination of fork()
	 *	    and open().  Actual worst case is:
	 *
	 *	    (minfilesperproc * maxprocperuid) + maxfilesperuser
	 */
	maxprocperuid = maxproc / 4;
	if (maxprocperuid < 128)
		maxprocperuid = maxproc / 2;
	minfilesperproc = 8;
	maxfilesperproc = maxfiles / 4;
	maxfilesperuser = maxfilesperproc * 2;
	maxfilesrootres = maxfiles / 20;

	/*
	 * Severe hack to try to prevent pipe() descriptors from
	 * blowing away kernel memory.
	 */
	if (KvaSize <= (vm_offset_t)(1536LL * 1024 * 1024) &&
	    maxfilesperuser > 20000) {
		maxfilesperuser = 20000;
	}

	maxposixlocksperuid = MAXPOSIXLOCKSPERUID;
	TUNABLE_INT_FETCH("kern.maxposixlocksperuid", &maxposixlocksperuid);

	/*
	 * Unless overriden, NBUF is typically 0 (auto-sized later).
	 */
	nbuf = NBUF;
	TUNABLE_LONG_FETCH("kern.nbuf", &nbuf);

	ncallout = 16 + maxproc + maxfiles;
	TUNABLE_INT_FETCH("kern.ncallout", &ncallout);
}

/*
 * Sysctl stringifying handler for kern.vmm_guest.
 */
static int
sysctl_kern_vmm_guest(SYSCTL_HANDLER_ARGS)
{
	return (SYSCTL_OUT(req, vmm_guest_sysctl_names[vmm_guest], 
	    strlen(vmm_guest_sysctl_names[vmm_guest])));
}
