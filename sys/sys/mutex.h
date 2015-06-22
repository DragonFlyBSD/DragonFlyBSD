/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
 */

#ifndef	_SYS_MUTEX_H_
#define	_SYS_MUTEX_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#include <machine/atomic.h>
#include <machine/cpufunc.h>

/*
 * The general mutex structure provides recursive shared and exclusive
 * locks, downgrade, a non-blocking upgrade, and various other functions.
 *
 * The structure is 16-byte aligned and either 16 or 32 bytes, designed
 * for 32 or 64 bit cpus.
 */
struct thread;

struct mtx_link {
	struct mtx_link	*next;
	struct mtx_link	*prev;
	struct thread	*owner;
	int		state;
	void		(*callback)(struct mtx_link *, void *arg, int error);
	void		*arg;
};

typedef struct mtx_link	mtx_link_t;

struct mtx {
	volatile u_int	mtx_lock;
	int		mtx_reserved01;	/* future use & struct alignmnent */
	struct thread	*mtx_owner;
	mtx_link_t	*mtx_exlink;
	mtx_link_t	*mtx_shlink;
	const char	*mtx_ident;
} __cachealign;

typedef struct mtx	mtx_t;
typedef u_int		mtx_state_t;

#define MTX_INITIALIZER(ident)	{ .mtx_lock = 0, .mtx_owner = NULL, \
			  	  .mtx_exlink = NULL, .mtx_shlink = NULL, \
				  .mtx_ident = ident }

#define MTX_EXCLUSIVE	0x80000000
#define MTX_SHWANTED	0x40000000
#define MTX_EXWANTED	0x20000000
#define MTX_LINKSPIN	0x10000000
#define MTX_MASK	0x0FFFFFFF

#define MTX_OWNER_NONE	NULL
#define MTX_OWNER_ANON	((struct thread *)-2)

#define MTX_LINK_IDLE		0
#define MTX_LINK_LINKED_SH	(MTX_LINK_LINKED | 1)
#define MTX_LINK_LINKED_EX	(MTX_LINK_LINKED | 2)
#define MTX_LINK_ACQUIRED	3
#define MTX_LINK_CALLEDBACK	4
#define MTX_LINK_ABORTED	5

#define MTX_LINK_LINKED		0x1000

#endif

/*
 * See also sys/mutex2.h
 */
#ifdef _KERNEL

int	_mtx_lock_ex_link(mtx_t *mtx, mtx_link_t *link, int flags, int to);
int	_mtx_lock_ex(mtx_t *mtx, int flags, int to);
int	_mtx_lock_sh_link(mtx_t *mtx, mtx_link_t *link, int flags, int to);
int	_mtx_lock_sh(mtx_t *mtx, int flags, int to);
int	_mtx_lock_ex_quick(mtx_t *mtx);
int	_mtx_lock_sh_quick(mtx_t *mtx);
void	_mtx_spinlock(mtx_t *mtx);
int	_mtx_spinlock_try(mtx_t *mtx);
int	_mtx_lock_ex_try(mtx_t *mtx);
int	_mtx_lock_sh_try(mtx_t *mtx);
void	_mtx_downgrade(mtx_t *mtx);
int	_mtx_upgrade_try(mtx_t *mtx);
void	_mtx_unlock(mtx_t *mtx);
void	mtx_abort_link(mtx_t *mtx, mtx_link_t *link);
int	mtx_wait_link(mtx_t *mtx, mtx_link_t *link, int flags, int to);

#endif

#endif
