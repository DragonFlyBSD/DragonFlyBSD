/*-
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)param.h	8.3 (Berkeley) 4/4/95
 * $FreeBSD: src/sys/sys/param.h,v 1.61.2.38 2003/05/22 17:12:01 fjoe Exp $
 */

#ifndef _SYS_PARAM_H_
#define _SYS_PARAM_H_

/*
 * Historic BSD #defines -- probably will remain untouched for all time.
 */
#define BSD	199506		/* System version (year & month). */
#define BSD4_3	1
#define BSD4_4	1

/*
 * __DragonFly_version number.  The number doesn't really meaningfully
 * translate to a version number.
 *
 * 170000 - base development version after 1.6 branch
 * 170001 - base development version before 1.8 branch
 * 190000 - base development version after 1.8 branch
 * 195000 - base development version after 1.10 branch
 * 197500 - base development version before 1.12 branch
 * 197600 - 1.12 branch
 * 197700 - base development version after 1.12 branch
 * 199900 - base development version before 2.0 branch
 * 200000 - 2.0 branch
 * 200100 - base development version after 2.0 branch
 * 200101 - lchflags syscall
 * 200200 - 2.2 branch
 * 200201 - base development version after 2.2 branch
 * 200202 - major changes to libc
 * 200203 - introduce PCI domain
 * 200204 - suser() & suser_cred() removal
 * 200205 - devfs import
 * 200206 - *sleep() renames
 * 200400 - 2.4 release
 * 200500 - 2.5 master
 * 200600 - 2.6 release
 * 200700 - 2.7 master
 * 200800 - 2.8 release			October 2010
 * 200900 - 2.9 master
 * 200901 - prototype changes for alphasort(3) and scandir(3)
 * 200902 - Xerox NS protocol removal
 * 201000 - 2.10 release
 * 201100 - 2.11 master
 * 201200 - 2.12 release
 * 201300 - 2.13 master
 * 201301 - header rename: <vfs/gnu/ext2fs/...> -> <gnu/vfs/ext2fs/...>
 * 201302 - header <crypt.h> is gone
 * 201303 - <netatalk/...> and <netproto/atalk/...> are gone
 * 201304 - Added wcscasecmp, wcsncasecmp to libc
 * 201305 - Sync libm with NetBSD-current libm (new functions added)
 * 201306 - Sync libm with FreeBSD-current libm (~50 new functions)
 * 300000 - 3.0 release
 * 300100 - 3.1 master
 * 300101 - i4b (ISDN) removal
 * 300102 - <sys/ata.h> is now just a link of <sys/nata.h>
 * 300103 - if SIG_IGN is set on SIGCHLD, do not keep zombie children
 * 300200 - 3.2 release
 * 300300 - 3.3 master
 * 300301 - Add eaccess syscall
 * 300302 - fpsave changes - ucontext_t, mcontext_t, sigcontext, sigframe
 * 300303 - Demarcation of old m4/flex with new m4/flex
 * 300304 - Update to dialog-1.2-20121230
 * 300400 - 3.4 release
 * 300500 - 3.5 master
 * 300501 - Convert libm to FreeBSD's version
 * 300502 - GEM and i915 KMS support in kernel
 * 300503 - Upgrade libiconv, locales, and associated libc functions
 * 300600 - 3.6 release
 * 300700 - 3.7 master
 * 300701 - Relocate bus/smbus/smb.h to dev/smbus/smb/smb.h
 * 300702 - drm/i915 update
 * 300703 - Make usb4bsd default
 * 300704 - Removal of IPX, NCP and NWFS support
 * 300705 - Removal of ATM support.
 * 300800 - 3.8 release
 * 300900 - 3.9 master
 * 300901 - drm/i915 hardware context support added
 * 400000 - 4.0 release
 * 400100 - 4.1 development
 * 400101 - Removal of SCTP support.
 * 400102 - Sound system update from FreeBSD
 * 400103 - Milestone - availability of gcc50 in base
 * 400104 - struct lwp_params (a public struct) members renaming
 * 400105 - Switch to gcc50 as the primary compiler
 * 400106 - Added pipe2() system call
 * 400107 - Add futimens() and utimensat() syscalls
 * 400200 - 4.2 release
 * 400300 - 4.3 development
 * 400301 - posix compliant iconv (no const qualifier)
 * 400302 - Replacement of libm with OpenBSD's libm
 * 400303 - environ and __progname are no longer linkable symbols
 * 400304 - Activate symbol versioning for libc.so (still on version 8)
 * 400305 - Add accept4() system call
 * 400306 - Add libexecinfo to base
 * 400307 - drm/i915 kernel module renamed to i915.ko
 * 400308 - <malloc.h> removal
 * 400309 - Add lwp_setname() system call
 * 400400 - 4.4 release
 * 400500 - 4.5 development
 * 400501 - unionfs removal
 * 400502 - private libraries: ssh ldns edit ncurses
 * 400503 - libarchive-3.0.2 import (add bsdcat)
 * 400600 - 4.6 release
 * 400700 - 4.7 development
 * 400701 - getline() visibility changes
 * 400702 - private library: libressl
 * 400703 - resolved conflicts of md, crypt and ressl libraries
 * 400704 - binutils update to 2.27
 * 400705 - lwp_{set,get}affinity()
 * 400706 - sched_{set,get}affinity()
 * 400707 - pthread_{set,get}affinity_np()
 * 400708 - lwp_create2()
 * 400709 - pthread_attr_{set,get}affinity_np()
 * 400710 - sched_getcpu();
 * 400711 - move lwp syscalls to sys/lwp.h
 * 400712 - restore lwp syscalls (except lwp_create*) declaration
 * 400713 - add sysctl kern.cp_times
 * 400800 - 4.8 release
 * 400900 - 4.9 development
 * 400901 - moved sigtramp, NX protection, sigtramp sysctl
 * 400902 - change CPU_SETSIZE to signed; allow proc to change self affinity
 * 400903 - malloc_type cleanup
 * 400904 - pad rtstatistics
 * 400905 - PTHREAD_STACK_MIN increase: 1024 => 16384
 * 400906 - lwpid_t >=1, instead of >=0
 * 400907 - pthread_getthreadid_np()
 * 400908 - {clock,pthread}_getcpuclockid()
 * 400909 - deleted ortentry, SIOC{ADD,DEL}RT, RTM_OLD{ADD,DEL}
 * 400910 - routing table is only available on netisr_ncpus
 * 500000 - 5.0 release
 * 500100 - 5.1 development
 * 500101 - kernel ppp removal
 * 500102 - <sys/sysref{,2}.h> inclusions removed from some public headers
 * 500103 - faith removal
 * 500104 - cfmakesane()
 * 500105 - _SC_LEVEL1_DCACHE_LINESIZE sysconf()
 * 500106 - %b and %r formats removal
 * 500107 - <machine/apm_bios.h> removal
 * 500200 - 5.2 release
 * 500300 - 5.3 development
 * 500301 - rename some public UFS constants
 * 500302 - move IOCTLTRIM to a better header and rename it to DAIOCTRIM
 * 500303 - get rid of sgtty (superseded by termios)
 * 500304 - remove IPSEC/FAST_IPSEC
 * 500305 - remove <sys/ioctl_compat.h> for good
 * 500306 - strsuftoll(), strsuftollx()
 * 500307 - tcsetsid()
 * 500308 - xdr_uint16_t()
 * 500309 - drop support for some ancient ioctls (OSIOCGIF*)
 * 500310 - remove more unimplemented ioctls
 * 500311 - add VIS_ALL to vis(3)
 * 500312 - OpenPAM Resedacea upgrade
 * 500313 - remove vmnet support from tap(4) (VMIO_* ioctls)
 * 500314 - add TAPGIFNAME to tap(4)
 * 500315 - add TUNGIFNAME to tun(4)
 * 500316 - add SIOC[ADG]IFGROUP, SIOCGIFGMEMB ioctl
 * 500317 - add wait6() and waitid() syscalls
 * 500400 - 5.4 release
 * 500500 - 5.5 development
 * 500501 - reallocarray() added to libc
 * 500502 - puffs etc. removed
 * 500503 - Lowered DATA rlimit supported by mmap(), libc sbrk() emulation
 *	    had to be rewritten.  libc brk() removed entirely.  These changes
 *	    are required to allow mmap hints to utilize lowered data rlimits.
 * 500504 - removed <sys/semaphore.h>, only <semaphore.h> remains
 * 500505 - rename <sys/termios.h> to <termios.h>
 * 500506 - LibreSSL, OpenSSH, XZ, libarchive update, libopie/libmd deprecation
 * 500600 - 5.6 release
 * 500700 - 5.7 development
 * 500701 - libopie/libmd removal
 * 500702 - TCP_KEEP* milliseconds -> seconds
 * 500703 - Static TLS bindings support for late-loaded shared libraries
 * 500704 - Announce IP6 address flag changes via route(4)
 * 500705 - Move us to utmpx only, delete utmp
 * 500706 - Switch to the now common three argument versions of the
 *	    timespecadd() and timespecsub() macros in <sys/time.h>
 *	    Also: RTM_VERSION bump to 7; CMSG alignment raised from 4B to 8B
 * 500707 - libradius/libtacplus removal
 * 500708 - Handle SIOCSIFMTU directly in tap(4) to support MTU > 1500
 * 500709 - Implement lwp_getname() and signal safety
 * 500710 - Implement getrandom() and __realpath() system calls
 * 500800 - 5.8 release
 * 500900 - 5.9 development
 * 500901 - fparseln() was moved from libutil to libc
 * 500902 - unlocked flavors of fflush(), fputc(), fputs(), fread(), fwrite()
 * 500903 - add SIOCGIFXMEDIA ioctl
 * 500904 - add IPPROTO_IP/IP_RECVTOS.
 * 500905 - add IPPROTO_IP/IP_SENDSRCADDR and IP_TOS control message.
 * 500906 - add struct ip_mreqn for IPPROTO_IP/IP_MULTICAST_IF,
 *          IP_ADD_MEMBERSHIP and IP_DROP_MEMBERSHIP.
 * 500907 - add clock_nanosleep()
 * 500908 - add fexecve() (also byteswap.h exists as of this vers)
 * 500909 - add AF_ARP
 * 600000 - 6.0 release
 * 600100 - 6.1 development
 * 600101 - kernel_{map,pmap,object} and {buffer,clean,pager}_map globals
 *          become pointers
 * 600102 - add nvmm(4) and libnvmm(3)
 * 600103 - remove the old vmm code
 * 600104 - add posix_fallocate()
 * 600105 - add fdatasync()
 * 600106 - msdosfs support in makefs(8)
 * 600107 - remove sys/gnu/vfs/ext2fs
 * 600200 - 6.2 release
 * 600300 - 6.3 development
 * 600301 - add strerror_l()
 * 600302 - change sysctl KERN_PROC behavior
 * 600400 - 6.4 release
 * 600500 - 6.5 development
 * 600501 - add fopencookie(3)
 * 600502 - WIFSIGNALED(x) excludes SIGCONT
 * 600503 - kldstat(2) supports module's full path
 * 600504 - change m_copyback(9) to forbid mbuf expansion
 * 600505 - add SO_USER_COOKIE to setsockopt(2)/getsockopt(2)
 * 600506 - add mbuf m_pkthdr.loop_cnt for loop detection
 * 600507 - remove mbuf m_pkthdr.loop_cnt and replace with
 *          if_tunnel_check_nesting()
 * 600508 - add ND6_IFF_AUTO_LINKLOCAL and ND6_IFF_NO_DAD;
 *          change ip6.accept_rtadv and ip6.auto_linklocal behavior
 * 600509 - add futimesat() (legacy function), add HAVE_FUTIMESAT
 * 600510 - add PROC_REAP_KILL to procctl(2)
 * 600511 - remove <timers.h>
 * 600512 - remove obsolete DSO_COMPATMBR from <sys/disk.h>
 * 600513 - add F_MAXFD and support POSIX O_CLOFORK
 */
