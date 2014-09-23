
/*
 * ng_mppc.c
 *
 * Copyright (c) 1996-2000 Whistle Communications, Inc.
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
 * $Whistle: ng_mppc.c,v 1.4 1999/11/25 00:10:12 archie Exp $
 * $FreeBSD: src/sys/netgraph/ng_mppc.c,v 1.1.2.7 2002/12/16 17:58:42 archie Exp $
 */

/*
 * Microsoft PPP compression (MPPC) and encryption (MPPE) netgraph node type.
 *
 * You must define one or both of the NETGRAPH_MPPC_COMPRESSION and/or
 * NETGRAPH_MPPC_ENCRYPTION options for this node type to be useful.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/syslog.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include "ng_mppc.h"

#include "opt_netgraph.h"

#if !defined(NETGRAPH_MPPC_COMPRESSION) && !defined(NETGRAPH_MPPC_ENCRYPTION)
#error Need either NETGRAPH_MPPC_COMPRESSION or NETGRAPH_MPPC_ENCRYPTION
#endif

#ifdef NETGRAPH_MPPC_COMPRESSION
/* XXX this file doesn't exist yet, but hopefully someday it will... */
#include <net/mppc.h>
#endif
#ifdef NETGRAPH_MPPC_ENCRYPTION
#include <crypto/rc4/rc4.h>
#endif
#include <crypto/sha1.h>

/* Decompression blowup */
#define MPPC_DECOMP_BUFSIZE	8092            /* allocate buffer this big */
#define MPPC_DECOMP_SAFETY	100             /*   plus this much margin */

/* MPPC/MPPE header length */
#define MPPC_HDRLEN		2

/* Key length */
#define KEYLEN(b)		(((b) & MPPE_128) ? 16 : 8)

/*
 * When packets are lost with MPPE, we may have to re-key arbitrarily
 * many times to 'catch up' to the new jumped-ahead sequence number.
 * Since this can be expensive, we pose a limit on how many re-keyings
 * we will do at one time to avoid a possible D.O.S. vulnerability.
 * This should instead be a configurable parameter.
 */
#define MPPE_MAX_REKEY		1000

/* MPPC packet header bits */
#define MPPC_FLAG_FLUSHED	0x8000		/* xmitter reset state */
#define MPPC_FLAG_RESTART	0x4000		/* compress history restart */
#define MPPC_FLAG_COMPRESSED	0x2000		/* packet is compresed */
#define MPPC_FLAG_ENCRYPTED	0x1000		/* packet is encrypted */
#define MPPC_CCOUNT_MASK	0x0fff		/* sequence number mask */

#define MPPE_UPDATE_MASK	0xff		/* coherency count when we're */
#define MPPE_UPDATE_FLAG	0xff		/*   supposed to update key */

#define MPPC_COMP_OK		0x05
#define MPPC_DECOMP_OK		0x05

/* Per direction info */
struct ng_mppc_dir {
	struct ng_mppc_config	cfg;		/* configuration */
	hook_p			hook;		/* netgraph hook */
	u_int16_t		cc:12;		/* coherency count */
	u_char			flushed;	/* clean history (xmit only) */
#ifdef NETGRAPH_MPPC_COMPRESSION
	u_char			*history;	/* compression history */
#endif
#ifdef NETGRAPH_MPPC_ENCRYPTION
	u_char			key[MPPE_KEY_LEN];	/* session key */
	struct rc4_state	rc4;			/* rc4 state */
#endif
};

/* Node private data */
struct ng_mppc_private {
	struct ng_mppc_dir	xmit;		/* compress/encrypt config */
	struct ng_mppc_dir	recv;		/* decompress/decrypt config */
	char			*ctrlpath;	/* path to controlling node */
};
typedef struct ng_mppc_private *priv_p;

/* Netgraph node methods */
static ng_constructor_t	ng_mppc_constructor;
static ng_rcvmsg_t	ng_mppc_rcvmsg;
static ng_shutdown_t	ng_mppc_rmnode;
static ng_newhook_t	ng_mppc_newhook;
static ng_rcvdata_t	ng_mppc_rcvdata;
static ng_disconnect_t	ng_mppc_disconnect;

/* Helper functions */
static int	ng_mppc_compress(node_p node,
			struct mbuf *m, struct mbuf **resultp);
static int	ng_mppc_decompress(node_p node,
			struct mbuf *m, struct mbuf **resultp);
