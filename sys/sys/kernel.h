/*-
 * Copyright (c) 1995 Terrence R. Lambert
 * All rights reserved.
 *
 * Copyright (c) 1990, 1993
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
 *	@(#)kernel.h	8.3 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/sys/kernel.h,v 1.63.2.9 2002/07/02 23:00:30 archie Exp $
 */

#ifndef _SYS_KERNEL_H_
#define _SYS_KERNEL_H_

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#else

#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_LINKER_SET_H_
#include <sys/linker_set.h>
#endif

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

/* Global variables for the kernel. */

/* 1.1 */
extern unsigned long hostid;
extern char hostname[MAXHOSTNAMELEN];
extern int hostnamelen;
extern char domainname[MAXHOSTNAMELEN];
extern int domainnamelen;
extern char kernelname[MAXPATHLEN];

/* 1.2 */
extern struct timespec boottime;

extern struct timezone tz;			/* XXX */

extern int ustick;			/* usec per tick (1000000 / hz) */
extern int nstick;			/* nsec per tick (1000000000 / hz) */
extern int tickadj;			/* "standard" clock skew, us./tick */
extern int hz;				/* system clock's frequency */
extern int psratio;			/* ratio: prof / stat */
extern int stathz;			/* statistics clock's frequency */
extern int profhz;			/* profiling clock's frequency */
extern int ticks;
extern int lbolt;			/* once a second sleep address */
extern void *lbolt_syncer;		/* approx 1 hz but may be sped up */
extern enum vmm_guest_type vmm_guest; 	/* Running as virtual machine guest? */

enum vmm_guest_type {
	VMM_GUEST_NONE = 0,
	VMM_GUEST_QEMU,
	VMM_GUEST_PLEX86,
	VMM_GUEST_BOCHS,
	VMM_GUEST_XEN,
	VMM_GUEST_BHYVE,
	VMM_GUEST_KVM,
	VMM_GUEST_VMWARE,
	VMM_GUEST_VPC,
	VMM_GUEST_VBOX,
	VMM_GUEST_PARALLELS,
	VMM_GUEST_VKERNEL,
	VMM_GUEST_UNKNOWN,
	VMM_GUEST_LAST
};

/*
 * Enumerated types for known system startup interfaces.
 *
 * Startup occurs in ascending numeric order; the list entries are
 * sorted prior to attempting startup to guarantee order.  Items
 * of the same level are arbitrated for order based on the 'order'
 * element.
 *
 * These numbers are arbitrary and are chosen ONLY for ordering; the
 * enumeration values are explicit rather than implicit to provide
 * for binary compatibility with inserted elements.
 *
 * The SI_SUB_RUN_SCHEDULER value must have the highest lexical value.
 */
enum sysinit_sub_id {
	/*
	 * Special cased
	 */
	SI_SPECIAL_DUMMY	= 0x0000000,	/* not executed; for linker*/
	SI_SPECIAL_DONE		= 0x0000001,	/* flag sysinit completion */

	/*
	 * Memory management subsystems.
	 */
	SI_BOOT1_TUNABLES	= 0x0700000,	/* establish tunable values */
	SI_BOOT1_COPYRIGHT	= 0x0800000,	/* first use of console*/
	SI_BOOT1_LOCK		= 0x0900000,	/* lockmgr locks and tokens */
	SI_BOOT1_VM		= 0x1000000,	/* virtual memory system init*/
	SI_BOOT1_ALLOCATOR	= 0x1400000,	/* slab allocator */
	SI_BOOT1_KMALLOC	= 0x1600000,	/* kmalloc inits */
	SI_BOOT1_POST		= 0x1800000,	/* post boot1 inits */

	/*
	 * Fickle ordering.  objcache and softclock need to know what
	 * ncpus is to initialize properly, clocks (e.g. hardclock)
	 * need softclock to work, and we can't finish initializing
	 * the APs until the system clock has been initialized.
	 * Also, clock registration and smp configuration registration
	 * must occur before SMP.  Messy messy.
	 */
	SI_BOOT2_LEAVE_CRIT	= 0x1900000,
	SI_BOOT2_PRESMP		= 0x1a00000,	/* register SMP configs */
	SI_BOOT2_START_CPU	= 0x1a40000,	/* start CPU (BSP) */
	SI_BOOT2_LAPIC		= 0x1a50000,	/* configure Local APIC */
	SI_BOOT2_START_APS	= 0x1a60000,	/* start all APs */
	SI_BOOT2_IOAPIC		= 0x1a70000,	/* configure I/O APIC */
	SI_BOOT2_FINISH_PIC	= 0x1a80000,	/* finish PIC configure */
	SI_BOOT2_FINISH_CPU	= 0x1a90000,	/* finish CPU startup */
	SI_BOOT2_CLOCKREG	= 0x1ac0000,	/* register available clocks */
	SI_BOOT2_OBJCACHE	= 0x1b00000,
	SI_BOOT2_SOFTCLOCK	= 0x1b80000,
	SI_BOOT2_CLOCKS		= 0x1c00000,	/* select & start clocks */
	SI_BOOT2_FINISH_SMP	= 0x1c80000,	/* SMP go (& synch clocks) */
	SI_BOOT2_POST_SMP	= 0x1cc0000,	/* post-SMP low level */

