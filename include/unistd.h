/*-
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)unistd.h	8.12 (Berkeley) 4/27/95
 * $FreeBSD: src/include/unistd.h,v 1.93 2009/03/14 19:11:08 das Exp $
 */

#ifndef _UNISTD_H_
#define	_UNISTD_H_

#include <sys/cdefs.h>
#include <sys/unistd.h>
#include <sys/_null.h>

#if __BSD_VISIBLE
#include <sys/types.h>
#else
#include <machine/stdint.h>
#endif

#ifndef _INTPTR_T_DECLARED
typedef	__intptr_t	intptr_t;
#define	_INTPTR_T_DECLARED
#endif

#ifndef _GID_T_DECLARED
typedef	__uint32_t	gid_t;		/* XXX __gid_t */
#define	_GID_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef	__off_t		off_t;
#define	_OFF_T_DECLARED
#endif

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;		/* _GCC_SIZE_T OK */
#define	_SIZE_T_DECLARED
#endif

#ifndef _SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#define	_SSIZE_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef	__uint32_t	uid_t;		/* XXX __uid_t */
#define	_UID_T_DECLARED
#endif

#if __XSI_VISIBLE && __XSI_VISIBLE < 700
#ifndef _USECONDS_T_DECLARED
typedef	__uint32_t	useconds_t;	/* microseconds (unsigned) */
#define	_USECONDS_T_DECLARED
#endif
#endif

#define	 STDIN_FILENO	0	/* standard input file descriptor */
#define	STDOUT_FILENO	1	/* standard output file descriptor */
#define	STDERR_FILENO	2	/* standard error file descriptor */

#if __XSI_VISIBLE || __POSIX_VISIBLE >= 200112
#define	F_ULOCK		0	/* unlock locked section */
#define	F_LOCK		1	/* lock a section for exclusive use */
#define	F_TLOCK		2	/* test and lock a section for exclusive use */
#define	F_TEST		3	/* test a section for locks by other procs */
#endif

/*
 * Extended API support (DragonFly specific)
 */
#if __BSD_VISIBLE

#define _HAVE_EXTEXIT		1
#define _HAVE_EXTPREAD		1
#define _HAVE_EXTPREADV		1
#define _HAVE_EXTPWRITE		1
#define _HAVE_EXTPWRITEV	1

#endif

/*
 * POSIX options and option groups we unconditionally do or don't
 * implement.  This list includes those options which are exclusively
 * implemented (or not) in user mode.  Please keep this list in
 * alphabetical order.
 *
 * Anything which is defined as zero below **must** have an
 * implementation for the corresponding sysconf() which is able to
 * determine conclusively whether or not the feature is supported.
 * Anything which is defined as other than -1 below **must** have
 * complete headers, types, and function declarations as specified by
 * the POSIX standard; however, if the relevant sysconf() function
 * returns -1, the functions may be stubbed out.
 */
#define	_POSIX_READER_WRITER_LOCKS	200112L	/* mandatory ([THR]) */
#define	_POSIX_REGEXP			1	/* mandatory */
#define	_POSIX_SHELL			1	/* mandatory */
#define	_POSIX_SPAWN			200112L	/* [SPN] */
#define	_POSIX_THREAD_ATTR_STACKADDR	200112L	/* [TSA] */
#define	_POSIX_THREAD_ATTR_STACKSIZE	200112L	/* [TSS] */
#define	_POSIX_THREAD_PRIO_INHERIT	200112L	/* [TPI] */
#define	_POSIX_THREAD_PRIO_PROTECT	200112L	/* [TPP] */
#define	_POSIX_THREAD_PRIORITY_SCHEDULING 200112L /* [TPS] */
#define	_POSIX_THREAD_PROCESS_SHARED	-1	/* [TSH] */
#define	_POSIX_THREAD_ROBUST_PRIO_INHERIT -1	/* [RPI] */
#define	_POSIX_THREAD_ROBUST_PRIO_PROTECT -1	/* [RPP] */
#define	_POSIX_THREAD_SAFE_FUNCTIONS	200112L	/* mandatory ([TSF]) */
#define	_POSIX_THREAD_SPORADIC_SERVER	-1	/* [TSP] */
#define	_POSIX_THREADS			200112L	/* mandatory ([THR]) */
#define	_POSIX_TRACE			-1	/* [TRC] obsolescent */
#define	_POSIX_TRACE_EVENT_FILTER	-1	/* [TEF] obsolescent */
#define	_POSIX_TRACE_INHERIT		-1	/* [TRI] obsolescent */
#define	_POSIX_TRACE_LOG		-1	/* [TRL] obsolescent */

