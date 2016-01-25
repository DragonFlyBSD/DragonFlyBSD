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
 *	@(#)nfsm_subs.h	8.2 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/nfs/nfsm_subs.h,v 1.27.2.1 2000/10/28 16:27:27 dwmalone Exp $
 * $DragonFly: src/sys/vfs/nfs/nfsm_subs.h,v 1.10 2008/09/17 21:44:25 dillon Exp $
 */


#ifndef _NFS_NFSM_SUBS_H_
#define _NFS_NFSM_SUBS_H_

struct ucred;
struct vnode;

enum nfsm_state {
	NFSM_STATE_SETUP,
	NFSM_STATE_AUTH,
	NFSM_STATE_TRY,
	NFSM_STATE_WAITREPLY,
	NFSM_STATE_PROCESSREPLY,
	NFSM_STATE_DONE
};

typedef enum nfsm_state nfsm_state_t;


struct nfsm_info {
	/*
	 * These fields are used by various nfsm_* functions during
	 * the construction or deconstruction of a RPC.
	 */
	struct mbuf	*mb;
	struct mbuf	*md;
	struct mbuf	*mrep;
	struct mbuf	*mreq;
	caddr_t		bpos;
	caddr_t		dpos;
	int		v3;

	/*
	 * These fields are used by the request processing state
	 * machine.  mreq, md, dpos, and mrep above are also used.
	 */
	nfsm_state_t	state;
	u_int32_t	procnum;
	struct vnode	*vp;
	struct thread	*td;
	struct ucred	*cred;
	struct nfsreq	*req;
	struct nfsmount	*nmp;
	int		unused01;
	int		error;

	/*
	 * Retained state for higher level VOP and BIO operations
	 */
	struct bio	*bio;
	void		(*done)(struct nfsm_info *);
	union {
		struct {
			int	must_commit;
		} writerpc;
	} u;
};

#define info_writerpc	u.writerpc

typedef struct nfsm_info *nfsm_info_t;

#define NULLOUT(nfsmexp)				\
	do { if ((nfsmexp) == NULL) { 			\
		error = EBADRPC; goto nfsmout; }	\
	} while(0)

#define NEGATIVEOUT(nfsmexp)				\
	do { if ((nfsmexp) < 0) { 			\
		error = EBADRPC; goto nfsmout; }	\
	} while(0)

#define NEGKEEPOUT(nfsmexp)				\
	do { if ((nfsmexp) < 0) { 			\
		goto nfsmout; }				\
	} while(0)

#define NEGREPLYOUT(nfsmexp)				\
	do { 						\
		int rv = (nfsmexp);			\
		if (rv < 0) {				\
			if (rv == -2)			\
				nfsm_reply(&info, nfsd, slp, 0, &error); \
			goto nfsmout;			\
		}					\
	} while(0)

#define ERROROUT(nfsmexp)	if ((error = (nfsmexp)) != 0) goto nfsmout

/*
 * These macros do strange and peculiar things to mbuf chains for
 * the assistance of the nfs code. To attempt to use them for any
 * other purpose will be dangerous. (they make weird assumptions)
 */

/*
 * First define what the actual subs. return
 */
void	nfsm_reqhead(nfsm_info_t info, struct vnode *vp,
				u_long procid, int hsiz);
struct mbuf *nfsm_rpchead (struct ucred *cr, int nmflag, int procid,
				int auth_type, int auth_len, char *auth_str,
				int verf_len, char *verf_str,
				struct mbuf *mrest, int mrest_len,
				struct mbuf **mbp, u_int32_t *xidp);
void	*nfsm_build(nfsm_info_t info, int bytes);
void	*nfsm_dissect(nfsm_info_t info, int bytes);
int	nfsm_fhtom(nfsm_info_t info, struct vnode *vp);
void	nfsm_srvfhtom(nfsm_info_t info, fhandle_t *fhp);
void	nfsm_srvpostop_fh(nfsm_info_t info, fhandle_t *fhp);
int nfsm_mtofh(nfsm_info_t info, struct vnode *dvp,
				struct vnode **vpp, int *gotvpp);
int	nfsm_getfh(nfsm_info_t info, nfsfh_t **fhpp);
int	nfsm_loadattr(nfsm_info_t info, struct vnode *vp, struct vattr *vap);
int	nfsm_postop_attr(nfsm_info_t info, struct vnode *vp,
				int *attrp, int lflags);
int	nfsm_wcc_data(nfsm_info_t info, struct vnode *vp, int *attrp);
void	nfsm_v3attrbuild(nfsm_info_t info, struct vattr *vap, int full);
int	nfsm_strsiz(nfsm_info_t info, int maxlen);
int	nfsm_srvstrsiz(nfsm_info_t info, int maxlen, int *errorp);
int	nfsm_srvnamesiz(nfsm_info_t info, int *errorp);
int	nfsm_mtouio(nfsm_info_t info, struct uio *uiop, int len);
int	nfsm_mtobio(nfsm_info_t info, struct bio *bio, int len);

int	nfsm_uiotom(nfsm_info_t info, struct uio *uiop, int len);
int	nfsm_biotom(nfsm_info_t info, struct bio *bio, int off, int len);
int	nfsm_request(nfsm_info_t info, struct vnode *vp, int procnum,
				thread_t td, struct ucred *cred, int *errorp);
void	nfsm_request_bio(nfsm_info_t info, struct vnode *vp, int procnum,
				thread_t td, struct ucred *cred);
int	nfsm_strtom(nfsm_info_t info, const void *data, int len, int maxlen);
int	nfsm_reply(nfsm_info_t info, struct nfsrv_descript *nfsd,
				struct nfssvc_sock *slp, int siz, int *errorp);
void	nfsm_writereply(nfsm_info_t info, struct nfsrv_descript *nfsd,
				struct nfssvc_sock *slp, int error, int siz);
int	nfsm_adv(nfsm_info_t info, int len);
int	nfsm_srvmtofh(nfsm_info_t info, struct nfsrv_descript *nfsd,
				fhandle_t *fhp, int *errorp);
void	*_nfsm_clget(nfsm_info_t info, struct mbuf **mp1, struct mbuf **mp2,
				char **bp, char **be);
int	nfsm_srvsattr(nfsm_info_t info, struct vattr *vap);
int	nfsm_mbuftouio(struct mbuf **mrep, struct uio *uiop,
				int siz, caddr_t *dpos);
int	nfsm_mbuftobio(struct mbuf **mrep, struct bio *bio,
				int siz, caddr_t *dpos);
int	nfsm_uiotombuf (struct uio *uiop, struct mbuf **mq,
				int siz, caddr_t *bpos);
int	nfsm_biotombuf (struct bio *bio, struct mbuf **mq, int off,
				int siz, caddr_t *bpos);
int	nfsm_disct(struct mbuf **mdp, caddr_t *dposp, int siz,
				int left, caddr_t *cp2);
int	nfsm_strtmbuf (struct mbuf **, char **, const char *, long);
void	nfsm_adj(struct mbuf *mp, int len, int nul);
void	nfsm_srvwcc_data(nfsm_info_t info, struct nfsrv_descript *nfsd,
				int before_ret, struct vattr *before_vap,
				int after_ret, struct vattr *after_vap);
void	nfsm_srvpostop_attr(nfsm_info_t info, struct nfsrv_descript *nfsd,
				int after_ret, struct vattr *after_vap);
void	nfsm_srvfattr(struct nfsrv_descript *nfsd, struct vattr *vap,
				struct nfs_fattr *fp);

int     nfs_request (struct nfsm_info *, nfsm_state_t, nfsm_state_t);

#define nfsm_clget(info, mp1, mp2, bp, be)	\
	((bp >= be) ? _nfsm_clget(info, &mp1, &mp2, &bp, &be) : (void *)bp)

#define nfsm_rndup(a)   (((a) + 3) & (~0x3))

#define NFSV3_WCCRATTR	0
#define NFSV3_WCCCHK	1

#endif
