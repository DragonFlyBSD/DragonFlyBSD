/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2026 The DragonFly Project.
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
 */

#ifndef _MACHINE_VMPARAM_H_
#define	_MACHINE_VMPARAM_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#define	MAXTSIZ		(32UL*1024*1024*1024)
#ifndef DFLDSIZ
#define	DFLDSIZ		(128UL*1024*1024)
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(32UL*1024*1024*1024)
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(8UL*1024*1024)
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(512UL*1024*1024)
#endif
#ifndef	MAXTHRSSIZ
#define	MAXTHRSSIZ	(128UL*1024*1024*1024)
#endif
#ifndef SGROWSIZ
#define	SGROWSIZ	(128UL*1024)
#endif

#define	MAXSLP		20

#define VM_MIN_USER_ADDRESS	((vm_offset_t)0)
#define VM_MAX_USER_ADDRESS	((vm_offset_t)0x0000ffffffffffffUL)

#define USRSTACK		VM_MAX_USER_ADDRESS

#define VM_MAX_ADDRESS		VM_MAX_USER_ADDRESS
#define VM_MIN_ADDRESS		VM_MIN_USER_ADDRESS

#define VM_MIN_KERNEL_ADDRESS	((vm_offset_t)0xffff000000000000UL)
#define VM_MAX_KERNEL_ADDRESS	((vm_offset_t)0xffffffffffffffffUL)

#endif /* _MACHINE_VMPARAM_H_ */
