/*	$NetBSD: pfil.h,v 1.22 2003/06/23 12:57:08 martin Exp $	*/
/* $DragonFly: src/sys/net/pfil.h,v 1.11 2008/09/20 06:08:13 sephe Exp $ */

/*
 * Copyright (c) 1996 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NET_PFIL_H_
#define _NET_PFIL_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

struct mbuf;
struct ifnet;
struct packet_filter_hook;

typedef int	(*pfil_func_t)(void *, struct mbuf **, struct ifnet *, int);

#define PFIL_IN		0x00000001
#define PFIL_OUT	0x00000002
#define PFIL_MPSAFE	0x00000004
#define PFIL_ALL	(PFIL_IN|PFIL_OUT)

typedef	TAILQ_HEAD(pfil_list, packet_filter_hook) pfil_list_t;

#define	PFIL_TYPE_AF		1	/* key is AF_* type */
#define	PFIL_TYPE_IFNET		2	/* key is ifnet pointer */

struct pfil_head {
	pfil_list_t	*ph_in;
	pfil_list_t	*ph_out;
	int		ph_type;
	int		ph_hashooks;	/* 0 if no hooks installed */
	union {
		u_long		phu_val;
		void		*phu_ptr;
	} ph_un;
#define	ph_af		ph_un.phu_val
#define	ph_ifnet	ph_un.phu_ptr
	LIST_ENTRY(pfil_head) ph_list;
};
typedef struct pfil_head pfil_head_t;

int	pfil_run_hooks(struct pfil_head *, struct mbuf **, struct ifnet *, int);

int	pfil_add_hook(pfil_func_t, void *, int, struct pfil_head *);
int	pfil_remove_hook(pfil_func_t, void *, int, struct pfil_head *);

int	pfil_head_register(struct pfil_head *);
int	pfil_head_unregister(struct pfil_head *);

struct pfil_head *pfil_head_get(int, u_long);

extern int filters_default_to_accept;

/*
 * Used for a quick shortcut around the pfil routines if no hooks have been
 * installed.
 */
static __inline int
pfil_has_hooks(struct pfil_head *ph)
{
	return(ph->ph_hashooks);
}

/* XXX */
#if defined(_KERNEL_OPT)
#include "ipfilter.h"
#endif

#endif /* _NET_PFIL_H_ */