static void	ng_mppc_getkey(const u_char *h, u_char *h2, int len);
static void	ng_mppc_updatekey(u_int32_t bits,
			u_char *key0, u_char *key, struct rc4_state *rc4);
static void	ng_mppc_reset_req(node_p node);

/* Node type descriptor */
static struct ng_type ng_mppc_typestruct = {
	NG_VERSION,
	NG_MPPC_NODE_TYPE,
	NULL,
	ng_mppc_constructor,
	ng_mppc_rcvmsg,
	ng_mppc_rmnode,
	ng_mppc_newhook,
	NULL,
	NULL,
	ng_mppc_rcvdata,
	ng_mppc_rcvdata,
	ng_mppc_disconnect,
	NULL
};
NETGRAPH_INIT(mppc, &ng_mppc_typestruct);

/* Fixed bit pattern to weaken keysize down to 40 or 56 bits */
static const u_char ng_mppe_weakenkey[3] = { 0xd1, 0x26, 0x9e };

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Node type constructor
 */
static int
ng_mppc_constructor(node_p *nodep)
{
	priv_p priv;
	int error;

	/* Allocate private structure */
	priv = kmalloc(sizeof(*priv), M_NETGRAPH, M_NOWAIT | M_ZERO);
	if (priv == NULL)
		return (ENOMEM);

	/* Call generic node constructor */
	if ((error = ng_make_node_common(&ng_mppc_typestruct, nodep))) {
		kfree(priv, M_NETGRAPH);
		return (error);
	}
	(*nodep)->private = priv;

	/* Done */
	return (0);
}

/*
 * Give our OK for a hook to be added
 */
