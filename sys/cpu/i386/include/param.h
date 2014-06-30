/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	from: @(#)param.h	5.8 (Berkeley) 6/28/91
 * $FreeBSD: src/sys/i386/include/param.h,v 1.54.2.8 2002/08/31 21:15:55 dillon Exp $
 */

#ifndef _CPU_PARAM_H_

/*
 * Do not prevent re-includes of <machine/param.h> if the file was included
 * with NO_NAMESPACE_POLLUTION, or expected macros will not exist.
 */
#ifndef _NO_NAMESPACE_POLLUTION
#define _CPU_PARAM_H_
#endif

/*
 * Machine dependent constants for Intel 386.
 */
#ifndef _CPU_PARAM_H1_
#define _CPU_PARAM_H1_

/*
 * Round p (pointer or byte index) up to a correctly-aligned value
 * for all data types (int, long, ...).   The result is unsigned int
 * and must be cast to any desired pointer type.
 */
#ifndef _ALIGNBYTES
#define _ALIGNBYTES	(sizeof(int) - 1)
#endif
#ifndef _ALIGN
#define _ALIGN(p)	(((unsigned)(p) + _ALIGNBYTES) & ~_ALIGNBYTES)
#endif

#ifndef _MACHINE
#define	_MACHINE	i386	/* cpu architecture */
#endif
#ifndef _MACHINE_ARCH
#define	_MACHINE_ARCH	i386	/* cpu architecture */
#endif

#endif	/* _CPU_PARAM_H1_ */

#ifndef _NO_NAMESPACE_POLLUTION

#ifndef MACHINE
#define	MACHINE		"i386"	/* cpu architecture */
#endif
#ifndef MACHINE_ARCH
#define	MACHINE_ARCH	"i386"	/* cpu architecture */
#endif

#define MID_MACHINE	MID_I386

/*
 * Use SMP_MAXCPU instead of MAXCPU for structures that are intended to
 * remain compatible between UP and SMP builds.
 *
 * NOTE: Max supported cpus is 32 because we use 32 bit cpu masks.  On
 *	 32-bit systems the kernel build defaults SMP_MAXCPU to 16 to
 *	 reduce KVM overhead, but can be increased up to 32.
 */
#ifndef SMP_MAXCPU
#define SMP_MAXCPU	16
#endif
#define MAXCPU		SMP_MAXCPU

#define ALIGNBYTES	_ALIGNBYTES
#define ALIGN(p)	_ALIGN(p)

#define PAGE_SHIFT	12
#define PAGE_SIZE	(1<<PAGE_SHIFT)	/* bytes per mmu page (level 2) */
#define PAGE_MASK	(PAGE_SIZE-1)
#define NPTEPG		(PAGE_SIZE/(sizeof (pt_entry_t)))

#define SEG_SHIFT	22
#define SEG_SIZE	(1<<SEG_SHIFT)	/* bytes per mmu segment (level 1) */
#define SEG_MASK	(SEG_SIZE-1)

#define NPDEPG		(PAGE_SIZE/(sizeof (pd_entry_t)))
#define PDRSHIFT	22		/* LOG2(NBPDR) */
#define NBPDR		(1<<PDRSHIFT)	/* bytes/page dir */
#define PDRMASK		(NBPDR-1)

#define DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#define DEV_BSIZE	(1 << DEV_BSHIFT)
#define DEV_BMASK	(DEV_BSIZE - 1)

#ifndef BLKDEV_IOSIZE
#define BLKDEV_IOSIZE	PAGE_SIZE	/* default block device I/O size */
#endif
#define DFLTPHYS	(64 * 1024)	/* default max raw I/O transfer size */
#define MAXPHYS		(128 * 1024)	/* max raw I/O transfer size */
#define MAXDUMPPGS	(MAXPHYS/PAGE_SIZE)

#define IOPAGES		2	/* pages of i/o permission bitmap */
#define UPAGES		4	/* pages of u-area */

/*
 * 32-bit machines do not have enough KVA, improve buffer cache
 * density at the cost of higher defragmentation and buffer_map
 * handling overheads.
 */
#define BKVASIZE        16384	/* override 64K default */

/*
 * Ceiling on amount of swblock kva space, can be changed via
 * kern.maxswzone /boot/loader.conf variable.
 */
#ifndef VM_SWZONE_SIZE_MAX
#define VM_SWZONE_SIZE_MAX	(32 * 1024 * 1024)
#endif

/*
 * Ceiling on size of buffer cache (really only effects write queueing,
 * the VM page cache is not effected), can be changed via
 * kern.maxbcache /boot/loader.conf variable.
 */
#ifndef VM_BCACHE_SIZE_MAX
#define VM_BCACHE_SIZE_MAX	(200 * 1024 * 1024)
#endif

/*
 * Some macros for units conversion
 */

/* clicks to bytes */
#define ctob(x)	((x)<<PAGE_SHIFT)

/* bytes to clicks */
#define btoc(x)	(((unsigned)(x)+PAGE_MASK)>>PAGE_SHIFT)

/*
 * btodb() is messy and perhaps slow because `bytes' may be an off_t.  We
 * want to shift an unsigned type to avoid sign extension and we don't
 * want to widen `bytes' unnecessarily.  Assume that the result fits in
 * a daddr_t.
 */
#define btodb(bytes)	 		/* calculates (bytes / DEV_BSIZE) */ \
	(sizeof (bytes) > sizeof(long) \
	 ? (daddr_t)((unsigned long long)(bytes) >> DEV_BSHIFT) \
	 : (daddr_t)((unsigned long)(bytes) >> DEV_BSHIFT))

#define dbtob(db)			/* calculates (db * DEV_BSIZE) */ \
	((off_t)(db) << DEV_BSHIFT)

/*
 * Mach derived conversion macros
 */
#define trunc_page(x)		((x) & ~PAGE_MASK)
#define round_page(x)		(((x) + PAGE_MASK) & ~PAGE_MASK)
#define trunc_4mpage(x)		((x) & ~PDRMASK)
#define round_4mpage(x)		((((x)) + PDRMASK) & ~PDRMASK)

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define atop(x)			((x) >> PAGE_SHIFT)
#endif
#define ptoa(x)			((vm_paddr_t)(x) << PAGE_SHIFT)

#define i386_btop(x)		((vm_pindex_t)((x) >> PAGE_SHIFT))
#define i386_ptob(x)		((vm_paddr_t)(x) << PAGE_SHIFT)

#define	pgtok(x)		((x) * (PAGE_SIZE / 1024))

#ifdef _KERNEL

/*
 * We put here the definition of two debugging macros/function which
 * are very convenient to have available.
 * The macro is called TSTMP() and is used to timestamp events in the
 * kernel using the TSC register, and export them to userland through
 * the sysctl variable debug.timestamp, which is a circular buffer
 * holding pairs of u_int32_t variables <timestamp, argument> .
 * They can be retrieved with something like
 *
 *	sysctl -b debug.timestamp | hexdump -e '"%15u %15u\n"'
 *
 * The function _TSTMP() is defined in i386/isa/clock.c. It does not
 * try to grab any locks or block interrupts or identify which CPU it
 * is running on. You are supposed to know what to do if you use it.
 *
 * The macros must be enabled with "options KERN_TIMESTAMP" in the kernel
 * config file, otherwise they default to an empty block.
 */

#ifdef KERN_TIMESTAMP
extern void _TSTMP(u_int32_t argument);
#define TSTMP(class, unit, event, par)	_TSTMP(	\
	(((class) &   0x0f) << 28 ) |		\
	(((unit)  &   0x0f) << 24 ) |		\
	(((event) &   0xff) << 16 ) |		\
	(((par)   & 0xffff)       )   )

#else /* !KERN_TIMESTAMP */
#define        _TSTMP(x)                       {}
#define        TSTMP(class, unit, event, par)  _TSTMP(0)
#endif /* !KERN_TIMESTAMP */
#endif /* _KERNEL */

#endif /* !_NO_NAMESPACE_POLLUTION */
#endif /* !_CPU_PARAM_H_ */