#undef __DragonFly_version
#define __DragonFly_version 600513	/* propagated to newvers */

#include <sys/_null.h>

#ifndef LOCORE
#include <sys/types.h>
#endif

/*
 * Machine-independent constants (some used in following include files).
 * Redefined constants are from POSIX 1003.1 limits file.
 *
 * MAXCOMLEN should be >= sizeof(ac_comm) (see <acct.h>)
 */
#include <sys/syslimits.h>

#define MAXCOMLEN	16		/* max command name remembered */
#define MAXINTERP	32		/* max interpreter file name length */
#define MAXLOGNAME	33		/* max login name length (incl. NUL) */
#define MAXUPRC		CHILD_MAX	/* max simultaneous processes */
#define NCARGS		ARG_MAX		/* max bytes for an exec function */
#define NGROUPS		NGROUPS_MAX	/* max number groups */
#define NOFILE		OPEN_MAX	/* max open files per process */
#define NOGROUP		65535		/* marker for empty group set member */
#define MAXHOSTNAMELEN	256		/* max hostname size */
#define SPECNAMELEN	63		/* max length of devicename */

/* More types and definitions used throughout the kernel. */
#ifdef _KERNEL
#include <sys/cdefs.h>
#include <sys/errno.h>
#include <sys/time.h>

