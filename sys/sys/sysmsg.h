/*
 * Copyright (c) 2003-2020 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
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
struct sysmsg {
	union {
	    void    *resultp;		/* misc pointer data or result */
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

#define sysmsg_result	sm_result.iresult
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

#endif /* _KERNEL */

#endif /* !_SYS_SYSMSG_H_ */