#define	_POSIX2_C_BIND			200809L	/* mandatory */
#define	_POSIX2_C_DEV			200809L	/* [CD] */
#define	_POSIX2_CHAR_TERM		1
#define	_POSIX2_FORT_DEV		-1	/* [FD] obsolescent */
#define	_POSIX2_FORT_RUN		200809L	/* [FR] */
#define	_POSIX2_LOCALEDEF		200809L
#define	_POSIX2_PBS			-1	/* [BE] obsolescent */
#define	_POSIX2_PBS_ACCOUNTING		-1	/* [BE] obsolescent */
#define	_POSIX2_PBS_CHECKPOINT		-1	/* [BE] obsolescent */
#define	_POSIX2_PBS_LOCATE		-1	/* [BE] obsolescent */
#define	_POSIX2_PBS_MESSAGE		-1	/* [BE] obsolescent */
#define	_POSIX2_PBS_TRACK		-1	/* [BE] obsolescent */
#define	_POSIX2_SW_DEV			200809L	/* [SD] */
#define	_POSIX2_UPE			200809L	/* [UP] */

#define	_V6_ILP32_OFF32			-1
#define	_V6_ILP32_OFFBIG		0
#define	_V6_LP64_OFF64			0
#define	_V6_LPBIG_OFFBIG		-1

#define	_V7_ILP32_OFF32			-1
#define	_V7_ILP32_OFFBIG		0
#define	_V7_LP64_OFF64			0
#define	_V7_LPBIG_OFFBIG		-1

#if __XSI_VISIBLE
#define	_XOPEN_CRYPT			-1
#define	_XOPEN_ENH_I18N			1	/* mandatory */
#define	_XOPEN_LEGACY			-1	/* until _XOPEN_SOURCE < 700 */
#define	_XOPEN_REALTIME			-1	/* missing: [ML] and [SIO] */
#define	_XOPEN_REALTIME_THREADS		-1	/* missing: [RPI] and [RPP] */
#define	_XOPEN_UNIX			-1
#endif

/* Define the POSIX.2 version we target for compliance. */
#define	_POSIX2_VERSION		199212L

/*
 * POSIX-style system configuration variable accessors (for the
 * sysconf function).  The kernel does not directly implement the
 * sysconf() interface; rather, a C library stub translates references
 * to sysconf() into calls to sysctl() using a giant switch statement.
 * Those that are marked `user' are implemented entirely in the C
 * library and never query the kernel.  pathconf() is implemented
 * directly by the kernel so those are not defined here.
 */
#define	_SC_ARG_MAX		 1
#define	_SC_CHILD_MAX		 2
#define	_SC_CLK_TCK		 3
#define	_SC_NGROUPS_MAX		 4
#define	_SC_OPEN_MAX		 5
#define	_SC_JOB_CONTROL		 6
#define	_SC_SAVED_IDS		 7
#define	_SC_VERSION		 8
#define	_SC_BC_BASE_MAX		 9 /* user */
#define	_SC_BC_DIM_MAX		10 /* user */
#define	_SC_BC_SCALE_MAX	11 /* user */
#define	_SC_BC_STRING_MAX	12 /* user */
#define	_SC_COLL_WEIGHTS_MAX	13 /* user */
#define	_SC_EXPR_NEST_MAX	14 /* user */
#define	_SC_LINE_MAX		15 /* user */
#define	_SC_RE_DUP_MAX		16 /* user */
#define	_SC_2_VERSION		17 /* user */
#define	_SC_2_C_BIND		18 /* user */
#define	_SC_2_C_DEV		19 /* user */
#define	_SC_2_CHAR_TERM		20 /* user */
#define	_SC_2_FORT_DEV		21 /* user */
#define	_SC_2_FORT_RUN		22 /* user */
#define	_SC_2_LOCALEDEF		23 /* user */
#define	_SC_2_SW_DEV		24 /* user */
#define	_SC_2_UPE		25 /* user */
#define	_SC_STREAM_MAX		26 /* user */
#define	_SC_TZNAME_MAX		27 /* user */

