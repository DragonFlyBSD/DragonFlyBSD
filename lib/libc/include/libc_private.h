/*
 * Copyright (c) 1998 John Birrell <jb@cimlogic.com.au>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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
 * $FreeBSD: head/lib/libc/include/libc_private.h 213153 2010-09-25 01:57:47Z davidxu $
 *
 * Private definitions for libc, libc_r and libpthread.
 *
 */

#ifndef _LIBC_PRIVATE_H_
#define _LIBC_PRIVATE_H_

#include <sys/_pthreadtypes.h>

/*
 * This global flag is non-zero when a process has created one
 * or more threads. It is used to avoid calling locking functions
 * when they are not required.
 */
extern int	__isthreaded;

/*
 * File lock contention is difficult to diagnose without knowing
 * where locks were set. Allow a debug library to be built which
 * records the source file and line number of each lock call.
 */
#ifdef	_FLOCK_DEBUG
#define _FLOCKFILE(x)	_flockfile_debug(x, __FILE__, __LINE__)
#else
#define _FLOCKFILE(x)	_flockfile(x)
#endif

/*
 * Macros for locking and unlocking FILEs. These test if the
 * process is threaded to avoid locking when not required.
 */
#define	FLOCKFILE(fp)		if (__isthreaded) _FLOCKFILE(fp)
#define	FUNLOCKFILE(fp)		if (__isthreaded) _funlockfile(fp)

/*
 * Initialise TLS static programs
 */
int __get_errno_GS_offset(void);
void *__get_errno_GOT_ptr(void);

struct statfs;
struct statvfs;

/*
 * yplib internal interfaces
 */
#ifdef YP
int _yp_check(char **);
#endif

/*
 * This is a pointer in the C run-time startup code. It is used
 * by getprogname() and setprogname().
 */
const char *__progname;

/*
 * Function to clean up streams, called from abort() and exit().
 */
extern void (*__cleanup)(void);

/* execve() with PATH processing to implement posix_spawnp() */
int _execvpe(const char *, char * const *, char * const *);
void _nmalloc_thr_init(void);

struct dl_phdr_info;
int __elf_phdr_match_addr(struct dl_phdr_info *, void *);

/*
 * libc should use libc_dlopen internally, which respects a global
 * flag where loading of new shared objects can be restricted.
 */
void *libc_dlopen(const char *, int);

/*
 * For dynamic linker
 */
void _rtld_error(const char *fmt, ...);

/*
 * Provides pthread_once()-like functionality for both single-threaded
 * and multi-threaded applications
 */
int _once(pthread_once_t *, void (*)(void));

#endif /* _LIBC_PRIVATE_H_ */
