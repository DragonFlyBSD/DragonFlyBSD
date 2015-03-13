/*	$NetBSD: pfil.c,v 1.20 2001/11/12 23:49:46 lukem Exp $	*/
/* $DragonFly: src/sys/net/pfil.c,v 1.14 2008/09/20 06:08:13 sephe Exp $ */

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

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/pfil.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <sys/mplock2.h>

#define PFIL_CFGPORT	netisr_cpuport(0)

#define PFIL_GETMPLOCK(pfh) \
do { \
	if (((pfh)->pfil_flags & PFIL_MPSAFE) == 0) \
		get_mplock(); \
} while (0)

#define PFIL_RELMPLOCK(pfh) \
do { \
	if (((pfh)->pfil_flags & PFIL_MPSAFE) == 0) \
		rel_mplock(); \
} while (0)

/*
 * The packet filter hooks are designed for anything to call them to
 * possibly intercept the packet.
 */
struct packet_filter_hook {
        TAILQ_ENTRY(packet_filter_hook) pfil_link;
	pfil_func_t	pfil_func;
	void		*pfil_arg;
	int		pfil_flags;
};

struct netmsg_pfil {
	struct netmsg_base	base;
	pfil_func_t		pfil_func;
	void			*pfil_arg;
	int			pfil_flags;
	struct pfil_head	*pfil_ph;
};

static LIST_HEAD(, pfil_head) pfil_head_list =
	LIST_HEAD_INITIALIZER(&pfil_head_list);

static pfil_list_t	*pfil_list_alloc(void);
static void		pfil_list_free(pfil_list_t *);
static void 		pfil_list_dup(const pfil_list_t *, pfil_list_t *,
				      const struct packet_filter_hook *);
static void 		pfil_list_add(pfil_list_t *, pfil_func_t, void *, int);
static struct packet_filter_hook *
			pfil_list_find(const pfil_list_t *, pfil_func_t,
				       const void *);

static void		pfil_remove_hook_dispatch(netmsg_t);
static void		pfil_add_hook_dispatch(netmsg_t);

int filters_default_to_accept = 0;
SYSCTL_INT(_net, OID_AUTO, filters_default_to_accept, CTLFLAG_RW,
    &filters_default_to_accept, 0,
    "cause ipfw* modules to not block by default");
TUNABLE_INT("net.filters_default_to_accept", &filters_default_to_accept);

/*
 * pfil_run_hooks() runs the specified packet filter hooks.
 */
int
pfil_run_hooks(struct pfil_head *ph, struct mbuf **mp, struct ifnet *ifp,
    int dir)
{
	struct packet_filter_hook *pfh;
	struct mbuf *m = *mp;
	pfil_list_t *list;
	int rv = 0;

	if (dir == PFIL_IN)
		list = ph->ph_in;
	else if (dir == PFIL_OUT)
		list = ph->ph_out;
	else
		return 0; /* XXX panic? */

	TAILQ_FOREACH(pfh, list, pfil_link) {
		if (pfh->pfil_func != NULL) {
			PFIL_GETMPLOCK(pfh);
			rv = pfh->pfil_func(pfh->pfil_arg, &m, ifp, dir);
			PFIL_RELMPLOCK(pfh);

			if (rv != 0 || m == NULL)
				break;
		}
	}

	*mp = m;
	return (rv);
}

/*
 * pfil_head_register() registers a pfil_head with the packet filter
 * hook mechanism.
 */
int
pfil_head_register(struct pfil_head *ph)
{
	struct pfil_head *lph;

	LIST_FOREACH(lph, &pfil_head_list, ph_list) {
		if (ph->ph_type == lph->ph_type &&
		    ph->ph_un.phu_val == lph->ph_un.phu_val)
			return EEXIST;
	}

	ph->ph_in = pfil_list_alloc();
	ph->ph_out = pfil_list_alloc();
	ph->ph_hashooks = 0;

	LIST_INSERT_HEAD(&pfil_head_list, ph, ph_list);

	return (0);
}

/*
 * pfil_head_unregister() removes a pfil_head from the packet filter
 * hook mechanism.
 */
int
pfil_head_unregister(struct pfil_head *pfh)
{
	LIST_REMOVE(pfh, ph_list);
	return (0);
}

/*
 * pfil_head_get() returns the pfil_head for a given key/dlt.
 */
struct pfil_head *
pfil_head_get(int type, u_long val)
{
	struct pfil_head *ph;

	LIST_FOREACH(ph, &pfil_head_list, ph_list) {
		if (ph->ph_type == type && ph->ph_un.phu_val == val)
			break;
	}
	return (ph);
}