#define FALSE	0
#define TRUE	1
#endif

#ifndef _KERNEL
/* Signals. */
#include <sys/select.h>
#include <sys/signal.h>
#endif

/* Machine type dependent parameters. */
#include <machine/alignbytes.h>
#include <machine/param.h>
#ifndef _KERNEL
#include <machine/limits.h>
#endif

/*
 * WARNING! Max supported cpu's due to PWAKEUP_CPUMASK is 16384.
 */
#define PCATCH		0x00000100	/* tsleep checks signals */
#define PUSRFLAG1	0x00000200	/* Subsystem specific flag */
#define PINTERLOCKED	0x00000400	/* Interlocked tsleep */
#define PWAKEUP_CPUMASK	0x00003FFF	/* start cpu for chained wakeups */
#define PWAKEUP_MYCPU	0x00004000	/* wakeup on current cpu only */
#define PWAKEUP_ONE	0x00008000	/* argument to wakeup: only one */
#define PDOMAIN_MASK	0xFFFF0000	/* address domains for wakeup */
#define PDOMAIN_UMTX	0x00010000	/* independant domain for UMTX */
#define PDOMAIN_XLOCK	0x00020000	/* independant domain for fifo_lock */
#define PDOMAIN_FBSD0	0x01000000	/* freebsd sleepq queues */
#define PDOMAIN_FBSDINC	0x00010000	/* freebsd sleepq queue 1 */
#define PWAKEUP_ENCODE(domain, cpu)	((domain) | (cpu))
#define PWAKEUP_DECODE(domain)		((domain) & PWAKEUP_CPUMASK)

