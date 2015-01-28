/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)mman.h	8.2 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/sys/mman.h,v 1.29.2.1 2001/08/25 07:25:43 dillon Exp $
 */

#ifndef _SYS_MMAN_H_
#define	_SYS_MMAN_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif

#if __BSD_VISIBLE
/*
 * Inheritance for minherit()
 */
#define	INHERIT_SHARE  0
#define	INHERIT_COPY   1
#define	INHERIT_NONE   2
#endif

/*
 * Protections are chosen from these bits, or-ed together
 */
#define	PROT_NONE	0x00	/* no permissions */
#define	PROT_READ	0x01	/* pages can be read */
#define	PROT_WRITE	0x02	/* pages can be written */
#define	PROT_EXEC	0x04	/* pages can be executed */

/*
 * Flags contain sharing type and options.
 * Sharing types; choose one.
 */
#define	MAP_SHARED	0x0001		/* share changes */
#define	MAP_PRIVATE	0x0002		/* changes are private */
#if __BSD_VISIBLE
#define	MAP_COPY	MAP_PRIVATE	/* Obsolete */
#endif

/*
 * Other flags
 */
#define	MAP_FIXED	 0x0010	/* map addr must be exactly as requested */
#if __BSD_VISIBLE
#define	MAP_RENAME	 0x0020	/* Sun: rename private pages to file */
#define	MAP_NORESERVE	 0x0040	/* Sun: don't reserve needed swap area */
#define	MAP_INHERIT	 0x0080	/* region is retained after exec */
#define	MAP_NOEXTEND	 0x0100	/* for MAP_FILE, don't change file size */
#define	MAP_HASSEMAPHORE 0x0200	/* region may contain semaphores */
#define	MAP_STACK	 0x0400	/* region grows down, like a stack */
#define	MAP_NOSYNC	 0x0800 /* page to but do not sync underlying file */

/*
 * Mapping type
 */
#define	MAP_FILE	0x0000		/* map from file (default) */
#define	MAP_ANON	0x1000		/* allocated from memory, swap space */
#define	MAP_ANONYMOUS	MAP_ANON	/* alias for compatibility */
#define	MAP_VPAGETABLE	0x2000		/* manage with virtualized page table */

/*
 * Extended flags
 */
#define	MAP_TRYFIXED	0x00010000 /* attempt hint address, even within heap */
#define	MAP_NOCORE	0x00020000 /* dont include these pages in a coredump */
#define	MAP_SIZEALIGN	0x00040000 /* size is also an alignment requirement */
#endif /* __BSD_VISIBLE */

#if __POSIX_VISIBLE >= 199309
/*
 * Process memory locking
 */
#define	MCL_CURRENT	0x0001	/* Lock only current memory */
#define	MCL_FUTURE	0x0002	/* Lock all future memory as well */
#endif /* __POSIX_VISIBLE >= 199309 */

/*
 * Error return from mmap()
 */
#define	MAP_FAILED	((void *)-1)

/*
 * msync() flags
 */
#define	MS_SYNC		0x0000	/* msync synchronously */
#define	MS_ASYNC	0x0001	/* return immediately */
#define	MS_INVALIDATE	0x0002	/* invalidate all cached data */

/*
 * Advice to madvise
 */
#define	_MADV_NORMAL	0	/* no further special treatment */
#define	_MADV_RANDOM	1	/* expect random page references */
#define	_MADV_SEQUENTIAL 2	/* expect sequential page references */
#define	_MADV_WILLNEED	3	/* will need these pages */
#define	_MADV_DONTNEED	4	/* dont need these pages */

#if __BSD_VISIBLE
#define	MADV_NORMAL	_MADV_NORMAL
#define	MADV_RANDOM	_MADV_RANDOM
#define	MADV_SEQUENTIAL	_MADV_SEQUENTIAL
#define	MADV_WILLNEED	_MADV_WILLNEED
#define	MADV_DONTNEED	_MADV_DONTNEED
#define	MADV_FREE	5	/* dont need these pages, and junk contents */
#define	MADV_NOSYNC	6	/* try to avoid flushes to physical media */
#define	MADV_AUTOSYNC	7	/* revert to default flushing strategy */
#define	MADV_NOCORE	8	/* do not include these pages in a core file */
#define	MADV_CORE	9	/* revert to including pages in a core file */
#define	MADV_INVAL	10	/* virt page tables have changed, inval pmap */
#define	MADV_SETMAP	11	/* set page table directory page for map */
#endif

/*
 * Advice to posix_madvise()
 */
#if __POSIX_VISIBLE >= 200112
#define	POSIX_MADV_NORMAL	_MADV_NORMAL
#define	POSIX_MADV_RANDOM	_MADV_RANDOM
#define	POSIX_MADV_SEQUENTIAL	_MADV_SEQUENTIAL
#define	POSIX_MADV_WILLNEED	_MADV_WILLNEED
#define	POSIX_MADV_DONTNEED	_MADV_DONTNEED
#endif

#if __BSD_VISIBLE
/*
 * mcontrol() must be used for these functions instead of madvise()
 */
#define	MADV_CONTROL_START	MADV_INVAL
#define	MADV_CONTROL_END	MADV_SETMAP

/*
 * Return bits from mincore
 */
#define	MINCORE_INCORE	 	 0x1 /* Page is incore */
#define	MINCORE_REFERENCED	 0x2 /* Page has been referenced by us */
#define	MINCORE_MODIFIED	 0x4 /* Page has been modified by us */
#define	MINCORE_REFERENCED_OTHER 0x8 /* Page has been referenced */
#define	MINCORE_MODIFIED_OTHER	0x10 /* Page has been modified */
#define	MINCORE_SUPER		0x20 /* Page is a "super" page */
#endif

__BEGIN_DECLS
#if __POSIX_VISIBLE >= 199309
int	mlockall(int);
int	munlockall(void);
int	shm_open(const char *, int, mode_t);
int	shm_unlink(const char *);
#endif /* __POSIX_VISIBLE >= 199309 */
int	mlock(const void *, size_t);
#ifndef _MMAP_DECLARED
#define	_MMAP_DECLARED
void *	mmap(void *, size_t, int, int, int, off_t);
#endif
int	mprotect(void *, size_t, int);
int	msync(void *, size_t, int);
int	munlock(const void *, size_t);
int	munmap(void *, size_t);
#if __POSIX_VISIBLE >= 200112
int	posix_madvise(void *, size_t, int);
#endif
#if __BSD_VISIBLE
int	madvise(void *, size_t, int);
int	mcontrol(void *, size_t, int, off_t);
int	mincore(const void *, size_t, char *);
int	minherit(void *, size_t, int);
#endif
__END_DECLS

#endif
