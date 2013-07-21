/*-
 * Copyright (c) 1982, 1988, 1991, 1993
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
 *	@(#)systm.h	8.7 (Berkeley) 3/29/95
 * $FreeBSD: src/sys/sys/systm.h,v 1.111.2.18 2002/12/17 18:04:02 sam Exp $
 */

#ifndef _SYS_SYSTM_H_
#define	_SYS_SYSTM_H_

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#else

#ifndef _MACHINE_TYPES_H_
#include <machine/types.h>
#endif
#include <machine/stdarg.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <sys/callout.h>

extern int securelevel;		/* system security level (see init(8)) */
extern int kernel_mem_readonly;	/* disable writes to kernel memory */

extern int cold;		/* nonzero if we are doing a cold boot */
extern int tsleep_now_works;	/* tsleep won't just return any more */
extern const char *panicstr;	/* panic message */
extern int dumping;		/* system is dumping */
extern int safepri;		/* safe ipl when cold or panicing */
extern int osreldate;		/* System release date */
extern char version[];		/* system version */
extern char copyright[];	/* system copyright */

extern int selwait;		/* select timeout address */

extern u_char curpriority;	/* priority of current process */

extern long physmem;		/* physical memory */

extern cdev_t dumpdev;		/* dump device */
extern u_int64_t dumplo64;	/* block number into dumpdev, start of dump */

extern cdev_t rootdev;		/* root device */
extern cdev_t rootdevs[2];	/* possible root devices */
extern char *rootdevnames[2];	/* names of possible root devices */
extern char *ZeroPage;

extern int boothowto;		/* reboot flags, from console subsystem */
extern int bootverbose;		/* nonzero to print verbose messages */

extern int maxusers;		/* system tune hint */

extern int ncpus;		/* total number of cpus (real, hyper, virtual)*/
extern int ncpus2;		/* ncpus rounded down to power of 2 */
extern int ncpus2_shift;	/* log base 2 of ncpus2 */
extern int ncpus2_mask;		/* ncpus2 - 1 */
extern int ncpus_fit;		/* round up to a power of 2 */
extern int ncpus_fit_mask;	/* ncpus_fit - 1 */
extern int clocks_running;	/* timing/timeout subsystem is operational */

/* XXX TGEN these don't belong here, they're MD on i386/x86_64 */
extern u_int cpu_feature;	/* CPUID_* features */
extern u_int cpu_feature2;	/* CPUID2_* features */
extern u_int cpu_mi_feature;	/* CPU_MI_XXX machine-nonspecific features */
extern cpumask_t usched_global_cpumask;

extern int nfs_diskless_valid;	/* NFS diskless params were obtained */
extern vm_paddr_t Maxmem;	/* Highest physical memory address in system */

#ifdef	INVARIANTS		/* The option is always available */
#define	KASSERT(exp,msg)	do { if (__predict_false(!(exp)))	\
					panic msg; } while (0)
#define KKASSERT(exp)		do { if (__predict_false(!(exp)))	  \
					panic("assertion \"%s\" failed "  \
					"in %s at %s:%u", #exp, __func__, \
					__FILE__, __LINE__); } while (0)
#define __debugvar
#else
#define	KASSERT(exp,msg)
#define	KKASSERT(exp)
#define __debugvar		__attribute__((__unused__))
#endif

#define	CTASSERT(x)		_CTASSERT(x, __LINE__)
#define	_CTASSERT(x, y)		__CTASSERT(x, y)
#define	__CTASSERT(x, y)	typedef char __assert ## y[(x) ? 1 : -1]

/*
 * General function declarations.
 */

struct intrframe;
struct spinlock;
struct lock;
struct mtx;
struct lwkt_serialize;
struct malloc_type;
struct proc;
struct xwait;
struct timeval;
struct tty;
struct uio;
struct globaldata;
struct thread;
struct trapframe;
struct user;
struct vmspace;
struct savetls;
struct krate;

void	Debugger (const char *msg);
void	print_backtrace(int count);
void	mi_gdinit (struct globaldata *gd, int cpu);
void	mi_proc0init(struct globaldata *gd, struct user *proc0paddr);
int	nullop (void);
int	seltrue (cdev_t dev, int which);
int	ureadc (int, struct uio *);
void	*hashinit (int count, struct malloc_type *type, u_long *hashmask);
void	hashdestroy(void *vhashtbl, struct malloc_type *type, u_long hashmask);
void	*phashinit (int count, struct malloc_type *type, u_long *nentries);
void	*hashinit_ext (int count, size_t size,
			struct malloc_type *type, u_long *hashmask);
void	*phashinit_ext (int count, size_t size,
			struct malloc_type *type, u_long *nentries);

