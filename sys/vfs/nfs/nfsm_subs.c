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
void
nfsm_reqhead(nfsm_info_t info, struct vnode *vp, u_long procid, int hsiz)
{
	info->mb = m_getl(hsiz, MB_WAIT, MT_DATA, 0, NULL);
	info->mb->m_len = 0;
	info->mreq = info->mb;
	info->bpos = mtod(info->mb, caddr_t);
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
	struct nfsm_info info;
	struct mbuf *mb2;
	u_int32_t *tl;
	u_int32_t xid;
	int siz, grpsiz, authsiz, dsiz;
	int i;

	authsiz = nfsm_rndup(auth_len);
	dsiz = authsiz + 10 * NFSX_UNSIGNED;
	info.mb = m_getl(dsiz, MB_WAIT, MT_DATA, M_PKTHDR, NULL);
	if (dsiz < MINCLSIZE) {
		if (dsiz < MHLEN)
			MH_ALIGN(info.mb, dsiz);
		else
			MH_ALIGN(info.mb, 8 * NFSX_UNSIGNED);
	}
	info.mb->m_len = info.mb->m_pkthdr.len = 0;
	info.mreq = info.mb;
	info.bpos = mtod(info.mb, caddr_t);

	/*
	 * First the RPC header.
	 */
	tl = nfsm_build(&info, 8 * NFSX_UNSIGNED);

	/* Get a pretty random xid to start with */
	if (!nfs_xid)
		nfs_xid = krandom();

	do {
		xid = atomic_fetchadd_int(&nfs_xid, 1);
	} while (xid == 0);

	*tl++ = *xidp = txdr_unsigned(xid);
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
		tl = nfsm_build(&info, auth_len);
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
			if (M_TRAILINGSPACE(info.mb) == 0) {
				mb2 = m_getl(siz, MB_WAIT, MT_DATA, 0, NULL);
				mb2->m_len = 0;
				info.mb->m_next = mb2;
				info.mb = mb2;
				info.bpos = mtod(info.mb, caddr_t);
			}
			i = min(siz, M_TRAILINGSPACE(info.mb));
			bcopy(auth_str, info.bpos, i);
			info.mb->m_len += i;
			auth_str += i;
			info.bpos += i;
			siz -= i;
		}
		if ((siz = (nfsm_rndup(auth_len) - auth_len)) > 0) {
			for (i = 0; i < siz; i++)
				*info.bpos++ = '\0';
			info.mb->m_len += siz;
		}
		break;
	}

	/*
	 * And the verifier...
	 */
	tl = nfsm_build(&info, 2 * NFSX_UNSIGNED);
	if (verf_str) {
		*tl++ = txdr_unsigned(RPCAUTH_KERB4);
		*tl = txdr_unsigned(verf_len);
		siz = verf_len;
		while (siz > 0) {
			if (M_TRAILINGSPACE(info.mb) == 0) {
				mb2 = m_getl(siz, MB_WAIT, MT_DATA,
						  0, NULL);
				mb2->m_len = 0;
				info.mb->m_next = mb2;
				info.mb = mb2;
				info.bpos = mtod(info.mb, caddr_t);
			}
			i = min(siz, M_TRAILINGSPACE(info.mb));
			bcopy(verf_str, info.bpos, i);
			info.mb->m_len += i;
			verf_str += i;
			info.bpos += i;
			siz -= i;
		}
		if ((siz = (nfsm_rndup(verf_len) - verf_len)) > 0) {
			for (i = 0; i < siz; i++)
				*info.bpos++ = '\0';
			info.mb->m_len += siz;
		}
	} else {
		*tl++ = txdr_unsigned(RPCAUTH_NULL);
		*tl = 0;
	}
	info.mb->m_next = mrest;
	info.mreq->m_pkthdr.len = authsiz + 10 * NFSX_UNSIGNED + mrest_len;
	info.mreq->m_pkthdr.rcvif = NULL;
	*mbp = info.mb;
	return (info.mreq);
}

