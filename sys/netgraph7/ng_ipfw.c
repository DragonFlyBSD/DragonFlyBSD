/*-
 * Copyright 2005, Gleb Smirnoff <glebius@FreeBSD.org>
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
 * $FreeBSD: src/sys/netgraph/ng_ipfw.c,v 1.9 2006/02/14 15:22:24 ru Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/syslog.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip_fw.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include "ng_message.h"
#include "ng_parse.h"
#include "ng_ipfw.h"
#include "netgraph.h"

static int		ng_ipfw_mod_event(module_t mod, int event, void *data);
static ng_constructor_t	ng_ipfw_constructor;
static ng_shutdown_t	ng_ipfw_shutdown;
static ng_newhook_t	ng_ipfw_newhook;
static ng_connect_t	ng_ipfw_connect;
static ng_findhook_t	ng_ipfw_findhook;
static ng_rcvdata_t	ng_ipfw_rcvdata;
static ng_disconnect_t	ng_ipfw_disconnect;

static hook_p		ng_ipfw_findhook1(node_p, u_int16_t );
static int		ng_ipfw_input(struct mbuf **, int, struct ip_fw_args *,
			    int);

/* We have only one node */
static node_p	fw_node;

/* Netgraph node type descriptor */
static struct ng_type ng_ipfw_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_IPFW_NODE_TYPE,
	.mod_event =	ng_ipfw_mod_event,
	.constructor =	ng_ipfw_constructor,
	.shutdown =	ng_ipfw_shutdown,
	.newhook =	ng_ipfw_newhook,
	.connect =	ng_ipfw_connect,
	.findhook =	ng_ipfw_findhook,
	.rcvdata =	ng_ipfw_rcvdata,
	.disconnect =	ng_ipfw_disconnect,
};
NETGRAPH_INIT(ipfw, &ng_ipfw_typestruct);
MODULE_DEPEND(ng_ipfw, ipfw, 2, 2, 2);

/* Information we store for each hook */
struct ng_ipfw_hook_priv {
        hook_p		hook;
	u_int16_t	rulenum;
};
typedef struct ng_ipfw_hook_priv *hpriv_p;

static int
ng_ipfw_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:

		if (ng_ipfw_input_p != NULL) {
			error = EEXIST;
			break;
		}

		/* Setup node without any private data */
		if ((error = ng_make_node_common(&ng_ipfw_typestruct, &fw_node))
		    != 0) {
			log(LOG_ERR, "%s: can't create ng_ipfw node", __func__);
                	break;
		}

		/* Try to name node */
		if (ng_name_node(fw_node, "ipfw") != 0)
			log(LOG_WARNING, "%s: failed to name node \"ipfw\"",
			    __func__);

		/* Register hook */
		ng_ipfw_input_p = ng_ipfw_input;
		break;

	case MOD_UNLOAD:
		 /*
		  * This won't happen if a node exists.
		  * ng_ipfw_input_p is already cleared.
		  */
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static int
ng_ipfw_constructor(node_p node)
{
	return (EINVAL);	/* Only one node */
}

static int
ng_ipfw_newhook(node_p node, hook_p hook, const char *name)
{
	hpriv_p	hpriv;
	u_int16_t rulenum;
	const char *cp;
	char *endptr;

	/* Protect from leading zero */
	if (name[0] == '0' && name[1] != '\0')
		return (EINVAL);

	/* Check that name contains only digits */
	for (cp = name; *cp != '\0'; cp++)
		if (!isdigit(*cp))
			return (EINVAL);

	/* Convert it to integer */
	rulenum = (u_int16_t)strtol(name, &endptr, 10);
	if (*endptr != '\0')
		return (EINVAL);

	/* Allocate memory for this hook's private data */
	hpriv = kmalloc(sizeof(*hpriv), M_NETGRAPH,
			M_WAITOK | M_NULLOK | M_ZERO);
	if (hpriv== NULL)
		return (ENOMEM);

	hpriv->hook = hook;
	hpriv->rulenum = rulenum;

	NG_HOOK_SET_PRIVATE(hook, hpriv);

	return(0);
}

/*
 * Set hooks into queueing mode, to avoid recursion between
 * netgraph layer and ip_{input,output}.
 */
static int
ng_ipfw_connect(hook_p hook)
{
	NG_HOOK_FORCE_QUEUE(hook);
	return (0);
}