int	cpu_sanitize_frame (struct trapframe *);
int	cpu_sanitize_tls (struct savetls *);
void	cpu_spinlock_contested(void);
void	cpu_halt (void);
void	cpu_reset (void);
void	cpu_boot (int);
void	cpu_rootconf (void);
void	cpu_vmspace_alloc(struct vmspace *);
void	cpu_vmspace_free(struct vmspace *);
void	cpu_vkernel_trap(struct trapframe *, int);
void	set_user_TLS(void);
void	set_vkernel_fp(struct trapframe *);
int	kvm_access_check(vm_offset_t, vm_offset_t, int);

vm_paddr_t kvtop(void *addr);

extern uint32_t crc32_tab[];
uint32_t crc32(const void *buf, size_t size);
uint32_t crc32_ext(const void *buf, size_t size, uint32_t ocrc);
uint32_t iscsi_crc32(const void *buf, size_t size);
uint32_t iscsi_crc32_ext(const void *buf, size_t size, uint32_t ocrc);
void	init_param1 (void);
void	init_param2 (int physpages);
void	tablefull (const char *);
int	kvcprintf (char const *, void (*)(int, void*), void *, int,
		      __va_list) __printflike(1, 0);
void	kvcreinitspin(void);
int	log (int, const char *, ...) __printflike(2, 3);
void	logwakeup (void);
void	log_console (struct uio *);
int	kprintf (const char *, ...) __printflike(1, 2);
void	kprintf0 (const char *, ...) __printflike(1, 2);
void	krateprintf (struct krate *, const char *, ...) __printflike(2, 3);
int	ksnprintf (char *, size_t, const char *, ...) __printflike(3, 4);
int	ksnrprintf (char *, size_t, int, const char *, ...) __printflike(4, 5);
int	ksprintf (char *buf, const char *, ...) __printflike(2, 3);
int	uprintf (const char *, ...) __printflike(1, 2);
int	kvprintf (const char *, __va_list) __printflike(1, 0);
int	kvsnprintf (char *, size_t, const char *,
			__va_list) __printflike(3, 0);
int	kvsnrprintf (char *, size_t, int, const char *,
			__va_list) __printflike(4, 0);
int	kvasnrprintf (char **, size_t, int, const char *,
			__va_list) __printflike(4, 0);
int     kvsprintf (char *buf, const char *,
			__va_list) __printflike(2, 0);
int	ttyprintf (struct tty *, const char *, ...) __printflike(2, 3);
void	hexdump (const void *ptr, int length, const char *hdr, int flags);
#define	HD_COLUMN_MASK	0xff
#define	HD_DELIM_MASK	0xff00
#define	HD_OMIT_COUNT	(1 << 16)
#define	HD_OMIT_HEX	(1 << 17)
#define	HD_OMIT_CHARS	(1 << 18)
int	ksscanf (const char *, char const *, ...) __scanflike(2, 3);
int	kvsscanf (const char *, char const *, __va_list) __scanflike(2, 0);
void	kvasfree(char **);

long	strtol (const char *, char **, int);
u_long	strtoul (const char *, char **, int);
quad_t	strtoq (const char *, char **, int);
u_quad_t strtouq (const char *, char **, int);

/*
 * note: some functions commonly used by device drivers may be passed
 * pointers to volatile storage, volatile set to avoid warnings.
 *
 * NOTE: bcopyb() - is a dumb byte-granular bcopy.  This routine is
 *		    explicitly not meant to be sophisticated.
 * NOTE: bcopyi() - is a dumb int-granular bcopy (len is still in bytes).
 *		    This routine is explicitly not meant to be sophisticated.
 */
void	bcopyb (const void *from, void *to, size_t len);
void	bcopyi (const void *from, void *to, size_t len);
void	bcopy (volatile const void *from, volatile void *to, size_t len);
void	ovbcopy (const void *from, void *to, size_t len);
void	bzero (volatile void *buf, size_t len);
void	bzeront (volatile void *buf, size_t len);
void	*memcpy (void *to, const void *from, size_t len);

int	copystr (const void *kfaddr, void *kdaddr, size_t len,
		size_t *lencopied);
int	copyinstr (const void *udaddr, void *kaddr, size_t len,
		size_t *lencopied);
int	copyin (const void *udaddr, void *kaddr, size_t len);
int	copyout (const void *kaddr, void *udaddr, size_t len);

int	fubyte (const void *base);
int	subyte (void *base, int byte);
long	fuword (const void *base);
int	suword (void *base, long word);
int	suword32 (void *base, int word);
int	fusword (void *base);
int	susword (void *base, int word);
u_long	casuword(volatile u_long *p, u_long oldval, u_long newval);

