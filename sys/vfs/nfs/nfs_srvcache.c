/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfs_srvcache.c	8.3 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/nfs/nfs_srvcache.c,v 1.21 2000/02/13 03:32:06 peter Exp $
 */

/*
 * Reference: Chet Juszczak, "Improving the Performance and Correctness
 *		of an NFS Server", in Proc. Winter 1989 USENIX Conference,
 *		pages 53-63. San Diego, February 1989.
 */
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>	/* for dup_sockaddr */

#include <netinet/in.h>
#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsrvcache.h"

#ifndef NFS_NOSERVER 
static long numnfsrvcache;
static long desirednfsrvcache;

#define	NFSRCHASH(xid) \
	(&nfsrvhashtbl[((xid) + ((xid) >> 24)) & nfsrvhash])
static LIST_HEAD(nfsrvhash, nfsrvcache) *nfsrvhashtbl;
static TAILQ_HEAD(nfsrvlru, nfsrvcache) nfsrvlruhead;
static u_long nfsrvhash;

#define TRUE	1
#define	FALSE	0

#define	NETFAMILY(rp) \
		(((rp)->rc_flag & RC_INETADDR) ? AF_INET : AF_ISO)

struct lwkt_token srvcache_token = LWKT_TOKEN_INITIALIZER(srvcache_token);

/*
 * Static array that defines which nfs rpc's are nonidempotent
 */
static int nonidempotent[NFS_NPROCS] = {
	FALSE,
	FALSE,
	TRUE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
};

/* True iff the rpc reply is an nfs status ONLY! */
static int nfsv2_repstat[NFS_NPROCS] = {
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	FALSE,
	TRUE,
	FALSE,
	FALSE,
};

/*
 * Size the NFS server's duplicate request cache at 1/2 the nmbclusters,
 * within a (64, 2048) range. This is to prevent all mbuf clusters being
 * tied up in the NFS dupreq cache for small values of nmbclusters.
 */
static void
nfsrvcache_size_change(void)
{
	desirednfsrvcache = nmbclusters / 2;
	desirednfsrvcache = MIN(desirednfsrvcache, NFSRVCACHE_MAX_SIZE);
	desirednfsrvcache = MAX(desirednfsrvcache, NFSRVCACHE_MIN_SIZE);
}

/*
 * Initialize the server request cache list
 */
void
nfsrv_initcache(void)
{
	nfsrvcache_size_change();
	nfsrvhashtbl = hashinit(desirednfsrvcache, M_NFSD, &nfsrvhash);
	TAILQ_INIT(&nfsrvlruhead);
}

/*
 * Look for the request in the cache
 * If found then
 *    return action and optionally reply
 * else
 *    insert it in the cache
 *
 * The rules are as follows:
 * - if in progress, return DROP request
 * - if completed within DELAY of the current time, return DROP it
 * - if completed a longer time ago return REPLY if the reply was cached or
 *   return DOIT
 * Update/add new request at end of lru list
 */
int
nfsrv_getcache(struct nfsrv_descript *nd, struct nfssvc_sock *slp,
	       struct mbuf **repp)
{
	struct nfsrvcache *rp;
	struct mbuf *mb;
	struct sockaddr_in *saddr;
	caddr_t bpos;
	int ret;

	/*
	 * Don't cache recent requests for reliable transport protocols.
	 * (Maybe we should for the case of a reconnect, but..)
	 */
	if (!nd->nd_nam2)
		return (RC_DOIT);

