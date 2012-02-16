/*	$NetBSD: puffs_sys.h,v 1.77 2011/01/11 14:04:54 kefren Exp $	*/

/*
 * Copyright (c) 2005, 2006  Antti Kantee.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Google Summer of Code program and the Ulla Tuominen Foundation.
 * The Google SoC project was mentored by Bill Studenmund.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PUFFS_SYS_H_
#define _PUFFS_SYS_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/condvar.h>
#include <sys/lock.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/select.h>
#include <sys/queue.h>

#include <vfs/puffs/puffs_msgif.h>

MALLOC_DECLARE(M_PUFFS);

extern struct vop_ops puffs_vnode_vops;
extern struct vop_ops puffs_fifo_vops;

#ifdef DEBUG
#ifndef PUFFSDEBUG
#define PUFFSDEBUG
#endif
#endif

#ifdef PUFFSDEBUG
extern int puffsdebug; /* puffs_subr.c */
#define DPRINTF(x) if (puffsdebug > 0) kprintf x
#define DPRINTF_VERBOSE(x) if (puffsdebug > 1) kprintf x
#else
#define DPRINTF(x)
#define DPRINTF_VERBOSE(x)
#endif

#define MPTOPUFFSMP(mp) ((struct puffs_mount *)((mp)->mnt_data))
#define PMPTOMP(pmp) (pmp->pmp_mp)
#define VPTOPP(vp) ((struct puffs_node *)(vp)->v_data)
#define VPTOPNC(vp) (((struct puffs_node *)(vp)->v_data)->pn_cookie)
#define VPTOPUFFSMP(vp) MPTOPUFFSMP((vp)->v_mount)

/* we don't pass the kernel overlay to userspace */
#define PUFFS_TOFHSIZE(s) ((s)==0 ? (s) : (s)+4)
#define PUFFS_FROMFHSIZE(s) ((s)==0 ? (s) : (s)-4)

