/*
 * (MPSAFE)
 *
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
 * @(#)uipc_mbuf.c	8.2 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/kern/uipc_mbuf.c,v 1.51.2.24 2003/04/15 06:59:29 silby Exp $
 */

#include "opt_param.h"
#include "opt_mbuf_stress_test.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/domain.h>
#include <sys/objcache.h>
#include <sys/tree.h>
#include <sys/protosw.h>
#include <sys/uio.h>
#include <sys/thread.h>
#include <sys/globaldata.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <machine/atomic.h>
#include <machine/limits.h>

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

/*
 * mbuf tracking for debugging purposes
 */
#ifdef MBUF_DEBUG

static MALLOC_DEFINE(M_MTRACK, "mtrack", "mtrack");

struct mbctrack;
RB_HEAD(mbuf_rb_tree, mbtrack);
RB_PROTOTYPE2(mbuf_rb_tree, mbtrack, rb_node, mbtrack_cmp, struct mbuf *);

struct mbtrack {
	RB_ENTRY(mbtrack) rb_node;
	int trackid;
	struct mbuf *m;
};

static int
mbtrack_cmp(struct mbtrack *mb1, struct mbtrack *mb2)
{
	if (mb1->m < mb2->m)
		return(-1);
	if (mb1->m > mb2->m)
		return(1);
	return(0);
}

RB_GENERATE2(mbuf_rb_tree, mbtrack, rb_node, mbtrack_cmp, struct mbuf *, m);

struct mbuf_rb_tree	mbuf_track_root;
static struct spinlock	mbuf_track_spin = SPINLOCK_INITIALIZER(mbuf_track_spin, "mbuf_track_spin");

static void
mbuftrack(struct mbuf *m)
{
	struct mbtrack *mbt;

	mbt = kmalloc(sizeof(*mbt), M_MTRACK, M_INTWAIT|M_ZERO);
	spin_lock(&mbuf_track_spin);
	mbt->m = m;
	if (mbuf_rb_tree_RB_INSERT(&mbuf_track_root, mbt)) {
		spin_unlock(&mbuf_track_spin);
		panic("mbuftrack: mbuf %p already being tracked", m);
	}
	spin_unlock(&mbuf_track_spin);
}

static void
mbufuntrack(struct mbuf *m)
{
	struct mbtrack *mbt;

	spin_lock(&mbuf_track_spin);
	mbt = mbuf_rb_tree_RB_LOOKUP(&mbuf_track_root, m);
	if (mbt == NULL) {
		spin_unlock(&mbuf_track_spin);
		panic("mbufuntrack: mbuf %p was not tracked", m);
	} else {
		mbuf_rb_tree_RB_REMOVE(&mbuf_track_root, mbt);
		spin_unlock(&mbuf_track_spin);
		kfree(mbt, M_MTRACK);
	}
}

void
mbuftrackid(struct mbuf *m, int trackid)
{
	struct mbtrack *mbt;
	struct mbuf *n;

	spin_lock(&mbuf_track_spin);
	while (m) { 
		n = m->m_nextpkt;
		while (m) {
			mbt = mbuf_rb_tree_RB_LOOKUP(&mbuf_track_root, m);
			if (mbt == NULL) {
				spin_unlock(&mbuf_track_spin);
				panic("mbuftrackid: mbuf %p not tracked", m);
			}
			mbt->trackid = trackid;
			m = m->m_next;
		}
		m = n;
	}
	spin_unlock(&mbuf_track_spin);
}

static int
mbuftrack_callback(struct mbtrack *mbt, void *arg)
{
	struct sysctl_req *req = arg;
	char buf[64];
	int error;

	ksnprintf(buf, sizeof(buf), "mbuf %p track %d\n", mbt->m, mbt->trackid);

	spin_unlock(&mbuf_track_spin);
	error = SYSCTL_OUT(req, buf, strlen(buf));
	spin_lock(&mbuf_track_spin);
	if (error)	
		return(-error);
	return(0);
}

static int
mbuftrack_show(SYSCTL_HANDLER_ARGS)
{
	int error;

	spin_lock(&mbuf_track_spin);
	error = mbuf_rb_tree_RB_SCAN(&mbuf_track_root, NULL,
				     mbuftrack_callback, req);
	spin_unlock(&mbuf_track_spin);
	return (-error);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, showmbufs, CTLFLAG_RD|CTLTYPE_STRING,
	    0, 0, mbuftrack_show, "A", "Show all in-use mbufs");

#else

#define mbuftrack(m)
#define mbufuntrack(m)

#endif

static void mbinit(void *);
SYSINIT(mbuf, SI_BOOT2_MACHDEP, SI_ORDER_FIRST, mbinit, NULL)

struct mbtypes_stat {
	u_long	stats[MT_NTYPES];
} __cachealign;

static struct mbtypes_stat	mbtypes[SMP_MAXCPU];

static struct mbstat mbstat[SMP_MAXCPU] __cachealign;
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
struct objcache *mclmeta_cache, *mjclmeta_cache;
struct objcache *mbufcluster_cache, *mbufphdrcluster_cache;
struct objcache *mbufjcluster_cache, *mbufphdrjcluster_cache;

int		nmbclusters;
static int	nmbjclusters;
int		nmbufs;

static int	mjclph_cachefrac;
static int	mjcl_cachefrac;
static int	mclph_cachefrac;
static int	mcl_cachefrac;

SYSCTL_INT(_kern_ipc, KIPC_MAX_LINKHDR, max_linkhdr, CTLFLAG_RW,
	&max_linkhdr, 0, "Max size of a link-level header");
SYSCTL_INT(_kern_ipc, KIPC_MAX_PROTOHDR, max_protohdr, CTLFLAG_RW,
	&max_protohdr, 0, "Max size of a protocol header");
SYSCTL_INT(_kern_ipc, KIPC_MAX_HDR, max_hdr, CTLFLAG_RW, &max_hdr, 0,
	"Max size of link+protocol headers");