void *
nfsm_build(nfsm_info_t info, int bytes)
{
	struct mbuf *mb2;
	void *ptr;

	if (bytes > M_TRAILINGSPACE(info->mb)) {
		MGET(mb2, MB_WAIT, MT_DATA);
		if (bytes > MLEN)
			panic("build > MLEN");
		info->mb->m_next = mb2;
		info->mb = mb2;
		info->mb->m_len = 0;
		info->bpos = mtod(info->mb, caddr_t);
	}
	ptr = info->bpos;
	info->mb->m_len += bytes;
	info->bpos += bytes;
	return (ptr);
}

/*
 *
 * If NULL returned caller is expected to abort with an EBADRPC error.
 * Caller will usually use the NULLOUT macro.
 */
void *
nfsm_dissect(nfsm_info_t info, int bytes)
{
	caddr_t cp2;
	void *ptr;
	int error;
	int n;

	/*
	 * Check for missing reply packet.  This typically occurs if there
	 * is a soft termination w/too many retries.
	 */
	if (info->md == NULL) {
		if (info->mrep) {
			m_freem(info->mrep);
			info->mrep = NULL;
		}
		return NULL;
	}

	/*
	 * Otherwise any error will be due to the packet format
	 */
	n = mtod(info->md, caddr_t) + info->md->m_len - info->dpos;
	if (bytes <= n) {
		ptr = info->dpos;
		info->dpos += bytes;
	} else {
		error = nfsm_disct(&info->md, &info->dpos, bytes, n, &cp2);
		if (error) {
			m_freem(info->mrep);
			info->mrep = NULL;
			ptr = NULL;
		} else {
			ptr = cp2;
		}
	}
	return (ptr);
}

/*
 *
 * Caller is expected to abort if non-zero error is returned.
 */
int
nfsm_fhtom(nfsm_info_t info, struct vnode *vp)
{
	u_int32_t *tl;
	caddr_t cp;
	int error;
	int n;

	if (info->v3) {
		n = nfsm_rndup(VTONFS(vp)->n_fhsize) + NFSX_UNSIGNED;
		if (n <= M_TRAILINGSPACE(info->mb)) {
			tl = nfsm_build(info, n);
			*tl++ = txdr_unsigned(VTONFS(vp)->n_fhsize);
			*(tl + ((n >> 2) - 2)) = 0;
			bcopy((caddr_t)VTONFS(vp)->n_fhp,(caddr_t)tl,
				VTONFS(vp)->n_fhsize);
			error = 0;
		} else if ((error = nfsm_strtmbuf(&info->mb, &info->bpos,
						(caddr_t)VTONFS(vp)->n_fhp,
						VTONFS(vp)->n_fhsize)) != 0) {
			m_freem(info->mreq);
			info->mreq = NULL;
		}
	} else {
		cp = nfsm_build(info, NFSX_V2FH);
		bcopy(VTONFS(vp)->n_fhp, cp, NFSX_V2FH);
		error = 0;
	}
	return (error);
}

void
nfsm_srvfhtom(nfsm_info_t info, fhandle_t *fhp)
{
	u_int32_t *tl;

	if (info->v3) {
		tl = nfsm_build(info, NFSX_UNSIGNED + NFSX_V3FH);
		*tl++ = txdr_unsigned(NFSX_V3FH);
		bcopy(fhp, tl, NFSX_V3FH);
	} else {
		tl = nfsm_build(info, NFSX_V2FH);
		bcopy(fhp, tl, NFSX_V2FH);
	}
}

void
nfsm_srvpostop_fh(nfsm_info_t info, fhandle_t *fhp)
{
	u_int32_t *tl;

	tl = nfsm_build(info, 2 * NFSX_UNSIGNED + NFSX_V3FH);
	*tl++ = nfs_true;
	*tl++ = txdr_unsigned(NFSX_V3FH);
	bcopy(fhp, tl, NFSX_V3FH);
}