static void
pfil_add_hook_dispatch(netmsg_t nmsg)
{
	struct netmsg_pfil *pfilmsg = (struct netmsg_pfil *)nmsg;
	pfil_func_t func = pfilmsg->pfil_func;
	void *arg = pfilmsg->pfil_arg;
	int flags = pfilmsg->pfil_flags;
	struct pfil_head *ph = pfilmsg->pfil_ph;
	const struct packet_filter_hook *pfh;
	pfil_list_t *list_in = NULL, *list_out = NULL;
	pfil_list_t *old_list_in = NULL, *old_list_out = NULL;
	int err = 0;

	/* This probably should not happen ... */
	if ((flags & (PFIL_IN | PFIL_OUT)) == 0)
		goto reply; /* XXX panic? */

	/*
	 * If pfil hooks exist on any of the requested lists,
	 * then bail out.
	 */
	if (flags & PFIL_IN) {
		pfh = pfil_list_find(ph->ph_in, func, arg);
		if (pfh != NULL) {
			err = EEXIST;
			goto reply;
		}
	}
	if (flags & PFIL_OUT) {
		pfh = pfil_list_find(ph->ph_out, func, arg);
		if (pfh != NULL) {
			err = EEXIST;
			goto reply;
		}
	}

	/*
	 * Duplicate the requested lists, install new hooks
	 * on the duplication
	 */
	if (flags & PFIL_IN) {
		list_in = pfil_list_alloc();
		pfil_list_dup(ph->ph_in, list_in, NULL);
		pfil_list_add(list_in, func, arg, flags & ~PFIL_OUT);
	}
	if (flags & PFIL_OUT) {
		list_out = pfil_list_alloc();
		pfil_list_dup(ph->ph_out, list_out, NULL);
		pfil_list_add(list_out, func, arg, flags & ~PFIL_IN);
	}

	/*
	 * Switch list pointers, but keep the old ones
	 */
	if (list_in != NULL) {
		old_list_in = ph->ph_in;
		ph->ph_in = list_in;
	}
	if (list_out != NULL) {
		old_list_out = ph->ph_out;
		ph->ph_out = list_out;
	}

	/*
	 * Wait until everyone has finished the old lists iteration
	 */
	netmsg_service_sync();
	ph->ph_hashooks = 1;

	/*
	 * Now it is safe to free the old lists, since no one sees it
	 */
	if (old_list_in != NULL)
		pfil_list_free(old_list_in);
	if (old_list_out != NULL)
		pfil_list_free(old_list_out);
reply:
	lwkt_replymsg(&nmsg->base.lmsg, err);
}

/*
 * pfil_add_hook() adds a function to the packet filter hook.  the
 * flags are:
 *	PFIL_IN		call me on incoming packets
 *	PFIL_OUT	call me on outgoing packets
 *	PFIL_ALL	call me on all of the above
 *	PFIL_MPSAFE	call me without BGL
 */
int
pfil_add_hook(pfil_func_t func, void *arg, int flags, struct pfil_head *ph)
{
	struct netmsg_pfil pfilmsg;
	netmsg_base_t nmsg;
	int error;

	nmsg = &pfilmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
		    0, pfil_add_hook_dispatch);
	pfilmsg.pfil_func = func;
	pfilmsg.pfil_arg = arg;
	pfilmsg.pfil_flags = flags;
	pfilmsg.pfil_ph = ph;

	error = lwkt_domsg(PFIL_CFGPORT, &nmsg->lmsg, 0);
	return error;
}