#define NZERO	0		/* default "nice" */

#define NBBY	8		/* number of bits in a byte */
#define NBPW	sizeof(int)	/* number of bytes per word (integer) */

#define CMASK	022		/* default file mask: S_IWGRP|S_IWOTH */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define NOUDEV	(dev_t)(-1)	/* non-existent device */
#define NOMAJ	256		/* non-existent device */
#endif

#ifndef _KERNEL
#define NODEV	(dev_t)(-1)	/* non-existent device */
#endif

/*
 * cpu_mi_feature bits
 */
#define CPU_MI_BZERONT	0x00000001
#define CPU_MI_MONITOR	0x00000010

/*
 * File system parameters and macros.
 *
 * MAXBSIZE -	Filesystems are made out of blocks of at most MAXBSIZE bytes
 *		per block.  MAXBSIZE may be made larger without effecting
 *		any existing filesystems as long as it does not exceed MAXPHYS,
 *		and may be made smaller at the risk of not being able to use
 *		filesystems which require a block size exceeding MAXBSIZE.
 *
 * NBUFCALCSIZE - Calculate sufficient buffer cache buffers for the memory
 *		desired as if each buffer were sized to this value (actual
 *		real memory use).  Hysteresis works both ways.
 */
#define MAXBSIZE	65536		/* must be power of 2 */
#define NBUFCALCSIZE	16384		/* for nbuf calculation only */

#define MAXFRAG 	8

/*
 * MAXPATHLEN defines the longest permissible path length after expanding
 * symbolic links. It is used to allocate a temporary buffer from the buffer
 * pool in which to do the name expansion, hence should be a power of two,
 * and must be less than or equal to MAXBSIZE.  MAXSYMLINKS defines the
 * maximum number of symbolic links that may be expanded in a path name.
 * It should be set high enough to allow all legitimate uses, but halt
 * infinite loops reasonably quickly.
 */
#define MAXPATHLEN	PATH_MAX
#define MAXSYMLINKS	32

/* Bit map related macros. */
#define setbit(a,i)	((a)[(i)/NBBY] |= 1<<((i)%NBBY))
#define clrbit(a,i)	((a)[(i)/NBBY] &= ~(1<<((i)%NBBY)))
#define isset(a,i)	((a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define isclr(a,i)	(((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)

/* Macros for counting and rounding. */
#ifndef howmany
#define howmany(x, y)	(((x)+((y)-1))/(y))
#endif
#define nitems(x)	NELEM(x)
#define rounddown(x, y)	(((x)/(y))*(y))
#define rounddown2(x, y) ((x) & ~((y) - 1))	   /* y power of two */
#define roundup(x, y)	((((x)+((y)-1))/(y))*(y))  /* to any y */
#define roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */
#define powerof2(x)	((((x)-1)&(x))==0)

/*
 * VM objects can be larger than the virtual address space, make sure
 * we don't cut-off the mask.
 */
#define trunc_page64(x)           ((x) & ~(int64_t)PAGE_MASK)
#define round_page64(x)           (((x) + PAGE_MASK) & ~(int64_t)PAGE_MASK)