static int
ng_mppc_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = node->private;
	hook_p *hookPtr;

	/* Check hook name */
	if (strcmp(name, NG_MPPC_HOOK_COMP) == 0)
		hookPtr = &priv->xmit.hook;
	else if (strcmp(name, NG_MPPC_HOOK_DECOMP) == 0)
		hookPtr = &priv->recv.hook;
	else
		return (EINVAL);

	/* See if already connected */
	if (*hookPtr != NULL)
		return (EISCONN);

	/* OK */
	*hookPtr = hook;
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_mppc_rcvmsg(node_p node, struct ng_mesg *msg,
	      const char *raddr, struct ng_mesg **rptr)
{
	const priv_p priv = node->private;
	struct ng_mesg *resp = NULL;
	int error = 0;

	switch (msg->header.typecookie) {
	case NGM_MPPC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_MPPC_CONFIG_COMP:
		case NGM_MPPC_CONFIG_DECOMP:
		    {
			struct ng_mppc_config *const cfg
			    = (struct ng_mppc_config *)msg->data;
			const int isComp =
			    msg->header.cmd == NGM_MPPC_CONFIG_COMP;
			struct ng_mppc_dir *const d = isComp ?
			    &priv->xmit : &priv->recv;

			/* Check configuration */
			if (msg->header.arglen != sizeof(*cfg))
				ERROUT(EINVAL);
			if (cfg->enable) {
				if ((cfg->bits & ~MPPC_VALID_BITS) != 0)
					ERROUT(EINVAL);
#ifndef NETGRAPH_MPPC_COMPRESSION
				if ((cfg->bits & MPPC_BIT) != 0)
					ERROUT(EPROTONOSUPPORT);
#endif
#ifndef NETGRAPH_MPPC_ENCRYPTION
				if ((cfg->bits & MPPE_BITS) != 0)
					ERROUT(EPROTONOSUPPORT);
#endif
			} else
				cfg->bits = 0;

			/* Save return address so we can send reset-req's */
			if (!isComp && priv->ctrlpath != NULL) {
				kfree(priv->ctrlpath, M_NETGRAPH);
				priv->ctrlpath = NULL;
			}
			if (!isComp && raddr != NULL) {
				priv->ctrlpath = kmalloc(strlen(raddr) + 1,
							 M_NETGRAPH, M_NOWAIT);
				if (priv->ctrlpath == NULL)
					ERROUT(ENOMEM);
				strcpy(priv->ctrlpath, raddr);
			}

			/* Configuration is OK, reset to it */
			d->cfg = *cfg;

#ifdef NETGRAPH_MPPC_COMPRESSION
			/* Initialize state buffers for compression */
			if (d->history != NULL) {
				kfree(d->history, M_NETGRAPH);
				d->history = NULL;
			}
			if ((cfg->bits & MPPC_BIT) != 0) {
				d->history = kmalloc(isComp ? MPPC_SizeOfCompressionHistory() : MPPC_SizeOfDecompressionHistory(),
						     M_NETGRAPH, M_NOWAIT);
				if (d->history == NULL)
					ERROUT(ENOMEM);
				if (isComp)
					MPPC_InitCompressionHistory(d->history);
				else {
					MPPC_InitDecompressionHistory(
					    d->history);
				}
			}
#endif

#ifdef NETGRAPH_MPPC_ENCRYPTION
			/* Generate initial session keys for encryption */
			if ((cfg->bits & MPPE_BITS) != 0) {
				const int keylen = KEYLEN(cfg->bits);

				bcopy(cfg->startkey, d->key, keylen);
				ng_mppc_getkey(cfg->startkey, d->key, keylen);
				if ((cfg->bits & MPPE_40) != 0)
					bcopy(&ng_mppe_weakenkey, d->key, 3);
				else if ((cfg->bits & MPPE_56) != 0)
					bcopy(&ng_mppe_weakenkey, d->key, 1);
				rc4_init(&d->rc4, d->key, keylen);
			}
#endif

			/* Initialize other state */
			d->cc = 0;
			d->flushed = 0;
			break;
		    }

		case NGM_MPPC_RESETREQ:
			ng_mppc_reset_req(node);
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
 * Receive incoming data on our hook.
 */
static int
ng_mppc_rcvdata(hook_p hook, struct mbuf *m, meta_p meta)
{
	const node_p node = hook->node;
	const priv_p priv = node->private;
	struct mbuf *out;
	int error;

	/* Compress and/or encrypt */
	if (hook == priv->xmit.hook) {
		if (!priv->xmit.cfg.enable) {
			NG_FREE_DATA(m, meta);
			return (ENXIO);
		}
		if ((error = ng_mppc_compress(node, m, &out)) != 0) {
			NG_FREE_DATA(m, meta);
			return(error);
		}
		m_freem(m);
		NG_SEND_DATA(error, priv->xmit.hook, out, meta);
		return (error);
	}

	/* Decompress and/or decrypt */
	if (hook == priv->recv.hook) {
		if (!priv->recv.cfg.enable) {
			NG_FREE_DATA(m, meta);
			return (ENXIO);
		}
		if ((error = ng_mppc_decompress(node, m, &out)) != 0) {
			NG_FREE_DATA(m, meta);
			if (error == EINVAL && priv->ctrlpath != NULL) {
				struct ng_mesg *msg;

				/* Need to send a reset-request */
				NG_MKMESSAGE(msg, NGM_MPPC_COOKIE,
				    NGM_MPPC_RESETREQ, 0, M_NOWAIT);
				if (msg == NULL)
					return (error);
				ng_send_msg(node, msg, priv->ctrlpath, NULL);
			}
			return (error);
		}
		m_freem(m);
		NG_SEND_DATA(error, priv->recv.hook, out, meta);
		return (error);
	}

	/* Oops */
	panic("%s: unknown hook", __func__);
}

/*
 * Destroy node
 */
static int
ng_mppc_rmnode(node_p node)
{
	const priv_p priv = node->private;

	/* Take down netgraph node */
	node->flags |= NG_INVALID;
	ng_cutlinks(node);
	ng_unname(node);
	if (priv->ctrlpath != NULL)
		kfree(priv->ctrlpath, M_NETGRAPH);
#ifdef NETGRAPH_MPPC_COMPRESSION
	if (priv->xmit.history != NULL)
		kfree(priv->xmit.history, M_NETGRAPH);
	if (priv->recv.history != NULL)
		kfree(priv->recv.history, M_NETGRAPH);
#endif
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
ng_mppc_disconnect(hook_p hook)
{
	const node_p node = hook->node;
	const priv_p priv = node->private;

	/* Zero out hook pointer */
	if (hook == priv->xmit.hook)
		priv->xmit.hook = NULL;
	if (hook == priv->recv.hook)
		priv->recv.hook = NULL;

	/* Go away if no longer connected */
	if (node->numhooks == 0)
		ng_rmnode(node);
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * Compress/encrypt a packet and put the result in a new mbuf at *resultp.
 * The original mbuf is not free'd.
 */
static int
ng_mppc_compress(node_p node, struct mbuf *m, struct mbuf **resultp)
{
	const priv_p priv = node->private;
	struct ng_mppc_dir *const d = &priv->xmit;
	u_char *inbuf, *outbuf;
	int outlen, inlen;
	u_int16_t header;

	/* Initialize */
	*resultp = NULL;
	header = d->cc;
	if (d->flushed) {
		header |= MPPC_FLAG_FLUSHED;
		d->flushed = 0;
	}

	/* Work with contiguous regions of memory */
	inlen = m->m_pkthdr.len;
	inbuf = kmalloc(inlen, M_NETGRAPH, M_NOWAIT);
	if (inbuf == NULL)
		return (ENOMEM);
	m_copydata(m, 0, inlen, (caddr_t)inbuf);
	if ((d->cfg.bits & MPPC_BIT) != 0)
		outlen = MPPC_MAX_BLOWUP(inlen);
	else
		outlen = MPPC_HDRLEN + inlen;
	outbuf = kmalloc(outlen, M_NETGRAPH, M_NOWAIT);
	if (outbuf == NULL) {
		kfree(inbuf, M_NETGRAPH);
		return (ENOMEM);
	}

	/* Compress "inbuf" into "outbuf" (if compression enabled) */
#ifdef NETGRAPH_MPPC_COMPRESSION
	if ((d->cfg.bits & MPPC_BIT) != 0) {
		u_short flags = MPPC_MANDATORY_COMPRESS_FLAGS;
		u_char *source, *dest;
		u_long sourceCnt, destCnt;
		int rtn;

		/* Prepare to compress */
		source = inbuf;
		sourceCnt = inlen;
		dest = outbuf + MPPC_HDRLEN;
		destCnt = outlen - MPPC_HDRLEN;
		if ((d->cfg.bits & MPPE_STATELESS) == 0)
			flags |= MPPC_SAVE_HISTORY;

		/* Compress */
		rtn = MPPC_Compress(&source, &dest, &sourceCnt,
			&destCnt, d->history, flags, 0);

		/* Check return value */
		KASSERT(rtn != MPPC_INVALID, ("%s: invalid", __func__));
		if ((rtn & MPPC_EXPANDED) == 0
		    && (rtn & MPPC_COMP_OK) == MPPC_COMP_OK) {
			outlen -= destCnt;     
			header |= MPPC_FLAG_COMPRESSED;
			if ((rtn & MPPC_RESTART_HISTORY) != 0)
				header |= MPPC_FLAG_RESTART;  
		}
		d->flushed = (rtn & MPPC_EXPANDED) != 0
		    || (flags & MPPC_SAVE_HISTORY) == 0;
	}
#endif

	/* If we did not compress this packet, copy it to output buffer */
	if ((header & MPPC_FLAG_COMPRESSED) == 0) {
		bcopy(inbuf, outbuf + MPPC_HDRLEN, inlen);
		outlen = MPPC_HDRLEN + inlen;
	}
	kfree(inbuf, M_NETGRAPH);

	/* Always set the flushed bit in stateless mode */
	if ((d->cfg.bits & MPPE_STATELESS) != 0)
		header |= MPPC_FLAG_FLUSHED;

	/* Now encrypt packet (if encryption enabled) */
#ifdef NETGRAPH_MPPC_ENCRYPTION
	if ((d->cfg.bits & MPPE_BITS) != 0) {

		/* Set header bits; need to reset key if we say we did */
		header |= MPPC_FLAG_ENCRYPTED;
		if ((header & MPPC_FLAG_FLUSHED) != 0)
			rc4_init(&d->rc4, d->key, KEYLEN(d->cfg.bits));

		/* Update key if it's time */
		if ((d->cfg.bits & MPPE_STATELESS) != 0
		    || (d->cc & MPPE_UPDATE_MASK) == MPPE_UPDATE_FLAG) {
			  ng_mppc_updatekey(d->cfg.bits,
			      d->cfg.startkey, d->key, &d->rc4);
		}

		/* Encrypt packet */
		rc4_crypt(&d->rc4, outbuf + MPPC_HDRLEN,
			outbuf + MPPC_HDRLEN, outlen - MPPC_HDRLEN);
	}
#endif

	/* Update sequence number */
	d->cc++;

	/* Install header */
	*((u_int16_t *)outbuf) = htons(header);

	/* Return packet in an mbuf */
	*resultp = m_devget((caddr_t)outbuf, outlen, 0, NULL, NULL);
	kfree(outbuf, M_NETGRAPH);
	return (*resultp == NULL ? ENOBUFS : 0);
}

/*
 * Decompress/decrypt packet and put the result in a new mbuf at *resultp.
 * The original mbuf is not free'd.
 */
static int
ng_mppc_decompress(node_p node, struct mbuf *m, struct mbuf **resultp)
{
	const priv_p priv = node->private;
	struct ng_mppc_dir *const d = &priv->recv;
	u_int16_t header, cc;
	u_int numLost;
	u_char *buf;
	int len;

	/* Pull off header */
	if (m->m_pkthdr.len < MPPC_HDRLEN)
		return (EINVAL);
	m_copydata(m, 0, MPPC_HDRLEN, (caddr_t)&header);
	header = ntohs(header);
	cc = (header & MPPC_CCOUNT_MASK);

	/* Copy payload into a contiguous region of memory */
	len = m->m_pkthdr.len - MPPC_HDRLEN;
	buf = kmalloc(len, M_NETGRAPH, M_NOWAIT);
	if (buf == NULL)
		return (ENOMEM);
	m_copydata(m, MPPC_HDRLEN, len, (caddr_t)buf);

	/* Check for an unexpected jump in the sequence number */
	numLost = ((cc - d->cc) & MPPC_CCOUNT_MASK);

	/* If flushed bit set, we can always handle packet */
	if ((header & MPPC_FLAG_FLUSHED) != 0) {
#ifdef NETGRAPH_MPPC_COMPRESSION
		if (d->history != NULL)
			MPPC_InitDecompressionHistory(d->history);
#endif
#ifdef NETGRAPH_MPPC_ENCRYPTION
		if ((d->cfg.bits & MPPE_BITS) != 0) {
			u_int rekey;

			/* How many times are we going to have to re-key? */
			rekey = ((d->cfg.bits & MPPE_STATELESS) != 0) ?
			    numLost : (numLost / (MPPE_UPDATE_MASK + 1));
			if (rekey > MPPE_MAX_REKEY) {
				log(LOG_ERR, "%s: too many (%d) packets"
				    " dropped, disabling node %p!",
				    __func__, numLost, node);
				priv->recv.cfg.enable = 0;
				goto failed;
			}

			/* Re-key as necessary to catch up to peer */
			while (d->cc != cc) {
				if ((d->cfg.bits & MPPE_STATELESS) != 0
				    || (d->cc & MPPE_UPDATE_MASK)
				      == MPPE_UPDATE_FLAG) {
					ng_mppc_updatekey(d->cfg.bits,
					    d->cfg.startkey, d->key, &d->rc4);
				}
				d->cc++;
			}

			/* Reset key (except in stateless mode, see below) */
			if ((d->cfg.bits & MPPE_STATELESS) == 0)
				rc4_init(&d->rc4, d->key, KEYLEN(d->cfg.bits));
		}
#endif
		d->cc = cc;		/* skip over lost seq numbers */
		numLost = 0;		/* act like no packets were lost */
	}

	/* Can't decode non-sequential packets without a flushed bit */
	if (numLost != 0)
		goto failed;

	/* Decrypt packet */
	if ((header & MPPC_FLAG_ENCRYPTED) != 0) {

		/* Are we not expecting encryption? */
		if ((d->cfg.bits & MPPE_BITS) == 0) {
			log(LOG_ERR, "%s: rec'd unexpectedly %s packet",
				__func__, "encrypted");
			goto failed;
		}

#ifdef NETGRAPH_MPPC_ENCRYPTION
		/* Update key if it's time (always in stateless mode) */
		if ((d->cfg.bits & MPPE_STATELESS) != 0
		    || (d->cc & MPPE_UPDATE_MASK) == MPPE_UPDATE_FLAG) {
			ng_mppc_updatekey(d->cfg.bits,
			    d->cfg.startkey, d->key, &d->rc4);
		}

		/* Decrypt packet */
		rc4_crypt(&d->rc4, buf, buf, len);
#endif
	} else {

		/* Are we expecting encryption? */
		if ((d->cfg.bits & MPPE_BITS) != 0) {
			log(LOG_ERR, "%s: rec'd unexpectedly %s packet",
				__func__, "unencrypted");
			goto failed;
		}
	}

	/* Update coherency count for next time (12 bit arithmetic) */
	d->cc++;

	/* Check for unexpected compressed packet */
	if ((header & MPPC_FLAG_COMPRESSED) != 0
	    && (d->cfg.bits & MPPC_BIT) == 0) {
		log(LOG_ERR, "%s: rec'd unexpectedly %s packet",
			__func__, "compressed");
failed:
		kfree(buf, M_NETGRAPH);
		return (EINVAL);
	}

#ifdef NETGRAPH_MPPC_COMPRESSION
	/* Decompress packet */
	if ((header & MPPC_FLAG_COMPRESSED) != 0) {
		int flags = MPPC_MANDATORY_DECOMPRESS_FLAGS;
		u_char *decompbuf, *source, *dest;
		u_long sourceCnt, destCnt;
		int decomplen, rtn;

		/* Allocate a buffer for decompressed data */
		decompbuf = kmalloc(MPPC_DECOMP_BUFSIZE + MPPC_DECOMP_SAFETY,
				    M_NETGRAPH, M_NOWAIT);
		if (decompbuf == NULL) {
			kfree(buf, M_NETGRAPH);
			return (ENOMEM);
		}
		decomplen = MPPC_DECOMP_BUFSIZE;

		/* Prepare to decompress */
		source = buf;
		sourceCnt = len;
		dest = decompbuf;
		destCnt = decomplen;
		if ((header & MPPC_FLAG_RESTART) != 0)
			flags |= MPPC_RESTART_HISTORY;

		/* Decompress */
		rtn = MPPC_Decompress(&source, &dest,
			&sourceCnt, &destCnt, d->history, flags);

		/* Check return value */
		KASSERT(rtn != MPPC_INVALID, ("%s: invalid", __func__));
		if ((rtn & MPPC_DEST_EXHAUSTED) != 0
		    || (rtn & MPPC_DECOMP_OK) != MPPC_DECOMP_OK) {
			log(LOG_ERR, "%s: decomp returned 0x%x",
			    __func__, rtn);
			kfree(decompbuf, M_NETGRAPH);
			goto failed;
		}

		/* Replace compressed data with decompressed data */
		kfree(buf, M_NETGRAPH);
		buf = decompbuf;
		len = decomplen - destCnt;
	}
#endif

	/* Return result in an mbuf */
	*resultp = m_devget((caddr_t)buf, len, 0, NULL, NULL);
	kfree(buf, M_NETGRAPH);
	return (*resultp == NULL ? ENOBUFS : 0);
}

/*
 * The peer has sent us a CCP ResetRequest, so reset our transmit state.
 */
static void
ng_mppc_reset_req(node_p node)
{   
	const priv_p priv = node->private;
	struct ng_mppc_dir *const d = &priv->xmit;

#ifdef NETGRAPH_MPPC_COMPRESSION
	if (d->history != NULL)
		MPPC_InitCompressionHistory(d->history);
#endif
#ifdef NETGRAPH_MPPC_ENCRYPTION
	if ((d->cfg.bits & MPPE_STATELESS) == 0)
		rc4_init(&d->rc4, d->key, KEYLEN(d->cfg.bits));
#endif
	d->flushed = 1;
}   

/*
 * Generate a new encryption key
 */
static void
ng_mppc_getkey(const u_char *h, u_char *h2, int len)
{
	static const u_char pad1[10] =
	    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, };
	static const u_char pad2[10] =
	    { 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, };
	u_char hash[20];
	SHA1_CTX c;
	int k;

	bzero(&hash, sizeof(hash));
	SHA1Init(&c);
	SHA1Update(&c, h, len);
	for (k = 0; k < 4; k++)
		SHA1Update(&c, pad1, sizeof(pad2));
	SHA1Update(&c, h2, len);
	for (k = 0; k < 4; k++)
		SHA1Update(&c, pad2, sizeof(pad2));
	SHA1Final(hash, &c);
	bcopy(hash, h2, len);
}

/*
 * Update the encryption key
 */
static void
ng_mppc_updatekey(u_int32_t bits,
	u_char *key0, u_char *key, struct rc4_state *rc4)
{ 
	const int keylen = KEYLEN(bits);

	ng_mppc_getkey(key0, key, keylen);
	rc4_init(rc4, key, keylen);
	rc4_crypt(rc4, key, key, keylen);
	if ((bits & MPPE_40) != 0)
		bcopy(&ng_mppe_weakenkey, key, 3);
	else if ((bits & MPPE_56) != 0)
		bcopy(&ng_mppe_weakenkey, key, 1);
	rc4_init(rc4, key, keylen);
}