	/*
	 * Finish up core kernel initialization and set up the process
	 * abstraction.
	 */
	SI_BOOT2_BIOS		= 0x1d00000,
	SI_BOOT2_MACHDEP	= 0x1d80000,
	SI_BOOT2_KLD		= 0x1e00000,
	SI_BOOT2_CPU_TOPOLOGY	= 0x1e40000,
	SI_BOOT2_USCHED		= 0x1e80000,
	SI_BOOT2_PROC0		= 0x1f00000,

	/*
	 * Continue with miscellanious system initialization
	 */
	SI_SUB_CREATE_INIT	= 0x2300000,	/* create the init process */
	SI_SUB_PRE_DRIVERS	= 0x2380000,
	SI_SUB_DRIVERS		= 0x2400000,	/* Let Drivers initialize */
	SI_SUB_CONFIGURE	= 0x3800000,	/* Configure devices */
	SI_SUB_ISWARM		= 0x3c00000,	/* No longer in cold boot */
	SI_SUB_VFS		= 0x4000000,	/* virtual file system*/
	SI_SUB_HELPER_THREADS	= 0x5400000,	/* misc helper threads */
	SI_SUB_CLIST		= 0x5800000,	/* clists*/
	SI_SUB_SYSV_SHM		= 0x6400000,	/* System V shared memory*/
	SI_SUB_SYSV_SEM		= 0x6800000,	/* System V semaphores*/
	SI_SUB_SYSV_MSG		= 0x6C00000,	/* System V message queues*/
	SI_SUB_P1003_1B		= 0x6E00000,	/* P1003.1B realtime */
	SI_SUB_PSEUDO		= 0x7000000,	/* pseudo devices*/
	SI_SUB_EXEC		= 0x7400000,	/* execve() handlers */
	SI_SUB_PROTO_IF		= 0x8400000,	/* interfaces */
	SI_SUB_PROTO_DOMAIN	= 0x8800000,	/* domains (address families?)*/
	SI_SUB_PROTO_IFATTACHDOMAIN	
				= 0x8800001,	/* domain dependent data init */
	SI_SUB_PROTO_END	= 0x8ffffff,	/* network protocol post-init */
	SI_SUB_KPROF		= 0x9000000,	/* kernel profiling*/
	SI_SUB_KICK_SCHEDULER	= 0xa000000,	/* start the timeout events*/
	SI_SUB_INT_CONFIG_HOOKS	= 0xa800000,	/* Interrupts enabled config */

	/*
	 * Root filesystem setup, finish up with the major system
	 * demons.
	 */
	SI_SUB_ROOT_CONF	= 0xb000000,	/* Find root devices */
	SI_SUB_DUMP_CONF	= 0xb200000,	/* Find dump devices */
	SI_SUB_RAID		= 0xb300000,	/* Configure vinum */
	SI_SUB_DM_TARGETS	= 0xb3a0000,	/* configure dm targets */
	SI_SUB_MOUNT_ROOT	= 0xb400000,	/* root mount*/
	SI_SUB_PROC0_POST	= 0xd000000,	/* proc 0 cleanup*/
	SI_SUB_KTHREAD_INIT	= 0xe000000,	/* init process*/
	SI_SUB_KTHREAD_PAGE	= 0xe400000,	/* pageout daemon*/
	SI_SUB_KTHREAD_VM	= 0xe800000,	/* vm daemon*/
	SI_SUB_KTHREAD_BUF	= 0xea00000,	/* buffer daemon*/
	SI_SUB_KTHREAD_UPDATE	= 0xec00000,	/* update daemon*/
	SI_SUB_KTHREAD_IDLE	= 0xee00000,	/* idle procs*/
	SI_SUB_RUN_SCHEDULER	= 0xfffffff	/* scheduler: no return*/
};


/*
 * Some enumerated orders; "ANY" sorts last.
 */