#if __POSIX_VISIBLE >= 199309
#define	_SC_ASYNCHRONOUS_IO	28
#define	_SC_MAPPED_FILES	29
#define	_SC_MEMLOCK		30
#define	_SC_MEMLOCK_RANGE	31
#define	_SC_MEMORY_PROTECTION	32
#define	_SC_MESSAGE_PASSING	33
#define	_SC_PRIORITIZED_IO	34
#define	_SC_PRIORITY_SCHEDULING	35
#define	_SC_REALTIME_SIGNALS	36
#define	_SC_SEMAPHORES		37
#define	_SC_FSYNC		38
#define	_SC_SHARED_MEMORY_OBJECTS 39
#define	_SC_SYNCHRONIZED_IO	40
#define	_SC_TIMERS		41
#define	_SC_AIO_LISTIO_MAX	42
#define	_SC_AIO_MAX		43
#define	_SC_AIO_PRIO_DELTA_MAX	44
#define	_SC_DELAYTIMER_MAX	45
#define	_SC_MQ_OPEN_MAX		46
#define	_SC_PAGESIZE		47
#define	_SC_RTSIG_MAX		48
#define	_SC_SEM_NSEMS_MAX	49
#define	_SC_SEM_VALUE_MAX	50
#define	_SC_SIGQUEUE_MAX	51
#define	_SC_TIMER_MAX		52
#endif

#if __POSIX_VISIBLE >= 200112
#define	_SC_2_PBS		59 /* user */
#define	_SC_2_PBS_ACCOUNTING	60 /* user */
#define	_SC_2_PBS_CHECKPOINT	61 /* user */
#define	_SC_2_PBS_LOCATE	62 /* user */
#define	_SC_2_PBS_MESSAGE	63 /* user */
#define	_SC_2_PBS_TRACK		64 /* user */
#define	_SC_ADVISORY_INFO	65
#define	_SC_BARRIERS		66 /* user */
#define	_SC_CLOCK_SELECTION	67
#define	_SC_CPUTIME		68
#define	_SC_FILE_LOCKING	69
#define	_SC_GETGR_R_SIZE_MAX	70 /* user */
#define	_SC_GETPW_R_SIZE_MAX	71 /* user */
#define	_SC_HOST_NAME_MAX	72
#define	_SC_LOGIN_NAME_MAX	73
#define	_SC_MONOTONIC_CLOCK	74
#define	_SC_MQ_PRIO_MAX		75
#define	_SC_READER_WRITER_LOCKS	76 /* user */
#define	_SC_REGEXP		77 /* user */
#define	_SC_SHELL		78 /* user */
#define	_SC_SPAWN		79 /* user */
#define	_SC_SPIN_LOCKS		80 /* user */
#define	_SC_SPORADIC_SERVER	81
#define	_SC_THREAD_ATTR_STACKADDR 82 /* user */
#define	_SC_THREAD_ATTR_STACKSIZE 83 /* user */
#define	_SC_THREAD_CPUTIME	84 /* user */
#define	_SC_THREAD_DESTRUCTOR_ITERATIONS 85 /* user */
#define	_SC_THREAD_KEYS_MAX	86 /* user */
#define	_SC_THREAD_PRIO_INHERIT	87 /* user */
#define	_SC_THREAD_PRIO_PROTECT	88 /* user */
#define	_SC_THREAD_PRIORITY_SCHEDULING 89 /* user */
#define	_SC_THREAD_PROCESS_SHARED 90 /* user */
#define	_SC_THREAD_SAFE_FUNCTIONS 91 /* user */
#define	_SC_THREAD_SPORADIC_SERVER 92 /* user */
#define	_SC_THREAD_STACK_MIN	93 /* user */
#define	_SC_THREAD_THREADS_MAX	94 /* user */
#define	_SC_TIMEOUTS		95 /* user */
#define	_SC_THREADS		96 /* user */
#define	_SC_TRACE		97 /* user */
#define	_SC_TRACE_EVENT_FILTER	98 /* user */
#define	_SC_TRACE_INHERIT	99 /* user */
#define	_SC_TRACE_LOG		100 /* user */
#define	_SC_TTY_NAME_MAX	101 /* user */
#define	_SC_TYPED_MEMORY_OBJECTS 102
#define	_SC_V6_ILP32_OFF32	103 /* user */
#define	_SC_V6_ILP32_OFFBIG	104 /* user */
#define	_SC_V6_LP64_OFF64	105 /* user */
#define	_SC_V6_LPBIG_OFFBIG	106 /* user */
#define	_SC_IPV6		118
#define	_SC_RAW_SOCKETS		119
#define	_SC_SYMLOOP_MAX		120
#endif