/*
 * Round p (pointer or byte index) up to a correctly-aligned value
 * for all data types (int, long, ...).   The result is unsigned int
 * and must be cast to any desired pointer type.  Single underscore
 * versions are for FreeBSD/OpenBSD compat.
 */
#define _ALIGNBYTES	__ALIGNBYTES
#define _ALIGN(p)	__ALIGNPTR(p)
#ifndef ALIGNBYTES
#define ALIGNBYTES	__ALIGNBYTES
#endif
#ifndef ALIGN
#define ALIGN(p)	__ALIGNPTR(p)
#endif

/* Macros for min/max. */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* Macro for array size */
#define NELEM(ary) 	(sizeof(ary) / sizeof((ary)[0]))

/*
 * Constants for setting the parameters of the kernel memory allocator.
 *
 * 2 ** MINBUCKET is the smallest unit of memory that will be
 * allocated. It must be at least large enough to hold a pointer.
 *
 * Units of memory less or equal to MAXALLOCSAVE will permanently
 * allocate physical memory; requests for these size pieces of
 * memory are quite fast. Allocations greater than MAXALLOCSAVE must
 * always allocate and free physical memory; requests for these
 * size allocations should be done infrequently as they will be slow.
 *
 * Constraints: PAGE_SIZE <= MAXALLOCSAVE <= 2 ** (MINBUCKET + 14), and
 * MAXALLOCSIZE must be a power of two.
 */
#define MINBUCKET	4		/* 4 => min allocation of 16 bytes */
#define MAXALLOCSAVE	(2 * PAGE_SIZE)

/*
 * Scale factor for scaled integers used to count %cpu time and load avgs.
 *
 * The number of CPU `tick's that map to a unique `%age' can be expressed
 * by the formula (1 / (2 ^ (FSHIFT - 11))).  The maximum load average that
 * can be calculated (assuming 32 bits) can be closely approximated using
 * the formula (2 ^ (2 * (16 - FSHIFT))) for (FSHIFT < 15).
 *
 * For the scheduler to maintain a 1:1 mapping of CPU `tick' to `%age',
 * FSHIFT must be at least 11; this gives us a maximum load avg of ~1024.
 */
#define FSHIFT	11		/* bits to right of fixed binary point */
#define FSCALE	(1<<FSHIFT)

#define dbtoc(db)			/* calculates devblks to pages */ \
	((db + (ctodb(1) - 1)) >> (PAGE_SHIFT - DEV_BSHIFT))

#define ctodb(db)			/* calculates pages to devblks */ \
	((db) << (PAGE_SHIFT - DEV_BSHIFT))

#define MJUMPAGESIZE	PAGE_SIZE	/* jumbo cluster 4k */
#define MJUM9BYTES	(9 * 1024)	/* jumbo cluster 9k */
#define MJUM16BYTES	(16 * 1024)	/* jumbo cluster 16k */

/*
 * MBUF Management.
 *
 * Because many drivers can't deal with multiple DMA segments all mbufs
 * must avoid crossing a page boundary.  While we can accomodate mbufs
 * which are not a power-of-2 sized kmalloc() will only guarantee
 * non-crossing alignment if we use a power-of-2.  256 is no longer large
 * enough due to m_hdr and m_pkthdr bloat.
 *
 * MCLBYTES must be a power of 2 and is typically significantly larger
 * than MSIZE, sufficient to hold a standard-mtu packet.  2K is considered
 * reasonable.
 */
#ifndef MSIZE
#define MSIZE		512		/* size of an mbuf */
#endif

#ifndef MCLSHIFT
#define MCLSHIFT        11              /* convert bytes to m_buf clusters */
#endif
#define MCLBYTES        (1 << MCLSHIFT) /* size of an m_buf cluster */
#define MCLOFSET        (MCLBYTES - 1)  /* offset within an m_buf cluster */

/*
 * Make this available for most of the kernel.  There were too many
 * things that included sys/systm.h just for panic().
 */
#ifdef _KERNEL
void	panic (const char *, ...) __dead2 __printflike(1, 2);
#endif

#ifndef _BYTEORDER_FUNC_DEFINED
#define	_BYTEORDER_FUNC_DEFINED
#define	htonl(x)	__htonl(x)
#define	htons(x)	__htons(x)
#define	ntohl(x)	__ntohl(x)
#define	ntohs(x)	__ntohs(x)
#endif

/*
 * Access a variable length array that has been declared as a fixed
 * length array.
 */
#define	__PAST_END(array, offset) (((__typeof__(*(array)) *)(array))[offset])

#endif	/* _SYS_PARAM_H_ */