void	realitexpire (void *);
void	DELAY(int usec);
void	DRIVERSLEEP(int usec);

void	startprofclock (struct proc *);
void	stopprofclock (struct proc *);
void	setstatclockrate (int hzrate);

/*
 * Kernel environment support functions and sundry.
 */
char	*kgetenv (const char *name);
int	ksetenv(const char *name, const char *value);
int	kunsetenv(const char *name);
void	kfreeenv(char *env);
int	ktestenv(const char *name);
int	kgetenv_int (const char *name, int *data);
int	kgetenv_string (const char *name, char *data, int size);
int	kgetenv_ulong(const char *name, unsigned long *data);
int	kgetenv_quad (const char *name, quad_t *data);
int	kgetenv_long(const char *name, long *data);
extern char *kern_envp;

#ifdef APM_FIXUP_CALLTODO 
void	adjust_timeout_calltodo (struct timeval *time_change); 
#endif /* APM_FIXUP_CALLTODO */ 

#include <sys/libkern.h>

/* Initialize the world */
void	mi_startup (void);
void	nchinit (void);

/* Finalize the world. */
void	shutdown_nice (int);

/*
 * Kernel to clock driver interface.
 */
void	inittodr (time_t base);
void	resettodr (void);
void	startrtclock (void);

/* Timeouts */
typedef void timeout_t (void *);	/* timeout function type */

/* Interrupt management */

/* 
 * For the alpha arch, some of these functions are static __inline, and
 * the others should be.
 */
#if defined(__i386__) || defined(__x86_64__)
void		setdelayed (void);
void		setsoftcambio (void);
void		setsoftcamnet (void);
void		setsoftunused02 (void);
void		setsoftcrypto (void);
void		setsoftunused01 (void);
void		setsofttty (void);
void		setsoftvm (void);
void		setsofttq (void);
void		schedsofttty (void);
void		splz (void);
void		splz_check (void);
void		cpu_mmw_pause_int(int*, int);
void		cpu_mmw_pause_long(long*, long);
#endif /* __i386__ */

/*
 * Various callout lists.
 */

/* Exit callout list declarations. */
typedef void (*exitlist_fn) (struct thread *td);

int	at_exit (exitlist_fn function);
int	rm_at_exit (exitlist_fn function);

/* Fork callout list declarations. */
typedef void (*forklist_fn) (struct proc *parent, struct proc *child,
				 int flags);

int	at_fork (forklist_fn function);
int	rm_at_fork (forklist_fn function);

extern struct globaldata	*panic_cpu_gd;

/* 
 * Common `proc' functions are declared here so that proc.h can be included
 * less often.
 */
int	tsleep (const volatile void *, int, const char *, int);
int	ssleep (const volatile void *, struct spinlock *, int, const char *, int);
int	lksleep (const volatile void *, struct lock *, int, const char *, int);
int	mtxsleep (const volatile void *, struct mtx *, int, const char *, int);
int	zsleep(const volatile void *, struct lwkt_serialize *, int, const char *, int);
void	tsleep_interlock (const volatile void *, int);
void	tsleep_remove (struct thread *);
int	lwkt_sleep (const char *, int);
void	tstop (void);
void	wakeup (const volatile void *chan);
void	wakeup_one (const volatile void *chan);
void	wakeup_mycpu (const volatile void *chan);
void	wakeup_mycpu_one (const volatile void *chan);
void	wakeup_oncpu (struct globaldata *gd, const volatile void *chan);
void	wakeup_oncpu_one (struct globaldata *gd, const volatile void *chan);
void	wakeup_domain (const volatile void *chan, int domain);
void	wakeup_domain_one (const volatile void *chan, int domain);
void	wakeup_start_delayed(void);
void	wakeup_end_delayed(void);

/*
 * Common `cdev_t' stuff are declared here to avoid #include poisoning
 */

int major(cdev_t x);
int minor(cdev_t x);
udev_t dev2udev(cdev_t x);
cdev_t udev2dev(udev_t x, int b);
int uminor(udev_t dev);
int umajor(udev_t dev);
udev_t makeudev(int x, int y);

/*
 * Unit number allocation API. (kern/subr_unit.c)
 */
struct unrhdr;
struct unrhdr *new_unrhdr(int low, int high, struct lock *lock);
void delete_unrhdr(struct unrhdr *uh);
int alloc_unr(struct unrhdr *uh);
int alloc_unrl(struct unrhdr *uh);
void free_unr(struct unrhdr *uh, u_int item);

#endif	/* _KERNEL */
#endif /* !_SYS_SYSTM_H_ */
