/*
 * SYS/SYSMSG.H
 *
 * $DragonFly: src/sys/sys/sysmsg.h,v 1.12 2008/08/25 23:35:47 dillon Exp $
 */

#ifndef _SYS_SYSMSG_H_
#define _SYS_SYSMSG_H_

#ifdef _KERNEL

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_SYSPROTO_H_
#include <sys/sysproto.h>
#endif
#ifndef _SYS_SYSUNION_H_
#include <sys/sysunion.h>
#endif

/*
 * The sysmsg holds the kernelland version of a system call's arguments
 * and return value.  It typically preceeds the syscall arguments in sysunion
 * (see sys/sysunion.h).
 *
 * WARNING: fds must be long so it translates to two 64 bit registers
 * on 64 bit architectures.
 */
union sysunion;

struct sysmsg {
	union {
	    void    *resultp;		/* misc pointer data or result */
	    int     result;		/* DEPRECATED - AUDIT -> iresult */
	    int     iresult;		/* standard 'int'eger result */
	    long    lresult;		/* long result */
	    size_t  szresult;		/* size_t result */
	    long    fds[2];		/* double result */
	    __int32_t result32;		/* 32 bit result */
	    __int64_t result64;		/* 64 bit result */
	    __off_t offset;		/* off_t result */
	    register_t reg;
	} sm_result;
	struct trapframe *sm_frame;	/* trapframe - saved user context */
	union sysunion extargs;		/* if more than 6 args */
} __packed;

struct lwp;
union sysunion;

#endif

#ifdef _KERNEL
#define sysmsg_result	sm_result.result
#define sysmsg_iresult	sm_result.iresult
#define sysmsg_lresult	sm_result.lresult
#define sysmsg_szresult	sm_result.szresult
#define sysmsg_resultp	sm_result.resultp
#define sysmsg_fds	sm_result.fds
#define sysmsg_offset	sm_result.offset
#define sysmsg_result32	sm_result.result32
#define sysmsg_result64	sm_result.result64
#define sysmsg_reg	sm_result.reg
#define sysmsg_frame	sm_frame
#endif

#endif

