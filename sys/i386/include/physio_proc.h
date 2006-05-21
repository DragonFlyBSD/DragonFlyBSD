/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/include/physio_proc.h,v 1.1.2.1 2000/10/29 11:05:48 non Exp $
 * $DragonFly: src/sys/i386/include/Attic/physio_proc.h,v 1.13 2006/05/21 03:43:44 dillon Exp $
 * $NecBSD: physio_proc.h,v 3.4 1999/07/23 20:47:03 honda Exp $
 * $NetBSD$
 */

#ifndef _MACHINE_PHYSIO_PROC_H_
#define _MACHINE_PHYSIO_PROC_H_

#ifndef _KERNEL

#error "This file should not be included by userland programs."

#else

#ifndef _SYS_BUF_H_
#include <sys/buf.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif

struct physio_proc { };

static __inline struct physio_proc *
physio_proc_enter(struct buf *bp)
{
	return NULL;
}

static __inline void
physio_proc_leave(struct physio_proc *pp)
{
	return;
}

static __inline void
physio_proc_init(void)
{
	return;
}

#endif	/* _KERNEL */
#endif	/* _MACHINE_PHYSIO_PROC_H_ */