enum sysinit_elem_order {
	SI_ORDER_FIRST		= 0x0000000,	/* first*/
	SI_ORDER_SECOND		= 0x0000001,	/* second*/
	SI_ORDER_THIRD		= 0x0000002,	/* third*/
	SI_ORDER_MIDDLE		= 0x1000000,	/* somewhere in the middle */
	SI_ORDER_ANY		= 0xfffffff	/* last*/
};


/*
 * A system initialization call instance
 *
 * At the moment there is one instance of sysinit.  We probably do not
 * want two which is why this code is if'd out, but we definitely want
 * to discern SYSINIT's which take non-constant data pointers and
 * SYSINIT's which take constant data pointers,
 *
 * The C_* macros take functions expecting const void * arguments 
 * while the non-C_* macros take functions expecting just void * arguments.
 *
 * With -Wcast-qual on, the compiler issues warnings:
 *	- if we pass non-const data or functions taking non-const data
 *	  to a C_* macro.
 *
 *	- if we pass const data to the normal macros
 *
 * However, no warning is issued if we pass a function taking const data
 * through a normal non-const macro.  This is ok because the function is
 * saying it won't modify the data so we don't care whether the data is
 * modifiable or not.
 */

typedef void (*sysinit_nfunc_t) (void *);
typedef void (*sysinit_cfunc_t) (const void *);

struct sysinit {
	unsigned int	subsystem;		/* subsystem identifier*/
	unsigned int	order;			/* init order within subsystem*/
	sysinit_cfunc_t func;			/* function		*/
	const void	*udata;			/* multiplexer/argument */
	const char	*name;
};

/*
 * Default: no special processing
 *
 * The C_ version of SYSINIT is for data pointers to const
 * data ( and functions taking data pointers to const data ).
 * At the moment it is no different from SYSINIT and thus
 * still results in warnings.
 *
 * The casts are necessary to have the compiler produce the
 * correct warnings when -Wcast-qual is used.
 *
 */
