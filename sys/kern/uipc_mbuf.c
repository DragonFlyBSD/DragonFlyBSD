/*
 * Copyright (c) 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

/*
 * Copyright (c) 2004 Jeffrey M. Hsu.  All rights reserved.
 *
 * License terms: all terms for the DragonFly license above plus the following:
 *
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Jeffrey M. Hsu
 *	for the DragonFly Project.
 *
 *    This requirement may be waived with permission from Jeffrey Hsu.
 *    This requirement will sunset and may be removed on July 8 2005,
 *    after which the standard DragonFly license (as shown above) will
 *    apply.
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1991, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#)uipc_mbuf.c	8.2 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/kern/uipc_mbuf.c,v 1.51.2.24 2003/04/15 06:59:29 silby Exp $
 * $DragonFly: src/sys/kern/uipc_mbuf.c,v 1.52 2005/06/17 18:58:02 dillon Exp $
 */

#include "opt_param.h"
#include "opt_ddb.h"
#include "opt_mbuf_stress_test.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/domain.h>
#include <sys/objcache.h>
#include <sys/protosw.h>
#include <sys/uio.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#ifdef INVARIANTS
#include <machine/cpu.h>
#endif

/*
 * mbuf cluster meta-data
 */
struct mbcluster {
	int32_t	mcl_refs;
	void	*mcl_data;
};

static void mbinit(void *);
SYSINIT(mbuf, SI_SUB_MBUF, SI_ORDER_FIRST, mbinit, NULL)

static u_long	mbtypes[MT_NTYPES];

struct mbstat mbstat;
int	max_linkhdr;
int	max_protohdr;
int	max_hdr;
int	max_datalen;
int	m_defragpackets;
int	m_defragbytes;
int	m_defraguseless;
int	m_defragfailure;
#ifdef MBUF_STRESS_TEST
int	m_defragrandomfailures;
#endif

struct objcache *mbuf_cache, *mbufphdr_cache;
struct objcache *mclmeta_cache;
struct objcache *mbufcluster_cache, *mbufphdrcluster_cache;

int	nmbclusters;
int	nmbufs;

SYSCTL_INT(_kern_ipc, KIPC_MAX_LINKHDR, max_linkhdr, CTLFLAG_RW,
	   &max_linkhdr, 0, "");
SYSCTL_INT(_kern_ipc, KIPC_MAX_PROTOHDR, max_protohdr, CTLFLAG_RW,
	   &max_protohdr, 0, "");
SYSCTL_INT(_kern_ipc, KIPC_MAX_HDR, max_hdr, CTLFLAG_RW, &max_hdr, 0, "");
SYSCTL_INT(_kern_ipc, KIPC_MAX_DATALEN, max_datalen, CTLFLAG_RW,
	   &max_datalen, 0, "");
SYSCTL_INT(_kern_ipc, OID_AUTO, mbuf_wait, CTLFLAG_RW,
	   &mbuf_wait, 0, "");
SYSCTL_STRUCT(_kern_ipc, KIPC_MBSTAT, mbstat, CTLFLAG_RW, &mbstat, mbstat, "");
SYSCTL_OPAQUE(_kern_ipc, OID_AUTO, mbtypes, CTLFLAG_RD, mbtypes,
	   sizeof(mbtypes), "LU", "");
SYSCTL_INT(_kern_ipc, KIPC_NMBCLUSTERS, nmbclusters, CTLFLAG_RW, 
	   &nmbclusters, 0, "Maximum number of mbuf clusters available");
SYSCTL_INT(_kern_ipc, OID_AUTO, nmbufs, CTLFLAG_RW, &nmbufs, 0,
	   "Maximum number of mbufs available"); 

SYSCTL_INT(_kern_ipc, OID_AUTO, m_defragpackets, CTLFLAG_RD,
	   &m_defragpackets, 0, "");
SYSCTL_INT(_kern_ipc, OID_AUTO, m_defragbytes, CTLFLAG_RD,
	   &m_defragbytes, 0, "");
SYSCTL_INT(_kern_ipc, OID_AUTO, m_defraguseless, CTLFLAG_RD,
	   &m_defraguseless, 0, "");
SYSCTL_INT(_kern_ipc, OID_AUTO, m_defragfailure, CTLFLAG_RD,
	   &m_defragfailure, 0, "");
#ifdef MBUF_STRESS_TEST
SYSCTL_INT(_kern_ipc, OID_AUTO, m_defragrandomfailures, CTLFLAG_RW,
	   &m_defragrandomfailures, 0, "");
#endif

static MALLOC_DEFINE(M_MBUF, "mbuf", "mbuf");
static MALLOC_DEFINE(M_MBUFCL, "mbufcl", "mbufcl");
static MALLOC_DEFINE(M_MCLMETA, "mclmeta", "mclmeta");

static void m_reclaim (void);
static void m_mclref(void *arg);
static void m_mclfree(void *arg);

#ifndef NMBCLUSTERS
#define NMBCLUSTERS	(512 + maxusers * 16)
#endif
#ifndef NMBUFS
#define NMBUFS		(nmbclusters * 2)
#endif

/*
 * Perform sanity checks of tunables declared above.
 */
static void
tunable_mbinit(void *dummy)
{

	/*
	 * This has to be done before VM init.
	 */
	nmbclusters = NMBCLUSTERS;
	TUNABLE_INT_FETCH("kern.ipc.nmbclusters", &nmbclusters);
	nmbufs = NMBUFS;
	TUNABLE_INT_FETCH("kern.ipc.nmbufs", &nmbufs);
	/* Sanity checks */
	if (nmbufs < nmbclusters * 2)
		nmbufs = nmbclusters * 2;

	return;
}
SYSINIT(tunable_mbinit, SI_SUB_TUNABLES, SI_ORDER_ANY, tunable_mbinit, NULL);

/* "number of clusters of pages" */
#define NCL_INIT	1

#define NMB_INIT	16

