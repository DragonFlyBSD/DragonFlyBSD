/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/sys/upcall.h,v 1.11 2006/09/10 21:35:11 dillon Exp $
 */

#ifndef _SYS_UPCALL_H_
#define _SYS_UPCALL_H_

struct thread;
struct lwp;

typedef void (*upcall_func_t)(void *);

struct upcall {
	int	upc_magic;
	int	upc_critoff;		/* offset of crit_count in uthread */
	int	upc_pending;		/* must follow crit_count */
	struct thread *upc_uthread;	/* pointer to user thread (opaque) */
};

#define UPCALL_MAGIC	0x55504331
#define UPCALL_MAXCOUNT	32

#define UPC_CONTROL_DISPATCH		1
#define UPC_CONTROL_NEXT		2
#define UPC_CONTROL_DELETE		3
#define UPC_CONTROL_POLL		4
#define UPC_CONTROL_POLLANDCLEAR	5
#define UPC_CONTROL_WAIT		6

#define UPC_RESERVED			32	/* # of reserved id's */

#if defined(_KERNEL)
/*
 * Kernel protoypes
 */

struct vmspace;

void upc_release(struct vmspace *vm, struct lwp *lp);
void postupcall(struct lwp *lp);

#else
/*
 * Userland prototypes
 */
int upc_register(struct upcall *, upcall_func_t, upcall_func_t, void *);
int upc_control(int, int, void *);
void upc_callused_wrapper(void *);

#endif

#endif