/*
 * Caller is expected to abort if non-zero error is returned.
 *
 * NOTE: (*vpp) may be loaded with a valid vnode even if (*gotvpp)
 *	 winds up 0.  The caller is responsible for dealing with (*vpp).
 */
int
nfsm_mtofh(nfsm_info_t info, struct vnode *dvp, struct vnode **vpp, int *gotvpp)
{
	struct nfsnode *ttnp;
	nfsfh_t *ttfhp;
	u_int32_t *tl;
	int ttfhsize;
	int error = 0;

	if (info->v3) {
		tl = nfsm_dissect(info, NFSX_UNSIGNED);
		if (tl == NULL)
			return(EBADRPC);
		*gotvpp = fxdr_unsigned(int, *tl);
	} else {
		*gotvpp = 1;
	}
	if (*gotvpp) {
		NEGATIVEOUT(ttfhsize = nfsm_getfh(info, &ttfhp));
		error = nfs_nget(dvp->v_mount, ttfhp, ttfhsize, &ttnp, NULL);
		if (error) {
			m_freem(info->mrep);
			info->mrep = NULL;
			return (error);
		}
		*vpp = NFSTOV(ttnp);
	}
	if (info->v3) {
		tl = nfsm_dissect(info, NFSX_UNSIGNED);
		if (tl == NULL)
			return (EBADRPC);
		if (*gotvpp) {
			*gotvpp = fxdr_unsigned(int, *tl);
		} else if (fxdr_unsigned(int, *tl)) {
			error = nfsm_adv(info, NFSX_V3FATTR);
			if (error)
				return (error);
		}
	}
	if (*gotvpp)
		error = nfsm_loadattr(info, *vpp, NULL);
nfsmout:
	return (error);
}

/*
 *
 * Caller is expected to abort with EBADRPC if a negative length is returned.
 */
