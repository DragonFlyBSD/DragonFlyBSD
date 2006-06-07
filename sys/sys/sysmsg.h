/*
 * SYS/SYSMSG.H
 *
 * $DragonFly: src/sys/sys/sysmsg.h,v 1.10 2006/06/07 03:02:11 dillon Exp $
 */

#ifndef _SYS_SYSMSG_H_
#define _SYS_SYSMSG_H_

#ifdef _KERNEL

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * The sysmsg holds the kernelland version of a system call's arguments
 * and return value.  It typically preceeds the syscall arguments in sysunion
 * (see sys/sysunion.h).
 */
union sysunion;

struct sysmsg {
	union {
	    void    *resultp;            /* misc pointer data or result */
	    int     result;              /* standard 'int'eger result */
	    long    lresult;             /* long result */
	    int     fds[2];              /* two int bit results */
	    __int32_t result32;          /* 32 bit result */
	    __int64_t result64;          /* 64 bit result */
	    __off_t offset;              /* off_t result */
	} sm_result;
};

struct lwp;
union sysunion;

#endif

#ifdef _KERNEL
#define sysmsg_result	sysmsg.sm_result.result
#define sysmsg_lresult	sysmsg.sm_result.lresult
#define sysmsg_resultp	sysmsg.sm_result.resultp
#define sysmsg_fds	sysmsg.sm_result.fds
#define sysmsg_offset	sysmsg.sm_result.offset
#define sysmsg_result32	sysmsg.sm_result.result32
#define sysmsg_result64	sysmsg.sm_result.result64
#endif

#endif