	lwkt_gettoken(&srvcache_token);
loop:
	for (rp = NFSRCHASH(nd->nd_retxid)->lh_first; rp != NULL;
	    rp = rp->rc_hash.le_next) {
	    if (nd->nd_retxid == rp->rc_xid && nd->nd_procnum == rp->rc_proc &&
		netaddr_match(NETFAMILY(rp), &rp->rc_haddr, nd->nd_nam)) {
		        NFS_DPF(RC, ("H%03x", rp->rc_xid & 0xfff));
			if ((rp->rc_flag & RC_LOCKED) != 0) {
				rp->rc_flag |= RC_WANTED;
				tsleep((caddr_t)rp, 0, "nfsrc", 0);
				goto loop;
			}
			rp->rc_flag |= RC_LOCKED;
			/* If not at end of LRU chain, move it there */
			if (TAILQ_NEXT(rp, rc_lru) != NULL) {
				TAILQ_REMOVE(&nfsrvlruhead, rp, rc_lru);
				TAILQ_INSERT_TAIL(&nfsrvlruhead, rp, rc_lru);
			}
			if (rp->rc_state == RC_UNUSED)
				panic("nfsrv cache");
			if (rp->rc_state == RC_INPROG) {
				nfsstats.srvcache_inproghits++;
				ret = RC_DROPIT;
			} else if (rp->rc_flag & RC_REPSTATUS) {
				nfsstats.srvcache_nonidemdonehits++;
				nfs_rephead(0, nd, slp, rp->rc_status,
					    repp, &mb, &bpos);
				ret = RC_REPLY;
			} else if (rp->rc_flag & RC_REPMBUF) {
				nfsstats.srvcache_nonidemdonehits++;
				*repp = m_copym(rp->rc_reply, 0, M_COPYALL,
						MB_WAIT);
				ret = RC_REPLY;
			} else {
				nfsstats.srvcache_idemdonehits++;
				rp->rc_state = RC_INPROG;
				ret = RC_DOIT;
			}
			rp->rc_flag &= ~RC_LOCKED;
			if (rp->rc_flag & RC_WANTED) {
				rp->rc_flag &= ~RC_WANTED;
				wakeup((caddr_t)rp);
			}
			lwkt_reltoken(&srvcache_token);
			return (ret);
		}
	}

	nfsstats.srvcache_misses++;
	NFS_DPF(RC, ("M%03x", nd->nd_retxid & 0xfff));
	if (numnfsrvcache < desirednfsrvcache) {
		rp = kmalloc((u_long)sizeof *rp, M_NFSD, M_WAITOK | M_ZERO);
		numnfsrvcache++;
		rp->rc_flag = RC_LOCKED;
	} else {
		rp = TAILQ_FIRST(&nfsrvlruhead);
		while ((rp->rc_flag & RC_LOCKED) != 0) {
			rp->rc_flag |= RC_WANTED;
			tsleep((caddr_t)rp, 0, "nfsrc", 0);
			rp = TAILQ_FIRST(&nfsrvlruhead);
		}
		rp->rc_flag |= RC_LOCKED;
		LIST_REMOVE(rp, rc_hash);
		TAILQ_REMOVE(&nfsrvlruhead, rp, rc_lru);
		if (rp->rc_flag & RC_REPMBUF) {
			m_freem(rp->rc_reply);
			rp->rc_reply = NULL;
			rp->rc_flag &= ~RC_REPMBUF;
		}
		if (rp->rc_flag & RC_NAM) {
			kfree(rp->rc_nam, M_SONAME);
			rp->rc_nam = NULL;
			rp->rc_flag &= ~RC_NAM;
		}
	}
	TAILQ_INSERT_TAIL(&nfsrvlruhead, rp, rc_lru);

	rp->rc_state = RC_INPROG;
	rp->rc_xid = nd->nd_retxid;
	saddr = (struct sockaddr_in *)nd->nd_nam;
	switch (saddr->sin_family) {
	case AF_INET:
		rp->rc_flag |= RC_INETADDR;
		rp->rc_inetaddr = saddr->sin_addr.s_addr;
		break;
	case AF_ISO:
	default:
		rp->rc_flag |= RC_NAM;
		rp->rc_nam = dup_sockaddr(nd->nd_nam);
		break;
	}
	rp->rc_proc = nd->nd_procnum;
	LIST_INSERT_HEAD(NFSRCHASH(nd->nd_retxid), rp, rc_hash);
	rp->rc_flag &= ~RC_LOCKED;
	if (rp->rc_flag & RC_WANTED) {
		rp->rc_flag &= ~RC_WANTED;
		wakeup((caddr_t)rp);
	}
	lwkt_reltoken(&srvcache_token);