SYSCTL_INT(_kern_ipc, KIPC_MAX_DATALEN, max_datalen, CTLFLAG_RW,
	&max_datalen, 0, "Max data payload size without headers");
SYSCTL_INT(_kern_ipc, OID_AUTO, mbuf_wait, CTLFLAG_RW,
	&mbuf_wait, 0, "Time in ticks to sleep after failed mbuf allocations");
static int do_mbstat(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_kern_ipc, KIPC_MBSTAT, mbstat, CTLTYPE_STRUCT|CTLFLAG_RD,
	0, 0, do_mbstat, "S,mbstat", "mbuf usage statistics");

static int do_mbtypes(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_kern_ipc, OID_AUTO, mbtypes, CTLTYPE_ULONG|CTLFLAG_RD,
	0, 0, do_mbtypes, "LU", "");

static int
do_mbstat(SYSCTL_HANDLER_ARGS)
{
	struct mbstat mbstat_total;
	struct mbstat *mbstat_totalp;
	int i;

	bzero(&mbstat_total, sizeof(mbstat_total));
	mbstat_totalp = &mbstat_total;

	for (i = 0; i < ncpus; i++)
	{
		mbstat_total.m_mbufs += mbstat[i].m_mbufs;	
		mbstat_total.m_clusters += mbstat[i].m_clusters;	
		mbstat_total.m_jclusters += mbstat[i].m_jclusters;	
		mbstat_total.m_clfree += mbstat[i].m_clfree;	
		mbstat_total.m_drops += mbstat[i].m_drops;	
		mbstat_total.m_wait += mbstat[i].m_wait;	
		mbstat_total.m_drain += mbstat[i].m_drain;	
		mbstat_total.m_mcfail += mbstat[i].m_mcfail;	
		mbstat_total.m_mpfail += mbstat[i].m_mpfail;	

	}
	/*
	 * The following fields are not cumulative fields so just
	 * get their values once.
	 */
	mbstat_total.m_msize = mbstat[0].m_msize;	
	mbstat_total.m_mclbytes = mbstat[0].m_mclbytes;	
	mbstat_total.m_minclsize = mbstat[0].m_minclsize;	
	mbstat_total.m_mlen = mbstat[0].m_mlen;	
	mbstat_total.m_mhlen = mbstat[0].m_mhlen;	

	return(sysctl_handle_opaque(oidp, mbstat_totalp, sizeof(mbstat_total), req));
}

static int
do_mbtypes(SYSCTL_HANDLER_ARGS)
{
	u_long totals[MT_NTYPES];
	int i, j;

	for (i = 0; i < MT_NTYPES; i++)
		totals[i] = 0;

	for (i = 0; i < ncpus; i++)
	{
		for (j = 0; j < MT_NTYPES; j++)
			totals[j] += mbtypes[i].stats[j];
	}

	return(sysctl_handle_opaque(oidp, totals, sizeof(totals), req));
}

/*
 * These are read-only because we do not currently have any code
 * to adjust the objcache limits after the fact.  The variables
 * may only be set as boot-time tunables.
 */
SYSCTL_INT(_kern_ipc, KIPC_NMBCLUSTERS, nmbclusters, CTLFLAG_RD,
	   &nmbclusters, 0, "Maximum number of mbuf clusters available");
SYSCTL_INT(_kern_ipc, OID_AUTO, nmbufs, CTLFLAG_RD, &nmbufs, 0,
	   "Maximum number of mbufs available");
SYSCTL_INT(_kern_ipc, OID_AUTO, nmbjclusters, CTLFLAG_RD, &nmbjclusters, 0,
	   "Maximum number of mbuf jclusters available");
SYSCTL_INT(_kern_ipc, OID_AUTO, mjclph_cachefrac, CTLFLAG_RD,
	   &mjclph_cachefrac, 0,
	   "Fraction of cacheable mbuf jclusters w/ pkthdr");
SYSCTL_INT(_kern_ipc, OID_AUTO, mjcl_cachefrac, CTLFLAG_RD,
	   &mjcl_cachefrac, 0,
	   "Fraction of cacheable mbuf jclusters");
SYSCTL_INT(_kern_ipc, OID_AUTO, mclph_cachefrac, CTLFLAG_RD,
    	   &mclph_cachefrac, 0,
	   "Fraction of cacheable mbuf clusters w/ pkthdr");
SYSCTL_INT(_kern_ipc, OID_AUTO, mcl_cachefrac, CTLFLAG_RD,
    	   &mcl_cachefrac, 0, "Fraction of cacheable mbuf clusters");

SYSCTL_INT(_kern_ipc, OID_AUTO, m_defragpackets, CTLFLAG_RD,
	   &m_defragpackets, 0, "Number of defragment packets");
SYSCTL_INT(_kern_ipc, OID_AUTO, m_defragbytes, CTLFLAG_RD,
	   &m_defragbytes, 0, "Number of defragment bytes");
SYSCTL_INT(_kern_ipc, OID_AUTO, m_defraguseless, CTLFLAG_RD,
	   &m_defraguseless, 0, "Number of useless defragment mbuf chain operations");
SYSCTL_INT(_kern_ipc, OID_AUTO, m_defragfailure, CTLFLAG_RD,
	   &m_defragfailure, 0, "Number of failed defragment mbuf chain operations");
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
static void m_mjclfree(void *arg);

/*
 * NOTE: Default NMBUFS must take into account a possible DOS attack
 *	 using fd passing on unix domain sockets.
 */
