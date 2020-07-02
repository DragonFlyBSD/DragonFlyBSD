/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	From: @(#)if.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/if_var.h,v 1.18.2.16 2003/04/15 18:11:19 fjoe Exp $
 * $FreeBSD: src/sys/net/if.h,v 1.58.2.9 2002/08/30 14:23:38 sobomax Exp $
 */

#ifndef	_NET_IF_CLONE_H_
#define	_NET_IF_CLONE_H_

#include <sys/cdefs.h>

#if __BSD_VISIBLE
/*
 * Structure used to query names of interface cloners.
 */
struct if_clonereq {
	int	ifcr_total;		/* total cloners (out) */
	int	ifcr_count;		/* room for this many in user buffer */
	char	*ifcr_buffer;		/* buffer for cloner names */
};
#endif /* __BSD_VISIBLE */

#ifdef _KERNEL

#include <sys/eventhandler.h>
#include <sys/queue.h>

/*
 * Structure describing a `cloning' interface.
 */
struct if_clone {
	LIST_ENTRY(if_clone) ifc_list;	/* on list of cloners */
	const char	*ifc_name;	/* name of device, e.g. `gif' */
	int		ifc_minifs;	/* minimum number of interfaces */
	int 		ifc_maxunit;	/* maximum unit number */
	unsigned char	*ifc_units;	/* bitmap to handle units */
	int 		ifc_bmlen;	/* bitmap length */

	int		(*ifc_create)(struct if_clone *, int,
				      caddr_t, caddr_t);
	int		(*ifc_destroy)(struct ifnet *);
};

#define IF_CLONE_INITIALIZER(name, create, destroy, minifs, maxunit)	\
	{ { 0 }, name, minifs, maxunit, NULL, 0, create, destroy }

/* interface clone event */
typedef void (*if_clone_event_handler_t)(void *, struct if_clone *);
EVENTHANDLER_DECLARE(if_clone_event, if_clone_event_handler_t);

int	if_clone_attach(struct if_clone *ifc);
void	if_clone_detach(struct if_clone *ifc);
int	if_clone_create(char *name, int len, caddr_t params, caddr_t data);
int	if_clone_destroy(const char *name);
int	if_clone_list(struct if_clonereq *ifcr);

#endif /* _KERNEL */

#endif /* !_NET_IF_CLONE_H_ */