#if __XSI_VISIBLE
#define	_SC_ATEXIT_MAX		107 /* user */
#define	_SC_IOV_MAX		56
#define	_SC_PAGE_SIZE		_SC_PAGESIZE
#define	_SC_XOPEN_CRYPT		108 /* user */
#define	_SC_XOPEN_ENH_I18N	109 /* user */
#define	_SC_XOPEN_LEGACY	110 /* user */
#define	_SC_XOPEN_REALTIME	111
#define	_SC_XOPEN_REALTIME_THREADS 112
#define	_SC_XOPEN_SHM		113
#define	_SC_XOPEN_STREAMS	114
#define	_SC_XOPEN_UNIX		115
#define	_SC_XOPEN_VERSION	116
#define	_SC_XOPEN_XCU_VERSION	117 /* user */
#endif

#if __BSD_VISIBLE
#define	_SC_NPROCESSORS_CONF	57
#define	_SC_NPROCESSORS_ONLN	58
#define	_SC_LEVEL1_DCACHE_LINESIZE 128
#endif

/* Extensions found in Solaris and Linux. */
#define	_SC_PHYS_PAGES		121

#if __POSIX_VISIBLE >= 200809
#define	_SC_V7_ILP32_OFF32	122 /* user */
#define	_SC_V7_ILP32_OFFBIG	123 /* user */
#define	_SC_V7_LP64_OFF64	124 /* user */
#define	_SC_V7_LPBIG_OFFBIG	125 /* user */
#define	_SC_THREAD_ROBUST_PRIO_INHERIT 126 /* user */
#define	_SC_THREAD_ROBUST_PRIO_PROTECT 127 /* user */
#endif

/* Keys for the confstr(3) function. */
#if __POSIX_VISIBLE >= 199209
#define	_CS_PATH		1	/* default value of PATH */
#endif

#if __POSIX_VISIBLE >= 200112
#define	_CS_POSIX_V6_ILP32_OFF32_CFLAGS		2
#define	_CS_POSIX_V6_ILP32_OFF32_LDFLAGS	3
#define	_CS_POSIX_V6_ILP32_OFF32_LIBS		4
#define	_CS_POSIX_V6_ILP32_OFFBIG_CFLAGS	5
#define	_CS_POSIX_V6_ILP32_OFFBIG_LDFLAGS	6
#define	_CS_POSIX_V6_ILP32_OFFBIG_LIBS		7
#define	_CS_POSIX_V6_LP64_OFF64_CFLAGS		8
#define	_CS_POSIX_V6_LP64_OFF64_LDFLAGS		9
#define	_CS_POSIX_V6_LP64_OFF64_LIBS		10
#define	_CS_POSIX_V6_LPBIG_OFFBIG_CFLAGS	11
#define	_CS_POSIX_V6_LPBIG_OFFBIG_LDFLAGS	12
#define	_CS_POSIX_V6_LPBIG_OFFBIG_LIBS		13
#define	_CS_POSIX_V6_WIDTH_RESTRICTED_ENVS	14
#endif