#ifndef NMBCLUSTERS
#define NMBCLUSTERS	(512 + maxusers * 16)
#endif
#ifndef MJCLPH_CACHEFRAC
#define MJCLPH_CACHEFRAC 16
#endif
#ifndef MJCL_CACHEFRAC
#define MJCL_CACHEFRAC	4
#endif
#ifndef MCLPH_CACHEFRAC
#define MCLPH_CACHEFRAC	16
#endif
#ifndef MCL_CACHEFRAC
#define MCL_CACHEFRAC	4
#endif
#ifndef NMBJCLUSTERS
#define NMBJCLUSTERS	(NMBCLUSTERS / 2)
#endif
#ifndef NMBUFS
#define NMBUFS		(nmbclusters * 2 + maxfiles)
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
	mjclph_cachefrac = MJCLPH_CACHEFRAC;
	TUNABLE_INT_FETCH("kern.ipc.mjclph_cachefrac", &mjclph_cachefrac);
	mjcl_cachefrac = MJCL_CACHEFRAC;
	TUNABLE_INT_FETCH("kern.ipc.mjcl_cachefrac", &mjcl_cachefrac);
	mclph_cachefrac = MCLPH_CACHEFRAC;
	TUNABLE_INT_FETCH("kern.ipc.mclph_cachefrac", &mclph_cachefrac);
	mcl_cachefrac = MCL_CACHEFRAC;
	TUNABLE_INT_FETCH("kern.ipc.mcl_cachefrac", &mcl_cachefrac);

	/*
	 * WARNING! each mcl cache feeds two mbuf caches, so the minimum
	 *	    cachefrac is 2.  For safety, use 3.
	 */
	if (mjclph_cachefrac < 3)
		mjclph_cachefrac = 3;
	if (mjcl_cachefrac < 3)
		mjcl_cachefrac = 3;
	if (mclph_cachefrac < 3)
		mclph_cachefrac = 3;
	if (mcl_cachefrac < 3)
		mcl_cachefrac = 3;

	nmbjclusters = NMBJCLUSTERS;
	TUNABLE_INT_FETCH("kern.ipc.nmbjclusters", &nmbjclusters);

	nmbufs = NMBUFS;
	TUNABLE_INT_FETCH("kern.ipc.nmbufs", &nmbufs);

	/* Sanity checks */
	if (nmbufs < nmbclusters * 2)
		nmbufs = nmbclusters * 2;
}
SYSINIT(tunable_mbinit, SI_BOOT1_TUNABLES, SI_ORDER_ANY,
	tunable_mbinit, NULL);

/* "number of clusters of pages" */
#define NCL_INIT	1

#define NMB_INIT	16

/*
 * The mbuf object cache only guarantees that m_next and m_nextpkt are
 * NULL and that m_data points to the beginning of the data area.  In
 * particular, m_len and m_pkthdr.len are uninitialized.  It is the
 * responsibility of the caller to initialize those fields before use.
 */

static __inline boolean_t
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
		buf = kmalloc(MCLBYTES, M_MBUFCL, M_NOWAIT | M_ZERO);
	else
		buf = kmalloc(MCLBYTES, M_MBUFCL, M_INTWAIT | M_ZERO);
	if (buf == NULL)
		return (FALSE);
	cl->mcl_refs = 0;
	cl->mcl_data = buf;
	return (TRUE);
}

static boolean_t
mjclmeta_ctor(void *obj, void *private, int ocflags)
{
	struct mbcluster *cl = obj;
	void *buf;

	if (ocflags & M_NOWAIT)
		buf = kmalloc(MJUMPAGESIZE, M_MBUFCL, M_NOWAIT | M_ZERO);
	else
		buf = kmalloc(MJUMPAGESIZE, M_MBUFCL, M_INTWAIT | M_ZERO);
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
	kfree(mcl->mcl_data, M_MBUFCL);
}

static void
linkjcluster(struct mbuf *m, struct mbcluster *cl, uint size)
{
	/*
	 * Add the cluster to the mbuf.  The caller will detect that the
	 * mbuf now has an attached cluster.
	 */
	m->m_ext.ext_arg = cl;
	m->m_ext.ext_buf = cl->mcl_data;
	m->m_ext.ext_ref = m_mclref;
	if (size != MCLBYTES)
		m->m_ext.ext_free = m_mjclfree;
	else
		m->m_ext.ext_free = m_mclfree;
	m->m_ext.ext_size = size;
	atomic_add_int(&cl->mcl_refs, 1);

	m->m_data = m->m_ext.ext_buf;
	m->m_flags |= M_EXT | M_EXT_CLUSTER;
}

static void
linkcluster(struct mbuf *m, struct mbcluster *cl)
{
	linkjcluster(m, cl, MCLBYTES);
}

static boolean_t
mbufphdrcluster_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;
	struct mbcluster *cl;

	mbufphdr_ctor(obj, private, ocflags);
	cl = objcache_get(mclmeta_cache, ocflags);
	if (cl == NULL) {
		++mbstat[mycpu->gd_cpuid].m_drops;
		return (FALSE);
	}
	m->m_flags |= M_CLCACHE;
	linkcluster(m, cl);
	return (TRUE);
}

static boolean_t
mbufphdrjcluster_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;
	struct mbcluster *cl;

	mbufphdr_ctor(obj, private, ocflags);
	cl = objcache_get(mjclmeta_cache, ocflags);
	if (cl == NULL) {
		++mbstat[mycpu->gd_cpuid].m_drops;
		return (FALSE);
	}
	m->m_flags |= M_CLCACHE;
	linkjcluster(m, cl, MJUMPAGESIZE);
	return (TRUE);
}

static boolean_t
mbufcluster_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;
	struct mbcluster *cl;

	mbuf_ctor(obj, private, ocflags);
	cl = objcache_get(mclmeta_cache, ocflags);
	if (cl == NULL) {
		++mbstat[mycpu->gd_cpuid].m_drops;
		return (FALSE);
	}
	m->m_flags |= M_CLCACHE;
	linkcluster(m, cl);
	return (TRUE);
}