/*
 * The mbuf object cache only guarantees that m_next and m_nextpkt are
 * NULL and that m_data points to the beginning of the data area.  In
 * particular, m_len and m_pkthdr.len are uninitialized.  It is the
 * responsibility of the caller to initialize those fields before use.
 */

static boolean_t __inline
mbuf_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;

	m->m_next = NULL;
	m->m_nextpkt = NULL;
	m->m_data = m->m_dat;
	m->m_flags = 0;

	return (TRUE);
}

/*
 * Initialize the mbuf and the packet header fields.
 */
static boolean_t
mbufphdr_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;

	m->m_next = NULL;
	m->m_nextpkt = NULL;
	m->m_data = m->m_pktdat;
	m->m_flags = M_PKTHDR | M_PHCACHE;

	m->m_pkthdr.rcvif = NULL;	/* eliminate XXX JH */
	SLIST_INIT(&m->m_pkthdr.tags);
	m->m_pkthdr.csum_flags = 0;	/* eliminate XXX JH */
	m->m_pkthdr.fw_flags = 0;	/* eliminate XXX JH */

	return (TRUE);
}

/*
 * A mbcluster object consists of 2K (MCLBYTES) cluster and a refcount.
 */
static boolean_t
mclmeta_ctor(void *obj, void *private, int ocflags)
{
	struct mbcluster *cl = obj;
	void *buf;

	if (ocflags & M_NOWAIT)
		buf = malloc(MCLBYTES, M_MBUFCL, M_NOWAIT | M_ZERO);
	else
		buf = malloc(MCLBYTES, M_MBUFCL, M_INTWAIT | M_ZERO);
	if (buf == NULL)
		return (FALSE);
	cl->mcl_refs = 0;
	cl->mcl_data = buf;
	return (TRUE);
}

static void
mclmeta_dtor(void *obj, void *private)
{
	struct mbcluster *mcl = obj;

	KKASSERT(mcl->mcl_refs == 0);
	free(mcl->mcl_data, M_MBUFCL);
}

static void
linkcluster(struct mbuf *m, struct mbcluster *cl)
{
	/*
	 * Add the cluster to the mbuf.  The caller will detect that the
	 * mbuf now has an attached cluster.
	 */
	m->m_ext.ext_arg = cl;
	m->m_ext.ext_buf = cl->mcl_data;
	m->m_ext.ext_ref = m_mclref;
	m->m_ext.ext_free = m_mclfree;
	m->m_ext.ext_size = MCLBYTES;
	++cl->mcl_refs;

	m->m_data = m->m_ext.ext_buf;
	m->m_flags |= M_EXT | M_EXT_CLUSTER;
}

static boolean_t
mbufphdrcluster_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;
	struct mbcluster *cl;

	mbufphdr_ctor(obj, private, ocflags);
	cl = objcache_get(mclmeta_cache, ocflags);
	if (cl == NULL)
		return (FALSE);
	m->m_flags |= M_CLCACHE;
	linkcluster(m, cl);
	return (TRUE);
}

static boolean_t
mbufcluster_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;
	struct mbcluster *cl;

	mbuf_ctor(obj, private, ocflags);
	cl = objcache_get(mclmeta_cache, ocflags);
	if (cl == NULL)
		return (FALSE);
	m->m_flags |= M_CLCACHE;
	linkcluster(m, cl);
	return (TRUE);
}

/*
 * Used for both the cluster and cluster PHDR caches.
 *
 * The mbuf may have lost its cluster due to sharing, deal
 * with the situation by checking M_EXT.
 */
static void
mbufcluster_dtor(void *obj, void *private)
{
	struct mbuf *m = obj;
	struct mbcluster *mcl;

	if (m->m_flags & M_EXT) {
		KKASSERT((m->m_flags & M_EXT_CLUSTER) != 0);
		mcl = m->m_ext.ext_arg;
		KKASSERT(mcl->mcl_refs == 1);
		mcl->mcl_refs = 0;
		objcache_put(mclmeta_cache, mcl);
	}
}

struct objcache_malloc_args mbuf_malloc_args = { MSIZE, M_MBUF };
struct objcache_malloc_args mclmeta_malloc_args =
	{ sizeof(struct mbcluster), M_MCLMETA };

