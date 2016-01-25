/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Scooter Morris at Genentech Inc.
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
 *	@(#)lockf.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD: src/sys/sys/lockf.h,v 1.10 1999/08/28 00:51:51 peter Exp $
 * $DragonFly: src/sys/sys/lockf.h,v 1.9 2006/05/20 02:42:13 dillon Exp $
 */

#ifndef _SYS_LOCKF_H_
#define	_SYS_LOCKF_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)
#error "This file should not be included by userland programs."
#endif

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

struct vop_advlock_args;

/*
 * The lockf structure is a kernel structure which contains the information
 * associated with the byte range locks on an inode. The lockf structure is
 * embedded in the inode structure.
 */

struct lockf_range {
	short		 lf_type;	/* Lock type: F_RDLCK, F_WRLCK */
	short		 lf_flags;	/* Lock flags: F_NOEND */
	off_t		 lf_start;	/* Byte # of the start of the lock */
	off_t		 lf_end;	/* Byte # of the end of the lock, */
	struct proc	*lf_owner;	/* owning process, NULL for flock locks */
	TAILQ_ENTRY(lockf_range) lf_link;
};

TAILQ_HEAD(lockf_range_list, lockf_range);

struct lockf {
	struct lockf_range_list lf_range;
	struct lockf_range_list lf_blocked;
	int init_done;
};

#ifdef _KERNEL
int	lf_advlock(struct vop_advlock_args *, struct lockf *, u_quad_t);
void	lf_count_adjust(struct proc *, int);

extern int maxposixlocksperuid;
#endif

#endif /* !_SYS_LOCKF_H_ */
