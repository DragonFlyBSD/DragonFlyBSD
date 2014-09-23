
/*
 * ng_ksocket.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD: src/sys/netgraph/ng_ksocket.c,v 1.5.2.14 2003/08/24 08:24:38 hsu Exp $
 * $Whistle: ng_ksocket.c,v 1.1 1999/11/16 20:04:40 archie Exp $
 */

/*
 * Kernel socket node type.  This node type is basically a kernel-mode
 * version of a socket... kindof like the reverse of the socket node type.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/protosw.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include "ng_ksocket.h"

#include <netinet/in.h>

#define OFFSETOF(s, e) ((char *)&((s *)0)->e - (char *)((s *)0))
#define SADATA_OFFSET	(OFFSETOF(struct sockaddr, sa_data))

/* Node private data */
struct ng_ksocket_private {
	node_p		node;
	hook_p		hook;
	struct socket	*so;
	LIST_HEAD(, ng_ksocket_private)	embryos;
	LIST_ENTRY(ng_ksocket_private)	siblings;
	u_int32_t	flags;
	u_int32_t	response_token;
	char		response_addr[NG_PATHSIZ];
};
typedef struct ng_ksocket_private *priv_p;

/* Flags for priv_p */
#define	KSF_CONNECTING	0x00000001	/* Waiting for connection complete */
#define	KSF_ACCEPTING	0x00000002	/* Waiting for accept complete */
#define	KSF_EOFSEEN	0x00000004	/* Have sent 0-length EOF mbuf */
#define	KSF_CLONED	0x00000008	/* Cloned from an accepting socket */
#define	KSF_EMBRYONIC	0x00000010	/* Cloned node with no hooks yet */
#define	KSF_SENDING	0x00000020	/* Sending on socket */

/* Internal commands which we send to ourselves */
#define	NGM_KSOCKET_INTERNAL_COOKIE	(NGM_KSOCKET_COOKIE + 1)

enum {
	NGM_KSOCKET_INTERNAL_UPCALL = 1
};

/* Netgraph node methods */
static ng_constructor_t	ng_ksocket_constructor;
static ng_rcvmsg_t	ng_ksocket_rcvmsg;
static ng_shutdown_t	ng_ksocket_rmnode;
static ng_newhook_t	ng_ksocket_newhook;
static ng_rcvdata_t	ng_ksocket_rcvdata;
static ng_disconnect_t	ng_ksocket_disconnect;

/* Alias structure */
struct ng_ksocket_alias {
	const char	*name;
	const int	value;
	const int	family;
};

/* Protocol family aliases */
static const struct ng_ksocket_alias ng_ksocket_families[] = {
	{ "local",	PF_LOCAL	},
	{ "inet",	PF_INET		},
	{ "inet6",	PF_INET6	},
	{ "atm",	PF_ATM		},
	{ NULL,		-1		},
};

/* Socket type aliases */
static const struct ng_ksocket_alias ng_ksocket_types[] = {
	{ "stream",	SOCK_STREAM	},
	{ "dgram",	SOCK_DGRAM	},
	{ "raw",	SOCK_RAW	},
	{ "rdm",	SOCK_RDM	},
	{ "seqpacket",	SOCK_SEQPACKET	},
	{ NULL,		-1		},
};

/* Protocol aliases */
static const struct ng_ksocket_alias ng_ksocket_protos[] = {
	{ "ip",		IPPROTO_IP,		PF_INET		},
	{ "raw",	IPPROTO_RAW,		PF_INET		},
	{ "icmp",	IPPROTO_ICMP,		PF_INET		},
	{ "igmp",	IPPROTO_IGMP,		PF_INET		},
	{ "tcp",	IPPROTO_TCP,		PF_INET		},
	{ "udp",	IPPROTO_UDP,		PF_INET		},
	{ "gre",	IPPROTO_GRE,		PF_INET		},
	{ "esp",	IPPROTO_ESP,		PF_INET		},
	{ "ah",		IPPROTO_AH,		PF_INET		},
	{ "swipe",	IPPROTO_SWIPE,		PF_INET		},
	{ "encap",	IPPROTO_ENCAP,		PF_INET		},
	{ "divert",	IPPROTO_DIVERT,		PF_INET		},
	{ "pim",	IPPROTO_PIM,		PF_INET		},
	{ NULL,		-1					},
};

/* Helper functions */
static int	ng_ksocket_check_accept(priv_p);
static void	ng_ksocket_finish_accept(priv_p, struct ng_mesg **);
static void	ng_ksocket_incoming(struct socket *so, void *arg, int waitflag);
static int	ng_ksocket_parse(const struct ng_ksocket_alias *aliases,
			const char *s, int family);