#define	C_SYSINIT(uniquifier, subsystem, order, func, ident)	\
	static struct sysinit uniquifier ## _sys_init = {	\
		subsystem,					\
		order,						\
		func,						\
		ident,						\
		#uniquifier                                     \
	};							\
	DATA_SET(sysinit_set,uniquifier ## _sys_init);

#define	SYSINIT(uniquifier, subsystem, order, func, ident)	\
	C_SYSINIT(uniquifier, subsystem, order,			\
	(sysinit_cfunc_t)(sysinit_nfunc_t)func, (void *)ident)

/*
 * Called on module unload: no special processing
 */
#define	C_SYSUNINIT(uniquifier, subsystem, order, func, ident)	\
	static struct sysinit uniquifier ## _sys_uninit = {	\
		subsystem,					\
		order,						\
		func,						\
		ident						\
	};							\
	DATA_SET(sysuninit_set,uniquifier ## _sys_uninit)

#define	SYSUNINIT(uniquifier, subsystem, order, func, ident)	\
	C_SYSUNINIT(uniquifier, subsystem, order,		\
	(sysinit_cfunc_t)(sysinit_nfunc_t)func, (void *)ident)

void	sysinit_add (struct sysinit **, struct sysinit **);

/*
 * Infrastructure for tunable 'constants'.  Value may be specified at compile
 * time or kernel load time.  Rules relating tunables together can be placed
 * in a SYSINIT function at SI_BOOT1_TUNABLES with SI_ORDER_LAST.
 */

extern void tunable_int_init(void *);

struct tunable_int {
	const char *path;
	int *var;
};
#define	TUNABLE_INT(path, var)					\
	_TUNABLE_INT((path), (var), __LINE__)
#define _TUNABLE_INT(path, var, line)				\
	__TUNABLE_INT((path), (var), line)

#define	__TUNABLE_INT(path, var, line)				\
	static struct tunable_int __tunable_int_ ## line = {	\
		path,						\
		var,						\
	};							\
	SYSINIT(__Tunable_init_ ## line, 			\
		SI_BOOT1_TUNABLES, SI_ORDER_MIDDLE, 		\
	        tunable_int_init, &__tunable_int_ ## line)

#define	TUNABLE_INT_FETCH(path, var)	kgetenv_int((path), (var))

/* Backwards compatibility with the old deprecated TUNABLE_INT_DECL API */
#define TUNABLE_INT_DECL(path, defval, var)	\
static void __Tunable_ ## var (void *ignored)	\
{						\
	(var) = (defval);			\
	TUNABLE_INT_FETCH((path), &(var));	\
}						\
SYSINIT(__Tunable_init_ ## var, SI_BOOT1_TUNABLES, SI_ORDER_MIDDLE, \
	__Tunable_ ## var , NULL);

extern void tunable_long_init(void *);

struct tunable_long {
	const char *path;
	long *var;
};
#define	TUNABLE_LONG(path, var)					\
	_TUNABLE_LONG((path), (var), __LINE__)
#define _TUNABLE_LONG(path, var, line)				\
	__TUNABLE_LONG((path), (var), line)

#define	__TUNABLE_LONG(path, var, line)				\
	static struct tunable_long __tunable_long_ ## line = {	\
		path,						\
		var,						\
	};							\
	SYSINIT(__Tunable_init_ ## line, 			\
		SI_BOOT1_TUNABLES, SI_ORDER_MIDDLE, 		\
	        tunable_long_init, &__tunable_long_ ## line)

#define	TUNABLE_LONG_FETCH(path, var)	kgetenv_long((path), (var))

extern void tunable_ulong_init(void *);

struct tunable_ulong {
	const char *path;
	unsigned long *var;
};
#define	TUNABLE_ULONG(path, var)				\
	_TUNABLE_ULONG((path), (var), __LINE__)
#define _TUNABLE_ULONG(path, var, line)				\
	__TUNABLE_ULONG((path), (var), line)

#define	__TUNABLE_ULONG(path, var, line)			\
	static struct tunable_ulong __tunable_ulong_ ## line = {\
		path,						\
		var,						\
	};							\
	SYSINIT(__Tunable_init_ ## line, 			\
		SI_BOOT1_TUNABLES, SI_ORDER_MIDDLE, 		\
	        tunable_ulong_init, &__tunable_ulong_ ## line)

#define	TUNABLE_ULONG_FETCH(path, var)	kgetenv_ulong((path), (var))

extern void tunable_quad_init(void *);
struct tunable_quad {
	const char *path;
	quad_t *var;
};
#define	TUNABLE_QUAD(path, var)					\
	_TUNABLE_QUAD((path), (var), __LINE__)
#define	_TUNABLE_QUAD(path, var, line)				\
	__TUNABLE_QUAD((path), (var), line)

#define	__TUNABLE_QUAD(path, var, line)			\
	static struct tunable_quad __tunable_quad_ ## line = {	\
		path,						\
		var,						\
	};							\
	SYSINIT(__Tunable_init_ ## line, 			\
		SI_BOOT1_TUNABLES, SI_ORDER_MIDDLE, 		\
		tunable_quad_init, &__tunable_quad_ ## line)

#define	TUNABLE_QUAD_FETCH(path, var)	kgetenv_quad((path), (var))

extern void tunable_str_init(void *);
struct tunable_str {
	const char *path;
	char *var;
	int size;
};
#define	TUNABLE_STR(path, var, size)				\
	_TUNABLE_STR((path), (var), (size), __LINE__)
#define	_TUNABLE_STR(path, var, size, line)			\
	__TUNABLE_STR((path), (var), (size), line)

#define	__TUNABLE_STR(path, var, size, line)			\
	static struct tunable_str __tunable_str_ ## line = {	\
		path,						\
		var,						\
		size,						\
	};							\
	SYSINIT(__Tunable_init_ ## line, 			\
		SI_BOOT1_TUNABLES, SI_ORDER_MIDDLE,		\
		tunable_str_init, &__tunable_str_ ## line)

#define	TUNABLE_STR_FETCH(path, var, size)			\
	kgetenv_string((path), (var), (size))

/*
 * Compatibility.  To be deprecated after LKM is removed.
 */
#ifndef _SYS_MODULE_H_
#include <sys/module.h>
#endif

#define	PSEUDO_SET(sym, name) \
	static int name ## _modevent(module_t mod, int type, void *data) \
	{ \
		void (*initfunc)(void *) = (void (*)(void *))data; \
		switch (type) { \
		case MOD_LOAD: \
			/* kprintf(#name " module load\n"); */ \
			initfunc(NULL); \
			break; \
		case MOD_UNLOAD: \
			kprintf(#name " module unload - not possible for this module type\n"); \
			return EINVAL; \
		} \
		return 0; \
	} \
	static moduledata_t name ## _mod = { \
		#name, \
		name ## _modevent, \
		(void *)sym \
	}; \
	DECLARE_MODULE(name, name ## _mod, SI_SUB_PSEUDO, SI_ORDER_ANY)

struct intr_config_hook {
	TAILQ_ENTRY(intr_config_hook) ich_links;
	void	(*ich_func) (void *);
	void	*ich_arg;
	const char *ich_desc;
	int	ich_order;
	int	ich_ran;
};

int	config_intrhook_establish (struct intr_config_hook *);
void	config_intrhook_disestablish (struct intr_config_hook *);

#endif	/* _KERNEL */
#endif /* !_SYS_KERNEL_H_*/