#if __POSIX_VISIBLE >= 200809
#define	_CS_POSIX_V7_ILP32_OFF32_CFLAGS		15
#define	_CS_POSIX_V7_ILP32_OFF32_LDFLAGS	16
#define	_CS_POSIX_V7_ILP32_OFF32_LIBS		17
#define	_CS_POSIX_V7_ILP32_OFFBIG_CFLAGS	18
#define	_CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS	19
#define	_CS_POSIX_V7_ILP32_OFFBIG_LIBS		20
#define	_CS_POSIX_V7_LP64_OFF64_CFLAGS		21
#define	_CS_POSIX_V7_LP64_OFF64_LDFLAGS		22
#define	_CS_POSIX_V7_LP64_OFF64_LIBS		23
#define	_CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS	24
#define	_CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS	25
#define	_CS_POSIX_V7_LPBIG_OFFBIG_LIBS		26
#define	_CS_POSIX_V7_WIDTH_RESTRICTED_ENVS	27
#define	_CS_V6_ENV				28
#define	_CS_V7_ENV				29
#endif

__BEGIN_DECLS
/* 1003.1-1990 */
void	 _exit(int) __dead2;
int	 access(const char *, int);
unsigned int	 alarm(unsigned int);
int	 chdir(const char *);
int	 chown(const char *, uid_t, gid_t);
int	 close(int);
int	 dup(int);
int	 dup2(int, int);
int	 execl(const char *, const char *, ...);
int	 execle(const char *, const char *, ...);
int	 execlp(const char *, const char *, ...);
int	 execv(const char *, char * const *);
int	 execve(const char *, char * const *, char * const *);
int	 execvp(const char *, char * const *);
pid_t	 fork(void);
long	 fpathconf(int, int);
char	*getcwd(char *, size_t);
gid_t	 getegid(void);
uid_t	 geteuid(void);
gid_t	 getgid(void);
int	 getgroups(int, gid_t []);
char	*getlogin(void);
pid_t	 getpgrp(void);
pid_t	 getpid(void);
pid_t	 getppid(void);
uid_t	 getuid(void);
int	 isatty(int);
int	 link(const char *, const char *);
off_t	 lseek(int, off_t, int);
long	 pathconf(const char *, int);
int	 pause(void);
int	 pipe(int *);
ssize_t	 read(int, void *, size_t);
int	 rmdir(const char *);
int	 setgid(gid_t);
int	 setpgid(pid_t, pid_t);
pid_t	 setsid(void);
int	 setuid(uid_t);
unsigned int	 sleep(unsigned int);
long	 sysconf(int);
pid_t	 tcgetpgrp(int);
int	 tcsetpgrp(int, pid_t);
char	*ttyname(int);
int	 ttyname_r(int, char *, size_t);
int	 unlink(const char *);
ssize_t	 write(int, const void *, size_t);

/* 1003.2-1992 */
#if __POSIX_VISIBLE >= 199209 || __XSI_VISIBLE
size_t	 confstr(int, char *, size_t);
#ifndef _GETOPT_DECLARED
#define	_GETOPT_DECLARED
int	 getopt(int, char * const [], const char *);

extern char *optarg;			/* getopt(3) external variables */
extern int optind, opterr, optopt;
#endif /* _GETOPT_DECLARED */
#endif

/* ISO/IEC 9945-1: 1996 */
#if __POSIX_VISIBLE >= 199506 || __XSI_VISIBLE
int	 fdatasync(int);
int	 fsync(int);