/************************************************************************
			STRUCT SOCKADDR PARSE TYPE
 ************************************************************************/

/* Get the length of the data portion of a generic struct sockaddr */
static int
ng_parse_generic_sockdata_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct sockaddr *sa;

	sa = (const struct sockaddr *)(buf - SADATA_OFFSET);
	return (sa->sa_len < SADATA_OFFSET) ? 0 : sa->sa_len - SADATA_OFFSET;
}

/* Type for the variable length data portion of a generic struct sockaddr */
static const struct ng_parse_type ng_ksocket_generic_sockdata_type = {
	&ng_parse_bytearray_type,
	&ng_parse_generic_sockdata_getLength
};

/* Type for a generic struct sockaddr */
static const struct ng_parse_struct_field
    ng_parse_generic_sockaddr_type_fields[] = {
	  { "len",	&ng_parse_uint8_type			},
	  { "family",	&ng_parse_uint8_type			},
	  { "data",	&ng_ksocket_generic_sockdata_type	},
	  { NULL }
};
static const struct ng_parse_type ng_ksocket_generic_sockaddr_type = {
	&ng_parse_struct_type,
	&ng_parse_generic_sockaddr_type_fields
};

/* Convert a struct sockaddr from ASCII to binary.  If its a protocol
   family that we specially handle, do that, otherwise defer to the
   generic parse type ng_ksocket_generic_sockaddr_type. */
static int
ng_ksocket_sockaddr_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	struct sockaddr *const sa = (struct sockaddr *)buf;
	enum ng_parse_token tok;
	char fambuf[32];
	int family, len;
	char *t;

	/* If next token is a left curly brace, use generic parse type */
	if ((tok = ng_parse_get_token(s, off, &len)) == T_LBRACE) {
		return (*ng_ksocket_generic_sockaddr_type.supertype->parse)
		    (&ng_ksocket_generic_sockaddr_type,
		    s, off, start, buf, buflen);
	}

	/* Get socket address family followed by a slash */
	while (isspace(s[*off]))
		(*off)++;
	if ((t = index(s + *off, '/')) == NULL)
		return (EINVAL);
	if ((len = t - (s + *off)) > sizeof(fambuf) - 1)
		return (EINVAL);
	strncpy(fambuf, s + *off, len);
	fambuf[len] = '\0';
	*off += len + 1;
	if ((family = ng_ksocket_parse(ng_ksocket_families, fambuf, 0)) == -1)
		return (EINVAL);

	/* Set family */
	if (*buflen < SADATA_OFFSET)
		return (ERANGE);
	sa->sa_family = family;

	/* Set family-specific data and length */
	switch (sa->sa_family) {
	case PF_LOCAL:		/* Get pathname */
	    {
		const int pathoff = OFFSETOF(struct sockaddr_un, sun_path);
		struct sockaddr_un *const sun = (struct sockaddr_un *)sa;
		int toklen, pathlen;
		char *path;

		if ((path = ng_get_string_token(s, off, &toklen)) == NULL)
			return (EINVAL);
		pathlen = strlen(path);
		if (pathlen > SOCK_MAXADDRLEN) {
			kfree(path, M_NETGRAPH);
			return (E2BIG);
		}
		if (*buflen < pathoff + pathlen) {
			kfree(path, M_NETGRAPH);
			return (ERANGE);
		}
		*off += toklen;
		bcopy(path, sun->sun_path, pathlen);
		sun->sun_len = pathoff + pathlen;
		kfree(path, M_NETGRAPH);
		break;
	    }

	case PF_INET:		/* Get an IP address with optional port */
	    {
		struct sockaddr_in *const sin = (struct sockaddr_in *)sa;
		int i;

		/* Parse this: <ipaddress>[:port] */
		for (i = 0; i < 4; i++) {
			u_long val;
			char *eptr;

			val = strtoul(s + *off, &eptr, 10);
			if (val > 0xff || eptr == s + *off)
				return (EINVAL);
			*off += (eptr - (s + *off));
			((u_char *)&sin->sin_addr)[i] = (u_char)val;
			if (i < 3) {
				if (s[*off] != '.')
					return (EINVAL);
				(*off)++;
			} else if (s[*off] == ':') {
				(*off)++;
				val = strtoul(s + *off, &eptr, 10);
				if (val > 0xffff || eptr == s + *off)
					return (EINVAL);
				*off += (eptr - (s + *off));
				sin->sin_port = htons(val);
			} else
				sin->sin_port = 0;
		}
		bzero(&sin->sin_zero, sizeof(sin->sin_zero));
		sin->sin_len = sizeof(*sin);
		break;
	    }

#if 0
	case PF_INET6:
#endif

	default:
		return (EINVAL);
	}

	/* Done */
	*buflen = sa->sa_len;
	return (0);
}