	return (RC_DOIT);
}

/*
 * Update a request cache entry after the rpc has been done
 */
void
nfsrv_updatecache(struct nfsrv_descript *nd, int repvalid, struct mbuf *repmbuf)
{
	struct nfsrvcache *rp;

	if (!nd->nd_nam2)
		return;

	lwkt_gettoken(&srvcache_token);
loop:
	for (rp = NFSRCHASH(nd->nd_retxid)->lh_first; rp != NULL;
	    rp = rp->rc_hash.le_next) {
	    if (nd->nd_retxid == rp->rc_xid && nd->nd_procnum == rp->rc_proc &&
		netaddr_match(NETFAMILY(rp), &rp->rc_haddr, nd->nd_nam)) {
			NFS_DPF(RC, ("U%03x", rp->rc_xid & 0xfff));
			if ((rp->rc_flag & RC_LOCKED) != 0) {
				rp->rc_flag |= RC_WANTED;
				tsleep((caddr_t)rp, 0, "nfsrc", 0);
				goto loop;
			}
			rp->rc_flag |= RC_LOCKED;
			if (rp->rc_state == RC_DONE) {
				/*
				 * This can occur if the cache is too small.
				 * Retransmits of the same request aren't 
				 * dropped so we may see the operation 
				 * complete more then once.
				 */
				if (rp->rc_flag & RC_REPMBUF) {
					m_freem(rp->rc_reply);
					rp->rc_reply = NULL;
					rp->rc_flag &= ~RC_REPMBUF;
				}
			}
			rp->rc_state = RC_DONE;

			/*
			 * If we have a valid reply update status and save
			 * the reply for non-idempotent rpc's.
			 */
			if (repvalid && nonidempotent[nd->nd_procnum]) {
				if ((nd->nd_flag & ND_NFSV3) == 0 &&
				  nfsv2_repstat[nfsv2_procid[nd->nd_procnum]]) {
					rp->rc_status = nd->nd_repstat;
					rp->rc_flag |= RC_REPSTATUS;
				} else {
					if (rp->rc_flag & RC_REPMBUF) {
						m_freem(rp->rc_reply);
						rp->rc_reply = NULL;
						rp->rc_flag &= ~RC_REPMBUF;
					}
					rp->rc_reply = m_copym(repmbuf, 0,
							M_COPYALL, MB_WAIT);
					rp->rc_flag |= RC_REPMBUF;
				}
			}
			rp->rc_flag &= ~RC_LOCKED;
			if (rp->rc_flag & RC_WANTED) {
				rp->rc_flag &= ~RC_WANTED;
				wakeup((caddr_t)rp);
			}
			break;
		}
	}
	lwkt_reltoken(&srvcache_token);
	NFS_DPF(RC, ("L%03x", nd->nd_retxid & 0xfff));
}

/*
 * Clean out the cache. Called when the last nfsd terminates.
 */
void
nfsrv_cleancache(void)
{
	struct nfsrvcache *rp;

	lwkt_gettoken(&srvcache_token);
	while ((rp = TAILQ_FIRST(&nfsrvlruhead)) != NULL) {
		if (rp->rc_flag & RC_LOCKED) {
			rp->rc_flag |= RC_WANTED;
			tsleep((caddr_t)rp, 0, "nfsrc", 0);
			continue;
		}
		LIST_REMOVE(rp, rc_hash);
		TAILQ_REMOVE(&nfsrvlruhead, rp, rc_lru);
		if (rp->rc_flag & RC_REPMBUF) {
			m_freem(rp->rc_reply);
			rp->rc_reply = NULL;
			rp->rc_flag &= ~RC_REPMBUF;
		}
		if (rp->rc_flag & RC_NAM) {
			kfree(rp->rc_nam, M_SONAME);
			rp->rc_nam = NULL;
			rp->rc_flag &= ~RC_NAM;
		}
		kfree(rp, M_NFSD);
	}
	numnfsrvcache = 0;
	lwkt_reltoken(&srvcache_token);
}

#endif /* NFS_NOSERVER */