static boolean_t
mbufjcluster_ctor(void *obj, void *private, int ocflags)
{
	struct mbuf *m = obj;
	struct mbcluster *cl;

	mbuf_ctor(obj, private, ocflags);
	cl = objcache_get(mjclmeta_cache, ocflags);
	if (cl == NULL) {
		++mbstat[mycpu->gd_cpuid].m_drops;
		return (FALSE);
	}
	m->m_flags |= M_CLCACHE;
	linkjcluster(m, cl, MJUMPAGESIZE);
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
		if (m->m_flags & M_EXT && m->m_ext.ext_size != MCLBYTES)
			objcache_put(mjclmeta_cache, mcl);
		else
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
	int mb_limit, cl_limit, ncl_limit, jcl_limit;
	int limit;
	int i;

	/*
	 * Initialize statistics
	 */
	for (i = 0; i < ncpus; i++) {
		mbstat[i].m_msize = MSIZE;
		mbstat[i].m_mclbytes = MCLBYTES;
		mbstat[i].m_mjumpagesize = MJUMPAGESIZE;
		mbstat[i].m_minclsize = MINCLSIZE;
		mbstat[i].m_mlen = MLEN;
		mbstat[i].m_mhlen = MHLEN;
	}

	/*
	 * Create objtect caches and save cluster limits, which will
	 * be used to adjust backing kmalloc pools' limit later.
	 */

	mb_limit = cl_limit = 0;

	limit = nmbufs;
	mbuf_cache = objcache_create("mbuf",
	    limit, nmbufs / 4,
	    mbuf_ctor, NULL, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mb_limit += limit;

	limit = nmbufs;
	mbufphdr_cache = objcache_create("mbuf pkt hdr",
	    limit, nmbufs / 4,
	    mbufphdr_ctor, NULL, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mb_limit += limit;

	ncl_limit = nmbclusters;
	mclmeta_cache = objcache_create("cluster mbuf",
	    ncl_limit, nmbclusters / 4,
	    mclmeta_ctor, mclmeta_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mclmeta_malloc_args);
	cl_limit += ncl_limit;

	jcl_limit = nmbjclusters;
	mjclmeta_cache = objcache_create("jcluster mbuf",
	    jcl_limit, nmbjclusters / 4,
	    mjclmeta_ctor, mclmeta_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mclmeta_malloc_args);
	cl_limit += jcl_limit;

	limit = nmbclusters;
	mbufcluster_cache = objcache_create("mbuf + cluster",
	    limit, nmbclusters / mcl_cachefrac,
	    mbufcluster_ctor, mbufcluster_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mb_limit += limit;

	limit = nmbclusters;
	mbufphdrcluster_cache = objcache_create("mbuf pkt hdr + cluster",
	    limit, nmbclusters / mclph_cachefrac,
	    mbufphdrcluster_ctor, mbufcluster_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mb_limit += limit;

	limit = nmbjclusters;
	mbufjcluster_cache = objcache_create("mbuf + jcluster",
	    limit, nmbjclusters / mjcl_cachefrac,
	    mbufjcluster_ctor, mbufcluster_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mb_limit += limit;

	limit = nmbjclusters;
	mbufphdrjcluster_cache = objcache_create("mbuf pkt hdr + jcluster",
	    limit, nmbjclusters / mjclph_cachefrac,
	    mbufphdrjcluster_ctor, mbufcluster_dtor, NULL,
	    objcache_malloc_alloc, objcache_malloc_free, &mbuf_malloc_args);
	mb_limit += limit;

	/*
	 * Adjust backing kmalloc pools' limit
	 *
	 * NOTE: We raise the limit by another 1/8 to take the effect
	 * of loosememuse into account.
	 */
	cl_limit += cl_limit / 8;
	kmalloc_raise_limit(mclmeta_malloc_args.mtype,
			    mclmeta_malloc_args.objsize * (size_t)cl_limit);
	kmalloc_raise_limit(M_MBUFCL,
			    (MCLBYTES * (size_t)ncl_limit) +
			    (MJUMPAGESIZE * (size_t)jcl_limit));

	mb_limit += mb_limit / 8;
	kmalloc_raise_limit(mbuf_malloc_args.mtype,
			    mbuf_malloc_args.objsize * (size_t)mb_limit);
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
	struct globaldata *gd = mycpu;

	++mbtypes[gd->gd_cpuid].stats[type];
	--mbtypes[gd->gd_cpuid].stats[m->m_type];
	m->m_type = type;
}

static void
m_reclaim(void)
{
	struct domain *dp;
	struct protosw *pr;

	kprintf("Debug: m_reclaim() called\n");

	SLIST_FOREACH(dp, &domains, dom_next) {
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
			if (pr->pr_drain)
				(*pr->pr_drain)();
		}
	}
	++mbstat[mycpu->gd_cpuid].m_drain;
}

static __inline void
updatestats(struct mbuf *m, int type)
{
	struct globaldata *gd = mycpu;

	m->m_type = type;
	mbuftrack(m);
#ifdef MBUF_DEBUG
	KASSERT(m->m_next == NULL, ("mbuf %p: bad m_next in get", m));
	KASSERT(m->m_nextpkt == NULL, ("mbuf %p: bad m_nextpkt in get", m));
#endif

	++mbtypes[gd->gd_cpuid].stats[type];
	++mbstat[gd->gd_cpuid].m_mbufs;

}

/*
 * Allocate an mbuf.
 */
struct mbuf *
m_get(int how, int type)
{
	struct mbuf *m;
	int ntries = 0;
	int ocf = MB_OCFLAG(how);

retryonce:

	m = objcache_get(mbuf_cache, ocf);

	if (m == NULL) {
		if ((ocf & M_WAITOK) && ntries++ == 0) {
			struct objcache *reclaimlist[] = {
				mbufphdr_cache,
				mbufcluster_cache,
				mbufphdrcluster_cache,
				mbufjcluster_cache,
				mbufphdrjcluster_cache
			};
			const int nreclaims = NELEM(reclaimlist);

			if (!objcache_reclaimlist(reclaimlist, nreclaims, ocf))
				m_reclaim();
			goto retryonce;
		}
		++mbstat[mycpu->gd_cpuid].m_drops;
		return (NULL);
	}
#ifdef MBUF_DEBUG
	KASSERT(m->m_data == m->m_dat, ("mbuf %p: bad m_data in get", m));
#endif
	m->m_len = 0;

	updatestats(m, type);
	return (m);
}

struct mbuf *
m_gethdr(int how, int type)
{
	struct mbuf *m;
	int ocf = MB_OCFLAG(how);
	int ntries = 0;

retryonce:

	m = objcache_get(mbufphdr_cache, ocf);

	if (m == NULL) {
		if ((ocf & M_WAITOK) && ntries++ == 0) {
			struct objcache *reclaimlist[] = {
				mbuf_cache,
				mbufcluster_cache, mbufphdrcluster_cache,
				mbufjcluster_cache, mbufphdrjcluster_cache
			};
			const int nreclaims = NELEM(reclaimlist);

			if (!objcache_reclaimlist(reclaimlist, nreclaims, ocf))
				m_reclaim();
			goto retryonce;
		}
		++mbstat[mycpu->gd_cpuid].m_drops;
		return (NULL);
	}
#ifdef MBUF_DEBUG
	KASSERT(m->m_data == m->m_pktdat, ("mbuf %p: bad m_data in get", m));
#endif
	m->m_len = 0;
	m->m_pkthdr.len = 0;

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

static struct mbuf *
m_getcl_cache(int how, short type, int flags, struct objcache *mbclc,
    struct objcache *mbphclc, u_long *cl_stats)
{
	struct mbuf *m = NULL;
	int ocflags = MB_OCFLAG(how);
	int ntries = 0;

retryonce:

	if (flags & M_PKTHDR)
		m = objcache_get(mbphclc, ocflags);
	else
		m = objcache_get(mbclc, ocflags);

	if (m == NULL) {
		if ((ocflags & M_WAITOK) && ntries++ == 0) {
			struct objcache *reclaimlist[1];

			if (flags & M_PKTHDR)
				reclaimlist[0] = mbclc;
			else
				reclaimlist[0] = mbphclc;
			if (!objcache_reclaimlist(reclaimlist, 1, ocflags))
				m_reclaim();
			goto retryonce;
		}
		++mbstat[mycpu->gd_cpuid].m_drops;
		return (NULL);
	}

#ifdef MBUF_DEBUG
	KASSERT(m->m_data == m->m_ext.ext_buf,
		("mbuf %p: bad m_data in get", m));
#endif
	m->m_type = type;
	m->m_len = 0;
	m->m_pkthdr.len = 0;	/* just do it unconditonally */

	mbuftrack(m);

	++mbtypes[mycpu->gd_cpuid].stats[type];
	++(*cl_stats);
	return (m);
}

struct mbuf *
m_getjcl(int how, short type, int flags, size_t size)
{
	struct objcache *mbclc, *mbphclc;
	u_long *cl_stats;

	switch (size) {
	case MCLBYTES:
		mbclc = mbufcluster_cache;
		mbphclc = mbufphdrcluster_cache;
		cl_stats = &mbstat[mycpu->gd_cpuid].m_clusters;
		break;

	default:
		mbclc = mbufjcluster_cache;
		mbphclc = mbufphdrjcluster_cache;
		cl_stats = &mbstat[mycpu->gd_cpuid].m_jclusters;
		break;
	}
	return m_getcl_cache(how, type, flags, mbclc, mbphclc, cl_stats);
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
	return m_getcl_cache(how, type, flags,
	    mbufcluster_cache, mbufphdrcluster_cache,
	    &mbstat[mycpu->gd_cpuid].m_clusters);
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
m_getm(struct mbuf *m0, int len, int type, int how)
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
	mcl = objcache_get(mclmeta_cache, MB_OCFLAG(how));
	if (mcl != NULL) {
		linkcluster(m, mcl);
		++mbstat[mycpu->gd_cpuid].m_clusters;
	} else {
		++mbstat[mycpu->gd_cpuid].m_drops;
	}
}

/*
 * Updates to mbcluster must be MPSAFE.  Only an entity which already has
 * a reference to the cluster can ref it, so we are in no danger of 
 * racing an add with a subtract.  But the operation must still be atomic
 * since multiple entities may have a reference on the cluster.
 *
 * m_mclfree() is almost the same but it must contend with two entities
 * freeing the cluster at the same time.
 */
static void
m_mclref(void *arg)
{
	struct mbcluster *mcl = arg;

	atomic_add_int(&mcl->mcl_refs, 1);
}

/*
 * When dereferencing a cluster we have to deal with a N->0 race, where
 * N entities free their references simultaniously.  To do this we use
 * atomic_fetchadd_int().
 */
static void
m_mclfree(void *arg)
{
	struct mbcluster *mcl = arg;

	if (atomic_fetchadd_int(&mcl->mcl_refs, -1) == 1) {
		--mbstat[mycpu->gd_cpuid].m_clusters;
		objcache_put(mclmeta_cache, mcl);
	}
}

static void
m_mjclfree(void *arg)
{
	struct mbcluster *mcl = arg;

	if (atomic_fetchadd_int(&mcl->mcl_refs, -1) == 1) {
		--mbstat[mycpu->gd_cpuid].m_jclusters;
		objcache_put(mjclmeta_cache, mcl);
	}
}

/*
 * Free a single mbuf and any associated external storage.  The successor,
 * if any, is returned.
 *
 * We do need to check non-first mbuf for m_aux, since some of existing
 * code does not call M_PREPEND properly.
 * (example: call to bpf_mtap from drivers)
 */

#ifdef MBUF_DEBUG

struct mbuf  *
_m_free(struct mbuf *m, const char *func)

#else

struct mbuf *
m_free(struct mbuf *m)

#endif
{
	struct mbuf *n;
	struct globaldata *gd = mycpu;

	KASSERT(m->m_type != MT_FREE, ("freeing free mbuf %p", m));
	KASSERT(M_TRAILINGSPACE(m) >= 0, ("overflowed mbuf %p", m));
	--mbtypes[gd->gd_cpuid].stats[m->m_type];

	n = m->m_next;

	/*
	 * Make sure the mbuf is in constructed state before returning it
	 * to the objcache.
	 */
	m->m_next = NULL;
	mbufuntrack(m);
#ifdef MBUF_DEBUG
	m->m_hdr.mh_lastfunc = func;
#endif
#ifdef notyet
	KKASSERT(m->m_nextpkt == NULL);
#else
	if (m->m_nextpkt != NULL) {
		static int afewtimes = 10;

		if (afewtimes-- > 0) {
			kprintf("mfree: m->m_nextpkt != NULL\n");
			print_backtrace(-1);
		}
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
			if (m->m_flags & M_EXT && m->m_ext.ext_size != MCLBYTES) {
				if (m->m_flags & M_PHCACHE)
					objcache_put(mbufphdrjcluster_cache, m);
				else
					objcache_put(mbufjcluster_cache, m);
				--mbstat[mycpu->gd_cpuid].m_jclusters;
			} else {
				if (m->m_flags & M_PHCACHE)
					objcache_put(mbufphdrcluster_cache, m);
				else
					objcache_put(mbufcluster_cache, m);
				--mbstat[mycpu->gd_cpuid].m_clusters;
			}
		} else {
			/*
			 * Hell.  Someone else has a ref on this cluster,
			 * we have to disconnect it which means we can't
			 * put it back into the mbufcluster_cache, we
			 * have to destroy the mbuf.
			 *
			 * Other mbuf references to the cluster will typically
			 * be M_EXT | M_EXT_CLUSTER but without M_CLCACHE.
			 *
			 * XXX we could try to connect another cluster to
			 * it.
			 */
			m->m_ext.ext_free(m->m_ext.ext_arg); 
			m->m_flags &= ~(M_EXT | M_EXT_CLUSTER);
			if (m->m_ext.ext_size == MCLBYTES) {
				if (m->m_flags & M_PHCACHE)
					objcache_dtor(mbufphdrcluster_cache, m);
				else
					objcache_dtor(mbufcluster_cache, m);
			} else {
				if (m->m_flags & M_PHCACHE)
					objcache_dtor(mbufphdrjcluster_cache, m);
				else
					objcache_dtor(mbufjcluster_cache, m);
			}
		}
		break;
	case M_EXT | M_EXT_CLUSTER:
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
		--mbstat[mycpu->gd_cpuid].m_mbufs;
		break;
	default:
		if (!panicstr)
			panic("bad mbuf flags %p %08x", m, m->m_flags);
		break;
	}
	return (n);
}

#ifdef MBUF_DEBUG

void
_m_freem(struct mbuf *m, const char *func)
{
	while (m)
		m = _m_free(m, func);
}

#else

void
m_freem(struct mbuf *m)
{
	while (m)
		m = m_free(m);
}

#endif

void
m_extadd(struct mbuf *m, caddr_t buf, u_int size,  void (*reff)(void *),
    void (*freef)(void *), void *arg)
{
	m->m_ext.ext_arg = arg;
	m->m_ext.ext_buf = buf;
	m->m_ext.ext_ref = reff;
	m->m_ext.ext_free = freef;
	m->m_ext.ext_size = size;
	reff(arg);
	m->m_data = buf;
	m->m_flags |= M_EXT;
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
 * The wait parameter is a choice of M_WAITOK/M_NOWAIT from caller.
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
	if (off == 0 && (m->m_flags & M_PKTHDR))
		copyhdr = 1;
	while (off > 0) {
		KASSERT(m != NULL, ("m_copym, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	np = &top;
	top = NULL;
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
		++mbstat[mycpu->gd_cpuid].m_mcfail;
	return (top);
nospace:
	m_freem(top);
	++mbstat[mycpu->gd_cpuid].m_mcfail;
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
	++mbstat[mycpu->gd_cpuid].m_mcfail;
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
	++mbstat[mycpu->gd_cpuid].m_mcfail;
	return (NULL);
}

/*
 * Copy the non-packet mbuf data chain into a new set of mbufs, including
 * copying any mbuf clusters.  This is typically used to realign a data
 * chain by nfs_realign().
 *
 * The original chain is left intact.  how should be M_WAITOK or M_NOWAIT
 * and NULL can be returned if M_NOWAIT is passed.
 *
 * Be careful to use cluster mbufs, a large mbuf chain converted to non
 * cluster mbufs can exhaust our supply of mbufs.
 */
struct mbuf *
m_dup_data(struct mbuf *m, int how)
{
	struct mbuf **p, *n, *top = NULL;
	int mlen, moff, chunk, gsize, nsize;

	/*
	 * Degenerate case
	 */
	if (m == NULL)
		return (NULL);

	/*
	 * Optimize the mbuf allocation but do not get too carried away.
	 */
	if (m->m_next || m->m_len > MLEN)
		if (m->m_flags & M_EXT && m->m_ext.ext_size == MCLBYTES)
			gsize = MCLBYTES;
		else
			gsize = MJUMPAGESIZE;
	else
		gsize = MLEN;

	/* Chain control */
	p = &top;
	n = NULL;
	nsize = 0;

	/*
	 * Scan the mbuf chain until nothing is left, the new mbuf chain
	 * will be allocated on the fly as needed.
	 */
	while (m) {
		mlen = m->m_len;
		moff = 0;

		while (mlen) {
			KKASSERT(m->m_type == MT_DATA);
			if (n == NULL) {
				n = m_getl(gsize, how, MT_DATA, 0, &nsize);
				n->m_len = 0;
				if (n == NULL)
					goto nospace;
				*p = n;
				p = &n->m_next;
			}
			chunk = imin(mlen, nsize);
			bcopy(m->m_data + moff, n->m_data + n->m_len, chunk);
			mlen -= chunk;
			moff += chunk;
			n->m_len += chunk;
			nsize -= chunk;
			if (nsize == 0)
				n = NULL;
		}
		m = m->m_next;
	}
	*p = NULL;
	return(top);
nospace:
	*p = NULL;
	m_freem(top);
	++mbstat[mycpu->gd_cpuid].m_mcfail;
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
			if (m->m_next == NULL)
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
 * Set the m_data pointer of a newly-allocated mbuf
 * to place an object of the specified size at the
 * end of the mbuf, longword aligned.
 */
void
m_align(struct mbuf *m, int len)
{
	int adjust;

	if (m->m_flags & M_EXT)
		adjust = m->m_ext.ext_size - len;
	else if (m->m_flags & M_PKTHDR)
		adjust = MHLEN - len;
	else
		adjust = MLEN - len;
	m->m_data += adjust &~ (sizeof(long)-1);
}

/*
 * Create a writable copy of the mbuf chain.  While doing this
 * we compact the chain with a goal of producing a chain with
 * at most two mbufs.  The second mbuf in this chain is likely
 * to be a cluster.  The primary purpose of this work is to create
 * a writable packet for encryption, compression, etc.  The
 * secondary goal is to linearize the data so the data can be
 * passed to crypto hardware in the most efficient manner possible.
 */
struct mbuf *
m_unshare(struct mbuf *m0, int how)
{
	struct mbuf *m, *mprev;
	struct mbuf *n, *mfirst, *mlast;
	int len, off;

	mprev = NULL;
	for (m = m0; m != NULL; m = mprev->m_next) {
		/*
		 * Regular mbufs are ignored unless there's a cluster
		 * in front of it that we can use to coalesce.  We do
		 * the latter mainly so later clusters can be coalesced
		 * also w/o having to handle them specially (i.e. convert
		 * mbuf+cluster -> cluster).  This optimization is heavily
		 * influenced by the assumption that we're running over
		 * Ethernet where MCLBYTES is large enough that the max
		 * packet size will permit lots of coalescing into a
		 * single cluster.  This in turn permits efficient
		 * crypto operations, especially when using hardware.
		 */
		if ((m->m_flags & M_EXT) == 0) {
			if (mprev && (mprev->m_flags & M_EXT) &&
			    m->m_len <= M_TRAILINGSPACE(mprev)) {
				/* XXX: this ignores mbuf types */
				memcpy(mtod(mprev, caddr_t) + mprev->m_len,
				       mtod(m, caddr_t), m->m_len);
				mprev->m_len += m->m_len;
				mprev->m_next = m->m_next;	/* unlink from chain */
				m_free(m);			/* reclaim mbuf */
			} else {
				mprev = m;
			}
			continue;
		}
		/*
		 * Writable mbufs are left alone (for now).
		 */
		if (M_WRITABLE(m)) {
			mprev = m;
			continue;
		}

		/*
		 * Not writable, replace with a copy or coalesce with
		 * the previous mbuf if possible (since we have to copy
		 * it anyway, we try to reduce the number of mbufs and
		 * clusters so that future work is easier).
		 */
		KASSERT(m->m_flags & M_EXT, ("m_flags 0x%x", m->m_flags));
		/* NB: we only coalesce into a cluster or larger */
		if (mprev != NULL && (mprev->m_flags & M_EXT) &&
		    m->m_len <= M_TRAILINGSPACE(mprev)) {
			/* XXX: this ignores mbuf types */
			memcpy(mtod(mprev, caddr_t) + mprev->m_len,
			       mtod(m, caddr_t), m->m_len);
			mprev->m_len += m->m_len;
			mprev->m_next = m->m_next;	/* unlink from chain */
			m_free(m);			/* reclaim mbuf */
			continue;
		}

		/*
		 * Allocate new space to hold the copy...
		 */
		/* XXX why can M_PKTHDR be set past the first mbuf? */
		if (mprev == NULL && (m->m_flags & M_PKTHDR)) {
			/*
			 * NB: if a packet header is present we must
			 * allocate the mbuf separately from any cluster
			 * because M_MOVE_PKTHDR will smash the data
			 * pointer and drop the M_EXT marker.
			 */
			MGETHDR(n, how, m->m_type);
			if (n == NULL) {
				m_freem(m0);
				return (NULL);
			}
			M_MOVE_PKTHDR(n, m);
			MCLGET(n, how);
			if ((n->m_flags & M_EXT) == 0) {
				m_free(n);
				m_freem(m0);
				return (NULL);
			}
		} else {
			n = m_getcl(how, m->m_type, m->m_flags);
			if (n == NULL) {
				m_freem(m0);
				return (NULL);
			}
		}
		/*
		 * ... and copy the data.  We deal with jumbo mbufs
		 * (i.e. m_len > MCLBYTES) by splitting them into
		 * clusters.  We could just malloc a buffer and make
		 * it external but too many device drivers don't know
		 * how to break up the non-contiguous memory when
		 * doing DMA.
		 */
		len = m->m_len;
		off = 0;
		mfirst = n;
		mlast = NULL;
		for (;;) {
			int cc = min(len, MCLBYTES);
			memcpy(mtod(n, caddr_t), mtod(m, caddr_t) + off, cc);
			n->m_len = cc;
			if (mlast != NULL)
				mlast->m_next = n;
			mlast = n;	

			len -= cc;
			if (len <= 0)
				break;
			off += cc;

			n = m_getcl(how, m->m_type, m->m_flags);
			if (n == NULL) {
				m_freem(mfirst);
				m_freem(m0);
				return (NULL);
			}
		}
		n->m_next = m->m_next; 
		if (mprev == NULL)
			m0 = mfirst;		/* new head of chain */
		else
			mprev->m_next = mfirst;	/* replace old mbuf */
		m_free(m);			/* release old mbuf */
		mprev = mfirst;
	}
	return (m0);
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
			m = m_gethdr(M_NOWAIT, n->m_type);
		else
			m = m_get(M_NOWAIT, n->m_type);
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
	++mbstat[mycpu->gd_cpuid].m_mcfail;
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
		m->m_next = NULL;
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
	m->m_next = NULL;
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
		m = m_getl(len, M_NOWAIT, MT_DATA, flags, &nsize);
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
 * Routine to pad mbuf to the specified length 'padto'.
 */
int
m_devpad(struct mbuf *m, int padto)
{
	struct mbuf *last = NULL;
	int padlen;

	if (padto <= m->m_pkthdr.len)
		return 0;

	padlen = padto - m->m_pkthdr.len;

	/* if there's only the packet-header and we can pad there, use it. */
	if (m->m_pkthdr.len == m->m_len && M_TRAILINGSPACE(m) >= padlen) {
		last = m;
	} else {
		/*
		 * Walk packet chain to find last mbuf. We will either
		 * pad there, or append a new mbuf and pad it
		 */
		for (last = m; last->m_next != NULL; last = last->m_next)
			; /* EMPTY */

		/* `last' now points to last in chain. */
		if (M_TRAILINGSPACE(last) < padlen) {
			struct mbuf *n;

			/* Allocate new empty mbuf, pad it.  Compact later. */
			MGET(n, M_NOWAIT, MT_DATA);
			if (n == NULL)
				return ENOBUFS;
			n->m_len = 0;
			last->m_next = n;
			last = n;
		}
	}
	KKASSERT(M_TRAILINGSPACE(last) >= padlen);
	KKASSERT(M_WRITABLE(last));

	/* Now zero the pad area */
	bzero(mtod(last, char *) + last->m_len, padlen);
	last->m_len += padlen;
	m->m_pkthdr.len += padlen;
	return 0;
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
			n = m_getclr(M_NOWAIT, m->m_type);
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
			n = m_get(M_NOWAIT, m->m_type);
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

/*
 * Append the specified data to the indicated mbuf chain,
 * Extend the mbuf chain if the new data does not fit in
 * existing space.
 *
 * Return 1 if able to complete the job; otherwise 0.
 */
int
m_append(struct mbuf *m0, int len, c_caddr_t cp)
{
	struct mbuf *m, *n;
	int remainder, space;

	for (m = m0; m->m_next != NULL; m = m->m_next)
		;
	remainder = len;
	space = M_TRAILINGSPACE(m);
	if (space > 0) {
		/*
		 * Copy into available space.
		 */
		if (space > remainder)
			space = remainder;
		bcopy(cp, mtod(m, caddr_t) + m->m_len, space);
		m->m_len += space;
		cp += space, remainder -= space;
	}
	while (remainder > 0) {
		/*
		 * Allocate a new mbuf; could check space
		 * and allocate a cluster instead.
		 */
		n = m_get(M_NOWAIT, m->m_type);
		if (n == NULL)
			break;
		n->m_len = min(MLEN, remainder);
		bcopy(cp, mtod(n, caddr_t), n->m_len);
		cp += n->m_len, remainder -= n->m_len;
		m->m_next = n;
		m = n;
	}
	if (m0->m_flags & M_PKTHDR)
		m0->m_pkthdr.len += len - remainder;
	return (remainder == 0);
}

/*
 * Apply function f to the data in an mbuf chain starting "off" bytes from
 * the beginning, continuing for "len" bytes.
 */
int
m_apply(struct mbuf *m, int off, int len,
    int (*f)(void *, void *, u_int), void *arg)
{
	u_int count;
	int rval;

	KASSERT(off >= 0, ("m_apply, negative off %d", off));
	KASSERT(len >= 0, ("m_apply, negative len %d", len));
	while (off > 0) {
		KASSERT(m != NULL, ("m_apply, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0) {
		KASSERT(m != NULL, ("m_apply, offset > size of mbuf chain"));
		count = min(m->m_len - off, len);
		rval = (*f)(arg, mtod(m, caddr_t) + off, count);
		if (rval)
			return (rval);
		len -= count;
		off = 0;
		m = m->m_next;
	}
	return (0);
}

/*
 * Return a pointer to mbuf/offset of location in mbuf chain.
 */
struct mbuf *
m_getptr(struct mbuf *m, int loc, int *off)
{

	while (loc >= 0) {
		/* Normal end of search. */
		if (m->m_len > loc) {
			*off = loc;
			return (m);
		} else {
			loc -= m->m_len;
			if (m->m_next == NULL) {
				if (loc == 0) {
					/* Point at the end of valid data. */
					*off = m->m_len;
					return (m);
				}
				return (NULL);
			}
			m = m->m_next;
		}
	}
	return (NULL);
}

void
m_print(const struct mbuf *m)
{
	int len;
	const struct mbuf *m2;
	char *hexstr;

	len = m->m_pkthdr.len;
	m2 = m;
	hexstr = kmalloc(HEX_NCPYLEN(len), M_TEMP, M_ZERO | M_WAITOK);
	while (len) {
		kprintf("%p %s\n", m2, hexncpy(m2->m_data, m2->m_len, hexstr,
			HEX_NCPYLEN(m2->m_len), "-"));
		len -= m2->m_len;
		m2 = m2->m_next;
	}
	kfree(hexstr, M_TEMP);
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
		int temp = karc4random() & 0xff;
		if (temp == 0xba)
			goto nospace;
	}
#endif
	
	m_final = m_getl(m0->m_pkthdr.len, how, MT_DATA, M_PKTHDR, &nsize);
	if (m_final == NULL)
		goto nospace;
	m_final->m_len = 0;	/* in case m0->m_pkthdr.len is zero */

	if (m_dup_pkthdr(m_final, m0, how) == 0)
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
	int flags = M_PKTHDR;
	int nsize;
	int error;
	int resid;

	do {
		if (uio->uio_resid > INT_MAX)
			resid = INT_MAX;
		else
			resid = (int)uio->uio_resid;
		m = m_getl(resid, M_WAITOK, MT_DATA, flags, &nsize);
		if (flags) {
			m->m_pkthdr.len = 0;
			/* Leave room for protocol headers. */
			if (resid < MHLEN)
				MH_ALIGN(m, resid);
			flags = 0;
		}
		m->m_len = imin(nsize, resid);
		error = uiomove(mtod(m, caddr_t), m->m_len, uio);
		if (error) {
			m_free(m);
			goto failed;
		}
		*mp = m;
		mp = &m->m_next;
		head->m_pkthdr.len += m->m_len;
	} while (uio->uio_resid > 0);

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