/* Convert a struct sockaddr from binary to ASCII */
static int
ng_ksocket_sockaddr_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const struct sockaddr *sa = (const struct sockaddr *)(data + *off);
	int slen = 0;

	/* Output socket address, either in special or generic format */
	switch (sa->sa_family) {
	case PF_LOCAL:
	    {
		const int pathoff = OFFSETOF(struct sockaddr_un, sun_path);
		const struct sockaddr_un *sun = (const struct sockaddr_un *)sa;
		const int pathlen = sun->sun_len - pathoff;
		char pathbuf[SOCK_MAXADDRLEN + 1];
		char *pathtoken;

		bcopy(sun->sun_path, pathbuf, pathlen);
		pathbuf[pathlen] = '\0';
		if ((pathtoken = ng_encode_string(pathbuf)) == NULL)
			return (ENOMEM);
		slen += ksnprintf(cbuf, cbuflen, "local/%s", pathtoken);
		kfree(pathtoken, M_NETGRAPH);
		if (slen >= cbuflen)
			return (ERANGE);
		*off += sun->sun_len;
		return (0);
	    }

	case PF_INET:
	    {
		const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;

		slen += ksnprintf(cbuf, cbuflen, "inet/%d.%d.%d.%d",
		  ((const u_char *)&sin->sin_addr)[0],
		  ((const u_char *)&sin->sin_addr)[1],
		  ((const u_char *)&sin->sin_addr)[2],
		  ((const u_char *)&sin->sin_addr)[3]);
		if (sin->sin_port != 0) {
			slen += ksnprintf(cbuf + strlen(cbuf),
			    cbuflen - strlen(cbuf), ":%d",
			    (u_int)ntohs(sin->sin_port));
		}
		if (slen >= cbuflen)
			return (ERANGE);
		*off += sizeof(*sin);
		return(0);
	    }

#if 0
	case PF_INET6:
#endif

	default:
		return (*ng_ksocket_generic_sockaddr_type.supertype->unparse)
		    (&ng_ksocket_generic_sockaddr_type,
		    data, off, cbuf, cbuflen);
	}
}

/* Parse type for struct sockaddr */
static const struct ng_parse_type ng_ksocket_sockaddr_type = {
	NULL,
	NULL,
	NULL,
	&ng_ksocket_sockaddr_parse,
	&ng_ksocket_sockaddr_unparse,
	NULL		/* no such thing as a default struct sockaddr */
};

/************************************************************************
		STRUCT NG_KSOCKET_SOCKOPT PARSE TYPE
 ************************************************************************/

/* Get length of the struct ng_ksocket_sockopt value field, which is the
   just the excess of the message argument portion over the length of
   the struct ng_ksocket_sockopt. */
static int
ng_parse_sockoptval_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	static const int offset = OFFSETOF(struct ng_ksocket_sockopt, value);
	const struct ng_ksocket_sockopt *sopt;
	const struct ng_mesg *msg;

	sopt = (const struct ng_ksocket_sockopt *)(buf - offset);
	msg = (const struct ng_mesg *)((const u_char *)sopt - sizeof(*msg));
	return msg->header.arglen - sizeof(*sopt);
}

/* Parse type for the option value part of a struct ng_ksocket_sockopt
   XXX Eventually, we should handle the different socket options specially.
   XXX This would avoid byte order problems, eg an integer value of 1 is
   XXX going to be "[1]" for little endian or "[3=1]" for big endian. */
static const struct ng_parse_type ng_ksocket_sockoptval_type = {
	&ng_parse_bytearray_type,
	&ng_parse_sockoptval_getLength
};

/* Parse type for struct ng_ksocket_sockopt */
static const struct ng_parse_struct_field ng_ksocket_sockopt_type_fields[]
	= NG_KSOCKET_SOCKOPT_INFO(&ng_ksocket_sockoptval_type);
static const struct ng_parse_type ng_ksocket_sockopt_type = {
	&ng_parse_struct_type,
	&ng_ksocket_sockopt_type_fields
};

/* Parse type for struct ng_ksocket_accept */
static const struct ng_parse_struct_field ng_ksocket_accept_type_fields[]
	= NGM_KSOCKET_ACCEPT_INFO;