int
nfsm_getfh(nfsm_info_t info, nfsfh_t **fhpp)
{
	u_int32_t *tl;
	int n;

	*fhpp = NULL;
	if (info->v3) {
		tl = nfsm_dissect(info, NFSX_UNSIGNED);
		if (tl == NULL)
			return(-1);
		if ((n = fxdr_unsigned(int, *tl)) <= 0 || n > NFSX_V3FHMAX) {
			m_freem(info->mrep);
			info->mrep = NULL;
			return(-1);
		}
	} else {
		n = NFSX_V2FH;
	}
	*fhpp = nfsm_dissect(info, nfsm_rndup(n));
	if (*fhpp == NULL)
		return(-1);
	return(n);
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_loadattr(nfsm_info_t info, struct vnode *vp, struct vattr *vap)
{
	int error;

	error = nfs_loadattrcache(vp, &info->md, &info->dpos, vap, 0);
	if (error) {
		m_freem(info->mrep);
		info->mrep = NULL;
		return (error);
	}
	return (0);
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_postop_attr(nfsm_info_t info, struct vnode *vp, int *attrp, int lflags)
{
	u_int32_t *tl;
	int error;

	tl = nfsm_dissect(info, NFSX_UNSIGNED);
	if (tl == NULL)
		return(EBADRPC);
	*attrp = fxdr_unsigned(int, *tl);
	if (*attrp) {
		error = nfs_loadattrcache(vp, &info->md, &info->dpos,
					  NULL, lflags);
		if (error) {
			*attrp = 0;
			m_freem(info->mrep);
			info->mrep = NULL;
			return (error);
		}
	}
	return (0);
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_wcc_data(nfsm_info_t info, struct vnode *vp, int *attrp)
{
	u_int32_t *tl;
	int error;
	int ttattrf;
	int ttretf = 0;

	tl = nfsm_dissect(info, NFSX_UNSIGNED);
	if (tl == NULL)
		return (EBADRPC);
	if (*tl == nfs_true) {
		tl = nfsm_dissect(info, 6 * NFSX_UNSIGNED);
		if (tl == NULL)
			return (EBADRPC);
		if (*attrp) {
			ttretf = (VTONFS(vp)->n_mtime ==
				fxdr_unsigned(u_int32_t, *(tl + 2)));
			if (ttretf == 0)
				VTONFS(vp)->n_flag |= NRMODIFIED;
		}
		error = nfsm_postop_attr(info, vp, &ttattrf,
				 NFS_LATTR_NOSHRINK|NFS_LATTR_NOMTIMECHECK);
		if (error)
			return(error);
	} else {
		error = nfsm_postop_attr(info, vp, &ttattrf,
					 NFS_LATTR_NOSHRINK);
		if (error)
			return(error);
	}
	if (*attrp)
		*attrp = ttretf;
	else
		*attrp = ttattrf;
	return(0);
}

/*
 * This function updates the attribute cache based on data returned in the
 * NFS reply for NFS RPCs that modify the target file.  If the RPC succeeds
 * a 'before' and 'after' mtime is returned that allows us to determine if
 * the new mtime attribute represents our modification or someone else's
 * modification.
 *
 * The flag argument returns non-0 if the original times matched, zero if
 * they did not match.  NRMODIFIED is automatically set if the before time
 * does not match the original n_mtime, and n_mtime is automatically updated
 * to the new after time (by nfsm_postop_attr()).
 *
 * If full is true, set all fields, otherwise just set mode and time fields
 */
void
nfsm_v3attrbuild(nfsm_info_t info, struct vattr *vap, int full)
{
	u_int32_t *tl;

	if (vap->va_mode != (mode_t)VNOVAL) {
		tl = nfsm_build(info, 2 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		*tl = txdr_unsigned(vap->va_mode);
	} else {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (full && vap->va_uid != (uid_t)VNOVAL) {
		tl = nfsm_build(info, 2 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		*tl = txdr_unsigned(vap->va_uid);
	} else {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (full && vap->va_gid != (gid_t)VNOVAL) {
		tl = nfsm_build(info, 2 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		*tl = txdr_unsigned(vap->va_gid);
	} else {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (full && vap->va_size != VNOVAL) {
		tl = nfsm_build(info, 3 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		txdr_hyper(vap->va_size, tl);
	} else {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (vap->va_atime.tv_sec != VNOVAL) {
		if (vap->va_atime.tv_sec != time_second) {
			tl = nfsm_build(info, 3 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(NFSV3SATTRTIME_TOCLIENT);
			txdr_nfsv3time(&vap->va_atime, tl);
		} else {
			tl = nfsm_build(info, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV3SATTRTIME_TOSERVER);
		}
	} else {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = txdr_unsigned(NFSV3SATTRTIME_DONTCHANGE);
	}
	if (vap->va_mtime.tv_sec != VNOVAL) {
		if (vap->va_mtime.tv_sec != time_second) {
			tl = nfsm_build(info, 3 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(NFSV3SATTRTIME_TOCLIENT);
			txdr_nfsv3time(&vap->va_mtime, tl);
		} else {
			tl = nfsm_build(info, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV3SATTRTIME_TOSERVER);
		}
	} else {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = txdr_unsigned(NFSV3SATTRTIME_DONTCHANGE);
	}
}

/*
 * Caller is expected to abort with EBADRPC if a negative length is returned.
 */
int
nfsm_strsiz(nfsm_info_t info, int maxlen)
{
	u_int32_t *tl;
	int len;

	tl = nfsm_dissect(info, NFSX_UNSIGNED);
	if (tl == NULL)
		return(-1);
	len = fxdr_unsigned(int32_t, *tl);
	if (len < 0 || len > maxlen)
		return(-1);
	return (len);
}

/*
 * Caller is expected to abort if a negative length is returned, but also
 * call nfsm_reply(0) if -2 is returned.
 *
 * This function sets *errorp.  Caller should not modify the error code.
 */
int
nfsm_srvstrsiz(nfsm_info_t info, int maxlen, int *errorp)
{
	u_int32_t *tl;
	int len;

	tl = nfsm_dissect(info, NFSX_UNSIGNED);
	if (tl == NULL) {
		*errorp = EBADRPC;
		return(-1);
	}
	len = fxdr_unsigned(int32_t,*tl);
	if (len > maxlen || len <= 0) {
		*errorp = EBADRPC;
		return(-2);
	}
	return(len);
}

/*
 * Caller is expected to abort if a negative length is returned, but also
 * call nfsm_reply(0) if -2 is returned.
 *
 * This function sets *errorp.  Caller should not modify the error code.
 */
int
nfsm_srvnamesiz(nfsm_info_t info, int *errorp)
{
	u_int32_t *tl;
	int len;

	tl = nfsm_dissect(info, NFSX_UNSIGNED);
	if (tl == NULL) {
		*errorp = EBADRPC;
		return(-1);
	}

	/*
	 * In this case if *errorp is not EBADRPC and we are NFSv3,
	 * nfsm_reply() will not return a negative number.  But all
	 * call cases assume len is valid so we really do want
	 * to return -1.
	 */
	len = fxdr_unsigned(int32_t,*tl);
	if (len > NFS_MAXNAMLEN)
		*errorp = NFSERR_NAMETOL;
	if (len <= 0)
		*errorp = EBADRPC;
	if (*errorp)
		return(-2);
	return (len);
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_mtouio(nfsm_info_t info, struct uio *uiop, int len)
{
	int error;

	if (len > 0 &&
	   (error = nfsm_mbuftouio(&info->md, uiop, len, &info->dpos)) != 0) {
		m_freem(info->mrep);
		info->mrep = NULL;
		return(error);
	}
	return(0);
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_mtobio(nfsm_info_t info, struct bio *bio, int len)
{
	int error;

	if (len > 0 &&
	   (error = nfsm_mbuftobio(&info->md, bio, len, &info->dpos)) != 0) {
		m_freem(info->mrep);
		info->mrep = NULL;
		return(error);
       }
       return (0);
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_uiotom(nfsm_info_t info, struct uio *uiop, int len)
{
	int error;

	error = nfsm_uiotombuf(uiop, &info->mb, len, &info->bpos);
	if (error) {
		m_freem(info->mreq);
		info->mreq = NULL;
		return (error);
	}
	return(0);
}

int
nfsm_biotom(nfsm_info_t info, struct bio *bio, int off, int len)
{
	int error;

	error = nfsm_biotombuf(bio, &info->mb, off, len, &info->bpos);
	if (error) {
		m_freem(info->mreq);
		info->mreq = NULL;
		return (error);
	}
	return(0);
}

/*
 * Caller is expected to abort if a negative value is returned.  This
 * function sets *errorp.  Caller should not modify the error code.
 *
 * We load up the remaining info fields and run the request state
 * machine until it is done.
 *
 * This call runs the entire state machine and does not return until
 * the command is complete.
 */
int
nfsm_request(nfsm_info_t info, struct vnode *vp, int procnum,
	     thread_t td, struct ucred *cred, int *errorp)
{
	info->state = NFSM_STATE_SETUP;
	info->procnum = procnum;
	info->vp = vp;
	info->td = td;
	info->cred = cred;
	info->bio = NULL;
	info->nmp = VFSTONFS(vp->v_mount);

	*errorp = nfs_request(info, NFSM_STATE_SETUP, NFSM_STATE_DONE);
	if (*errorp) {
		if ((*errorp & NFSERR_RETERR) == 0)
			return(-1);
		*errorp &= ~NFSERR_RETERR;
	}
	return(0);
}

/*
 * This call starts the state machine through the initial transmission.
 * Completion is via the bio.  The info structure must have installed
 * a 'done' callback.
 *
 * If we are unable to do the initial tx we generate the bio completion
 * ourselves.
 */
void
nfsm_request_bio(nfsm_info_t info, struct vnode *vp, int procnum,
	     thread_t td, struct ucred *cred)
{
	struct buf *bp;
	int error;

	info->state = NFSM_STATE_SETUP;
	info->procnum = procnum;
	info->vp = vp;
	info->td = td;
	info->cred = cred;
	info->nmp = VFSTONFS(vp->v_mount);

	error = nfs_request(info, NFSM_STATE_SETUP, NFSM_STATE_WAITREPLY);
	if (error != EINPROGRESS) {
		kprintf("nfsm_request_bio: early abort %d\n", error);
		bp = info->bio->bio_buf;
		if (error) {
			bp->b_flags |= B_ERROR;
			if (error == EIO)		/* unrecoverable */
				bp->b_flags |= B_INVAL;
		}
		bp->b_error = error;
		biodone(info->bio);
	}
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_strtom(nfsm_info_t info, const void *data, int len, int maxlen)
{
	u_int32_t *tl;
	int error;
	int n;

	if (len > maxlen) {
		m_freem(info->mreq);
		info->mreq = NULL;
		return(ENAMETOOLONG);
	}
	n = nfsm_rndup(len) + NFSX_UNSIGNED;
	if (n <= M_TRAILINGSPACE(info->mb)) {
		tl = nfsm_build(info, n);
		*tl++ = txdr_unsigned(len);
		*(tl + ((n >> 2) - 2)) = 0;
		bcopy(data, tl, len);
		error = 0;
	} else {
		error = nfsm_strtmbuf(&info->mb, &info->bpos, data, len);
		if (error) {
			m_freem(info->mreq);
			info->mreq = NULL;
		}
	}
	return (error);
}

/*
 * Caller is expected to abort if a negative value is returned.  This
 * function sets *errorp.  Caller should not modify the error code.
 */
int
nfsm_reply(nfsm_info_t info,
	   struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	   int siz, int *errorp)
{
	nfsd->nd_repstat = *errorp;
	if (*errorp && !(nfsd->nd_flag & ND_NFSV3))
		siz = 0;
	nfs_rephead(siz, nfsd, slp, *errorp, &info->mreq,
		    &info->mb, &info->bpos);
	if (info->mrep != NULL) {
		m_freem(info->mrep);
		info->mrep = NULL;
	}
	if (*errorp && (!(nfsd->nd_flag & ND_NFSV3) || *errorp == EBADRPC)) {
		*errorp = 0;
		return(-1);
	}
	return(0);
}

void
nfsm_writereply(nfsm_info_t info,
		struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
		int error, int siz)
{
	nfsd->nd_repstat = error;
	if (error && !(info->v3))
		siz = 0;
	nfs_rephead(siz, nfsd, slp, error, &info->mreq, &info->mb, &info->bpos);
}

/*
 * Caller is expected to abort if a non-zero error is returned.
 */
int
nfsm_adv(nfsm_info_t info, int len)
{
	int error;
	int n;

	n = mtod(info->md, caddr_t) + info->md->m_len - info->dpos;
	if (n >= len) {
		info->dpos += len;
		error = 0;
	} else if ((error = nfs_adv(&info->md, &info->dpos, len, n)) != 0) {
		m_freem(info->mrep);
		info->mrep = NULL;
	}
	return (error);
}

/*
 * Caller is expected to abort if a negative length is returned, but also
 * call nfsm_reply(0) if -2 is returned.
 *
 * This function sets *errorp.  Caller should not modify the error code.
 */
int
nfsm_srvmtofh(nfsm_info_t info, struct nfsrv_descript *nfsd,
	      fhandle_t *fhp, int *errorp)
{
	u_int32_t *tl;
	int fhlen;

	if (nfsd->nd_flag & ND_NFSV3) {
		tl = nfsm_dissect(info, NFSX_UNSIGNED);
		if (tl == NULL) {
			*errorp = EBADRPC;
			return(-1);
		}
		fhlen = fxdr_unsigned(int, *tl);
		if (fhlen != 0 && fhlen != NFSX_V3FH) {
			*errorp = EBADRPC;
			return(-2);
		}
	} else {
		fhlen = NFSX_V2FH;
	}
	if (fhlen != 0) {
		tl = nfsm_dissect(info, fhlen);
		if (tl == NULL) {
			*errorp = EBADRPC;
			return(-1);
		}
		bcopy(tl, fhp, fhlen);
	} else {
		bzero(fhp, NFSX_V3FH);
	}
	return(0);
}

void *
_nfsm_clget(nfsm_info_t info, struct mbuf **mp1, struct mbuf **mp2,
	    char **bp, char **be)
{
	if (*bp >= *be) {
		if (*mp1 == info->mb)
			(*mp1)->m_len += *bp - info->bpos;
		*mp1 = m_getcl(MB_WAIT, MT_DATA, 0);
		(*mp1)->m_len = MCLBYTES;
		(*mp2)->m_next = *mp1;
		*mp2 = *mp1;
		*bp = mtod(*mp1, caddr_t);
		*be = *bp + (*mp1)->m_len;
	}
	return(*bp);
}

int
nfsm_srvsattr(nfsm_info_t info, struct vattr *vap)
{
	u_int32_t *tl;
	int error = 0;

	NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
	if (*tl == nfs_true) {
		NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
		vap->va_mode = nfstov_mode(*tl);
	}
	NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
	if (*tl == nfs_true) {
		NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
		vap->va_uid = fxdr_unsigned(uid_t, *tl);
	}
	NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
	if (*tl == nfs_true) {
		NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
		vap->va_gid = fxdr_unsigned(gid_t, *tl);
	}
	NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
	if (*tl == nfs_true) {
		NULLOUT(tl = nfsm_dissect(info, 2 * NFSX_UNSIGNED));
		vap->va_size = fxdr_hyper(tl);
	}
	NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
	switch (fxdr_unsigned(int, *tl)) {
	case NFSV3SATTRTIME_TOCLIENT:
		NULLOUT(tl = nfsm_dissect(info, 2 * NFSX_UNSIGNED));
		fxdr_nfsv3time(tl, &vap->va_atime);
		break;
	case NFSV3SATTRTIME_TOSERVER:
		getnanotime(&vap->va_atime);
		break;
	}
	NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
	switch (fxdr_unsigned(int, *tl)) {
	case NFSV3SATTRTIME_TOCLIENT:
		NULLOUT(tl = nfsm_dissect(info, 2 * NFSX_UNSIGNED));
		fxdr_nfsv3time(tl, &vap->va_mtime);
		break;
	case NFSV3SATTRTIME_TOSERVER:
		getnanotime(&vap->va_mtime);
		break;
	}
nfsmout:
	return (error);
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
 * copies mbuf chain to the bio buffer
 */
int
nfsm_mbuftobio(struct mbuf **mrep, struct bio *bio, int size, caddr_t *dpos)
{
	struct buf *bp = bio->bio_buf;
	char *mbufcp;
	char *bio_cp;
	int xfer, len;
	struct mbuf *mp;
	long rem;
	int error = 0;
	int bio_left;

	mp = *mrep;
	mbufcp = *dpos;
	len = mtod(mp, caddr_t) + mp->m_len - mbufcp;
	rem = nfsm_rndup(size) - size;

	bio_left = bp->b_bcount;
	bio_cp = bp->b_data;

	while (size > 0) {
		while (len == 0) {
			mp = mp->m_next;
			if (mp == NULL)
				return (EBADRPC);
			mbufcp = mtod(mp, caddr_t);
			len = mp->m_len;
		}
		if ((xfer = len) > size)
			xfer = size;
		if (bio_left) {
			if (xfer > bio_left)
				xfer = bio_left;
			bcopy(mbufcp, bio_cp, xfer);
		} else {
			/*
			 * Not enough buffer space in the bio.
			 */
			return(EFBIG);
		}
		size -= xfer;
		bio_left -= xfer;
		bio_cp += xfer;
		len -= xfer;
		mbufcp += xfer;
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

int
nfsm_biotombuf(struct bio *bio, struct mbuf **mq, int off,
	       int siz, caddr_t *bpos)
{
	struct buf *bp = bio->bio_buf;
	struct mbuf *mp, *mp2;
	char *bio_cp;
	int bio_left;
	int xfer, mlen;
	int rem;
	boolean_t getcluster;
	char *cp;

	if (siz >= MINCLSIZE)
		getcluster = TRUE;
	else
		getcluster = FALSE;
	rem = nfsm_rndup(siz) - siz;
	mp = mp2 = *mq;

	bio_cp = bp->b_data + off;
	bio_left = siz;

	while (bio_left) {
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
		xfer = (bio_left < mlen) ? bio_left : mlen;
		bcopy(bio_cp, mtod(mp, caddr_t) + mp->m_len, xfer);
		mp->m_len += xfer;
		bio_left -= xfer;
		bio_cp += xfer;
	}
	if (rem > 0) {
		if (rem > M_TRAILINGSPACE(mp)) {
			MGET(mp, MB_WAIT, MT_DATA);
			mp->m_len = 0;
			mp2->m_next = mp;
		}
		cp = mtod(mp, caddr_t) + mp->m_len;
		for (mlen = 0; mlen < rem; mlen++)
			*cp++ = '\0';
		mp->m_len += rem;
		*bpos = cp;
	} else {
		*bpos = mtod(mp, caddr_t) + mp->m_len;
	}
	*mq = mp;
	return(0);
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
				mp2->m_len -= xfer;
				mp2->m_data += xfer;
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
nfsm_srvwcc_data(nfsm_info_t info, struct nfsrv_descript *nfsd,
		 int before_ret, struct vattr *before_vap,
		 int after_ret, struct vattr *after_vap)
{
	u_int32_t *tl;

	/*
	 * before_ret is 0 if before_vap is valid, non-zero if it isn't.
	 */
	if (before_ret) {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		tl = nfsm_build(info, 7 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		txdr_hyper(before_vap->va_size, tl);
		tl += 2;
		txdr_nfsv3time(&(before_vap->va_mtime), tl);
		tl += 2;
		txdr_nfsv3time(&(before_vap->va_ctime), tl);
	}
	nfsm_srvpostop_attr(info, nfsd, after_ret, after_vap);
}

void
nfsm_srvpostop_attr(nfsm_info_t info, struct nfsrv_descript *nfsd,
		   int after_ret, struct vattr *after_vap)
{
	struct nfs_fattr *fp;
	u_int32_t *tl;

	if (after_ret) {
		tl = nfsm_build(info, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		tl = nfsm_build(info, NFSX_UNSIGNED + NFSX_V3FATTR);
		*tl++ = nfs_true;
		fp = (struct nfs_fattr *)tl;
		nfsm_srvfattr(nfsd, after_vap, fp);
	}
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
		fp->fa3_rdev.specdata1 = txdr_unsigned(major(vap->va_rdev));
		fp->fa3_rdev.specdata2 = txdr_unsigned(minor(vap->va_rdev));
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
			fp->fa2_rdev = txdr_unsigned(makeudev(major(vap->va_rdev), minor(vap->va_rdev)));
		fp->fa2_blocks = txdr_unsigned(vap->va_bytes / NFS_FABLKSIZE);
		fp->fa2_fsid = txdr_unsigned(vap->va_fsid);
		fp->fa2_fileid = txdr_unsigned(vap->va_fileid);
		txdr_nfsv2time(&vap->va_atime, &fp->fa2_atime);
		txdr_nfsv2time(&vap->va_mtime, &fp->fa2_mtime);
		txdr_nfsv2time(&vap->va_ctime, &fp->fa2_ctime);
	}
}
