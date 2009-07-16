/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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
 */

/*
 * These functions support the macros and help fiddle mbuf chains for
 * the nfs op functions. They do things like create the rpc header and
 * copy data between mbuf chains and uio lists.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/nlookup.h>
#include <sys/namei.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/sysent.h>
#include <sys/syscall.h>
#include <sys/conf.h>
#include <sys/objcache.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <sys/buf2.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"
#include "nfsrtt.h"

#include <netinet/in.h>

static u_int32_t nfs_xid = 0;

/*
 * Create the header for an rpc request packet
 * The hsiz is the size of the rest of the nfs request header.
 * (just used to decide if a cluster is a good idea)
 */
struct mbuf *
nfsm_reqh(struct vnode *vp, u_long procid, int hsiz, caddr_t *bposp)
{
	struct mbuf *mb;
	caddr_t bpos;

	mb = m_getl(hsiz, MB_WAIT, MT_DATA, 0, NULL);
	mb->m_len = 0;
	bpos = mtod(mb, caddr_t);

	/* Finally, return values */
	*bposp = bpos;
	return (mb);
}

/*
 * Build the RPC header and fill in the authorization info.
 * The authorization string argument is only used when the credentials
 * come from outside of the kernel.
 * Returns the head of the mbuf list.
 */
struct mbuf *
nfsm_rpchead(struct ucred *cr, int nmflag, int procid, int auth_type,
	     int auth_len, char *auth_str, int verf_len, char *verf_str,
	     struct mbuf *mrest, int mrest_len, struct mbuf **mbp,
	     u_int32_t *xidp)
{
	struct mbuf *mb;
	u_int32_t *tl;
	caddr_t bpos;
	int i;
	struct mbuf *mreq, *mb2;
	int siz, grpsiz, authsiz, dsiz;

	authsiz = nfsm_rndup(auth_len);
	dsiz = authsiz + 10 * NFSX_UNSIGNED;
	mb = m_getl(dsiz, MB_WAIT, MT_DATA, M_PKTHDR, NULL);
	if (dsiz < MINCLSIZE) {
		if (dsiz < MHLEN)
			MH_ALIGN(mb, dsiz);
		else
			MH_ALIGN(mb, 8 * NFSX_UNSIGNED);
	}
	mb->m_len = mb->m_pkthdr.len = 0;
	mreq = mb;
	bpos = mtod(mb, caddr_t);

	/*
	 * First the RPC header.
	 */
	nfsm_build(tl, u_int32_t *, 8 * NFSX_UNSIGNED);

	/* Get a pretty random xid to start with */
	if (!nfs_xid)
		nfs_xid = krandom();
	/*
	 * Skip zero xid if it should ever happen.
	 */
	if (++nfs_xid == 0)
		nfs_xid++;

	*tl++ = *xidp = txdr_unsigned(nfs_xid);
	*tl++ = rpc_call;
	*tl++ = rpc_vers;
	*tl++ = txdr_unsigned(NFS_PROG);
	if (nmflag & NFSMNT_NFSV3)
		*tl++ = txdr_unsigned(NFS_VER3);
	else
		*tl++ = txdr_unsigned(NFS_VER2);
	if (nmflag & NFSMNT_NFSV3)
		*tl++ = txdr_unsigned(procid);
	else
		*tl++ = txdr_unsigned(nfsv2_procid[procid]);

	/*
	 * And then the authorization cred.
	 */
	*tl++ = txdr_unsigned(auth_type);
	*tl = txdr_unsigned(authsiz);
	switch (auth_type) {
	case RPCAUTH_UNIX:
		nfsm_build(tl, u_int32_t *, auth_len);
		*tl++ = 0;		/* stamp ?? */
		*tl++ = 0;		/* NULL hostname */
		*tl++ = txdr_unsigned(cr->cr_uid);
		*tl++ = txdr_unsigned(cr->cr_groups[0]);
		grpsiz = (auth_len >> 2) - 5;
		*tl++ = txdr_unsigned(grpsiz);
		for (i = 1; i <= grpsiz; i++)
			*tl++ = txdr_unsigned(cr->cr_groups[i]);
		break;
	case RPCAUTH_KERB4:
		siz = auth_len;
		while (siz > 0) {
			if (M_TRAILINGSPACE(mb) == 0) {
				mb2 = m_getl(siz, MB_WAIT, MT_DATA, 0, NULL);
				mb2->m_len = 0;
				mb->m_next = mb2;
				mb = mb2;
				bpos = mtod(mb, caddr_t);
			}
			i = min(siz, M_TRAILINGSPACE(mb));
			bcopy(auth_str, bpos, i);
			mb->m_len += i;
			auth_str += i;
			bpos += i;
			siz -= i;
		}
		if ((siz = (nfsm_rndup(auth_len) - auth_len)) > 0) {
			for (i = 0; i < siz; i++)
				*bpos++ = '\0';
			mb->m_len += siz;
		}
		break;
	};

	/*
	 * And the verifier...
	 */
	nfsm_build(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
	if (verf_str) {
		*tl++ = txdr_unsigned(RPCAUTH_KERB4);
		*tl = txdr_unsigned(verf_len);
		siz = verf_len;
		while (siz > 0) {
			if (M_TRAILINGSPACE(mb) == 0) {
				mb2 = m_getl(siz, MB_WAIT, MT_DATA, 0, NULL);
				mb2->m_len = 0;
				mb->m_next = mb2;
				mb = mb2;
				bpos = mtod(mb, caddr_t);
			}
			i = min(siz, M_TRAILINGSPACE(mb));
			bcopy(verf_str, bpos, i);
			mb->m_len += i;
			verf_str += i;
			bpos += i;
			siz -= i;
		}
		if ((siz = (nfsm_rndup(verf_len) - verf_len)) > 0) {
			for (i = 0; i < siz; i++)
				*bpos++ = '\0';
			mb->m_len += siz;
		}
	} else {
		*tl++ = txdr_unsigned(RPCAUTH_NULL);
		*tl = 0;
	}
	mb->m_next = mrest;
	mreq->m_pkthdr.len = authsiz + 10 * NFSX_UNSIGNED + mrest_len;
	mreq->m_pkthdr.rcvif = NULL;
	*mbp = mb;
	return (mreq);
}

/*
 * copies mbuf chain to the uio scatter/gather list
 */
int
nfsm_mbuftouio(struct mbuf **mrep, struct uio *uiop, int siz, caddr_t *dpos)
{
	char *mbufcp, *uiocp;
	int xfer, left, len;
	struct mbuf *mp;
	long uiosiz, rem;
	int error = 0;

	mp = *mrep;
	mbufcp = *dpos;
	len = mtod(mp, caddr_t)+mp->m_len-mbufcp;
	rem = nfsm_rndup(siz)-siz;
	while (siz > 0) {
		if (uiop->uio_iovcnt <= 0 || uiop->uio_iov == NULL)
			return (EFBIG);
		left = uiop->uio_iov->iov_len;
		uiocp = uiop->uio_iov->iov_base;
		if (left > siz)
			left = siz;
		uiosiz = left;
		while (left > 0) {
			while (len == 0) {
				mp = mp->m_next;
				if (mp == NULL)
					return (EBADRPC);
				mbufcp = mtod(mp, caddr_t);
				len = mp->m_len;
			}
			xfer = (left > len) ? len : left;
#ifdef notdef
			/* Not Yet.. */
			if (uiop->uio_iov->iov_op != NULL)
				(*(uiop->uio_iov->iov_op))
				(mbufcp, uiocp, xfer);
			else
#endif
			if (uiop->uio_segflg == UIO_SYSSPACE)
				bcopy(mbufcp, uiocp, xfer);
			else
				copyout(mbufcp, uiocp, xfer);
			left -= xfer;
			len -= xfer;
			mbufcp += xfer;
			uiocp += xfer;
			uiop->uio_offset += xfer;
			uiop->uio_resid -= xfer;
		}
		if (uiop->uio_iov->iov_len <= siz) {
			uiop->uio_iovcnt--;
			uiop->uio_iov++;
		} else {
			uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + uiosiz;
			uiop->uio_iov->iov_len -= uiosiz;
		}
		siz -= uiosiz;
	}
	*dpos = mbufcp;
	*mrep = mp;
	if (rem > 0) {
		if (len < rem)
			error = nfs_adv(mrep, dpos, rem, len);
		else
			*dpos += rem;
	}
	return (error);
}

/*
 * copies a uio scatter/gather list to an mbuf chain.
 * NOTE: can ony handle iovcnt == 1
 */
int
nfsm_uiotombuf(struct uio *uiop, struct mbuf **mq, int siz, caddr_t *bpos)
{
	char *uiocp;
	struct mbuf *mp, *mp2;
	int xfer, left, mlen;
	int uiosiz, rem;
	boolean_t getcluster;
	char *cp;

#ifdef DIAGNOSTIC
	if (uiop->uio_iovcnt != 1)
		panic("nfsm_uiotombuf: iovcnt != 1");
#endif

	if (siz >= MINCLSIZE)
		getcluster = TRUE;
	else
		getcluster = FALSE;
	rem = nfsm_rndup(siz) - siz;
	mp = mp2 = *mq;
	while (siz > 0) {
		left = uiop->uio_iov->iov_len;
		uiocp = uiop->uio_iov->iov_base;
		if (left > siz)
			left = siz;
		uiosiz = left;
		while (left > 0) {
			mlen = M_TRAILINGSPACE(mp);
			if (mlen == 0) {
				if (getcluster)
					mp = m_getcl(MB_WAIT, MT_DATA, 0);
				else
					mp = m_get(MB_WAIT, MT_DATA);
				mp->m_len = 0;
				mp2->m_next = mp;
				mp2 = mp;
				mlen = M_TRAILINGSPACE(mp);
			}
			xfer = (left > mlen) ? mlen : left;
#ifdef notdef
			/* Not Yet.. */
			if (uiop->uio_iov->iov_op != NULL)
				(*(uiop->uio_iov->iov_op))
				(uiocp, mtod(mp, caddr_t)+mp->m_len, xfer);
			else
#endif
			if (uiop->uio_segflg == UIO_SYSSPACE)
				bcopy(uiocp, mtod(mp, caddr_t)+mp->m_len, xfer);
			else
				copyin(uiocp, mtod(mp, caddr_t)+mp->m_len, xfer);
			mp->m_len += xfer;
			left -= xfer;
			uiocp += xfer;
			uiop->uio_offset += xfer;
			uiop->uio_resid -= xfer;
		}
		uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + uiosiz;
		uiop->uio_iov->iov_len -= uiosiz;
		siz -= uiosiz;
	}
	if (rem > 0) {
		if (rem > M_TRAILINGSPACE(mp)) {
			MGET(mp, MB_WAIT, MT_DATA);
			mp->m_len = 0;
			mp2->m_next = mp;
		}
		cp = mtod(mp, caddr_t)+mp->m_len;
		for (left = 0; left < rem; left++)
			*cp++ = '\0';
		mp->m_len += rem;
		*bpos = cp;
	} else
		*bpos = mtod(mp, caddr_t)+mp->m_len;
	*mq = mp;
	return (0);
}

/*
 * Help break down an mbuf chain by setting the first siz bytes contiguous
 * pointed to by returned val.
 * This is used by the macros nfsm_dissect and nfsm_dissecton for tough
 * cases. (The macros use the vars. dpos and dpos2)
 */
int
nfsm_disct(struct mbuf **mdp, caddr_t *dposp, int siz, int left, caddr_t *cp2)
{
	struct mbuf *mp, *mp2;
	int siz2, xfer;
	caddr_t p;

	mp = *mdp;
	while (left == 0) {
		*mdp = mp = mp->m_next;
		if (mp == NULL)
			return (EBADRPC);
		left = mp->m_len;
		*dposp = mtod(mp, caddr_t);
	}
	if (left >= siz) {
		*cp2 = *dposp;
		*dposp += siz;
	} else if (mp->m_next == NULL) {
		return (EBADRPC);
	} else if (siz > MHLEN) {
		panic("nfs S too big");
	} else {
		MGET(mp2, MB_WAIT, MT_DATA);
		mp2->m_next = mp->m_next;
		mp->m_next = mp2;
		mp->m_len -= left;
		mp = mp2;
		*cp2 = p = mtod(mp, caddr_t);
		bcopy(*dposp, p, left);		/* Copy what was left */
		siz2 = siz-left;
		p += left;
		mp2 = mp->m_next;
		/* Loop around copying up the siz2 bytes */
		while (siz2 > 0) {
			if (mp2 == NULL)
				return (EBADRPC);
			xfer = (siz2 > mp2->m_len) ? mp2->m_len : siz2;
			if (xfer > 0) {
				bcopy(mtod(mp2, caddr_t), p, xfer);
				NFSMADV(mp2, xfer);
				mp2->m_len -= xfer;
				p += xfer;
				siz2 -= xfer;
			}
			if (siz2 > 0)
				mp2 = mp2->m_next;
		}
		mp->m_len = siz;
		*mdp = mp2;
		*dposp = mtod(mp2, caddr_t);
	}
	return (0);
}

/*
 * Advance the position in the mbuf chain.
 */
int
nfs_adv(struct mbuf **mdp, caddr_t *dposp, int offs, int left)
{
	struct mbuf *m;
	int s;

	m = *mdp;
	s = left;
	while (s < offs) {
		offs -= s;
		m = m->m_next;
		if (m == NULL)
			return (EBADRPC);
		s = m->m_len;
	}
	*mdp = m;
	*dposp = mtod(m, caddr_t)+offs;
	return (0);
}

/*
 * Copy a string into mbufs for the hard cases...
 */
int
nfsm_strtmbuf(struct mbuf **mb, char **bpos, const char *cp, long siz)
{
	struct mbuf *m1 = NULL, *m2;
	long left, xfer, len, tlen;
	u_int32_t *tl;
	int putsize;

	putsize = 1;
	m2 = *mb;
	left = M_TRAILINGSPACE(m2);
	if (left > 0) {
		tl = ((u_int32_t *)(*bpos));
		*tl++ = txdr_unsigned(siz);
		putsize = 0;
		left -= NFSX_UNSIGNED;
		m2->m_len += NFSX_UNSIGNED;
		if (left > 0) {
			bcopy(cp, (caddr_t) tl, left);
			siz -= left;
			cp += left;
			m2->m_len += left;
			left = 0;
		}
	}
	/* Loop around adding mbufs */
	while (siz > 0) {
		int msize;

		m1 = m_getl(siz, MB_WAIT, MT_DATA, 0, &msize);
		m1->m_len = msize;
		m2->m_next = m1;
		m2 = m1;
		tl = mtod(m1, u_int32_t *);
		tlen = 0;
		if (putsize) {
			*tl++ = txdr_unsigned(siz);
			m1->m_len -= NFSX_UNSIGNED;
			tlen = NFSX_UNSIGNED;
			putsize = 0;
		}
		if (siz < m1->m_len) {
			len = nfsm_rndup(siz);
			xfer = siz;
			if (xfer < len)
				*(tl+(xfer>>2)) = 0;
		} else {
			xfer = len = m1->m_len;
		}
		bcopy(cp, (caddr_t) tl, xfer);
		m1->m_len = len+tlen;
		siz -= xfer;
		cp += xfer;
	}
	*mb = m1;
	*bpos = mtod(m1, caddr_t)+m1->m_len;
	return (0);
}

/*
 * A fiddled version of m_adj() that ensures null fill to a long
 * boundary and only trims off the back end
 */
void
nfsm_adj(struct mbuf *mp, int len, int nul)
{
	struct mbuf *m;
	int count, i;
	char *cp;

	/*
	 * Trim from tail.  Scan the mbuf chain,
	 * calculating its length and finding the last mbuf.
	 * If the adjustment only affects this mbuf, then just
	 * adjust and return.  Otherwise, rescan and truncate
	 * after the remaining size.
	 */
	count = 0;
	m = mp;
	for (;;) {
		count += m->m_len;
		if (m->m_next == NULL)
			break;
		m = m->m_next;
	}
	if (m->m_len > len) {
		m->m_len -= len;
		if (nul > 0) {
			cp = mtod(m, caddr_t)+m->m_len-nul;
			for (i = 0; i < nul; i++)
				*cp++ = '\0';
		}
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
	for (m = mp; m; m = m->m_next) {
		if (m->m_len >= count) {
			m->m_len = count;
			if (nul > 0) {
				cp = mtod(m, caddr_t)+m->m_len-nul;
				for (i = 0; i < nul; i++)
					*cp++ = '\0';
			}
			break;
		}
		count -= m->m_len;
	}
	for (m = m->m_next;m;m = m->m_next)
		m->m_len = 0;
}

/*
 * Make these functions instead of macros, so that the kernel text size
 * doesn't get too big...
 */
void
nfsm_srvwcc(struct nfsrv_descript *nfsd, int before_ret,
	    struct vattr *before_vap, int after_ret, struct vattr *after_vap,
	    struct mbuf **mbp, char **bposp)
{
	struct mbuf *mb = *mbp, *mb2;
	char *bpos = *bposp;
	u_int32_t *tl;

	/*
	 * before_ret is 0 if before_vap is valid, non-zero if it isn't.
	 */
	if (before_ret) {
		nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		nfsm_build(tl, u_int32_t *, 7 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		txdr_hyper(before_vap->va_size, tl);
		tl += 2;
		txdr_nfsv3time(&(before_vap->va_mtime), tl);
		tl += 2;
		txdr_nfsv3time(&(before_vap->va_ctime), tl);
	}
	*bposp = bpos;
	*mbp = mb;
	nfsm_srvpostopattr(nfsd, after_ret, after_vap, mbp, bposp);
}

void
nfsm_srvpostopattr(struct nfsrv_descript *nfsd, int after_ret,
		   struct vattr *after_vap, struct mbuf **mbp, char **bposp)
{
	struct mbuf *mb = *mbp, *mb2;
	char *bpos = *bposp;
	u_int32_t *tl;
	struct nfs_fattr *fp;

	if (after_ret) {
		nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED + NFSX_V3FATTR);
		*tl++ = nfs_true;
		fp = (struct nfs_fattr *)tl;
		nfsm_srvfattr(nfsd, after_vap, fp);
	}
	*mbp = mb;
	*bposp = bpos;
}

void
nfsm_srvfattr(struct nfsrv_descript *nfsd, struct vattr *vap,
	      struct nfs_fattr *fp)
{
	/*
	 * NFS seems to truncate nlink to 16 bits, don't let it overflow.
	 */
	if (vap->va_nlink > 65535)
		fp->fa_nlink = 65535;
	else
		fp->fa_nlink = txdr_unsigned(vap->va_nlink);
	fp->fa_uid = txdr_unsigned(vap->va_uid);
	fp->fa_gid = txdr_unsigned(vap->va_gid);
	if (nfsd->nd_flag & ND_NFSV3) {
		fp->fa_type = vtonfsv3_type(vap->va_type);
		fp->fa_mode = vtonfsv3_mode(vap->va_mode);
		txdr_hyper(vap->va_size, &fp->fa3_size);
		txdr_hyper(vap->va_bytes, &fp->fa3_used);
		fp->fa3_rdev.specdata1 = txdr_unsigned(vap->va_rmajor);
		fp->fa3_rdev.specdata2 = txdr_unsigned(vap->va_rminor);
		fp->fa3_fsid.nfsuquad[0] = 0;
		fp->fa3_fsid.nfsuquad[1] = txdr_unsigned(vap->va_fsid);
		txdr_hyper(vap->va_fileid, &fp->fa3_fileid);
		txdr_nfsv3time(&vap->va_atime, &fp->fa3_atime);
		txdr_nfsv3time(&vap->va_mtime, &fp->fa3_mtime);
		txdr_nfsv3time(&vap->va_ctime, &fp->fa3_ctime);
	} else {
		fp->fa_type = vtonfsv2_type(vap->va_type);
		fp->fa_mode = vtonfsv2_mode(vap->va_type, vap->va_mode);
		fp->fa2_size = txdr_unsigned(vap->va_size);
		fp->fa2_blocksize = txdr_unsigned(vap->va_blocksize);
		if (vap->va_type == VFIFO)
			fp->fa2_rdev = 0xffffffff;
		else
			fp->fa2_rdev = txdr_unsigned(makeudev(vap->va_rmajor, vap->va_rminor));
		fp->fa2_blocks = txdr_unsigned(vap->va_bytes / NFS_FABLKSIZE);
		fp->fa2_fsid = txdr_unsigned(vap->va_fsid);
		fp->fa2_fileid = txdr_unsigned(vap->va_fileid);
		txdr_nfsv2time(&vap->va_atime, &fp->fa2_atime);
		txdr_nfsv2time(&vap->va_mtime, &fp->fa2_mtime);
		txdr_nfsv2time(&vap->va_ctime, &fp->fa2_ctime);
	}
}
