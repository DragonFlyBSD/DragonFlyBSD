/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/upcall.h,v 1.6 2003/12/07 02:14:20 dillon Exp $
 */

#ifndef _SYS_UPCALL_H_
#define _SYS_UPCALL_H_

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

#define UPC_CRITADD			32	/* NOTE! same as TDPRI_CRIT */

#if defined(_KERNEL)
/*
 * Kernel protoypes
 */

struct vmspace;

void upc_release(struct vmspace *vm, struct proc *p);
void postupcall(struct proc *p);

#else
/*
 * Userland prototypes
 */
int upc_register(struct upcall *, upcall_func_t, upcall_func_t, void *);
int upc_control(int, int, void *);
void upc_callused_wrapper(void *);

#endif

#endif