/*
 * ftruncate() was in the POSIX Realtime Extension (it's used for shared
 * memory), but truncate() was not.
 */
int	 ftruncate(int, off_t);
#endif

#if __POSIX_VISIBLE >= 199506
int	 getlogin_r(char *, size_t);
#endif

/* 1003.1-2001 */
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
int	 fchown(int, uid_t, gid_t);
ssize_t	 readlink(const char * __restrict, char * __restrict, size_t);
#endif
#if __POSIX_VISIBLE >= 200112
int	 gethostname(char *, size_t);
int	 setegid(gid_t);
int	 seteuid(uid_t);
#endif

/* 1003.1-2008 */
#if __POSIX_VISIBLE >= 200809 || __XSI_VISIBLE
int	 getsid(pid_t _pid);
int	 fchdir(int);
int	 getpgid(pid_t _pid);
int	 lchown(const char *, uid_t, gid_t);
#ifndef _MKDTEMP_DECLARED
char	*mkdtemp(char *);
#define	_MKDTEMP_DECLARED
#endif
#ifndef _MKSTEMP_DECLARED
int	 mkstemp(char *);
#define	_MKSTEMP_DECLARED
#endif
ssize_t	 pread(int, void *, size_t, off_t);
ssize_t	 pwrite(int, const void *, size_t, off_t);

/* See comment at ftruncate() above. */
int	 truncate(const char *, off_t);
#endif /* __POSIX_VISIBLE >= 200809 || __XSI_VISIBLE */

#if __POSIX_VISIBLE >= 200809
int	faccessat(int, const char *, int, int);
int	fchownat(int, const char *, uid_t, gid_t, int);
int	fexecve(int, char *const [], char *const []);
int	linkat(int, const char *, int, const char *, int);
ssize_t	readlinkat(int, const char * __restrict, char * __restrict, size_t);
int	symlinkat(const char *, int, const char *);
int	unlinkat(int, const char *, int);
#endif /* __POSIX_VISIBLE >= 200809 */

/*
 * symlink() was originally in POSIX.1a, which was withdrawn after
 * being overtaken by events (1003.1-2001).  It was in XPG4.2, and of
 * course has been in BSD since 4.2.
 */
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE >= 402
int	 symlink(const char *, const char *);
#endif

/* X/Open System Interfaces */
#if __XSI_VISIBLE
char	*crypt(const char *, const char *);
int	 encrypt(char *, int);
long	 gethostid(void);
int	 lockf(int, int, off_t);
int	 nice(int);
int	 setregid(gid_t, gid_t);
int	 setreuid(uid_t, uid_t);
void	 swab(const void * __restrict, void * __restrict, ssize_t);
void	 sync(void);
#endif /* __XSI_VISIBLE */

#if __BSD_VISIBLE || (__XSI_VISIBLE && __XSI_VISIBLE < 600)
int	 chroot(const char *);
int	 getdtablesize(void);
int	 getpagesize(void) __pure2;
char	*getpass(const char *);
void	*sbrk(intptr_t);
#endif

#if __BSD_VISIBLE || (__XSI_VISIBLE && __XSI_VISIBLE < 700)
char	*getwd(char *);			/* LEGACY (obsoleted by getcwd()) */
useconds_t
	 ualarm(useconds_t, useconds_t);
int	 usleep(useconds_t);
pid_t	 vfork(void) __returns_twice;
#endif

#if __BSD_VISIBLE
struct iovec;
int	 acct(const char *);
int	 async_daemon(void);
int	 chroot_kernel(const char *);
int	 closefrom(int);
const char *
	 crypt_get_format(void);