/* Look up hook by name */
hook_p
ng_ipfw_findhook(node_p node, const char *name)
{
	u_int16_t n;	/* numeric representation of hook */
	char *endptr;

	n = (u_int16_t)strtol(name, &endptr, 10);
	if (*endptr != '\0')
		return NULL;
	return ng_ipfw_findhook1(node, n);
}

/* Look up hook by rule number */
static hook_p
ng_ipfw_findhook1(node_p node, u_int16_t rulenum)
{
	hook_p	hook;
	hpriv_p	hpriv;

	LIST_FOREACH(hook, &node->nd_hooks, hk_hooks) {
		hpriv = NG_HOOK_PRIVATE(hook);
		if (NG_HOOK_IS_VALID(hook) && (hpriv->rulenum == rulenum))
                        return (hook);
	}

	return (NULL);
}


static int
ng_ipfw_rcvdata(hook_p hook, item_p item)
{
	struct ng_ipfw_tag	*ngit;
	struct mbuf *m;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if ((ngit = (struct ng_ipfw_tag *)m_tag_locate(m, NGM_IPFW_COOKIE, 0,
	    NULL)) == NULL) {
		NG_FREE_M(m);
		return (EINVAL);	/* XXX: find smth better */
	}

	switch (ngit->dir) {
	case NG_IPFW_OUT:
	    {
		struct ip *ip;

		if (m->m_len < sizeof(struct ip) &&
		    (m = m_pullup(m, sizeof(struct ip))) == NULL)
			return (EINVAL);

		ip = mtod(m, struct ip *);

		return ip_output(m, NULL, NULL, IP_FORWARDING, NULL, NULL);
	    }
	case NG_IPFW_IN:
		ip_input(m);
		return (0);
	default:
		panic("ng_ipfw_rcvdata: bad dir %u", ngit->dir);
	}	

	/* not reached */
	return (0);
}

static int
ng_ipfw_input(struct mbuf **m0, int dir, struct ip_fw_args *fwa, int tee)
{
	struct mbuf *m;
	struct ng_ipfw_tag *ngit;
	struct ip *ip;
	hook_p	hook;
	int error = 0;

	/*
	 * Node must be loaded and corresponding hook must be present.
	 */
	if (fw_node == NULL || 
	   (hook = ng_ipfw_findhook1(fw_node, fwa->cookie)) == NULL) {
		if (tee == 0)
			m_freem(*m0);
		return (ESRCH);		/* no hook associated with this rule */
	}

	/*
	 * We have two modes: in normal mode we add a tag to packet, which is
	 * important to return packet back to IP stack. In tee mode we make
	 * a copy of a packet and forward it into netgraph without a tag.
	 */
	if (tee == 0) {
		m = *m0;
		*m0 = NULL;	/* it belongs now to netgraph */

		if ((ngit = (struct ng_ipfw_tag *)m_tag_alloc(NGM_IPFW_COOKIE,
		    0, TAGSIZ, M_NOWAIT)) == NULL) {
			m_freem(m);
			return (ENOMEM);
		}
		ngit->rule = fwa->rule;
		ngit->dir = dir;
		ngit->ifp = fwa->oif;
		m_tag_prepend(m, &ngit->mt);

	} else
		if ((m = m_dup(*m0, M_NOWAIT)) == NULL)
			return (ENOMEM);	/* which is ignored */

	if (m->m_len < sizeof(struct ip) &&
	    (m = m_pullup(m, sizeof(struct ip))) == NULL)
		return (EINVAL);

	ip = mtod(m, struct ip *);

	NG_SEND_DATA_ONLY(error, hook, m);

	return (error);
}

static int
ng_ipfw_shutdown(node_p node)
{

	/*
	 * After our single node has been removed,
	 * the only thing that can be done is
	 * 'kldunload ng_ipfw.ko'
	 */
	ng_ipfw_input_p = NULL;
	NG_NODE_UNREF(node);
	return (0);
}

static int
ng_ipfw_disconnect(hook_p hook)
{
	const hpriv_p hpriv = NG_HOOK_PRIVATE(hook);

	kfree(hpriv, M_NETGRAPH);
	NG_HOOK_SET_PRIVATE(hook, NULL);

	return (0);
}
