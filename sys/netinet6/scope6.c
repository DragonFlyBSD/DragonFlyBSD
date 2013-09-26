/*	$FreeBSD: src/sys/netinet6/scope6.c,v 1.1.2.3 2002/04/01 15:29:04 ume Exp $	*/
/*	$DragonFly: src/sys/netinet6/scope6.c,v 1.11 2008/01/05 14:02:40 swildner Exp $	*/
/*	$KAME: scope6.c,v 1.10 2000/07/24 13:29:31 itojun Exp $	*/

/*
 * Copyright (C) 2000 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/thread2.h>

#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>

#include <netinet6/in6_var.h>
#include <netinet6/scope6_var.h>

static struct scope6_id sid_default;

static __inline
struct scope6_id *
SID(struct ifnet *ifp)
{
	struct in6_ifextra *xtra;

	xtra = ifp->if_afdata[AF_INET6];
	if (xtra)
		return (xtra->scope6_id);
	return(NULL);
}

void
scope6_init(void)
{
	bzero(&sid_default, sizeof(sid_default));
}

struct scope6_id *
scope6_ifattach(struct ifnet *ifp)
{
	struct scope6_id *sid;

	sid = (struct scope6_id *)kmalloc(sizeof(*sid), M_IFADDR,
	    M_WAITOK | M_ZERO);

	crit_enter();

	/*
	 * XXX: IPV6_ADDR_SCOPE_xxx macros are not standard.
	 * Should we rather hardcode here?
	 */
	sid->s6id_list[IPV6_ADDR_SCOPE_NODELOCAL] = ifp->if_index;
	sid->s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL] = ifp->if_index;
#ifdef MULTI_SCOPE
	/* by default, we don't care about scope boundary for these scopes. */
	sid->s6id_list[IPV6_ADDR_SCOPE_SITELOCAL] = 1;
	sid->s6id_list[IPV6_ADDR_SCOPE_ORGLOCAL] = 1;
#endif

	crit_exit();
	return sid;
}

void
scope6_ifdetach(struct scope6_id *sid)
{
	kfree(sid, M_IFADDR);
}

int
scope6_set(struct ifnet *ifp, struct scope6_id *idlist)
{
	int i;
	int error = 0;
	struct scope6_id *sid = SID(ifp);

	if (!sid)	/* paranoid? */
		return (EINVAL);

	/*
	 * XXX: We need more consistency checks of the relationship among
	 * scopes (e.g. an organization should be larger than a site).
	 */

	/*
	 * TODO(XXX): after setting, we should reflect the changes to
	 * interface addresses, routing table entries, PCB entries...
	 */

	crit_enter();

	for (i = 0; i < 16; i++) {
		if (idlist->s6id_list[i] &&
		    idlist->s6id_list[i] != sid->s6id_list[i]) {
			if (i == IPV6_ADDR_SCOPE_LINKLOCAL &&
			    idlist->s6id_list[i] > if_index) {
				/*
				 * XXX: theoretically, there should be no
				 * relationship between link IDs and interface
				 * IDs, but we check the consistency for
				 * safety in later use.
				 */
				crit_exit();
				return (EINVAL);
			}

			/*
			 * XXX: we must need lots of work in this case,
			 * but we simply set the new value in this initial
			 * implementation.
			 */
			sid->s6id_list[i] = idlist->s6id_list[i];
		}
	}
	crit_exit();

	return (error);
}

int
scope6_get(struct ifnet *ifp, struct scope6_id *idlist)
{
	struct scope6_id *sid = SID(ifp);

	if (sid == NULL)	/* paranoid? */
		return (EINVAL);

	*idlist = *sid;

	return (0);
}


/*
 * Get a scope of the address. Node-local, link-local, site-local or global.
 */
int
in6_addrscope(struct in6_addr *addr)
{
	int scope;

	if (addr->s6_addr8[0] == 0xfe) {
		scope = addr->s6_addr8[1] & 0xc0;

		switch (scope) {
		case 0x80:
			return IPV6_ADDR_SCOPE_LINKLOCAL;
			break;
		case 0xc0:
			return IPV6_ADDR_SCOPE_SITELOCAL;
			break;
		default:
			return IPV6_ADDR_SCOPE_GLOBAL; /* just in case */
			break;
		}
	}


	if (addr->s6_addr8[0] == 0xff) {
		scope = addr->s6_addr8[1] & 0x0f;

		/*
		 * due to other scope such as reserved,
		 * return scope doesn't work.
		 */
		switch (scope) {
		case IPV6_ADDR_SCOPE_NODELOCAL:
			return IPV6_ADDR_SCOPE_NODELOCAL;
			break;
		case IPV6_ADDR_SCOPE_LINKLOCAL:
			return IPV6_ADDR_SCOPE_LINKLOCAL;
			break;
		case IPV6_ADDR_SCOPE_SITELOCAL:
			return IPV6_ADDR_SCOPE_SITELOCAL;
			break;
		default:
			return IPV6_ADDR_SCOPE_GLOBAL;
			break;
		}
	}

	if (bcmp(&kin6addr_loopback, addr, sizeof(*addr) - 1) == 0) {
		if (addr->s6_addr8[15] == 1) /* loopback */
			return IPV6_ADDR_SCOPE_NODELOCAL;
		if (addr->s6_addr8[15] == 0) /* unspecified */
			return IPV6_ADDR_SCOPE_LINKLOCAL;
	}

	return IPV6_ADDR_SCOPE_GLOBAL;
}