static const struct ng_parse_type ng_ksocket_accept_type = {
	&ng_parse_struct_type,
	&ng_ksocket_accept_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_ksocket_cmds[] = {
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_BIND,
	  "bind",
	  &ng_ksocket_sockaddr_type,
	  NULL
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_LISTEN,
	  "listen",
	  &ng_parse_int32_type,
	  NULL
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_ACCEPT,
	  "accept",
	  NULL,
	  &ng_ksocket_accept_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_CONNECT,
	  "connect",
	  &ng_ksocket_sockaddr_type,
	  &ng_parse_int32_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_GETNAME,
	  "getname",
	  NULL,
	  &ng_ksocket_sockaddr_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_GETPEERNAME,
	  "getpeername",
	  NULL,
	  &ng_ksocket_sockaddr_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_SETOPT,
	  "setopt",
	  &ng_ksocket_sockopt_type,
	  NULL
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_GETOPT,
	  "getopt",
	  &ng_ksocket_sockopt_type,
	  &ng_ksocket_sockopt_type
	},

	/* Internal commands */
	{
	  NGM_KSOCKET_INTERNAL_COOKIE,
	  NGM_KSOCKET_INTERNAL_UPCALL,
	  "upcall",
	  NULL,
	  NULL
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_ksocket_typestruct = {
	NG_VERSION,
	NG_KSOCKET_NODE_TYPE,
	NULL,
	ng_ksocket_constructor,
	ng_ksocket_rcvmsg,
	ng_ksocket_rmnode,
	ng_ksocket_newhook,
	NULL,
	NULL,
	ng_ksocket_rcvdata,
	ng_ksocket_rcvdata,
	ng_ksocket_disconnect,
	ng_ksocket_cmds
};
NETGRAPH_INIT(ksocket, &ng_ksocket_typestruct);

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Node type constructor
 */
static int
ng_ksocket_constructor(node_p *nodep)
{
	priv_p priv;
	int error;

	/* Allocate private structure */
	priv = kmalloc(sizeof(*priv), M_NETGRAPH, M_NOWAIT | M_ZERO);
	if (priv == NULL)
		return (ENOMEM);

	/* Call generic node constructor */
	if ((error = ng_make_node_common(&ng_ksocket_typestruct, nodep))) {
		kfree(priv, M_NETGRAPH);
		return (error);
	}
	(*nodep)->private = priv;
	priv->node = *nodep;
	LIST_INIT(&priv->embryos);

	/* Done */
	return (0);
}

/*
 * Give our OK for a hook to be added. The hook name is of the
 * form "<family>/<type>/<proto>" where the three components may
 * be decimal numbers or else aliases from the above lists.
 *
 * Connecting a hook amounts to opening the socket.  Disconnecting
 * the hook closes the socket and destroys the node as well.
 */
static int
ng_ksocket_newhook(node_p node, hook_p hook, const char *name0)
{
	struct thread *td = curthread->td_proc ? curthread : &thread0;	/* XXX broken */
	const priv_p priv = node->private;
	struct ng_mesg *msg;
	char *s1, *s2, name[NG_HOOKSIZ];
	int family, type, protocol, error;

	/* Check if we're already connected */
	if (priv->hook != NULL)
		return (EISCONN);

	if (priv->flags & KSF_CLONED) {
		if (priv->flags & KSF_EMBRYONIC) {
			/* Remove ourselves from our parent's embryo list */
			LIST_REMOVE(priv, siblings);
			priv->flags &= ~KSF_EMBRYONIC;
		}
	} else {
		/* Extract family, type, and protocol from hook name */
		ksnprintf(name, sizeof(name), "%s", name0);
		s1 = name;
		if ((s2 = index(s1, '/')) == NULL)
			return (EINVAL);
		*s2++ = '\0';
		family = ng_ksocket_parse(ng_ksocket_families, s1, 0);
		if (family == -1)
			return (EINVAL);
		s1 = s2;
		if ((s2 = index(s1, '/')) == NULL)
			return (EINVAL);
		*s2++ = '\0';
		type = ng_ksocket_parse(ng_ksocket_types, s1, 0);
		if (type == -1)
			return (EINVAL);
		s1 = s2;
		protocol = ng_ksocket_parse(ng_ksocket_protos, s1, family);
		if (protocol == -1)
			return (EINVAL);

		/* Create the socket */
		error = socreate(family, &priv->so, type, protocol, td);
		if (error != 0)
			return (error);

		/* XXX call soreserve() ? */

		/* Add our hook for incoming data and other events */
		priv->so->so_upcallarg = (caddr_t)node;
		priv->so->so_upcall = ng_ksocket_incoming;
		atomic_set_int(&priv->so->so_rcv.ssb_flags, SSB_UPCALL);
		atomic_set_int(&priv->so->so_snd.ssb_flags, SSB_UPCALL);
	}

	/* OK */
	priv->hook = hook;

	/*
	 * On a cloned socket we may have already received one or more
	 * upcalls which we couldn't handle without a hook.  Handle
	 * those now.  We cannot call the upcall function directly
	 * from here, because until this function has returned our
	 * hook isn't connected.  So we queue a message to ourselves
	 * which will cause the upcall function to be called a bit
	 * later.
	 */
	if (priv->flags & KSF_CLONED) {
		NG_MKMESSAGE(msg, NGM_KSOCKET_INTERNAL_COOKIE,
		    NGM_KSOCKET_INTERNAL_UPCALL, 0, M_NOWAIT);
		if (msg != NULL)
			ng_queue_msg(node, msg, ".:");
	}

	return (0);
}

/*
 * Receive a control message
 */
static int
ng_ksocket_rcvmsg(node_p node, struct ng_mesg *msg,
	      const char *raddr, struct ng_mesg **rptr)
{
	struct thread *td = curthread->td_proc ? curthread : &thread0;	/* XXX broken */
	const priv_p priv = node->private;
	struct socket *const so = priv->so;
	struct ng_mesg *resp = NULL;
	int error = 0;

	switch (msg->header.typecookie) {
	case NGM_KSOCKET_COOKIE:
		switch (msg->header.cmd) {
		case NGM_KSOCKET_BIND:
		    {
			struct sockaddr *const sa
			    = (struct sockaddr *)msg->data;

			/* Sanity check */
			if (msg->header.arglen < SADATA_OFFSET
			    || msg->header.arglen < sa->sa_len)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Bind */
			error = sobind(so, sa, td);
			break;
		    }
		case NGM_KSOCKET_LISTEN:
		    {
			/* Sanity check */
			if (msg->header.arglen != sizeof(int32_t))
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Listen */
			error = solisten(so, *((int32_t *)msg->data), td);
			break;
		    }

		case NGM_KSOCKET_ACCEPT:
		    {
			/* Sanity check */
			if (msg->header.arglen != 0)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Make sure the socket is capable of accepting */
			if (!(so->so_options & SO_ACCEPTCONN))
				ERROUT(EINVAL);
			if (priv->flags & KSF_ACCEPTING)
				ERROUT(EALREADY);

			error = ng_ksocket_check_accept(priv);
			if (error != 0 && error != EWOULDBLOCK)
				ERROUT(error);

			/*
			 * If a connection is already complete, take it.
			 * Otherwise let the upcall function deal with
			 * the connection when it comes in.
			 */
			priv->response_token = msg->header.token;
			strcpy(priv->response_addr, raddr);
			if (error == 0) {
				ng_ksocket_finish_accept(priv,
				    rptr != NULL ? &resp : NULL);
			} else
				priv->flags |= KSF_ACCEPTING;
			break;
		    }

		case NGM_KSOCKET_CONNECT:
		    {
			struct sockaddr *const sa
			    = (struct sockaddr *)msg->data;

			/* Sanity check */
			if (msg->header.arglen < SADATA_OFFSET
			    || msg->header.arglen < sa->sa_len)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Do connect */
			if ((so->so_state & SS_ISCONNECTING) != 0)
				ERROUT(EALREADY);
			if ((error = soconnect(so, sa, td, TRUE)) != 0) {
				soclrstate(so, SS_ISCONNECTING);
				ERROUT(error);
			}
			if ((so->so_state & SS_ISCONNECTING) != 0) {
				/* We will notify the sender when we connect */
				priv->response_token = msg->header.token;
				strcpy(priv->response_addr, raddr);
				priv->flags |= KSF_CONNECTING;
				ERROUT(EINPROGRESS);
			}
			break;
		    }

		case NGM_KSOCKET_GETNAME:
		case NGM_KSOCKET_GETPEERNAME:
		    {
			struct sockaddr *sa = NULL;
			int len;

			/* Sanity check */
			if (msg->header.arglen != 0)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Get function */
			if (msg->header.cmd == NGM_KSOCKET_GETPEERNAME) {
				if ((so->so_state
				    & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0) 
					ERROUT(ENOTCONN);
				error = so_pru_peeraddr(so, &sa);
			} else
				error = so_pru_sockaddr(so, &sa);

			/* Get local or peer address */
			if (error != 0)
				goto bail;
			len = (sa == NULL) ? 0 : sa->sa_len;

			/* Send it back in a response */
			NG_MKRESPONSE(resp, msg, len, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				goto bail;
			}
			bcopy(sa, resp->data, len);

		bail:
			/* Cleanup */
			if (sa != NULL)
				kfree(sa, M_SONAME);
			break;
		    }

		case NGM_KSOCKET_GETOPT:
		    {
			struct ng_ksocket_sockopt *ksopt = 
			    (struct ng_ksocket_sockopt *)msg->data;
			struct sockopt sopt;

			/* Sanity check */
			if (msg->header.arglen != sizeof(*ksopt))
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Get response with room for option value */
			NG_MKRESPONSE(resp, msg, sizeof(*ksopt)
			    + NG_KSOCKET_MAX_OPTLEN, M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);

			/* Get socket option, and put value in the response */
			sopt.sopt_dir = SOPT_GET;
			sopt.sopt_level = ksopt->level;
			sopt.sopt_name = ksopt->name;
			sopt.sopt_td = NULL;
			sopt.sopt_valsize = NG_KSOCKET_MAX_OPTLEN;
			ksopt = (struct ng_ksocket_sockopt *)resp->data;
			sopt.sopt_val = ksopt->value;
			if ((error = sogetopt(so, &sopt)) != 0) {
				kfree(resp, M_NETGRAPH);
				resp = NULL;
				break;
			}

			/* Set actual value length */
			resp->header.arglen = sizeof(*ksopt)
			    + sopt.sopt_valsize;
			break;
		    }

		case NGM_KSOCKET_SETOPT:
		    {
			struct ng_ksocket_sockopt *const ksopt = 
			    (struct ng_ksocket_sockopt *)msg->data;
			const int valsize = msg->header.arglen - sizeof(*ksopt);
			struct sockopt sopt;

			/* Sanity check */
			if (valsize < 0)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Set socket option */
			sopt.sopt_dir = SOPT_SET;
			sopt.sopt_level = ksopt->level;
			sopt.sopt_name = ksopt->name;
			sopt.sopt_val = ksopt->value;
			sopt.sopt_valsize = valsize;
			sopt.sopt_td = NULL;
			error = sosetopt(so, &sopt);
			break;
		    }

		default:
			error = EINVAL;
			break;
		}
		break;
	case NGM_KSOCKET_INTERNAL_COOKIE:
		switch (msg->header.cmd) {
		case NGM_KSOCKET_INTERNAL_UPCALL:
			if (so == NULL)
				ERROUT(ENXIO);
			(*priv->so->so_upcall)(so, so->so_upcallarg, M_NOWAIT);
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	if (rptr)
		*rptr = resp;
	else if (resp)
		kfree(resp, M_NETGRAPH);

done:
	kfree(msg, M_NETGRAPH);
	return (error);
}

/*
 * Receive incoming data on our hook.  Send it out the socket.
 */
static int
ng_ksocket_rcvdata(hook_p hook, struct mbuf *m, meta_p meta)
{
	struct thread *td = curthread->td_proc ? curthread : &thread0;	/* XXX broken */
	const node_p node = hook->node;
	const priv_p priv = node->private;
	struct socket *const so = priv->so;
	struct sockaddr *sa = NULL;
	int error;

	/* Avoid reentrantly sending on the socket */
	if ((priv->flags & KSF_SENDING) != 0) {
		NG_FREE_DATA(m, meta);
		return (EDEADLK);
	}

	/* If any meta info, look for peer socket address */
	if (meta != NULL) {
		struct meta_field_header *field;

		/* Look for peer socket address */
		for (field = &meta->options[0];
		    (caddr_t)field < (caddr_t)meta + meta->used_len;
		    field = (struct meta_field_header *)
		      ((caddr_t)field + field->len)) {
			if (field->cookie != NGM_KSOCKET_COOKIE
			    || field->type != NG_KSOCKET_META_SOCKADDR)
				continue;
			sa = (struct sockaddr *)field->data;
			break;
		}
	}

	/* Send packet */
	priv->flags |= KSF_SENDING;
	error = so_pru_sosend(so, sa, NULL, m, NULL, 0, td);
	priv->flags &= ~KSF_SENDING;

	/* Clean up and exit */
	NG_FREE_META(meta);
	return (error);
}

/*
 * Destroy node
 */
static int
ng_ksocket_rmnode(node_p node)
{
	const priv_p priv = node->private;
	priv_p embryo;

	/* Close our socket (if any) */
	if (priv->so != NULL) {
		priv->so->so_upcall = NULL;
		atomic_clear_int(&priv->so->so_rcv.ssb_flags, SSB_UPCALL);
		atomic_clear_int(&priv->so->so_snd.ssb_flags, SSB_UPCALL);
		soclose(priv->so, FNONBLOCK);
		priv->so = NULL;
	}

	/* If we are an embryo, take ourselves out of the parent's list */
	if (priv->flags & KSF_EMBRYONIC) {
		LIST_REMOVE(priv, siblings);
		priv->flags &= ~KSF_EMBRYONIC;
	}

	/* Remove any embryonic children we have */
	while (!LIST_EMPTY(&priv->embryos)) {
		embryo = LIST_FIRST(&priv->embryos);
		ng_rmnode(embryo->node);
	}

	/* Take down netgraph node */
	node->flags |= NG_INVALID;
	ng_cutlinks(node);
	ng_unname(node);
	bzero(priv, sizeof(*priv));
	kfree(priv, M_NETGRAPH);
	node->private = NULL;
	ng_unref(node);		/* let the node escape */
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_ksocket_disconnect(hook_p hook)
{
	KASSERT(hook->node->numhooks == 0,
	    ("%s: numhooks=%d?", __func__, hook->node->numhooks));
	ng_rmnode(hook->node);
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * When incoming data is appended to the socket, we get notified here.
 * This is also called whenever a significant event occurs for the socket.
 */
static void
ng_ksocket_incoming(struct socket *so, void *arg, int waitflag)
{
	const node_p node = arg;
	const priv_p priv = node->private;
	struct ng_mesg *response;
	int error;

	crit_enter();

	/* Sanity check */
	if ((node->flags & NG_INVALID) != 0) {
		crit_exit();
		return;
	}
	KASSERT(so == priv->so, ("%s: wrong socket", __func__));

	/* Check whether a pending connect operation has completed */
	if (priv->flags & KSF_CONNECTING) {
		if ((error = so->so_error) != 0) {
			so->so_error = 0;
			soclrstate(so, SS_ISCONNECTING);
		}
		if (!(so->so_state & SS_ISCONNECTING)) {
			NG_MKMESSAGE(response, NGM_KSOCKET_COOKIE,
			    NGM_KSOCKET_CONNECT, sizeof(int32_t), waitflag);
			if (response != NULL) {
				response->header.flags |= NGF_RESP;
				response->header.token = priv->response_token;
				*(int32_t *)response->data = error;
				/*
				 * XXX We use ng_queue_msg here because we are
				 * being called from deep in the bowels of the TCP
				 * stack.  Is this right, or should we let the
				 * receiver of the message worry about that?
				 */
				ng_queue_msg(node, response,
				    priv->response_addr);
			}
			priv->flags &= ~KSF_CONNECTING;
		}
	}

	/* Check whether a pending accept operation has completed */
	if (priv->flags & KSF_ACCEPTING) {
		error = ng_ksocket_check_accept(priv);
		if (error != EWOULDBLOCK)
			priv->flags &= ~KSF_ACCEPTING;
		if (error == 0)
			ng_ksocket_finish_accept(priv, NULL);
	}

	/*
	 * If we don't have a hook, we must handle data events later.  When
	 * the hook gets created and is connected, this upcall function
	 * will be called again.
	 */
	if (priv->hook == NULL) {
		crit_exit();
		return;
	}

	/* Read and forward available mbuf's */
	while (1) {
		struct sockaddr *sa = NULL;
		struct sockbuf sio;
		meta_p meta = NULL;
		struct mbuf *n;
		int flags;

		sbinit(&sio, 1000000000);
		flags = MSG_DONTWAIT;

		/* Try to get next packet from socket */
		error = so_pru_soreceive(so,
				((so->so_state & SS_ISCONNECTED) ? NULL : &sa),
				NULL, &sio, NULL, &flags);
		if (error)
			break;

		/* See if we got anything */
		if (sio.sb_mb == NULL) {
			if (sa != NULL)
				kfree(sa, M_SONAME);
			break;
		}

		/* Don't trust the various socket layers to get the
		   packet header and length correct (eg. kern/15175) */
		sio.sb_mb->m_pkthdr.len = 0;
		for (n = sio.sb_mb; n != NULL; n = n->m_next)
			sio.sb_mb->m_pkthdr.len += n->m_len;

		/* Put peer's socket address (if any) into a meta info blob */
		if (sa != NULL) {
			struct meta_field_header *mhead;
			u_int len;

			len = sizeof(*meta) + sizeof(*mhead) + sa->sa_len;
			meta = kmalloc(len, M_NETGRAPH, M_NOWAIT);
			if (meta == NULL) {
				kfree(sa, M_SONAME);
				goto sendit;
			}
			mhead = &meta->options[0];
			bzero(meta, sizeof(*meta));
			bzero(mhead, sizeof(*mhead));
			meta->allocated_len = len;
			meta->used_len = len;
			mhead->cookie = NGM_KSOCKET_COOKIE;
			mhead->type = NG_KSOCKET_META_SOCKADDR;
			mhead->len = sizeof(*mhead) + sa->sa_len;
			bcopy(sa, mhead->data, sa->sa_len);
			kfree(sa, M_SONAME);
		}
sendit:		/* Forward data with optional peer sockaddr as meta info */
		NG_SEND_DATA(error, priv->hook, sio.sb_mb, meta);
	}

	/*
	 * If the peer has closed the connection, forward a 0-length mbuf
	 * to indicate end-of-file.
	 */
	if (so->so_state & SS_CANTRCVMORE && !(priv->flags & KSF_EOFSEEN)) {
		struct mbuf *m;

		MGETHDR(m, waitflag, MT_DATA);
		if (m != NULL) {
			m->m_len = m->m_pkthdr.len = 0;
			NG_SEND_DATA_ONLY(error, priv->hook, m);
		}
		priv->flags |= KSF_EOFSEEN;
	}

	crit_exit();
}

/*
 * Check for a completed incoming connection and return 0 if one is found.
 * Otherwise return the appropriate error code.
 */
static int
ng_ksocket_check_accept(priv_p priv)
{
	struct socket *const head = priv->so;
	int error;

	lwkt_gettoken(&head->so_rcv.ssb_token);
	if ((error = head->so_error) != 0) {
		head->so_error = 0;
		lwkt_reltoken(&head->so_rcv.ssb_token);
		return error;
	}
	if (TAILQ_EMPTY(&head->so_comp)) {
		if (head->so_state & SS_CANTRCVMORE)
			error = ECONNABORTED;
		else
			error = EWOULDBLOCK;
	}
	lwkt_reltoken(&head->so_rcv.ssb_token);
	return error;
}

/*
 * Handle the first completed incoming connection, assumed to be already
 * on the socket's so_comp queue.
 */
static void
ng_ksocket_finish_accept(priv_p priv, struct ng_mesg **rptr)
{
	struct socket *const head = priv->so;
	struct socket *so;
	struct sockaddr *sa = NULL;
	struct ng_mesg *resp;
	struct ng_ksocket_accept *resp_data;
	node_p node2;
	priv_p priv2;
	int len;

	lwkt_getpooltoken(head);
	so = TAILQ_FIRST(&head->so_comp);
	if (so == NULL)	{	/* Should never happen */
		lwkt_relpooltoken(head);
		return;
	}
	TAILQ_REMOVE(&head->so_comp, so, so_list);
	head->so_qlen--;
	soclrstate(so, SS_COMP);
	so->so_head = NULL;
	soreference(so);

	lwkt_relpooltoken(head);

	/* XXX KNOTE(&head->so_rcv.ssb_sel.si_note, 0); */

	soaccept(so, &sa);

	len = OFFSETOF(struct ng_ksocket_accept, addr);
	if (sa != NULL)
		len += sa->sa_len;

	NG_MKMESSAGE(resp, NGM_KSOCKET_COOKIE, NGM_KSOCKET_ACCEPT, len,
	    M_NOWAIT);
	if (resp == NULL) {
		soclose(so, FNONBLOCK);
		goto out;
	}
	resp->header.flags |= NGF_RESP;
	resp->header.token = priv->response_token;

	/* Clone a ksocket node to wrap the new socket */
	if (ng_ksocket_constructor(&node2) != 0) {
		kfree(resp, M_NETGRAPH);
		soclose(so, FNONBLOCK);
		goto out;
	}
	priv2 = (priv_p)node2->private;
	priv2->so = so;
	priv2->flags |= KSF_CLONED | KSF_EMBRYONIC;

	/*
	 * Insert the cloned node into a list of embryonic children
	 * on the parent node.  When a hook is created on the cloned
	 * node it will be removed from this list.  When the parent
	 * is destroyed it will destroy any embryonic children it has.
	 */
	LIST_INSERT_HEAD(&priv->embryos, priv2, siblings);

	so->so_upcallarg = (caddr_t)node2;
	so->so_upcall = ng_ksocket_incoming;
	atomic_set_int(&so->so_rcv.ssb_flags, SSB_UPCALL);
	atomic_set_int(&so->so_snd.ssb_flags, SSB_UPCALL);

	/* Fill in the response data and send it or return it to the caller */
	resp_data = (struct ng_ksocket_accept *)resp->data;
	resp_data->nodeid = node2->ID;
	if (sa != NULL)
		bcopy(sa, &resp_data->addr, sa->sa_len);
	if (rptr != NULL)
		*rptr = resp;
	else
		ng_queue_msg(priv->node, resp, priv->response_addr);

out:
	if (sa != NULL)
		kfree(sa, M_SONAME);
}

/*
 * Parse out either an integer value or an alias.
 */
static int
ng_ksocket_parse(const struct ng_ksocket_alias *aliases,
	const char *s, int family)
{
	int k, val;
	char *eptr;

	/* Try aliases */
	for (k = 0; aliases[k].name != NULL; k++) {
		if (strcmp(s, aliases[k].name) == 0
		    && aliases[k].family == family)
			return aliases[k].value;
	}

	/* Try parsing as a number */
	val = (int)strtoul(s, &eptr, 10);
	if (val < 0 || *eptr != '\0')
		return (-1);
	return (val);
}