static void
pfil_remove_hook_dispatch(netmsg_t nmsg)
{
	struct netmsg_pfil *pfilmsg = (struct netmsg_pfil *)nmsg;
	pfil_func_t func = pfilmsg->pfil_func;
	void *arg = pfilmsg->pfil_arg;
	int flags = pfilmsg->pfil_flags;
	struct pfil_head *ph = pfilmsg->pfil_ph;
	struct packet_filter_hook *skip_in = NULL, *skip_out = NULL;
	pfil_list_t *list_in = NULL, *list_out = NULL;
	pfil_list_t *old_list_in = NULL, *old_list_out = NULL;
	int err = 0;

	/* This probably should not happen ... */
	if ((flags & (PFIL_IN | PFIL_OUT)) == 0)
		goto reply; /* XXX panic? */

	/*
	 * The pfil hook should exist on all requested lists,
	 * if not just bail out
	 */
	if (flags & PFIL_IN) {
		skip_in = pfil_list_find(ph->ph_in, func, arg);
		if (!skip_in) {
			err = ENOENT;
			goto reply;
		}
	}
	if (flags & PFIL_OUT) {
		skip_out = pfil_list_find(ph->ph_out, func, arg);
		if (!skip_out) {
			err = ENOENT;
			goto reply;
		}
	}

	/*
	 * Duplicate the requested lists, but the pfil hook to
	 * be deleted is not copied
	 */
	if (flags & PFIL_IN) {
		KKASSERT(skip_in != NULL);
		list_in = pfil_list_alloc();
		pfil_list_dup(ph->ph_in, list_in, skip_in);
	}
	if (flags & PFIL_OUT) {
		KKASSERT(skip_out != NULL);
		list_out = pfil_list_alloc();
		pfil_list_dup(ph->ph_out, list_out, skip_out);
	}

	/*
	 * Switch list pointers, but keep the old ones
	 */
	if (list_in != NULL) {
		old_list_in = ph->ph_in;
		ph->ph_in = list_in;
	}
	if (list_out != NULL) {
		old_list_out = ph->ph_out;
		ph->ph_out = list_out;
	}

	/*
	 * Wait until everyone has finished the old lists iteration
	 */
	if (TAILQ_EMPTY(ph->ph_in) && TAILQ_EMPTY(ph->ph_out))
		ph->ph_hashooks = 0;
	netmsg_service_sync();

	/*
	 * Now it is safe to free the old lists, since no one sees it
	 */
	if (old_list_in != NULL)
		pfil_list_free(old_list_in);
	if (old_list_out != NULL)
		pfil_list_free(old_list_out);
reply:
	lwkt_replymsg(&nmsg->base.lmsg, err);
}

/*
 * pfil_remove_hook removes a specific function from the packet filter
 * hook list.
 */
int
pfil_remove_hook(pfil_func_t func, void *arg, int flags, struct pfil_head *ph)
{
	struct netmsg_pfil pfilmsg;
	netmsg_base_t nmsg;

	nmsg = &pfilmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
		    0, pfil_remove_hook_dispatch);
	pfilmsg.pfil_func = func;
	pfilmsg.pfil_arg = arg;
	pfilmsg.pfil_flags = flags;
	pfilmsg.pfil_ph = ph;

	return lwkt_domsg(PFIL_CFGPORT, &nmsg->lmsg, 0);
}

static void
pfil_list_add(pfil_list_t *list, pfil_func_t func, void *arg, int flags)
{
	struct packet_filter_hook *pfh;

	KKASSERT(&curthread->td_msgport == PFIL_CFGPORT);

	pfh = kmalloc(sizeof(*pfh), M_IFADDR, M_WAITOK);

	pfh->pfil_func = func;
	pfh->pfil_arg  = arg;
	pfh->pfil_flags = flags;

	/*
	 * Insert the input list in reverse order of the output list
	 * so that the same path is followed in or out of the kernel.
	 */
	if (flags & PFIL_IN)
		TAILQ_INSERT_HEAD(list, pfh, pfil_link);
	else
		TAILQ_INSERT_TAIL(list, pfh, pfil_link);
}

static void
pfil_list_dup(const pfil_list_t *from, pfil_list_t *to,
	      const struct packet_filter_hook *skip)
{
	struct packet_filter_hook *pfh_to, *pfh_from;

	KKASSERT(&curthread->td_msgport == PFIL_CFGPORT);
	KKASSERT(TAILQ_EMPTY(to));

	TAILQ_FOREACH(pfh_from, from, pfil_link) {
		if (pfh_from == skip)
			continue;

		pfh_to = kmalloc(sizeof(*pfh_to), M_IFADDR, M_WAITOK);
		bcopy(pfh_from, pfh_to, sizeof(*pfh_to));

		TAILQ_INSERT_TAIL(to, pfh_to, pfil_link);
	}
}

static pfil_list_t *
pfil_list_alloc(void)
{
	pfil_list_t *list;

	list = kmalloc(sizeof(*list), M_IFADDR, M_WAITOK);
	TAILQ_INIT(list);
	return list;
}

static void
pfil_list_free(pfil_list_t *list)
{
	struct packet_filter_hook *pfh;

	while ((pfh = TAILQ_FIRST(list)) != NULL) {
		TAILQ_REMOVE(list, pfh, pfil_link);
		kfree(pfh, M_IFADDR);
	}
	kfree(list, M_IFADDR);
}

static struct packet_filter_hook *
pfil_list_find(const pfil_list_t *list, pfil_func_t func, const void *arg)
{
	struct packet_filter_hook *pfh;

	KKASSERT(&curthread->td_msgport == PFIL_CFGPORT);

	TAILQ_FOREACH(pfh, list, pfil_link) {
		if (pfh->pfil_func == func && pfh->pfil_arg == arg)
			return pfh;
	}
	return NULL;
}