#define ALLOPS(pmp) (pmp->pmp_flags & PUFFS_KFLAG_ALLOPS)
#define EXISTSOP(pmp, op) \
 (ALLOPS(pmp) || ((pmp)->pmp_vnopmask[PUFFS_VN_##op]))

#define PUFFS_USE_NAMECACHE(pmp)	\
    (((pmp)->pmp_flags & PUFFS_KFLAG_NOCACHE_NAME) == 0)
#define PUFFS_USE_PAGECACHE(pmp)	\
    (((pmp)->pmp_flags & PUFFS_KFLAG_NOCACHE_PAGE) == 0)

#define PUFFS_WCACHEINFO(pmp)	0

struct puffs_newcookie {
	puffs_cookie_t	pnc_cookie;

	LIST_ENTRY(puffs_newcookie) pnc_entries;
};

enum puffs_sopreqtype {
	PUFFS_SOPREQSYS_EXIT,
	PUFFS_SOPREQ_FLUSH,
	PUFFS_SOPREQ_UNMOUNT,
};

struct puffs_sopreq {
	union {
		struct puffs_req preq;
		struct puffs_flush pf;
	} psopr_u;

	enum puffs_sopreqtype psopr_sopreq;
	TAILQ_ENTRY(puffs_sopreq) psopr_entries;
};
#define psopr_preq psopr_u.preq
#define psopr_pf psopr_u.pf

TAILQ_HEAD(puffs_wq, puffs_msgpark);
LIST_HEAD(puffs_node_hashlist, puffs_node);
struct puffs_mount {
	struct lock	 		pmp_lock;

	struct puffs_kargs		pmp_args;
#define pmp_flags pmp_args.pa_flags
#define pmp_vnopmask pmp_args.pa_vnopmask

	struct puffs_wq			pmp_msg_touser;
	int				pmp_msg_touser_count;
	struct cv			pmp_msg_waiter_cv;
	size_t				pmp_msg_maxsize;

	struct puffs_wq			pmp_msg_replywait;

	struct puffs_node_hashlist	*pmp_pnodehash;
	int				pmp_npnodehash;

	LIST_HEAD(, puffs_newcookie)	pmp_newcookie;

	struct mount			*pmp_mp;

	struct vnode			*pmp_root;
	puffs_cookie_t			pmp_root_cookie;
	enum vtype			pmp_root_vtype;
	vsize_t				pmp_root_vsize;
	dev_t				pmp_root_rdev;

	struct putter_instance		*pmp_pi;

	unsigned int			pmp_refcount;
	struct cv			pmp_refcount_cv;

	struct cv			pmp_unmounting_cv;
	uint8_t				pmp_unmounting;

	uint8_t				pmp_status;
	uint8_t				pmp_suspend;

	uint8_t				*pmp_curput;
	size_t				pmp_curres;
	void				*pmp_curopaq;

	uint64_t			pmp_nextmsgid;

	struct lock			pmp_sopmtx;
	struct cv			pmp_sopcv;
	int				pmp_sopthrcount;
	TAILQ_HEAD(, puffs_sopreq)	pmp_sopreqs;
};

#define PUFFSTAT_BEFOREINIT	0
#define PUFFSTAT_MOUNTING	1
#define PUFFSTAT_RUNNING	2
#define PUFFSTAT_DYING		3 /* Do you want your possessions identified? */


#define PNODE_NOREFS	0x01	/* no backend reference			*/
#define PNODE_DYING	0x02	/* NOREFS + inactive			*/
#define PNODE_FAF	0x04	/* issue all operations as FAF		*/
#define PNODE_DOINACT	0x08	/* if inactive-on-demand, call inactive */

#define PNODE_METACACHE_ATIME	0x10	/* cache atime metadata */
#define PNODE_METACACHE_CTIME	0x20	/* cache atime metadata */
#define PNODE_METACACHE_MTIME	0x40	/* cache atime metadata */
#define PNODE_METACACHE_SIZE	0x80	/* cache atime metadata */
#define PNODE_METACACHE_MASK	0xf0

struct puffs_node {
	struct lock	pn_mtx;
	int		pn_refcount;

	puffs_cookie_t	pn_cookie;	/* userspace pnode cookie	*/
	struct vnode	*pn_vp;		/* backpointer to vnode		*/
	uint32_t	pn_stat;	/* node status			*/

#ifdef XXXDF
	struct selinfo	pn_sel;		/* for selecting on the node	*/
#endif
	short		pn_revents;	/* available events		*/

	/* metacache */
	struct timespec	pn_mc_atime;
	struct timespec	pn_mc_ctime;
	struct timespec	pn_mc_mtime;
	u_quad_t	pn_mc_size;

	voff_t		pn_serversize;

	struct lockf  	pn_lockf;

	LIST_ENTRY(puffs_node) pn_hashent;
};

typedef void (*parkdone_fn)(struct puffs_mount *, struct puffs_req *, void *);

struct puffs_msgpark;
void	puffs_msgif_init(void);
void	puffs_msgif_destroy(void);
int	puffs_msgmem_alloc(size_t, struct puffs_msgpark **, void **, int);
void	puffs_msgmem_release(struct puffs_msgpark *);

void	puffs_sop_thread(void *);

void	puffs_msg_setfaf(struct puffs_msgpark *);
void	puffs_msg_setdelta(struct puffs_msgpark *, size_t);
void	puffs_msg_setinfo(struct puffs_msgpark *, int, int, puffs_cookie_t);
void	puffs_msg_setcall(struct puffs_msgpark *, parkdone_fn, void *);

void	puffs_msg_enqueue(struct puffs_mount *, struct puffs_msgpark *);
int	puffs_msg_wait(struct puffs_mount *, struct puffs_msgpark *);
int	puffs_msg_wait2(struct puffs_mount *, struct puffs_msgpark *,
			struct puffs_node *, struct puffs_node *);

void	puffs_msg_sendresp(struct puffs_mount *, struct puffs_req *, int);

int	puffs_getvnode(struct mount *, puffs_cookie_t, enum vtype,
		       voff_t, struct vnode **);
int	puffs_newnode(struct mount *, struct vnode *, struct vnode **,
		      puffs_cookie_t, enum vtype);
void	puffs_putvnode(struct vnode *);

void	puffs_releasenode(struct puffs_node *);
void	puffs_referencenode(struct puffs_node *);

#define PUFFS_NOSUCHCOOKIE (-1)
int	puffs_cookie2vnode(struct puffs_mount *, puffs_cookie_t, int,
			   struct vnode **);
void	puffs_makecn(struct puffs_kcn *, struct puffs_kcred *,
		     const struct namecache *, struct ucred *);
void	puffs_credcvt(struct puffs_kcred *, struct ucred *);

void	puffs_parkdone_asyncbioread(struct puffs_mount *,
				    struct puffs_req *, void *);
void	puffs_parkdone_asyncbiowrite(struct puffs_mount *,
				     struct puffs_req *, void *);
void	puffs_parkdone_poll(struct puffs_mount *, struct puffs_req *, void *);

void	puffs_mp_reference(struct puffs_mount *);
void	puffs_mp_release(struct puffs_mount *);

void	puffs_gop_size(struct vnode *, off_t, off_t *, int);
void	puffs_gop_markupdate(struct vnode *, int);

void	puffs_senderr(struct puffs_mount *, int, int, const char *,
		      puffs_cookie_t);

void	puffs_updatenode(struct puffs_node *, int);
#define PUFFS_UPDATEATIME	0x01
#define PUFFS_UPDATECTIME	0x02
#define PUFFS_UPDATEMTIME	0x04

int	puffs_meta_setsize(struct vnode *, off_t, int);

void	puffs_userdead(struct puffs_mount *);

int	puffs_directread(struct vnode *, struct uio *, int, struct ucred *);
int	puffs_directwrite(struct vnode *, struct uio *, int, struct ucred *);
int	puffs_bioread(struct vnode *, struct uio *, int, struct ucred *);
int	puffs_biowrite(struct vnode *, struct uio *, int, struct ucred *);
int	puffs_doio(struct vnode *vp, struct bio *bio, struct thread *td);


/* for putter */
int	puffs_msgif_getout(void *, size_t, int, uint8_t **, size_t *, void **);
void	puffs_msgif_releaseout(void *, void *, int);
int	puffs_msgif_dispatch(void *, struct putter_hdr *);
size_t	puffs_msgif_waitcount(void *);
int	puffs_msgif_close(void *);

static __inline off_t
puffs_meta_getsize(struct vnode *vp)
{
	struct puffs_node *pn = VPTOPP(vp);

	if ((pn->pn_stat & PNODE_METACACHE_SIZE) != 0)
		return pn->pn_mc_size;
	return pn->pn_serversize;
}

static __inline int
checkerr(struct puffs_mount *pmp, int error, const char *str)
{

	if (error < 0 || error > ELAST) {
		puffs_senderr(pmp, PUFFS_ERR_ERROR, error, str, NULL);
		error = EPROTO;
	}

	return error;
}

#define PUFFS_MSG_VARS(type, a)						\
	struct puffs_##type##msg_##a *a##_msg;				\
	struct puffs_msgpark *park_##a = NULL

#define PUFFS_MSG_ALLOC(type, a)					\
	puffs_msgmem_alloc(sizeof(struct puffs_##type##msg_##a),	\
	    &park_##a, (void *)& a##_msg, 1)

#define PUFFS_MSG_RELEASE(a) 						\
do {									\
	if (park_##a) puffs_msgmem_release(park_##a);			\
} while (/*CONSTCOND*/0)

#define PUFFS_MSG_ENQUEUEWAIT(pmp, park, var)				\
do {									\
	puffs_msg_enqueue(pmp, park);					\
	var = puffs_msg_wait(pmp, park);				\
} while (/*CONSTCOND*/0)

#define PUFFS_MSG_ENQUEUEWAIT2(pmp, park, vp1, vp2, var)		\
do {									\
	puffs_msg_enqueue(pmp, park);					\
	var = puffs_msg_wait2(pmp, park, vp1, vp2);			\
} while (/*CONSTCOND*/0)

#endif /* _PUFFS_SYS_H_ */