int	 crypt_set_format(const char *);
int	 dup3(int, int, int);
int	 eaccess(const char *, int);
void	 endusershell(void);
int	 exect(const char *, char * const *, char * const *);
int	 execvP(const char *, const char *, char * const *);
void	 extexit(int, int, void *);
ssize_t	 extpread(int, void *, size_t, int, off_t);
ssize_t	 extpreadv(int, const struct iovec *, int, int, off_t);
ssize_t	 extpwrite(int, const void *, size_t, int, off_t);
ssize_t	 extpwritev(int, const struct iovec *, int, int, off_t);
char	*fflagstostr(unsigned long);
int	 getdomainname(char *, int);
int	 getentropy(void *, size_t);
int	 getgrouplist(const char *, gid_t, gid_t *, int *);
mode_t	 getmode(const void *, mode_t);
int	 getosreldate(void);
int	 getpeereid(int, uid_t *, gid_t *);
int	 getresgid(gid_t *, gid_t *, gid_t *);
int	 getresuid(uid_t *, uid_t *, uid_t *);
char	*getusershell(void);
int	 initgroups(const char *, gid_t);
int	 iruserok(unsigned long, int, const char *, const char *);
int	 iruserok_sa(const void *, int, int, const char *, const char *);
int	 issetugid(void);
long	 lpathconf(const char *, int);
#ifndef _LWP_GETTID_DECLARED
lwpid_t  lwp_gettid(void);
#define _LWP_GETTID_DECLARED
#endif
#ifndef _LWP_SETNAME_DECLARED
int	lwp_getname(lwpid_t, char *, size_t);
int	lwp_setname(lwpid_t, const char *);
#define _LWP_SETNAME_DECLARED
#endif
#ifndef	_MKNOD_DECLARED
int	 mknod(const char *, mode_t, dev_t);
#define	_MKNOD_DECLARED
#endif
int	 mkstemps(char *, int);
#ifndef _MKTEMP_DECLARED
char	*mktemp(char *);
#define	_MKTEMP_DECLARED
#endif
int	 nfssvc(int, void *);
int	 pipe2(int *, int);
int	 profil(char *, size_t, unsigned long, unsigned int);
int	 rcmd(char **, int, const char *, const char *, const char *, int *);
int	 rcmd_af(char **, int, const char *, const char *, const char *, int *,
		 int);
int	 rcmdsh(char **, int, const char *, const char *, const char *,
		const char *);
char	*re_comp(const char *);
int	 re_exec(const char *);
int	 reboot(int);
int	 revoke(const char *);
pid_t	 rfork(int);
pid_t	 rfork_thread(int, void *, int (*)(void *), void *);
int	 rresvport(int *);
int	 rresvport_af(int *, int);
int	 ruserok(const char *, int, const char *, const char *);
int	 setdomainname(const char *, int);
int	 setgroups(int, const gid_t *);
void	 sethostid(long);
int	 sethostname(const char *, int);
int	 setlogin(const char *);
void	*setmode(const char *);
int	 setpgrp(pid_t _pid, pid_t _pgrp); /* obsoleted by setpgid() */
void	 setproctitle(const char *_fmt, ...) __printf0like(1, 2);
int	 setresgid(gid_t, gid_t, gid_t);
int	 setresuid(uid_t, uid_t, uid_t);
int	 setrgid(gid_t);
int	 setruid(uid_t);
void	 setusershell(void);
int	 strtofflags(char **, unsigned long *, unsigned long *);
int	 swapoff(const char *);
int	 swapon(const char *);
int	 syscall(int, ...);
off_t	 __syscall(quad_t, ...);
int	 umtx_sleep(volatile const int *, int , int);
int	 umtx_wakeup(volatile const int *, int);
int	 undelete(const char *);
void	*valloc(size_t);			/* obsoleted by malloc() */
int	 varsym_get(int, const char *, char *, int);
int	 varsym_list(int, char *, int, int *);
int	 varsym_set(int, const char *, const char *);

#ifndef _OPTRESET_DECLARED
#define	_OPTRESET_DECLARED
extern int optreset;			/* getopt(3) external variable */
#endif
#endif /* __BSD_VISIBLE */
__END_DECLS

#endif /* !_UNISTD_H_ */