int
in6_addr2scopeid(struct ifnet *ifp,	/* must not be NULL */
		 struct in6_addr *addr)	/* must not be NULL */
{
	int scope;
	struct scope6_id *sid = SID(ifp);

#ifdef DIAGNOSTIC
	if (sid == NULL) { /* should not happen */
		panic("in6_addr2zoneid: scope array is NULL");
		/* NOTREACHED */
	}
#endif

	/*
	 * special case: the loopback address can only belong to a loopback
	 * interface.
	 */
	if (IN6_IS_ADDR_LOOPBACK(addr)) {
		if (!(ifp->if_flags & IFF_LOOPBACK))
			return (-1);
		else
			return (0); /* there's no ambiguity */
	}

	scope = in6_addrscope(addr);

	switch(scope) {
	case IPV6_ADDR_SCOPE_NODELOCAL:
		return (-1);	/* XXX: is this an appropriate value? */

	case IPV6_ADDR_SCOPE_LINKLOCAL:
		return (sid->s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL]);

	case IPV6_ADDR_SCOPE_SITELOCAL:
		return (sid->s6id_list[IPV6_ADDR_SCOPE_SITELOCAL]);

	case IPV6_ADDR_SCOPE_ORGLOCAL:
		return (sid->s6id_list[IPV6_ADDR_SCOPE_ORGLOCAL]);

	default:
		return (0);	/* XXX: treat as global. */
	}
}

void
scope6_setdefault(struct ifnet *ifp)	/* note that this might be NULL */
{
	/*
	 * Currently, this function just set the default "interfaces"
	 * and "links" according to the given interface.
	 * We might eventually have to separate the notion of "link" from
	 * "interface" and provide a user interface to set the default.
	 */
	if (ifp) {
		sid_default.s6id_list[IPV6_ADDR_SCOPE_NODELOCAL] =
			ifp->if_index;
		sid_default.s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL] =
			ifp->if_index;
	} else {
		sid_default.s6id_list[IPV6_ADDR_SCOPE_NODELOCAL] = 0;
		sid_default.s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL] = 0;
	}
}

int
scope6_get_default(struct scope6_id *idlist)
{
	*idlist = sid_default;

	return (0);
}

u_int32_t
scope6_addr2default(struct in6_addr *addr)
{
	/*
	 * special case: The loopback address should be considered as
	 * link-local, but there's no ambiguity in the syntax.
	 */
	if (IN6_IS_ADDR_LOOPBACK(addr))
		return (0);

	return (sid_default.s6id_list[in6_addrscope(addr)]);
}

/*
 * Determine the appropriate scope zone ID for in6 and ifp.  If ret_id is
 * non NULL, it is set to the zone ID.  If the zone ID needs to be embedded
  * in the in6_addr structure, in6 will be modified.
 */
int
in6_setscope(struct in6_addr *in6, struct ifnet *ifp, u_int32_t *ret_id)
{
	int scope;
	u_int32_t zoneid = 0;
	struct scope6_id *sid;

	ifnet_serialize_all(ifp);

	sid = SID(ifp);

#ifdef DIAGNOSTIC
	if (sid == NULL) { /* should not happen */
		panic("in6_setscope: scope array is NULL");
		/* NOTREACHED */
	}
#endif

	/*
	 * special case: the loopback address can only belong to a loopback
	 * interface.
	 */
	if (IN6_IS_ADDR_LOOPBACK(in6)) {
		if (!(ifp->if_flags & IFF_LOOPBACK)) {
			ifnet_deserialize_all(ifp);
			return (EINVAL);
		} else {
			if (ret_id != NULL)
				*ret_id = 0; /* there's no ambiguity */
			ifnet_deserialize_all(ifp);
			return (0);
		}
	}

	scope = in6_addrscope(in6);

	switch (scope) {
	case IPV6_ADDR_SCOPE_NODELOCAL: /* should be interface index */
		zoneid = sid->s6id_list[IPV6_ADDR_SCOPE_NODELOCAL];
		break;

	case IPV6_ADDR_SCOPE_LINKLOCAL:
		zoneid = sid->s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL];
		break;

	case IPV6_ADDR_SCOPE_SITELOCAL:
		zoneid = sid->s6id_list[IPV6_ADDR_SCOPE_SITELOCAL];
		break;

	case IPV6_ADDR_SCOPE_ORGLOCAL:
		zoneid = sid->s6id_list[IPV6_ADDR_SCOPE_ORGLOCAL];
		break;

	default:
		zoneid = 0;     /* XXX: treat as global. */
		break;
	}

	ifnet_deserialize_all(ifp);

	if (ret_id != NULL)
		*ret_id = zoneid;

	if (IN6_IS_SCOPE_LINKLOCAL(in6) || IN6_IS_ADDR_MC_NODELOCAL(in6) )
		in6->s6_addr16[1] = htons(zoneid & 0xffff); /* XXX */

	return (0);
}