/* ARGSUSED*/
static void
mbinit(void *dummy)
{
	mbstat.m_msize = MSIZE;
	mbstat.m_mclbytes = MCLBYTES;
	mbstat.m_minclsize = MINCLSIZE;
	mbstat.m_mlen = MLEN;
	mbstat.m_mhlen = MHLEN;

	mbuf_cache = objcache_create("mbuf", nmbufs, 0,
	    mbuf_ctor, null_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mbufphdr_cache = objcache_create("mbuf pkt hdr", nmbufs, 64,
	    mbufphdr_ctor, null_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mclmeta_cache = objcache_create("cluster mbuf", nmbclusters , 0,
	    mclmeta_ctor, mclmeta_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mclmeta_malloc_args);
	mbufcluster_cache = objcache_create("mbuf + cluster", nmbclusters, 0,
	    mbufcluster_ctor, mbufcluster_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mbufphdrcluster_cache = objcache_create("mbuf pkt hdr + cluster",
	    nmbclusters, 64, mbufphdrcluster_ctor, mbufcluster_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	return;
}

/*
 * Return the number of references to this mbuf's data.  0 is returned
 * if the mbuf is not M_EXT, a reference count is returned if it is
 * M_EXT | M_EXT_CLUSTER, and 99 is returned if it is a special M_EXT.
 */
int
m_sharecount(struct mbuf *m)
{
	switch (m->m_flags & (M_EXT | M_EXT_CLUSTER)) {
	case 0:
		return (0);
	case M_EXT:
		return (99);
	case M_EXT | M_EXT_CLUSTER:
		return (((struct mbcluster *)m->m_ext.ext_arg)->mcl_refs);
	}
	/* NOTREACHED */
	return (0);		/* to shut up compiler */
}

/*
 * change mbuf to new type
 */
void
m_chtype(struct mbuf *m, int type)
{
	crit_enter();
	++mbtypes[type];
	--mbtypes[m->m_type];
	m->m_type = type;
	crit_exit();
}

static void
m_reclaim(void)
{
	struct domain *dp;
	struct protosw *pr;

	crit_enter();
	SLIST_FOREACH(dp, &domains, dom_next) {
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
			if (pr->pr_drain)
				(*pr->pr_drain)();
		}
	}
	crit_exit();
	mbstat.m_drain++;
}

static void __inline
updatestats(struct mbuf *m, int type)
{
	m->m_type = type;

	crit_enter();
	++mbtypes[type];
	++mbstat.m_mbufs;
	crit_exit();
}

/*
 * Allocate an mbuf.
 */
struct mbuf *
m_get(int how, int type)
{
	struct mbuf *m;
	int ntries = 0;
	int ocf = MBTOM(how);

retryonce:

	m = objcache_get(mbuf_cache, ocf);

	if (m == NULL) {
		if ((how & MB_TRYWAIT) && ntries++ == 0) {
			struct objcache *reclaimlist[] = {
				mbufphdr_cache,
				mbufcluster_cache, mbufphdrcluster_cache
			};
			const int nreclaims = __arysize(reclaimlist);

			if (!objcache_reclaimlist(reclaimlist, nreclaims, ocf))
				m_reclaim();
			goto retryonce;
		}
		return (NULL);
	}

	updatestats(m, type);
	return (m);
}

struct mbuf *
m_gethdr(int how, int type)
{
	struct mbuf *m;
	int ocf = MBTOM(how);
	int ntries = 0;

retryonce:

	m = objcache_get(mbufphdr_cache, ocf);

	if (m == NULL) {
		if ((how & MB_TRYWAIT) && ntries++ == 0) {
			struct objcache *reclaimlist[] = {
				mbuf_cache,
				mbufcluster_cache, mbufphdrcluster_cache
			};
			const int nreclaims = __arysize(reclaimlist);

			if (!objcache_reclaimlist(reclaimlist, nreclaims, ocf))
				m_reclaim();
			goto retryonce;
		}
		return (NULL);
	}

	updatestats(m, type);
	return (m);
}

/*
 * Get a mbuf (not a mbuf cluster!) and zero it.
 * Deprecated.
 */
struct mbuf *
m_getclr(int how, int type)
{
	struct mbuf *m;

	m = m_get(how, type);
	if (m != NULL)
		bzero(m->m_data, MLEN);
	return (m);
}

/*
 * Returns an mbuf with an attached cluster.
 * Because many network drivers use this kind of buffers a lot, it is
 * convenient to keep a small pool of free buffers of this kind.
 * Even a small size such as 10 gives about 10% improvement in the
 * forwarding rate in a bridge or router.
 */
struct mbuf *
m_getcl(int how, short type, int flags)
{
	struct mbuf *m;
	int ocflags = MBTOM(how);
	int ntries = 0;

retryonce:

	if (flags & M_PKTHDR)
		m = objcache_get(mbufphdrcluster_cache, ocflags);
	else
		m = objcache_get(mbufcluster_cache, ocflags);

	if (m == NULL) {
		if ((how & MB_TRYWAIT) && ntries++ == 0) {
			struct objcache *reclaimlist[1];

			if (flags & M_PKTHDR)
				reclaimlist[0] = mbufcluster_cache;
			else
				reclaimlist[0] = mbufphdrcluster_cache;
			if (!objcache_reclaimlist(reclaimlist, 1, ocflags))
				m_reclaim();
			goto retryonce;
		}
		return (NULL);
	}

	m->m_type = type;

	crit_enter();
	++mbtypes[type];
	++mbstat.m_clusters;
	crit_exit();
	return (m);
}

/*
 * Allocate chain of requested length.
 */
struct mbuf *
m_getc(int len, int how, int type)
{
	struct mbuf *n, *nfirst = NULL, **ntail = &nfirst;
	int nsize;

	while (len > 0) {
		n = m_getl(len, how, type, 0, &nsize);
		if (n == NULL)
			goto failed;
		n->m_len = 0;
		*ntail = n;
		ntail = &n->m_next;
		len -= nsize;
	}
	return (nfirst);

failed:
	m_freem(nfirst);
	return (NULL);
}

/*
 * Allocate len-worth of mbufs and/or mbuf clusters (whatever fits best)
 * and return a pointer to the head of the allocated chain. If m0 is
 * non-null, then we assume that it is a single mbuf or an mbuf chain to
 * which we want len bytes worth of mbufs and/or clusters attached, and so
 * if we succeed in allocating it, we will just return a pointer to m0.
 *
 * If we happen to fail at any point during the allocation, we will free
 * up everything we have already allocated and return NULL.
 *
 * Deprecated.  Use m_getc() and m_cat() instead.
 */
struct mbuf *
m_getm(struct mbuf *m0, int len, int how, int type)
{
	struct mbuf *nfirst;

	nfirst = m_getc(len, how, type);

	if (m0 != NULL) {
		m_last(m0)->m_next = nfirst;
		return (m0);
	}

	return (nfirst);
}

/*
 * Adds a cluster to a normal mbuf, M_EXT is set on success.
 * Deprecated.  Use m_getcl() instead.
 */
void
m_mclget(struct mbuf *m, int how)
{
	struct mbcluster *mcl;

	KKASSERT((m->m_flags & M_EXT) == 0);
	mcl = objcache_get(mclmeta_cache, MBTOM(how));
	if (mcl != NULL) {
		linkcluster(m, mcl);
		crit_enter();
		++mbstat.m_clusters;
		/* leave the m_mbufs count intact for original mbuf */
		crit_exit();
	}
}

static void
m_mclref(void *arg)
{
	struct mbcluster *mcl = arg;

	atomic_add_int(&mcl->mcl_refs, 1);
}

static void
m_mclfree(void *arg)
{
	struct mbcluster *mcl = arg;

	/* XXX interrupt race.  Currently called from a critical section */
	if (mcl->mcl_refs > 1) {
		atomic_subtract_int(&mcl->mcl_refs, 1);
	} else {
		KKASSERT(mcl->mcl_refs == 1);
		mcl->mcl_refs = 0;
		objcache_put(mclmeta_cache, mcl);
	}
}

extern void db_print_backtrace(void);

/*
 * Free a single mbuf and any associated external storage.  The successor,
 * if any, is returned.
 *
 * We do need to check non-first mbuf for m_aux, since some of existing
 * code does not call M_PREPEND properly.
 * (example: call to bpf_mtap from drivers)
 */
struct mbuf *
m_free(struct mbuf *m)
{
	struct mbuf *n;

	KASSERT(m->m_type != MT_FREE, ("freeing free mbuf %p", m));
	--mbtypes[m->m_type];

	n = m->m_next;

	/*
	 * Make sure the mbuf is in constructed state before returning it
	 * to the objcache.
	 */
	m->m_next = NULL;
#ifdef notyet
	KKASSERT(m->m_nextpkt == NULL);
#else
	if (m->m_nextpkt != NULL) {
#ifdef DDB
		static int afewtimes = 10;

		if (afewtimes-- > 0) {
			printf("mfree: m->m_nextpkt != NULL\n");
			db_print_backtrace();
		}
#endif
		m->m_nextpkt = NULL;
	}
#endif
	if (m->m_flags & M_PKTHDR) {
		m_tag_delete_chain(m);		/* eliminate XXX JH */
	}

	m->m_flags &= (M_EXT | M_EXT_CLUSTER | M_CLCACHE | M_PHCACHE);

	/*
	 * Clean the M_PKTHDR state so we can return the mbuf to its original
	 * cache.  This is based on the PHCACHE flag which tells us whether
	 * the mbuf was originally allocated out of a packet-header cache
	 * or a non-packet-header cache.
	 */
	if (m->m_flags & M_PHCACHE) {
		m->m_flags |= M_PKTHDR;
		m->m_pkthdr.rcvif = NULL;	/* eliminate XXX JH */
		m->m_pkthdr.csum_flags = 0;	/* eliminate XXX JH */
		m->m_pkthdr.fw_flags = 0;	/* eliminate XXX JH */
		SLIST_INIT(&m->m_pkthdr.tags);
	}

	/*
	 * Handle remaining flags combinations.  M_CLCACHE tells us whether
	 * the mbuf was originally allocated from a cluster cache or not,
	 * and is totally separate from whether the mbuf is currently
	 * associated with a cluster.
	 */
	crit_enter();
	switch(m->m_flags & (M_CLCACHE | M_EXT | M_EXT_CLUSTER)) {
	case M_CLCACHE | M_EXT | M_EXT_CLUSTER:
		/*
		 * mbuf+cluster cache case.  The mbuf was allocated from the
		 * combined mbuf_cluster cache and can be returned to the
		 * cache if the cluster hasn't been shared.
		 */
		if (m_sharecount(m) == 1) {
			/*
			 * The cluster has not been shared, we can just
			 * reset the data pointer and return the mbuf
			 * to the cluster cache.  Note that the reference
			 * count is left intact (it is still associated with
			 * an mbuf).
			 */
			m->m_data = m->m_ext.ext_buf;
			if (m->m_flags & M_PHCACHE)
				objcache_put(mbufphdrcluster_cache, m);
			else
				objcache_put(mbufcluster_cache, m);
		} else {
			/*
			 * Hell.  Someone else has a ref on this cluster,
			 * we have to disconnect it which means we can't
			 * put it back into the mbufcluster_cache, we
			 * have to destroy the mbuf.
			 *
			 * XXX we could try to connect another cluster to
			 * it.
			 */
			m->m_ext.ext_free(m->m_ext.ext_arg); 
			m->m_flags &= ~(M_EXT | M_EXT_CLUSTER);
			if (m->m_flags & M_PHCACHE)
				objcache_dtor(mbufphdrcluster_cache, m);
			else
				objcache_dtor(mbufcluster_cache, m);
		}
		--mbstat.m_clusters;
		break;
	case M_EXT | M_EXT_CLUSTER:
		/*
		 * Normal cluster associated with an mbuf that was allocated
		 * from the normal mbuf pool rather then the cluster pool.
		 * The cluster has to be independantly disassociated from the
		 * mbuf.
		 */
		--mbstat.m_clusters;
		/* fall through */
	case M_EXT:
		/*
		 * Normal cluster association case, disconnect the cluster from
		 * the mbuf.  The cluster may or may not be custom.
		 */
		m->m_ext.ext_free(m->m_ext.ext_arg); 
		m->m_flags &= ~(M_EXT | M_EXT_CLUSTER);
		/* fall through */
	case 0:
		/*
		 * return the mbuf to the mbuf cache.
		 */
		if (m->m_flags & M_PHCACHE) {
			m->m_data = m->m_pktdat;
			objcache_put(mbufphdr_cache, m);
		} else {
			m->m_data = m->m_dat;
			objcache_put(mbuf_cache, m);
		}
		--mbstat.m_mbufs;
		break;
	default:
		if (!panicstr)
			panic("bad mbuf flags %p %08x\n", m, m->m_flags);
		break;
	}
	crit_exit();
	return (n);
}

void
m_freem(struct mbuf *m)
{
	crit_enter();
	while (m)
		m = m_free(m);
	crit_exit();
}

/*
 * mbuf utility routines
 */

/*
 * Lesser-used path for M_PREPEND: allocate new mbuf to prepend to chain and
 * copy junk along.
 */
struct mbuf *
m_prepend(struct mbuf *m, int len, int how)
{
	struct mbuf *mn;

	if (m->m_flags & M_PKTHDR)
	    mn = m_gethdr(how, m->m_type);
	else
	    mn = m_get(how, m->m_type);
	if (mn == NULL) {
		m_freem(m);
		return (NULL);
	}
	if (m->m_flags & M_PKTHDR)
		M_MOVE_PKTHDR(mn, m);
	mn->m_next = m;
	m = mn;
	if (len < MHLEN)
		MH_ALIGN(m, len);
	m->m_len = len;
	return (m);
}

/*
 * Make a copy of an mbuf chain starting "off0" bytes from the beginning,
 * continuing for "len" bytes.  If len is M_COPYALL, copy to end of mbuf.
 * The wait parameter is a choice of MB_WAIT/MB_DONTWAIT from caller.
 * Note that the copy is read-only, because clusters are not copied,
 * only their reference counts are incremented.
 */
struct mbuf *
m_copym(const struct mbuf *m, int off0, int len, int wait)
{
	struct mbuf *n, **np;
	int off = off0;
	struct mbuf *top;
	int copyhdr = 0;

	KASSERT(off >= 0, ("m_copym, negative off %d", off));
	KASSERT(len >= 0, ("m_copym, negative len %d", len));
	if (off == 0 && m->m_flags & M_PKTHDR)
		copyhdr = 1;
	while (off > 0) {
		KASSERT(m != NULL, ("m_copym, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	np = &top;
	top = 0;
	while (len > 0) {
		if (m == NULL) {
			KASSERT(len == M_COPYALL, 
			    ("m_copym, length > size of mbuf chain"));
			break;
		}
		/*
		 * Because we are sharing any cluster attachment below,
		 * be sure to get an mbuf that does not have a cluster
		 * associated with it.
		 */
		if (copyhdr)
			n = m_gethdr(wait, m->m_type);
		else
			n = m_get(wait, m->m_type);
		*np = n;
		if (n == NULL)
			goto nospace;
		if (copyhdr) {
			if (!m_dup_pkthdr(n, m, wait))
				goto nospace;
			if (len == M_COPYALL)
				n->m_pkthdr.len -= off0;
			else
				n->m_pkthdr.len = len;
			copyhdr = 0;
		}
		n->m_len = min(len, m->m_len - off);
		if (m->m_flags & M_EXT) {
			KKASSERT((n->m_flags & M_EXT) == 0);
			n->m_data = m->m_data + off;
			m->m_ext.ext_ref(m->m_ext.ext_arg); 
			n->m_ext = m->m_ext;
			n->m_flags |= m->m_flags & (M_EXT | M_EXT_CLUSTER);
		} else {
			bcopy(mtod(m, caddr_t)+off, mtod(n, caddr_t),
			    (unsigned)n->m_len);
		}
		if (len != M_COPYALL)
			len -= n->m_len;
		off = 0;
		m = m->m_next;
		np = &n->m_next;
	}
	if (top == NULL)
		mbstat.m_mcfail++;
	return (top);
nospace:
	m_freem(top);
	mbstat.m_mcfail++;
	return (NULL);
}

/*
 * Copy an entire packet, including header (which must be present).
 * An optimization of the common case `m_copym(m, 0, M_COPYALL, how)'.
 * Note that the copy is read-only, because clusters are not copied,
 * only their reference counts are incremented.
 * Preserve alignment of the first mbuf so if the creator has left
 * some room at the beginning (e.g. for inserting protocol headers)
 * the copies also have the room available.
 */
struct mbuf *
m_copypacket(struct mbuf *m, int how)
{
	struct mbuf *top, *n, *o;

	n = m_gethdr(how, m->m_type);
	top = n;
	if (!n)
		goto nospace;

	if (!m_dup_pkthdr(n, m, how))
		goto nospace;
	n->m_len = m->m_len;
	if (m->m_flags & M_EXT) {
		KKASSERT((n->m_flags & M_EXT) == 0);
		n->m_data = m->m_data;
		m->m_ext.ext_ref(m->m_ext.ext_arg); 
		n->m_ext = m->m_ext;
		n->m_flags |= m->m_flags & (M_EXT | M_EXT_CLUSTER);
	} else {
		n->m_data = n->m_pktdat + (m->m_data - m->m_pktdat );
		bcopy(mtod(m, char *), mtod(n, char *), n->m_len);
	}

	m = m->m_next;
	while (m) {
		o = m_get(how, m->m_type);
		if (!o)
			goto nospace;

		n->m_next = o;
		n = n->m_next;

		n->m_len = m->m_len;
		if (m->m_flags & M_EXT) {
			KKASSERT((n->m_flags & M_EXT) == 0);
			n->m_data = m->m_data;
			m->m_ext.ext_ref(m->m_ext.ext_arg); 
			n->m_ext = m->m_ext;
			n->m_flags |= m->m_flags & (M_EXT | M_EXT_CLUSTER);
		} else {
			bcopy(mtod(m, char *), mtod(n, char *), n->m_len);
		}

		m = m->m_next;
	}
	return top;
nospace:
	m_freem(top);
	mbstat.m_mcfail++;
	return (NULL);
}

/*
 * Copy data from an mbuf chain starting "off" bytes from the beginning,
 * continuing for "len" bytes, into the indicated buffer.
 */
void
m_copydata(const struct mbuf *m, int off, int len, caddr_t cp)
{
	unsigned count;

	KASSERT(off >= 0, ("m_copydata, negative off %d", off));
	KASSERT(len >= 0, ("m_copydata, negative len %d", len));
	while (off > 0) {
		KASSERT(m != NULL, ("m_copydata, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0) {
		KASSERT(m != NULL, ("m_copydata, length > size of mbuf chain"));
		count = min(m->m_len - off, len);
		bcopy(mtod(m, caddr_t) + off, cp, count);
		len -= count;
		cp += count;
		off = 0;
		m = m->m_next;
	}
}

/*
 * Copy a packet header mbuf chain into a completely new chain, including
 * copying any mbuf clusters.  Use this instead of m_copypacket() when
 * you need a writable copy of an mbuf chain.
 */
struct mbuf *
m_dup(struct mbuf *m, int how)
{
	struct mbuf **p, *top = NULL;
	int remain, moff, nsize;

	/* Sanity check */
	if (m == NULL)
		return (NULL);
	KASSERT((m->m_flags & M_PKTHDR) != 0, ("%s: !PKTHDR", __func__));

	/* While there's more data, get a new mbuf, tack it on, and fill it */
	remain = m->m_pkthdr.len;
	moff = 0;
	p = &top;
	while (remain > 0 || top == NULL) {	/* allow m->m_pkthdr.len == 0 */
		struct mbuf *n;

		/* Get the next new mbuf */
		n = m_getl(remain, how, m->m_type, top == NULL ? M_PKTHDR : 0,
			   &nsize);
		if (n == NULL)
			goto nospace;
		if (top == NULL)
			if (!m_dup_pkthdr(n, m, how))
				goto nospace0;

		/* Link it into the new chain */
		*p = n;
		p = &n->m_next;

		/* Copy data from original mbuf(s) into new mbuf */
		n->m_len = 0;
		while (n->m_len < nsize && m != NULL) {
			int chunk = min(nsize - n->m_len, m->m_len - moff);

			bcopy(m->m_data + moff, n->m_data + n->m_len, chunk);
			moff += chunk;
			n->m_len += chunk;
			remain -= chunk;
			if (moff == m->m_len) {
				m = m->m_next;
				moff = 0;
			}
		}

		/* Check correct total mbuf length */
		KASSERT((remain > 0 && m != NULL) || (remain == 0 && m == NULL),
			("%s: bogus m_pkthdr.len", __func__));
	}
	return (top);

nospace:
	m_freem(top);
nospace0:
	mbstat.m_mcfail++;
	return (NULL);
}

/*
 * Concatenate mbuf chain n to m.
 * Both chains must be of the same type (e.g. MT_DATA).
 * Any m_pkthdr is not updated.
 */
void
m_cat(struct mbuf *m, struct mbuf *n)
{
	m = m_last(m);
	while (n) {
		if (m->m_flags & M_EXT ||
		    m->m_data + m->m_len + n->m_len >= &m->m_dat[MLEN]) {
			/* just join the two chains */
			m->m_next = n;
			return;
		}
		/* splat the data from one into the other */
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t) + m->m_len,
		    (u_int)n->m_len);
		m->m_len += n->m_len;
		n = m_free(n);
	}
}

void
m_adj(struct mbuf *mp, int req_len)
{
	int len = req_len;
	struct mbuf *m;
	int count;

	if ((m = mp) == NULL)
		return;
	if (len >= 0) {
		/*
		 * Trim from head.
		 */
		while (m != NULL && len > 0) {
			if (m->m_len <= len) {
				len -= m->m_len;
				m->m_len = 0;
				m = m->m_next;
			} else {
				m->m_len -= len;
				m->m_data += len;
				len = 0;
			}
		}
		m = mp;
		if (mp->m_flags & M_PKTHDR)
			m->m_pkthdr.len -= (req_len - len);
	} else {
		/*
		 * Trim from tail.  Scan the mbuf chain,
		 * calculating its length and finding the last mbuf.
		 * If the adjustment only affects this mbuf, then just
		 * adjust and return.  Otherwise, rescan and truncate
		 * after the remaining size.
		 */
		len = -len;
		count = 0;
		for (;;) {
			count += m->m_len;
			if (m->m_next == (struct mbuf *)0)
				break;
			m = m->m_next;
		}
		if (m->m_len >= len) {
			m->m_len -= len;
			if (mp->m_flags & M_PKTHDR)
				mp->m_pkthdr.len -= len;
			return;
		}
		count -= len;
		if (count < 0)
			count = 0;
		/*
		 * Correct length for chain is "count".
		 * Find the mbuf with last data, adjust its length,
		 * and toss data from remaining mbufs on chain.
		 */
		m = mp;
		if (m->m_flags & M_PKTHDR)
			m->m_pkthdr.len = count;
		for (; m; m = m->m_next) {
			if (m->m_len >= count) {
				m->m_len = count;
				break;
			}
			count -= m->m_len;
		}
		while (m->m_next)
			(m = m->m_next) ->m_len = 0;
	}
}

/*
 * Rearrange an mbuf chain so that len bytes are contiguous
 * and in the data area of an mbuf (so that mtod will work for a structure
 * of size len).  Returns the resulting mbuf chain on success, frees it and
 * returns null on failure.  If there is room, it will add up to
 * max_protohdr-len extra bytes to the contiguous region in an attempt to
 * avoid being called next time.
 */
struct mbuf *
m_pullup(struct mbuf *n, int len)
{
	struct mbuf *m;
	int count;
	int space;

	/*
	 * If first mbuf has no cluster, and has room for len bytes
	 * without shifting current data, pullup into it,
	 * otherwise allocate a new mbuf to prepend to the chain.
	 */
	if (!(n->m_flags & M_EXT) &&
	    n->m_data + len < &n->m_dat[MLEN] &&
	    n->m_next) {
		if (n->m_len >= len)
			return (n);
		m = n;
		n = n->m_next;
		len -= m->m_len;
	} else {
		if (len > MHLEN)
			goto bad;
		if (n->m_flags & M_PKTHDR)
			m = m_gethdr(MB_DONTWAIT, n->m_type);
		else
			m = m_get(MB_DONTWAIT, n->m_type);
		if (m == NULL)
			goto bad;
		m->m_len = 0;
		if (n->m_flags & M_PKTHDR)
			M_MOVE_PKTHDR(m, n);
	}
	space = &m->m_dat[MLEN] - (m->m_data + m->m_len);
	do {
		count = min(min(max(len, max_protohdr), space), n->m_len);
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t) + m->m_len,
		  (unsigned)count);
		len -= count;
		m->m_len += count;
		n->m_len -= count;
		space -= count;
		if (n->m_len)
			n->m_data += count;
		else
			n = m_free(n);
	} while (len > 0 && n);
	if (len > 0) {
		m_free(m);
		goto bad;
	}
	m->m_next = n;
	return (m);
bad:
	m_freem(n);
	mbstat.m_mpfail++;
	return (NULL);
}

/*
 * Partition an mbuf chain in two pieces, returning the tail --
 * all but the first len0 bytes.  In case of failure, it returns NULL and
 * attempts to restore the chain to its original state.
 *
 * Note that the resulting mbufs might be read-only, because the new
 * mbuf can end up sharing an mbuf cluster with the original mbuf if
 * the "breaking point" happens to lie within a cluster mbuf. Use the
 * M_WRITABLE() macro to check for this case.
 */
struct mbuf *
m_split(struct mbuf *m0, int len0, int wait)
{
	struct mbuf *m, *n;
	unsigned len = len0, remain;

	for (m = m0; m && len > m->m_len; m = m->m_next)
		len -= m->m_len;
	if (m == NULL)
		return (NULL);
	remain = m->m_len - len;
	if (m0->m_flags & M_PKTHDR) {
		n = m_gethdr(wait, m0->m_type);
		if (n == NULL)
			return (NULL);
		n->m_pkthdr.rcvif = m0->m_pkthdr.rcvif;
		n->m_pkthdr.len = m0->m_pkthdr.len - len0;
		m0->m_pkthdr.len = len0;
		if (m->m_flags & M_EXT)
			goto extpacket;
		if (remain > MHLEN) {
			/* m can't be the lead packet */
			MH_ALIGN(n, 0);
			n->m_next = m_split(m, len, wait);
			if (n->m_next == NULL) {
				m_free(n);
				return (NULL);
			} else {
				n->m_len = 0;
				return (n);
			}
		} else
			MH_ALIGN(n, remain);
	} else if (remain == 0) {
		n = m->m_next;
		m->m_next = 0;
		return (n);
	} else {
		n = m_get(wait, m->m_type);
		if (n == NULL)
			return (NULL);
		M_ALIGN(n, remain);
	}
extpacket:
	if (m->m_flags & M_EXT) {
		KKASSERT((n->m_flags & M_EXT) == 0);
		n->m_data = m->m_data + len;
		m->m_ext.ext_ref(m->m_ext.ext_arg); 
		n->m_ext = m->m_ext;
		n->m_flags |= m->m_flags & (M_EXT | M_EXT_CLUSTER);
	} else {
		bcopy(mtod(m, caddr_t) + len, mtod(n, caddr_t), remain);
	}
	n->m_len = remain;
	m->m_len = len;
	n->m_next = m->m_next;
	m->m_next = 0;
	return (n);
}

/*
 * Routine to copy from device local memory into mbufs.
 * Note: "offset" is ill-defined and always called as 0, so ignore it.
 */
struct mbuf *
m_devget(char *buf, int len, int offset, struct ifnet *ifp,
    void (*copy)(volatile const void *from, volatile void *to, size_t length))
{
	struct mbuf *m, *mfirst = NULL, **mtail;
	int nsize, flags;

	if (copy == NULL)
		copy = bcopy;
	mtail = &mfirst;
	flags = M_PKTHDR;

	while (len > 0) {
		m = m_getl(len, MB_DONTWAIT, MT_DATA, flags, &nsize);
		if (m == NULL) {
			m_freem(mfirst);
			return (NULL);
		}
		m->m_len = min(len, nsize);

		if (flags & M_PKTHDR) {
			if (len + max_linkhdr <= nsize)
				m->m_data += max_linkhdr;
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = len;
			flags = 0;
		}

		copy(buf, m->m_data, (unsigned)m->m_len);
		buf += m->m_len;
		len -= m->m_len;
		*mtail = m;
		mtail = &m->m_next;
	}

	return (mfirst);
}

/*
 * Copy data from a buffer back into the indicated mbuf chain,
 * starting "off" bytes from the beginning, extending the mbuf
 * chain if necessary.
 */
void
m_copyback(struct mbuf *m0, int off, int len, caddr_t cp)
{
	int mlen;
	struct mbuf *m = m0, *n;
	int totlen = 0;

	if (m0 == NULL)
		return;
	while (off > (mlen = m->m_len)) {
		off -= mlen;
		totlen += mlen;
		if (m->m_next == NULL) {
			n = m_getclr(MB_DONTWAIT, m->m_type);
			if (n == NULL)
				goto out;
			n->m_len = min(MLEN, len + off);
			m->m_next = n;
		}
		m = m->m_next;
	}
	while (len > 0) {
		mlen = min (m->m_len - off, len);
		bcopy(cp, off + mtod(m, caddr_t), (unsigned)mlen);
		cp += mlen;
		len -= mlen;
		mlen += off;
		off = 0;
		totlen += mlen;
		if (len == 0)
			break;
		if (m->m_next == NULL) {
			n = m_get(MB_DONTWAIT, m->m_type);
			if (n == NULL)
				break;
			n->m_len = min(MLEN, len);
			m->m_next = n;
		}
		m = m->m_next;
	}
out:	if (((m = m0)->m_flags & M_PKTHDR) && (m->m_pkthdr.len < totlen))
		m->m_pkthdr.len = totlen;
}

void
m_print(const struct mbuf *m)
{
	int len;
	const struct mbuf *m2;

	len = m->m_pkthdr.len;
	m2 = m;
	while (len) {
		printf("%p %*D\n", m2, m2->m_len, (u_char *)m2->m_data, "-");
		len -= m2->m_len;
		m2 = m2->m_next;
	}
	return;
}

/*
 * "Move" mbuf pkthdr from "from" to "to".
 * "from" must have M_PKTHDR set, and "to" must be empty.
 */
void
m_move_pkthdr(struct mbuf *to, struct mbuf *from)
{
	KASSERT((to->m_flags & M_PKTHDR), ("m_move_pkthdr: not packet header"));

	to->m_flags |= from->m_flags & M_COPYFLAGS;
	to->m_pkthdr = from->m_pkthdr;		/* especially tags */
	SLIST_INIT(&from->m_pkthdr.tags);	/* purge tags from src */
}

/*
 * Duplicate "from"'s mbuf pkthdr in "to".
 * "from" must have M_PKTHDR set, and "to" must be empty.
 * In particular, this does a deep copy of the packet tags.
 */
int
m_dup_pkthdr(struct mbuf *to, const struct mbuf *from, int how)
{
	KASSERT((to->m_flags & M_PKTHDR), ("m_dup_pkthdr: not packet header"));

	to->m_flags = (from->m_flags & M_COPYFLAGS) |
		      (to->m_flags & ~M_COPYFLAGS);
	to->m_pkthdr = from->m_pkthdr;
	SLIST_INIT(&to->m_pkthdr.tags);
	return (m_tag_copy_chain(to, from, how));
}

/*
 * Defragment a mbuf chain, returning the shortest possible
 * chain of mbufs and clusters.  If allocation fails and
 * this cannot be completed, NULL will be returned, but
 * the passed in chain will be unchanged.  Upon success,
 * the original chain will be freed, and the new chain
 * will be returned.
 *
 * If a non-packet header is passed in, the original
 * mbuf (chain?) will be returned unharmed.
 *
 * m_defrag_nofree doesn't free the passed in mbuf.
 */
struct mbuf *
m_defrag(struct mbuf *m0, int how)
{
	struct mbuf *m_new;

	if ((m_new = m_defrag_nofree(m0, how)) == NULL)
		return (NULL);
	if (m_new != m0)
		m_freem(m0);
	return (m_new);
}

struct mbuf *
m_defrag_nofree(struct mbuf *m0, int how)
{
	struct mbuf	*m_new = NULL, *m_final = NULL;
	int		progress = 0, length, nsize;

	if (!(m0->m_flags & M_PKTHDR))
		return (m0);

#ifdef MBUF_STRESS_TEST
	if (m_defragrandomfailures) {
		int temp = arc4random() & 0xff;
		if (temp == 0xba)
			goto nospace;
	}
#endif
	
	m_final = m_getl(m0->m_pkthdr.len, how, MT_DATA, M_PKTHDR, &nsize);
	if (m_final == NULL)
		goto nospace;
	m_final->m_len = 0;	/* in case m0->m_pkthdr.len is zero */

	if (m_dup_pkthdr(m_final, m0, how) == NULL)
		goto nospace;

	m_new = m_final;

	while (progress < m0->m_pkthdr.len) {
		length = m0->m_pkthdr.len - progress;
		if (length > MCLBYTES)
			length = MCLBYTES;

		if (m_new == NULL) {
			m_new = m_getl(length, how, MT_DATA, 0, &nsize);
			if (m_new == NULL)
				goto nospace;
		}

		m_copydata(m0, progress, length, mtod(m_new, caddr_t));
		progress += length;
		m_new->m_len = length;
		if (m_new != m_final)
			m_cat(m_final, m_new);
		m_new = NULL;
	}
	if (m0->m_next == NULL)
		m_defraguseless++;
	m_defragpackets++;
	m_defragbytes += m_final->m_pkthdr.len;
	return (m_final);
nospace:
	m_defragfailure++;
	if (m_new)
		m_free(m_new);
	m_freem(m_final);
	return (NULL);
}

/*
 * Move data from uio into mbufs.
 */
struct mbuf *
m_uiomove(struct uio *uio)
{
	struct mbuf *m;			/* current working mbuf */
	struct mbuf *head = NULL;	/* result mbuf chain */
	struct mbuf **mp = &head;
	int resid = uio->uio_resid, nsize, flags = M_PKTHDR, error;

	do {
		m = m_getl(resid, MB_WAIT, MT_DATA, flags, &nsize);
		if (flags) {
			m->m_pkthdr.len = 0;
			/* Leave room for protocol headers. */
			if (resid < MHLEN)
				MH_ALIGN(m, resid);
			flags = 0;
		}
		m->m_len = min(nsize, resid);
		error = uiomove(mtod(m, caddr_t), m->m_len, uio);
		if (error) {
			m_free(m);
			goto failed;
		}
		*mp = m;
		mp = &m->m_next;
		head->m_pkthdr.len += m->m_len;
		resid -= m->m_len;
	} while (resid > 0);

	return (head);

failed:
	m_freem(head);
	return (NULL);
}

struct mbuf *
m_last(struct mbuf *m)
{
	while (m->m_next)
		m = m->m_next;
	return (m);
}

/*
 * Return the number of bytes in an mbuf chain.
 * If lastm is not NULL, also return the last mbuf.
 */
u_int
m_lengthm(struct mbuf *m, struct mbuf **lastm)
{
	u_int len = 0;
	struct mbuf *prev = m;

	while (m) {
		len += m->m_len;
		prev = m;
		m = m->m_next;
	}
	if (lastm != NULL)
		*lastm = prev;
	return (len);
}

/*
 * Like m_lengthm(), except also keep track of mbuf usage.
 */
u_int
m_countm(struct mbuf *m, struct mbuf **lastm, u_int *pmbcnt)
{
	u_int len = 0, mbcnt = 0;
	struct mbuf *prev = m;

	while (m) {
		len += m->m_len;
		mbcnt += MSIZE;
		if (m->m_flags & M_EXT)
			mbcnt += m->m_ext.ext_size;
		prev = m;
		m = m->m_next;
	}
	if (lastm != NULL)
		*lastm = prev;
	*pmbcnt = mbcnt;
	return (len);
}
