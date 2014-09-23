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
 *	@(#)nfs_serv.c  8.8 (Berkeley) 7/31/95
 * $FreeBSD: src/sys/nfs/nfs_serv.c,v 1.93.2.6 2002/12/29 18:19:53 dillon Exp $
 */

/*
 * nfs version 2 and 3 server calls to vnode ops
 * - these routines generally have 3 phases
 *   1 - break down and validate rpc request in mbuf list
 *   2 - do the vnode ops for the request
 *       (surprisingly ?? many are very similar to syscalls in vfs_syscalls.c)
 *   3 - build the rpc reply in an mbuf list
 *   nb:
 *	- do not mix the phases, since the nfsm_?? macros can return failures
 *	  on a bad rpc or similar and do not do any vrele() or vput()'s
 *
 *      - the nfsm_reply() macro generates an nfs rpc reply with the nfs
 *	error number iff error != 0 whereas
 *	returning an error from the server function implies a fatal error
 *	such as a badly constructed rpc request that should be dropped without
 *	a reply.
 *	For Version 3, nfsm_reply() does not return for the error case, since
 *	most version 3 rpcs return more than the status for error cases.
 *
 * Other notes:
 *	Warning: always pay careful attention to resource cleanup on return
 *	and note that nfsm_*() macros can terminate a procedure on certain
 *	errors.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/nlookup.h>
#include <sys/namei.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/buf.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>

#include <sys/buf2.h>

#include <sys/thread2.h>

#include "nfsproto.h"
#include "rpcv2.h"
#include "nfs.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"

#ifdef NFSRV_DEBUG
#define nfsdbprintf(info)	kprintf info
#else
#define nfsdbprintf(info)
#endif

#define MAX_REORDERED_RPC	(16)
#define MAX_COMMIT_COUNT	(1024 * 1024)

#define NUM_HEURISTIC		1031
#define NHUSE_INIT		64
#define NHUSE_INC		16
#define NHUSE_MAX		2048

static struct nfsheur {
    struct vnode *nh_vp;	/* vp to match (unreferenced pointer) */
    off_t nh_nextoff;		/* next offset for sequential detection */
    int nh_use;			/* use count for selection */
    int nh_seqcount;		/* heuristic */
} nfsheur[NUM_HEURISTIC];

nfstype nfsv3_type[9] = { NFNON, NFREG, NFDIR, NFBLK, NFCHR, NFLNK, NFSOCK,
		      NFFIFO, NFNON };
#ifndef NFS_NOSERVER 
nfstype nfsv2_type[9] = { NFNON, NFREG, NFDIR, NFBLK, NFCHR, NFLNK, NFNON,
		      NFCHR, NFNON };

int nfsrvw_procrastinate = NFS_GATHERDELAY * 1000;
int nfsrvw_procrastinate_v3 = 0;

static struct timespec	nfsver;

SYSCTL_DECL(_vfs_nfs);

int nfs_async;
SYSCTL_INT(_vfs_nfs, OID_AUTO, async, CTLFLAG_RW, &nfs_async, 0,
    "Enable unstable and fast writes");
static int nfs_commit_blks;
static int nfs_commit_miss;
SYSCTL_INT(_vfs_nfs, OID_AUTO, commit_blks, CTLFLAG_RW, &nfs_commit_blks, 0,
    "Number of committed blocks");
SYSCTL_INT(_vfs_nfs, OID_AUTO, commit_miss, CTLFLAG_RW, &nfs_commit_miss, 0,
    "Number of nfs blocks committed from dirty buffers");

static int nfsrv_access (struct mount *, struct vnode *, int,
			struct ucred *, int, struct thread *, int);
static void nfsrvw_coalesce (struct nfsrv_descript *,
		struct nfsrv_descript *);

/*
 * Heuristic to detect sequential operation.
 */
static struct nfsheur *
nfsrv_sequential_heuristic(struct uio *uio, struct vnode *vp, int writeop)
{
	struct nfsheur *nh;
	int hi, try;

	/* Locate best candidate */
	try = 32;
	hi = ((int)(vm_offset_t) vp / sizeof(struct vnode)) % NUM_HEURISTIC;
	nh = &nfsheur[hi];

	while (try--) {
		if (nfsheur[hi].nh_vp == vp) {
			nh = &nfsheur[hi];
			break;
		}
		if (nfsheur[hi].nh_use > 0)
			--nfsheur[hi].nh_use;
		hi = (hi + 1) % NUM_HEURISTIC;
		if (nfsheur[hi].nh_use < nh->nh_use)
			nh = &nfsheur[hi];
	}

	/* Initialize hint if this is a new file */
	if (nh->nh_vp != vp) {
		nh->nh_vp = vp;
		nh->nh_nextoff = uio->uio_offset;
		nh->nh_use = NHUSE_INIT;
		if (uio->uio_offset == 0)
			nh->nh_seqcount = 4;
		else
			nh->nh_seqcount = 1;
	}

	/*
	 * Calculate heuristic
	 *
	 * See vfs_vnops.c:sequential_heuristic().
	 */
	if ((uio->uio_offset == 0 && nh->nh_seqcount > 0) ||
	    uio->uio_offset == nh->nh_nextoff) {
		nh->nh_seqcount += howmany(uio->uio_resid, 16384);
		if (nh->nh_seqcount > IO_SEQMAX)
			nh->nh_seqcount = IO_SEQMAX;
	} else if (qabs(uio->uio_offset - nh->nh_nextoff) <= MAX_REORDERED_RPC *
		imax(vp->v_mount->mnt_stat.f_iosize, uio->uio_resid)) {
		    /* Probably a reordered RPC, leave seqcount alone. */
	} else if (nh->nh_seqcount > 1) {
		nh->nh_seqcount /= 2;
	} else {
		nh->nh_seqcount = 0;
	}
	nh->nh_use += NHUSE_INC;
	if (nh->nh_use > NHUSE_MAX)
		nh->nh_use = NHUSE_MAX;
	return (nh);
}

/*
 * nfs v3 access service
 */
int
nfsrv3_access(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	      struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	nfsfh_t nfh;
	fhandle_t *fhp;
	int error = 0, rdonly, getret;
	struct vattr vattr, *vap = &vattr;
	u_long testmode, nfsmode;
	struct nfsm_info info;
	u_int32_t *tl;

	info.dpos = nfsd->nd_dpos;
	info.md = nfsd->nd_md;
	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam, &rdonly,
	    (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, 1, NULL);
		error = 0;
		goto nfsmout;
	}
	nfsmode = fxdr_unsigned(u_int32_t, *tl);
	if ((nfsmode & NFSV3ACCESS_READ) &&
		nfsrv_access(mp, vp, VREAD, cred, rdonly, td, 0))
		nfsmode &= ~NFSV3ACCESS_READ;
	if (vp->v_type == VDIR)
		testmode = (NFSV3ACCESS_MODIFY | NFSV3ACCESS_EXTEND |
			NFSV3ACCESS_DELETE);
	else
		testmode = (NFSV3ACCESS_MODIFY | NFSV3ACCESS_EXTEND);
	if ((nfsmode & testmode) &&
		nfsrv_access(mp, vp, VWRITE, cred, rdonly, td, 0))
		nfsmode &= ~testmode;
	if (vp->v_type == VDIR)
		testmode = NFSV3ACCESS_LOOKUP;
	else
		testmode = NFSV3ACCESS_EXECUTE;
	if ((nfsmode & testmode) &&
		nfsrv_access(mp, vp, VEXEC, cred, rdonly, td, 0))
		nfsmode &= ~testmode;
	getret = VOP_GETATTR(vp, vap);
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_POSTOPATTR(1) + NFSX_UNSIGNED, &error));
	nfsm_srvpostop_attr(&info, nfsd, getret, vap);
	tl = nfsm_build(&info, NFSX_UNSIGNED);
	*tl = txdr_unsigned(nfsmode);
nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs getattr service
 */
int
nfsrv_getattr(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	      struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct nfs_fattr *fp;
	struct vattr va;
	struct vattr *vap = &va;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	nfsfh_t nfh;
	fhandle_t *fhp;
	int error = 0, rdonly;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.mreq = NULL;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, 0, &error));
		error = 0;
		goto nfsmout;
	}
	error = VOP_GETATTR(vp, vap);
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_FATTR(nfsd->nd_flag & ND_NFSV3), &error));
	if (error) {
		error = 0;
		goto nfsmout;
	}
	fp = nfsm_build(&info, NFSX_FATTR(nfsd->nd_flag & ND_NFSV3));
	nfsm_srvfattr(nfsd, vap, fp);
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs setattr service
 */
int
nfsrv_setattr(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	      struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct vattr va, preat;
	struct vattr *vap = &va;
	struct nfsv2_sattr *sp;
	struct nfs_fattr *fp;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	nfsfh_t nfh;
	fhandle_t *fhp;
	u_int32_t *tl;
	int error = 0, rdonly, preat_ret = 1, postat_ret = 1;
	int gcheck = 0;
	struct timespec guard;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	guard.tv_sec = 0;	/* fix compiler warning */
	guard.tv_nsec = 0;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	VATTR_NULL(vap);
	if (info.v3) {
		ERROROUT(nfsm_srvsattr(&info, vap));
		NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
		gcheck = fxdr_unsigned(int, *tl);
		if (gcheck) {
			NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
			fxdr_nfsv3time(tl, &guard);
		}
	} else {
		NULLOUT(sp = nfsm_dissect(&info, NFSX_V2SATTR));
		/*
		 * Nah nah nah nah na nah
		 * There is a bug in the Sun client that puts 0xffff in the mode
		 * field of sattr when it should put in 0xffffffff. The u_short
		 * doesn't sign extend.
		 * --> check the low order 2 bytes for 0xffff
		 */
		if ((fxdr_unsigned(int, sp->sa_mode) & 0xffff) != 0xffff)
			vap->va_mode = nfstov_mode(sp->sa_mode);
		if (sp->sa_uid != nfs_xdrneg1)
			vap->va_uid = fxdr_unsigned(uid_t, sp->sa_uid);
		if (sp->sa_gid != nfs_xdrneg1)
			vap->va_gid = fxdr_unsigned(gid_t, sp->sa_gid);
		if (sp->sa_size != nfs_xdrneg1)
			vap->va_size = fxdr_unsigned(u_quad_t, sp->sa_size);
		if (sp->sa_atime.nfsv2_sec != nfs_xdrneg1) {
#ifdef notyet
			fxdr_nfsv2time(&sp->sa_atime, &vap->va_atime);
#else
			vap->va_atime.tv_sec =
				fxdr_unsigned(int32_t, sp->sa_atime.nfsv2_sec);
			vap->va_atime.tv_nsec = 0;
#endif
		}
		if (sp->sa_mtime.nfsv2_sec != nfs_xdrneg1)
			fxdr_nfsv2time(&sp->sa_mtime, &vap->va_mtime);

	}

	/*
	 * Now that we have all the fields, lets do it.
	 */
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam, &rdonly,
		(nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      2 * NFSX_UNSIGNED, &error));
		nfsm_srvwcc_data(&info, nfsd, preat_ret, &preat,
				 postat_ret, vap);
		error = 0;
		goto nfsmout;
	}

	/*
	 * vp now an active resource, pay careful attention to cleanup
	 */

	if (info.v3) {
		error = preat_ret = VOP_GETATTR(vp, &preat);
		if (!error && gcheck &&
			(preat.va_ctime.tv_sec != guard.tv_sec ||
			 preat.va_ctime.tv_nsec != guard.tv_nsec))
			error = NFSERR_NOT_SYNC;
		if (error) {
			vput(vp);
			vp = NULL;
			NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
					      NFSX_WCCDATA(info.v3), &error));
			nfsm_srvwcc_data(&info, nfsd, preat_ret, &preat,
					 postat_ret, vap);
			error = 0;
			goto nfsmout;
		}
	}

	/*
	 * If the size is being changed write acces is required, otherwise
	 * just check for a read only file system.
	 */
	if (vap->va_size == ((u_quad_t)((quad_t) -1))) {
		if (rdonly || (mp->mnt_flag & MNT_RDONLY)) {
			error = EROFS;
			goto out;
		}
	} else {
		if (vp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		} else if ((error = nfsrv_access(mp, vp, VWRITE, cred, rdonly,
			    td, 0)) != 0){ 
			goto out;
		}
	}
	error = VOP_SETATTR(vp, vap, cred);
	postat_ret = VOP_GETATTR(vp, vap);
	if (!error)
		error = postat_ret;
out:
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
		   NFSX_WCCORFATTR(info.v3), &error));
	if (info.v3) {
		nfsm_srvwcc_data(&info, nfsd, preat_ret, &preat,
				 postat_ret, vap);
		error = 0;
		goto nfsmout;
	} else {
		fp = nfsm_build(&info, NFSX_V2FATTR);
		nfsm_srvfattr(nfsd, vap, fp);
	}
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs lookup rpc
 */
int
nfsrv_lookup(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	     struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct nfs_fattr *fp;
	struct nlookupdata nd;
	struct vnode *vp;
	struct vnode *dirp;
	struct nchandle nch;
	nfsfh_t nfh;
	fhandle_t *fhp;
	int error = 0, len, dirattr_ret = 1;
	int pubflag;
	struct vattr va, dirattr, *vap = &va;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = NULL;
	vp = NULL;

	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	pubflag = nfs_ispublicfh(fhp);

	error = nfs_namei(&nd, cred, 0, NULL, &vp,
		fhp, len, slp, nam, &info.md, &info.dpos,
		&dirp, td, (nfsd->nd_flag & ND_KERBAUTH), pubflag);

	/*
	 * namei failure, only dirp to cleanup.  Clear out garbarge from
	 * structure in case macros jump to nfsmout.
	 */

	if (error) {
		if (dirp) {
			if (info.v3)
				dirattr_ret = VOP_GETATTR(dirp, &dirattr);
			vrele(dirp);
			dirp = NULL;
		}
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_POSTOPATTR(info.v3), &error));
		nfsm_srvpostop_attr(&info, nfsd, dirattr_ret, &dirattr);
		error = 0;
		goto nfsmout;
	}

	/*
	 * Locate index file for public filehandle
	 *
	 * error is 0 on entry and 0 on exit from this block.
	 */

	if (pubflag) {
		if (vp->v_type == VDIR && nfs_pub.np_index != NULL) {
			/*
			 * Setup call to lookup() to see if we can find
			 * the index file. Arguably, this doesn't belong
			 * in a kernel.. Ugh.  If an error occurs, do not
			 * try to install an index file and then clear the
			 * error.
			 *
			 * When we replace nd with ind and redirect ndp,
			 * maintenance of ni_startdir and ni_vp shift to
			 * ind and we have to clean them up in the old nd.
			 * However, the cnd resource continues to be maintained
			 * via the original nd.  Confused?  You aren't alone!
			 */
			vn_unlock(vp);
			cache_copy(&nd.nl_nch, &nch);
			nlookup_done(&nd);
			error = nlookup_init_raw(&nd, nfs_pub.np_index,
						UIO_SYSSPACE, 0, cred, &nch);
			cache_drop(&nch);
			if (error == 0)
				error = nlookup(&nd);

			if (error == 0) {
				/*
				 * Found an index file. Get rid of
				 * the old references.  transfer vp and
				 * load up the new vp.  Fortunately we do
				 * not have to deal with dvp, that would be
				 * a huge mess.
				 */
				if (dirp)	
					vrele(dirp);
				dirp = vp;
				vp = NULL;
				error = cache_vget(&nd.nl_nch, nd.nl_cred,
							LK_EXCLUSIVE, &vp);
				KKASSERT(error == 0);
			}
			error = 0;
		}
		/*
		 * If the public filehandle was used, check that this lookup
		 * didn't result in a filehandle outside the publicly exported
		 * filesystem.  We clear the poor vp here to avoid lockups due
		 * to NFS I/O.
		 */

		if (vp->v_mount != nfs_pub.np_mount) {
			vput(vp);
			vp = NULL;
			error = EPERM;
		}
	}

	if (dirp) {
		if (info.v3)
			dirattr_ret = VOP_GETATTR(dirp, &dirattr);
		vrele(dirp);
		dirp = NULL;
	}

	/*
	 * Resources at this point:
	 *	ndp->ni_vp	may not be NULL
	 *
	 */

	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_POSTOPATTR(info.v3), &error));
		nfsm_srvpostop_attr(&info, nfsd, dirattr_ret, &dirattr);
		error = 0;
		goto nfsmout;
	}

	/*
	 * Clear out some resources prior to potentially blocking.  This
	 * is not as critical as ni_dvp resources in other routines, but
	 * it helps.
	 */
	nlookup_done(&nd);

	/*
	 * Get underlying attribute, then release remaining resources ( for
	 * the same potential blocking reason ) and reply.
	 */
	bzero(&fhp->fh_fid, sizeof(fhp->fh_fid));
	error = VFS_VPTOFH(vp, &fhp->fh_fid);
	if (!error)
		error = VOP_GETATTR(vp, vap);

	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_SRVFH(info.v3) +
			      NFSX_POSTOPORFATTR(info.v3) +
			      NFSX_POSTOPATTR(info.v3),
			      &error));
	if (error) {
		nfsm_srvpostop_attr(&info, nfsd, dirattr_ret, &dirattr);
		error = 0;
		goto nfsmout;
	}
	nfsm_srvfhtom(&info, fhp);
	if (info.v3) {
		nfsm_srvpostop_attr(&info, nfsd, 0, vap);
		nfsm_srvpostop_attr(&info, nfsd, dirattr_ret, &dirattr);
	} else {
		fp = nfsm_build(&info, NFSX_V2FATTR);
		nfsm_srvfattr(nfsd, vap, fp);
	}

nfsmout:
	*mrq = info.mreq;
	if (dirp)
		vrele(dirp);
	nlookup_done(&nd);		/* may be called twice */
	if (vp)
		vput(vp);
	return (error);
}

/*
 * nfs readlink service
 */
int
nfsrv_readlink(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	       struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct iovec iv[(NFS_MAXPATHLEN+MLEN-1)/MLEN];
	struct iovec *ivp = iv;
	u_int32_t *tl;
	int error = 0, rdonly, i, tlen, len, getret;
	struct mbuf *mp1, *mp2, *mp3;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	struct vattr attr;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct uio io, *uiop = &io;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	bzero(&io, sizeof(struct uio));

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
#ifndef nolint
	mp2 = NULL;
#endif
	mp3 = NULL;
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	len = 0;
	i = 0;
	while (len < NFS_MAXPATHLEN) {
		mp1 = m_getcl(MB_WAIT, MT_DATA, 0);
		mp1->m_len = MCLBYTES;
		if (len == 0)
			mp3 = mp2 = mp1;
		else {
			mp2->m_next = mp1;
			mp2 = mp1;
		}
		if ((len + mp1->m_len) > NFS_MAXPATHLEN) {
			mp1->m_len = NFS_MAXPATHLEN-len;
			len = NFS_MAXPATHLEN;
		} else
			len += mp1->m_len;
		ivp->iov_base = mtod(mp1, caddr_t);
		ivp->iov_len = mp1->m_len;
		i++;
		ivp++;
	}
	uiop->uio_iov = iv;
	uiop->uio_iovcnt = i;
	uiop->uio_offset = 0;
	uiop->uio_resid = len;
	uiop->uio_rw = UIO_READ;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_td = NULL;
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      2 * NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, 1, NULL);
		error = 0;
		goto nfsmout;
	}
	if (vp->v_type != VLNK) {
		if (info.v3)
			error = EINVAL;
		else
			error = ENXIO;
		goto out;
	}
	error = VOP_READLINK(vp, uiop, cred);
out:
	getret = VOP_GETATTR(vp, &attr);
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			     NFSX_POSTOPATTR(info.v3) + NFSX_UNSIGNED,
			     &error));
	if (info.v3) {
		nfsm_srvpostop_attr(&info, nfsd, getret, &attr);
		if (error) {
			error = 0;
			goto nfsmout;
		}
	}
	if (uiop->uio_resid > 0) {
		len -= uiop->uio_resid;
		tlen = nfsm_rndup(len);
		nfsm_adj(mp3, NFS_MAXPATHLEN-tlen, tlen-len);
	}
	tl = nfsm_build(&info, NFSX_UNSIGNED);
	*tl = txdr_unsigned(len);
	info.mb->m_next = mp3;
	mp3 = NULL;
nfsmout:
	*mrq = info.mreq;
	if (mp3)
		m_freem(mp3);
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs read service
 */
int
nfsrv_read(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	   struct thread *td, struct mbuf **mrq)
{
	struct nfsm_info info;
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct iovec *iv;
	struct iovec *iv2;
	struct mbuf *m;
	struct nfs_fattr *fp;
	u_int32_t *tl;
	int i;
	int reqlen;
	int error = 0, rdonly, cnt, len, left, siz, tlen, getret;
	struct mbuf *m2;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct uio io, *uiop = &io;
	struct vattr va, *vap = &va;
	struct nfsheur *nh;
	off_t off;
	int ioflag = 0;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	if (info.v3) {
		NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
		off = fxdr_hyper(tl);
	} else {
		NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
		off = (off_t)fxdr_unsigned(u_int32_t, *tl);
	}
	NEGREPLYOUT(reqlen = nfsm_srvstrsiz(&info,
					    NFS_SRVMAXDATA(nfsd), &error));

	/*
	 * Reference vp.  If an error occurs, vp will be invalid, but we
	 * have to NULL it just in case.  The macros might goto nfsmout
	 * as well.
	 */

	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		vp = NULL;
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      2 * NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, 1, NULL);
		error = 0;
		goto nfsmout;
	}

	if (vp->v_type != VREG) {
		if (info.v3)
			error = EINVAL;
		else
			error = (vp->v_type == VDIR) ? EISDIR : EACCES;
	}
	if (!error) {
	    if ((error = nfsrv_access(mp, vp, VREAD, cred, rdonly, td, 1)) != 0)
		error = nfsrv_access(mp, vp, VEXEC, cred, rdonly, td, 1);
	}
	getret = VOP_GETATTR(vp, vap);
	if (!error)
		error = getret;
	if (error) {
		vput(vp);
		vp = NULL;
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_POSTOPATTR(info.v3), &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, vap);
		error = 0;
		goto nfsmout;
	}

	/*
	 * Calculate byte count to read
	 */

	if (off >= vap->va_size)
		cnt = 0;
	else if ((off + reqlen) > vap->va_size)
		cnt = vap->va_size - off;
	else
		cnt = reqlen;

	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_POSTOPORFATTR(info.v3) +
			      3 * NFSX_UNSIGNED + nfsm_rndup(cnt),
			      &error));
	if (info.v3) {
		tl = nfsm_build(&info, NFSX_V3FATTR + 4 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		fp = (struct nfs_fattr *)tl;
		tl += (NFSX_V3FATTR / sizeof (u_int32_t));
	} else {
		tl = nfsm_build(&info, NFSX_V2FATTR + NFSX_UNSIGNED);
		fp = (struct nfs_fattr *)tl;
		tl += (NFSX_V2FATTR / sizeof (u_int32_t));
	}
	len = left = nfsm_rndup(cnt);
	if (cnt > 0) {
		/*
		 * Generate the mbuf list with the uio_iov ref. to it.
		 */
		i = 0;
		m = m2 = info.mb;
		while (left > 0) {
			siz = min(M_TRAILINGSPACE(m), left);
			if (siz > 0) {
				left -= siz;
				i++;
			}
			if (left > 0) {
				m = m_getcl(MB_WAIT, MT_DATA, 0);
				m->m_len = 0;
				m2->m_next = m;
				m2 = m;
			}
		}
		iv = kmalloc(i * sizeof(struct iovec), M_TEMP, M_WAITOK);
		uiop->uio_iov = iv2 = iv;
		m = info.mb;
		left = len;
		i = 0;
		while (left > 0) {
			if (m == NULL)
				panic("nfsrv_read iov");
			siz = min(M_TRAILINGSPACE(m), left);
			if (siz > 0) {
				iv->iov_base = mtod(m, caddr_t) + m->m_len;
				iv->iov_len = siz;
				m->m_len += siz;
				left -= siz;
				iv++;
				i++;
			}
			m = m->m_next;
		}
		uiop->uio_iovcnt = i;
		uiop->uio_offset = off;
		uiop->uio_resid = len;
		uiop->uio_rw = UIO_READ;
		uiop->uio_segflg = UIO_SYSSPACE;
		nh = nfsrv_sequential_heuristic(uiop, vp, 0);
		ioflag |= nh->nh_seqcount << IO_SEQSHIFT;
		error = VOP_READ(vp, uiop, IO_NODELOCKED | ioflag, cred);
		if (error == 0) {
			off = uiop->uio_offset;
			nh->nh_nextoff = off;
		}
		kfree((caddr_t)iv2, M_TEMP);
		if (error || (getret = VOP_GETATTR(vp, vap))) {
			if (!error)
				error = getret;
			m_freem(info.mreq);
			info.mreq = NULL;
			vput(vp);
			vp = NULL;
			NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
					      NFSX_POSTOPATTR(info.v3),
					      &error));
			nfsm_srvpostop_attr(&info, nfsd, getret, vap);
			error = 0;
			goto nfsmout;
		}
	} else {
		uiop->uio_resid = 0;
	}
	vput(vp);
	vp = NULL;
	nfsm_srvfattr(nfsd, vap, fp);
	tlen = len - uiop->uio_resid;
	cnt = cnt < tlen ? cnt : tlen;
	tlen = nfsm_rndup(cnt);
	if (len != tlen || tlen != cnt)
		nfsm_adj(info.mb, len - tlen, tlen - cnt);
	if (info.v3) {
		*tl++ = txdr_unsigned(cnt);
		if (cnt < reqlen)
			*tl++ = nfs_true;
		else
			*tl++ = nfs_false;
	}
	*tl = txdr_unsigned(cnt);
nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs write service
 */
int
nfsrv_write(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct iovec *ivp;
	int i, cnt;
	struct mbuf *mp1;
	struct nfs_fattr *fp;
	struct iovec *iv;
	struct vattr va, forat;
	struct vattr *vap = &va;
	u_int32_t *tl;
	int error = 0, rdonly, len, forat_ret = 1;
	int ioflags, aftat_ret = 1, retlen, zeroing, adjust;
	int stable = NFSV3WRITE_FILESYNC;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	struct nfsheur *nh;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct uio io, *uiop = &io;
	struct nfsm_info info;
	off_t off;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	if (info.mrep == NULL) {
		error = 0;
		goto nfsmout;
	}
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	if (info.v3) {
		NULLOUT(tl = nfsm_dissect(&info, 5 * NFSX_UNSIGNED));
		off = fxdr_hyper(tl);
		tl += 3;
		stable = fxdr_unsigned(int, *tl++);
	} else {
		NULLOUT(tl = nfsm_dissect(&info, 4 * NFSX_UNSIGNED));
		off = (off_t)fxdr_unsigned(u_int32_t, *++tl);
		tl += 2;
		if (nfs_async)
	    		stable = NFSV3WRITE_UNSTABLE;
	}
	retlen = len = fxdr_unsigned(int32_t, *tl);
	cnt = i = 0;

	/*
	 * For NFS Version 2, it is not obvious what a write of zero length
	 * should do, but I might as well be consistent with Version 3,
	 * which is to return ok so long as there are no permission problems.
	 */
	if (len > 0) {
	    zeroing = 1;
	    mp1 = info.mrep;
	    while (mp1) {
		if (mp1 == info.md) {
			zeroing = 0;
			adjust = info.dpos - mtod(mp1, caddr_t);
			mp1->m_len -= adjust;
			if (mp1->m_len > 0 && adjust > 0)
				mp1->m_data += adjust;
		}
		if (zeroing)
			mp1->m_len = 0;
		else if (mp1->m_len > 0) {
			i += mp1->m_len;
			if (i > len) {
				mp1->m_len -= (i - len);
				zeroing	= 1;
			}
			if (mp1->m_len > 0)
				cnt++;
		}
		mp1 = mp1->m_next;
	    }
	}
	if (len > NFS_MAXDATA || len < 0 || i < len) {
		error = EIO;
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      2 * NFSX_UNSIGNED, &error));
		nfsm_srvwcc_data(&info, nfsd, forat_ret, &forat,
				 aftat_ret, vap);
		error = 0;
		goto nfsmout;
	}
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		vp = NULL;
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      2 * NFSX_UNSIGNED, &error));
		nfsm_srvwcc_data(&info, nfsd, forat_ret, &forat,
				 aftat_ret, vap);
		error = 0;
		goto nfsmout;
	}
	if (info.v3)
		forat_ret = VOP_GETATTR(vp, &forat);
	if (vp->v_type != VREG) {
		if (info.v3)
			error = EINVAL;
		else
			error = (vp->v_type == VDIR) ? EISDIR : EACCES;
	}
	if (!error) {
		error = nfsrv_access(mp, vp, VWRITE, cred, rdonly, td, 1);
	}
	if (error) {
		vput(vp);
		vp = NULL;
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_WCCDATA(info.v3), &error));
		nfsm_srvwcc_data(&info, nfsd, forat_ret, &forat,
				 aftat_ret, vap);
		error = 0;
		goto nfsmout;
	}

	if (len > 0) {
	    ivp = kmalloc(cnt * sizeof(struct iovec), M_TEMP, M_WAITOK);
	    uiop->uio_iov = iv = ivp;
	    uiop->uio_iovcnt = cnt;
	    mp1 = info.mrep;
	    while (mp1) {
		if (mp1->m_len > 0) {
			ivp->iov_base = mtod(mp1, caddr_t);
			ivp->iov_len = mp1->m_len;
			ivp++;
		}
		mp1 = mp1->m_next;
	    }

	    /*
	     * XXX
	     * The IO_METASYNC flag indicates that all metadata (and not just
	     * enough to ensure data integrity) mus be written to stable storage
	     * synchronously.
	     * (IO_METASYNC is not yet implemented in 4.4BSD-Lite.)
	     */
	    if (stable == NFSV3WRITE_UNSTABLE)
		ioflags = IO_NODELOCKED;
	    else if (stable == NFSV3WRITE_DATASYNC)
		ioflags = (IO_SYNC | IO_NODELOCKED);
	    else
		ioflags = (IO_METASYNC | IO_SYNC | IO_NODELOCKED);
	    uiop->uio_resid = len;
	    uiop->uio_rw = UIO_WRITE;
	    uiop->uio_segflg = UIO_SYSSPACE;
	    uiop->uio_td = NULL;
	    uiop->uio_offset = off;
	    nh = nfsrv_sequential_heuristic(uiop, vp, 1);
	    ioflags |= nh->nh_seqcount << IO_SEQSHIFT;
	    error = VOP_WRITE(vp, uiop, ioflags, cred);
	    if (error == 0)
		nh->nh_nextoff = uiop->uio_offset;
	    nfsstats.srvvop_writes++;
	    kfree((caddr_t)iv, M_TEMP);
	}
	aftat_ret = VOP_GETATTR(vp, vap);
	vput(vp);
	vp = NULL;
	if (!error)
		error = aftat_ret;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_PREOPATTR(info.v3) +
			      NFSX_POSTOPORFATTR(info.v3) +
			      2 * NFSX_UNSIGNED + NFSX_WRITEVERF(info.v3),
			      &error));
	if (info.v3) {
		nfsm_srvwcc_data(&info, nfsd, forat_ret, &forat,
				 aftat_ret, vap);
		if (error) {
			error = 0;
			goto nfsmout;
		}
		tl = nfsm_build(&info, 4 * NFSX_UNSIGNED);
		*tl++ = txdr_unsigned(retlen);
		/*
		 * If nfs_async is set, then pretend the write was FILESYNC.
		 */
		if (stable == NFSV3WRITE_UNSTABLE && !nfs_async)
			*tl++ = txdr_unsigned(stable);
		else
			*tl++ = txdr_unsigned(NFSV3WRITE_FILESYNC);
		/*
		 * Actually, there is no need to txdr these fields,
		 * but it may make the values more human readable,
		 * for debugging purposes.
		 */
		if (nfsver.tv_sec == 0)
			nfsver = boottime;
		*tl++ = txdr_unsigned(nfsver.tv_sec);
		*tl = txdr_unsigned(nfsver.tv_nsec / 1000);
	} else {
		fp = nfsm_build(&info, NFSX_V2FATTR);
		nfsm_srvfattr(nfsd, vap, fp);
	}
nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * NFS write service with write gathering support. Called when
 * nfsrvw_procrastinate > 0.
 * See: Chet Juszczak, "Improving the Write Performance of an NFS Server",
 * in Proc. of the Winter 1994 Usenix Conference, pg. 247-259, San Franscisco,
 * Jan. 1994.
 */
int
nfsrv_writegather(struct nfsrv_descript **ndp, struct nfssvc_sock *slp,
		  struct thread *td, struct mbuf **mrq)
{
	struct iovec *ivp;
	struct nfsrv_descript *wp, *nfsd, *owp, *swp;
	struct nfs_fattr *fp;
	int i;
	struct iovec *iov;
	struct nfsrvw_delayhash *wpp;
	struct ucred *cred;
	struct vattr va, forat;
	u_int32_t *tl;
	int error = 0, rdonly, len, forat_ret = 1;
	int ioflags, aftat_ret = 1, adjust, zeroing;
	struct mbuf *mp1;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	struct uio io, *uiop = &io;
	u_quad_t cur_usec;
	struct nfsm_info info;

	info.mreq = NULL;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
#ifndef nolint
	i = 0;
	len = 0;
#endif
	if (*ndp) {
	    nfsd = *ndp;
	    *ndp = NULL;
	    info.mrep = nfsd->nd_mrep;
	    info.mreq = NULL;
	    info.md = nfsd->nd_md;
	    info.dpos = nfsd->nd_dpos;
	    info.v3 = (nfsd->nd_flag & ND_NFSV3);
	    cred = &nfsd->nd_cr;
	    LIST_INIT(&nfsd->nd_coalesce);
	    nfsd->nd_mreq = NULL;
	    nfsd->nd_stable = NFSV3WRITE_FILESYNC;
	    cur_usec = nfs_curusec();
	    nfsd->nd_time = cur_usec +
		(info.v3 ? nfsrvw_procrastinate_v3 : nfsrvw_procrastinate);
    
	    /*
	     * Now, get the write header..
	     */
	    NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, &nfsd->nd_fh, &error));
	    if (info.v3) {
		NULLOUT(tl = nfsm_dissect(&info, 5 * NFSX_UNSIGNED));
		nfsd->nd_off = fxdr_hyper(tl);
		tl += 3;
		nfsd->nd_stable = fxdr_unsigned(int, *tl++);
	    } else {
		NULLOUT(tl = nfsm_dissect(&info, 4 * NFSX_UNSIGNED));
		nfsd->nd_off = (off_t)fxdr_unsigned(u_int32_t, *++tl);
		tl += 2;
		if (nfs_async)
			nfsd->nd_stable = NFSV3WRITE_UNSTABLE;
	    }
	    len = fxdr_unsigned(int32_t, *tl);
	    nfsd->nd_len = len;
	    nfsd->nd_eoff = nfsd->nd_off + len;
    
	    /*
	     * Trim the header out of the mbuf list and trim off any trailing
	     * junk so that the mbuf list has only the write data.
	     */
	    zeroing = 1;
	    i = 0;
	    mp1 = info.mrep;
	    while (mp1) {
		if (mp1 == info.md) {
		    zeroing = 0;
		    adjust = info.dpos - mtod(mp1, caddr_t);
		    mp1->m_len -= adjust;
		    if (mp1->m_len > 0 && adjust > 0)
			mp1->m_data += adjust;
		}
		if (zeroing)
		    mp1->m_len = 0;
		else {
		    i += mp1->m_len;
		    if (i > len) {
			mp1->m_len -= (i - len);
			zeroing = 1;
		    }
		}
		mp1 = mp1->m_next;
	    }
	    if (len > NFS_MAXDATA || len < 0  || i < len) {
nfsmout:
		m_freem(info.mrep);
		info.mrep = NULL;
		error = EIO;
		nfsm_writereply(&info, nfsd, slp, error, 2 * NFSX_UNSIGNED);
		if (info.v3) {
		    nfsm_srvwcc_data(&info, nfsd, forat_ret, &forat,
				     aftat_ret, &va);
		}
		nfsd->nd_mreq = info.mreq;
		nfsd->nd_mrep = NULL;
		nfsd->nd_time = 0;
	    }
    
	    /*
	     * Add this entry to the hash and time queues.
	     */
	    owp = NULL;
	    wp = slp->ns_tq.lh_first;
	    while (wp && wp->nd_time < nfsd->nd_time) {
		owp = wp;
		wp = wp->nd_tq.le_next;
	    }
	    NFS_DPF(WG, ("Q%03x", nfsd->nd_retxid & 0xfff));
	    if (owp) {
		LIST_INSERT_AFTER(owp, nfsd, nd_tq);
	    } else {
		LIST_INSERT_HEAD(&slp->ns_tq, nfsd, nd_tq);
	    }
	    if (nfsd->nd_mrep) {
		wpp = NWDELAYHASH(slp, nfsd->nd_fh.fh_fid.fid_data);
		owp = NULL;
		wp = wpp->lh_first;
		while (wp &&
		    bcmp((caddr_t)&nfsd->nd_fh,(caddr_t)&wp->nd_fh,NFSX_V3FH)) {
		    owp = wp;
		    wp = wp->nd_hash.le_next;
		}
		while (wp && wp->nd_off < nfsd->nd_off &&
		    !bcmp((caddr_t)&nfsd->nd_fh,(caddr_t)&wp->nd_fh,NFSX_V3FH)) {
		    owp = wp;
		    wp = wp->nd_hash.le_next;
		}
		if (owp) {
		    LIST_INSERT_AFTER(owp, nfsd, nd_hash);

		    /*
		     * Search the hash list for overlapping entries and
		     * coalesce.
		     */
		    for(; nfsd && NFSW_CONTIG(owp, nfsd); nfsd = wp) {
			wp = nfsd->nd_hash.le_next;
			if (NFSW_SAMECRED(owp, nfsd))
			    nfsrvw_coalesce(owp, nfsd);
		    }
		} else {
		    LIST_INSERT_HEAD(wpp, nfsd, nd_hash);
		}
	    }
	}
    
	/*
	 * Now, do VOP_WRITE()s for any one(s) that need to be done now
	 * and generate the associated reply mbuf list(s).
	 */
loop1:
	cur_usec = nfs_curusec();
	for (nfsd = slp->ns_tq.lh_first; nfsd; nfsd = owp) {
		owp = nfsd->nd_tq.le_next;
		if (nfsd->nd_time > cur_usec)
		    break;
		if (nfsd->nd_mreq)
		    continue;
		NFS_DPF(WG, ("P%03x", nfsd->nd_retxid & 0xfff));
		LIST_REMOVE(nfsd, nd_tq);
		LIST_REMOVE(nfsd, nd_hash);
		info.mrep = nfsd->nd_mrep;
		info.mreq = NULL;
		info.v3 = (nfsd->nd_flag & ND_NFSV3);
		nfsd->nd_mrep = NULL;
		cred = &nfsd->nd_cr;
		forat_ret = aftat_ret = 1;
		error = nfsrv_fhtovp(&nfsd->nd_fh, 1, &mp, &vp, cred, slp, 
				     nfsd->nd_nam, &rdonly,
				     (nfsd->nd_flag & ND_KERBAUTH), TRUE);
		if (!error) {
		    if (info.v3)
			forat_ret = VOP_GETATTR(vp, &forat);
		    if (vp->v_type != VREG) {
			if (info.v3)
			    error = EINVAL;
			else
			    error = (vp->v_type == VDIR) ? EISDIR : EACCES;
		    }
		} else {
		    vp = NULL;
		}
		if (!error) {
		    error = nfsrv_access(mp, vp, VWRITE, cred, rdonly, td, 1);
		}
    
		if (nfsd->nd_stable == NFSV3WRITE_UNSTABLE)
		    ioflags = IO_NODELOCKED;
		else if (nfsd->nd_stable == NFSV3WRITE_DATASYNC)
		    ioflags = (IO_SYNC | IO_NODELOCKED);
		else
		    ioflags = (IO_METASYNC | IO_SYNC | IO_NODELOCKED);
		uiop->uio_rw = UIO_WRITE;
		uiop->uio_segflg = UIO_SYSSPACE;
		uiop->uio_td = NULL;
		uiop->uio_offset = nfsd->nd_off;
		uiop->uio_resid = nfsd->nd_eoff - nfsd->nd_off;
		if (uiop->uio_resid > 0) {
		    mp1 = info.mrep;
		    i = 0;
		    while (mp1) {
			if (mp1->m_len > 0)
			    i++;
			mp1 = mp1->m_next;
		    }
		    uiop->uio_iovcnt = i;
		    iov = kmalloc(i * sizeof(struct iovec), M_TEMP, M_WAITOK);
		    uiop->uio_iov = ivp = iov;
		    mp1 = info.mrep;
		    while (mp1) {
			if (mp1->m_len > 0) {
			    ivp->iov_base = mtod(mp1, caddr_t);
			    ivp->iov_len = mp1->m_len;
			    ivp++;
			}
			mp1 = mp1->m_next;
		    }
		    if (!error) {
			error = VOP_WRITE(vp, uiop, ioflags, cred);
			nfsstats.srvvop_writes++;
		    }
		    kfree((caddr_t)iov, M_TEMP);
		}
		m_freem(info.mrep);
		info.mrep = NULL;
		if (vp) {
		    aftat_ret = VOP_GETATTR(vp, &va);
		    vput(vp);
		    vp = NULL;
		}

		/*
		 * Loop around generating replies for all write rpcs that have
		 * now been completed.
		 */
		swp = nfsd;
		do {
		    NFS_DPF(WG, ("R%03x", nfsd->nd_retxid & 0xfff));
		    if (error) {
			nfsm_writereply(&info, nfsd, slp, error,
					NFSX_WCCDATA(info.v3));
			if (info.v3) {
			    nfsm_srvwcc_data(&info, nfsd, forat_ret, &forat,
					     aftat_ret, &va);
			}
		    } else {
			nfsm_writereply(&info, nfsd, slp, error,
					NFSX_PREOPATTR(info.v3) +
					NFSX_POSTOPORFATTR(info.v3) +
					2 * NFSX_UNSIGNED +
					NFSX_WRITEVERF(info.v3));
			if (info.v3) {
			    nfsm_srvwcc_data(&info, nfsd, forat_ret, &forat,
					     aftat_ret, &va);
			    tl = nfsm_build(&info, 4 * NFSX_UNSIGNED);
			    *tl++ = txdr_unsigned(nfsd->nd_len);
			    *tl++ = txdr_unsigned(swp->nd_stable);
			    /*
			     * Actually, there is no need to txdr these fields,
			     * but it may make the values more human readable,
			     * for debugging purposes.
			     */
			    if (nfsver.tv_sec == 0)
				    nfsver = boottime;
			    *tl++ = txdr_unsigned(nfsver.tv_sec);
			    *tl = txdr_unsigned(nfsver.tv_nsec / 1000);
			} else {
			    fp = nfsm_build(&info, NFSX_V2FATTR);
			    nfsm_srvfattr(nfsd, &va, fp);
			}
		    }
		    nfsd->nd_mreq = info.mreq;
		    if (nfsd->nd_mrep)
			panic("nfsrv_write: nd_mrep not free");

		    /*
		     * Done. Put it at the head of the timer queue so that
		     * the final phase can return the reply.
		     */
		    if (nfsd != swp) {
			nfsd->nd_time = 0;
			LIST_INSERT_HEAD(&slp->ns_tq, nfsd, nd_tq);
		    }
		    nfsd = swp->nd_coalesce.lh_first;
		    if (nfsd) {
			LIST_REMOVE(nfsd, nd_tq);
		    }
		} while (nfsd);
		swp->nd_time = 0;
		LIST_INSERT_HEAD(&slp->ns_tq, swp, nd_tq);
		goto loop1;
	}

	/*
	 * Search for a reply to return.
	 */
	for (nfsd = slp->ns_tq.lh_first; nfsd; nfsd = nfsd->nd_tq.le_next) {
		if (nfsd->nd_mreq) {
		    NFS_DPF(WG, ("X%03x", nfsd->nd_retxid & 0xfff));
		    LIST_REMOVE(nfsd, nd_tq);
		    break;
		}
	}
	if (nfsd) {
		*ndp = nfsd;
		*mrq = nfsd->nd_mreq;
	} else {
		*ndp = NULL;
		*mrq = NULL;
	}
	return (0);
}

/*
 * Coalesce the write request nfsd into owp. To do this we must:
 * - remove nfsd from the queues
 * - merge nfsd->nd_mrep into owp->nd_mrep
 * - update the nd_eoff and nd_stable for owp
 * - put nfsd on owp's nd_coalesce list
 * NB: Must be called at splsoftclock().
 */
static void
nfsrvw_coalesce(struct nfsrv_descript *owp, struct nfsrv_descript *nfsd)
{
        int overlap;
        struct mbuf *mp1;
	struct nfsrv_descript *p;

	NFS_DPF(WG, ("C%03x-%03x",
		     nfsd->nd_retxid & 0xfff, owp->nd_retxid & 0xfff));
        LIST_REMOVE(nfsd, nd_hash);
        LIST_REMOVE(nfsd, nd_tq);
        if (owp->nd_eoff < nfsd->nd_eoff) {
            overlap = owp->nd_eoff - nfsd->nd_off;
            if (overlap < 0)
                panic("nfsrv_coalesce: bad off");
            if (overlap > 0)
                m_adj(nfsd->nd_mrep, overlap);
            mp1 = owp->nd_mrep;
            while (mp1->m_next)
                mp1 = mp1->m_next;
            mp1->m_next = nfsd->nd_mrep;
            owp->nd_eoff = nfsd->nd_eoff;
        } else
            m_freem(nfsd->nd_mrep);
        nfsd->nd_mrep = NULL;
        if (nfsd->nd_stable == NFSV3WRITE_FILESYNC)
            owp->nd_stable = NFSV3WRITE_FILESYNC;
        else if (nfsd->nd_stable == NFSV3WRITE_DATASYNC &&
            owp->nd_stable == NFSV3WRITE_UNSTABLE)
            owp->nd_stable = NFSV3WRITE_DATASYNC;
        LIST_INSERT_HEAD(&owp->nd_coalesce, nfsd, nd_tq);

	/*
	 * If nfsd had anything else coalesced into it, transfer them
	 * to owp, otherwise their replies will never get sent.
	 */
	for (p = nfsd->nd_coalesce.lh_first; p;
	     p = nfsd->nd_coalesce.lh_first) {
	    LIST_REMOVE(p, nd_tq);
	    LIST_INSERT_HEAD(&owp->nd_coalesce, p, nd_tq);
	}
}

/*
 * nfs create service
 * now does a truncate to 0 length via. setattr if it already exists
 */
int
nfsrv_create(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	     struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct nfs_fattr *fp;
	struct vattr va, dirfor, diraft;
	struct vattr *vap = &va;
	struct nfsv2_sattr *sp;
	u_int32_t *tl;
	struct nlookupdata nd;
	int error = 0, len, tsize, dirfor_ret = 1, diraft_ret = 1;
	udev_t rdev = NOUDEV;
	caddr_t cp;
	int how, exclusive_flag = 0;
	struct vnode *dirp;
	struct vnode *dvp;
	struct vnode *vp;
	struct mount *mp;
	nfsfh_t nfh;
	fhandle_t *fhp;
	u_quad_t tempsize;
	u_char cverf[NFSX_V3CREATEVERF];
	struct nfsm_info info;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = NULL;
	dvp = NULL;
	vp = NULL;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	/*
	 * Call namei and do initial cleanup to get a few things
	 * out of the way.  If we get an initial error we cleanup
	 * and return here to avoid special-casing the invalid nd
	 * structure through the rest of the case.  dirp may be
	 * set even if an error occurs, but the nd structure will not
	 * be valid at all if an error occurs so we have to invalidate it
	 * prior to calling nfsm_reply ( which might goto nfsmout ).
	 */
	error = nfs_namei(&nd, cred, NLC_CREATE, &dvp, &vp,
			  fhp, len, slp, nam, &info.md, &info.dpos, &dirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	mp = vfs_getvfs(&fhp->fh_fsid);

	if (dirp) {
		if (info.v3) {
			dirfor_ret = VOP_GETATTR(dirp, &dirfor);
		} else {
			vrele(dirp);
			dirp = NULL;
		}
	}
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_WCCDATA(info.v3), &error));
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
		goto nfsmout;
	}

	/*
	 * No error.  Continue.  State:
	 *
	 *	dirp 		may be valid
	 *	vp		may be valid or NULL if the target does not
	 *			exist.
	 *	dvp		is valid
	 *
	 * The error state is set through the code and we may also do some
	 * opportunistic releasing of vnodes to avoid holding locks through
	 * NFS I/O.  The cleanup at the end is a catch-all
	 */

	VATTR_NULL(vap);
	if (info.v3) {
		NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
		how = fxdr_unsigned(int, *tl);
		switch (how) {
		case NFSV3CREATE_GUARDED:
			if (vp) {
				error = EEXIST;
				break;
			}
			/* fall through */
		case NFSV3CREATE_UNCHECKED:
			ERROROUT(nfsm_srvsattr(&info, vap));
			break;
		case NFSV3CREATE_EXCLUSIVE:
			NULLOUT(cp = nfsm_dissect(&info, NFSX_V3CREATEVERF));
			bcopy(cp, cverf, NFSX_V3CREATEVERF);
			exclusive_flag = 1;
			break;
		}
		vap->va_type = VREG;
	} else {
		NULLOUT(sp = nfsm_dissect(&info, NFSX_V2SATTR));
		vap->va_type = IFTOVT(fxdr_unsigned(u_int32_t, sp->sa_mode));
		if (vap->va_type == VNON)
			vap->va_type = VREG;
		vap->va_mode = nfstov_mode(sp->sa_mode);
		switch (vap->va_type) {
		case VREG:
			tsize = fxdr_unsigned(int32_t, sp->sa_size);
			if (tsize != -1)
				vap->va_size = (u_quad_t)tsize;
			break;
		case VCHR:
		case VBLK:
		case VFIFO:
			rdev = fxdr_unsigned(long, sp->sa_size);
			break;
		default:
			break;
		}
	}

	/*
	 * Iff doesn't exist, create it
	 * otherwise just truncate to 0 length
	 *   should I set the mode too ?
	 *
	 * The only possible error we can have at this point is EEXIST. 
	 * nd.ni_vp will also be non-NULL in that case.
	 */
	if (vp == NULL) {
		if (vap->va_mode == (mode_t)VNOVAL)
			vap->va_mode = 0;
		if (vap->va_type == VREG || vap->va_type == VSOCK) {
			vn_unlock(dvp);
			error = VOP_NCREATE(&nd.nl_nch, dvp, &vp,
					    nd.nl_cred, vap);
			vrele(dvp);
			dvp = NULL;
			if (error == 0) {
				if (exclusive_flag) {
					exclusive_flag = 0;
					VATTR_NULL(vap);
					bcopy(cverf, (caddr_t)&vap->va_atime,
						NFSX_V3CREATEVERF);
					error = VOP_SETATTR(vp, vap, cred);
				}
			}
		} else if (
			vap->va_type == VCHR || 
			vap->va_type == VBLK ||
			vap->va_type == VFIFO
		) {
			/*
			 * Handle SysV FIFO node special cases.  All other
			 * devices require super user to access.
			 */
			if (vap->va_type == VCHR && rdev == 0xffffffff)
				vap->va_type = VFIFO;
                        if (vap->va_type != VFIFO &&
                            (error = priv_check_cred(cred, PRIV_ROOT, 0))) {
				goto nfsmreply0;
                        }
			vap->va_rmajor = umajor(rdev);
			vap->va_rminor = uminor(rdev);

			vn_unlock(dvp);
			error = VOP_NMKNOD(&nd.nl_nch, dvp, &vp, nd.nl_cred, vap);
			vrele(dvp);
			dvp = NULL;
			if (error)
				goto nfsmreply0;
#if 0
			/*
			 * XXX what is this junk supposed to do ?
			 */

			vput(vp);
			vp = NULL;

			/*
			 * release dvp prior to lookup
			 */
			vput(dvp);
			dvp = NULL;

			/*
			 * Setup for lookup. 
			 *
			 * Even though LOCKPARENT was cleared, ni_dvp may
			 * be garbage. 
			 */
			nd.ni_cnd.cn_nameiop = NAMEI_LOOKUP;
			nd.ni_cnd.cn_flags &= ~(CNP_LOCKPARENT);
			nd.ni_cnd.cn_td = td;
			nd.ni_cnd.cn_cred = cred;

			error = lookup(&nd);
			nd.ni_dvp = NULL;

			if (error != 0) {
				NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
						      0, &error));
				/* fall through on certain errors */
			}
			nfsrv_object_create(nd.ni_vp);
			if (nd.ni_cnd.cn_flags & CNP_ISSYMLINK) {
				error = EINVAL;
				goto nfsmreply0;
			}
#endif
		} else {
			error = ENXIO;
		}
	} else {
		if (vap->va_size != -1) {
			error = nfsrv_access(mp, vp, VWRITE, cred,
			    (nd.nl_flags & NLC_NFS_RDONLY), td, 0);
			if (!error) {
				tempsize = vap->va_size;
				VATTR_NULL(vap);
				vap->va_size = tempsize;
				error = VOP_SETATTR(vp, vap, cred);
			}
		}
	}

	if (!error) {
		bzero(&fhp->fh_fid, sizeof(fhp->fh_fid));
		error = VFS_VPTOFH(vp, &fhp->fh_fid);
		if (!error)
			error = VOP_GETATTR(vp, vap);
	}
	if (info.v3) {
		if (exclusive_flag && !error &&
			bcmp(cverf, (caddr_t)&vap->va_atime, NFSX_V3CREATEVERF))
			error = EEXIST;
		diraft_ret = VOP_GETATTR(dirp, &diraft);
		vrele(dirp);
		dirp = NULL;
	}
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_SRVFH(info.v3) + NFSX_FATTR(info.v3) +
			      NFSX_WCCDATA(info.v3),
			      &error));
	if (info.v3) {
		if (!error) {
			nfsm_srvpostop_fh(&info, fhp);
			nfsm_srvpostop_attr(&info, nfsd, 0, vap);
		}
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
	} else {
		nfsm_srvfhtom(&info, fhp);
		fp = nfsm_build(&info, NFSX_V2FATTR);
		nfsm_srvfattr(nfsd, vap, fp);
	}
	goto nfsmout;

nfsmreply0:
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, 0, &error));
	error = 0;
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	if (dirp)
		vrele(dirp);
	nlookup_done(&nd);
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
	}
	if (vp)
		vput(vp);
	return (error);
}

/*
 * nfs v3 mknod service
 */
int
nfsrv_mknod(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct vattr va, dirfor, diraft;
	struct vattr *vap = &va;
	u_int32_t *tl;
	struct nlookupdata nd;
	int error = 0, len, dirfor_ret = 1, diraft_ret = 1;
	enum vtype vtyp;
	struct vnode *dirp;
	struct vnode *dvp;
	struct vnode *vp;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct nfsm_info info;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = NULL;
	dvp = NULL;
	vp = NULL;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;

	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	/*
	 * Handle nfs_namei() call.  If an error occurs, the nd structure
	 * is not valid.  However, nfsm_*() routines may still jump to
	 * nfsmout.
	 */

	error = nfs_namei(&nd, cred, NLC_CREATE, &dvp, &vp,
			  fhp, len, slp, nam, &info.md, &info.dpos, &dirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (dirp)
		dirfor_ret = VOP_GETATTR(dirp, &dirfor);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			   NFSX_WCCDATA(1), &error));
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
		goto nfsmout;
	}
	NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
	vtyp = nfsv3tov_type(*tl);
	if (vtyp != VCHR && vtyp != VBLK && vtyp != VSOCK && vtyp != VFIFO) {
		error = NFSERR_BADTYPE;
		goto out;
	}
	VATTR_NULL(vap);
	ERROROUT(nfsm_srvsattr(&info, vap));
	if (vtyp == VCHR || vtyp == VBLK) {
		NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
		vap->va_rmajor = fxdr_unsigned(u_int32_t, *tl++);
		vap->va_rminor = fxdr_unsigned(u_int32_t, *tl);
	}

	/*
	 * Iff doesn't exist, create it.
	 */
	if (vp) {
		error = EEXIST;
		goto out;
	}
	vap->va_type = vtyp;
	if (vap->va_mode == (mode_t)VNOVAL)
		vap->va_mode = 0;
	if (vtyp == VSOCK) {
		vn_unlock(dvp);
		error = VOP_NCREATE(&nd.nl_nch, dvp, &vp, nd.nl_cred, vap);
		vrele(dvp);
		dvp = NULL;
	} else {
		if (vtyp != VFIFO && (error = priv_check_cred(cred, PRIV_ROOT, 0)))
			goto out;

		vn_unlock(dvp);
		error = VOP_NMKNOD(&nd.nl_nch, dvp, &vp, nd.nl_cred, vap);
		vrele(dvp);
		dvp = NULL;
		if (error)
			goto out;
	}

	/*
	 * send response, cleanup, return.
	 */
out:
	nlookup_done(&nd);
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
		dvp = NULL;
	}
	if (!error) {
		bzero(&fhp->fh_fid, sizeof(fhp->fh_fid));
		error = VFS_VPTOFH(vp, &fhp->fh_fid);
		if (!error)
			error = VOP_GETATTR(vp, vap);
	}
	if (vp) {
		vput(vp);
		vp = NULL;
	}
	diraft_ret = VOP_GETATTR(dirp, &diraft);
	if (dirp) {
		vrele(dirp);
		dirp = NULL;
	}
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_SRVFH(1) + NFSX_POSTOPATTR(1) +
			      NFSX_WCCDATA(1), &error));
	if (!error) {
		nfsm_srvpostop_fh(&info, fhp);
		nfsm_srvpostop_attr(&info, nfsd, 0, vap);
	}
	nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
			 diraft_ret, &diraft);
	*mrq = info.mreq;
	return (0);
nfsmout:
	*mrq = info.mreq;
	if (dirp)
		vrele(dirp);
	nlookup_done(&nd);
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
	}
	if (vp)
		vput(vp);
	return (error);
}

/*
 * nfs remove service
 */
int
nfsrv_remove(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	     struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct nlookupdata nd;
	int error = 0, len, dirfor_ret = 1, diraft_ret = 1;
	struct vnode *dirp;
	struct vnode *dvp;
	struct vnode *vp;
	struct vattr dirfor, diraft;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct nfsm_info info;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = NULL;
	dvp = NULL;
	vp = NULL;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	error = nfs_namei(&nd, cred, NLC_DELETE, &dvp, &vp,
			  fhp, len, slp, nam, &info.md, &info.dpos, &dirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (dirp) {
		if (info.v3)
			dirfor_ret = VOP_GETATTR(dirp, &dirfor);
	}
	if (error == 0) {
		if (vp->v_type == VDIR) {
			error = EPERM;		/* POSIX */
			goto out;
		}
		/*
		 * The root of a mounted filesystem cannot be deleted.
		 */
		if (vp->v_flag & VROOT) {
			error = EBUSY;
			goto out;
		}
out:
		if (!error) {
			if (dvp != vp)
				vn_unlock(dvp);
			if (vp) {
				vput(vp);
				vp = NULL;
			}
			error = VOP_NREMOVE(&nd.nl_nch, dvp, nd.nl_cred);
			vrele(dvp);
			dvp = NULL;
		}
	}
	if (dirp && info.v3)
		diraft_ret = VOP_GETATTR(dirp, &diraft);
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_WCCDATA(info.v3), &error));
	if (info.v3) {
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
	}
nfsmout:
	*mrq = info.mreq;
	nlookup_done(&nd);
	if (dirp)
		vrele(dirp);
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
	}
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs rename service
 */
int
nfsrv_rename(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	     struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	int error = 0, len, len2, fdirfor_ret = 1, fdiraft_ret = 1;
	int tdirfor_ret = 1, tdiraft_ret = 1;
	struct nlookupdata fromnd, tond;
	struct vnode *fvp, *fdirp, *fdvp;
	struct vnode *tvp, *tdirp, *tdvp;
	struct namecache *ncp;
	struct vattr fdirfor, fdiraft, tdirfor, tdiraft;
	nfsfh_t fnfh, tnfh;
	fhandle_t *ffhp, *tfhp;
	uid_t saved_uid;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
#ifndef nolint
	fvp = NULL;
#endif
	ffhp = &fnfh.fh_generic;
	tfhp = &tnfh.fh_generic;

	/*
	 * Clear fields incase goto nfsmout occurs from macro.
	 */

	nlookup_zero(&fromnd);
	nlookup_zero(&tond);
	fdirp = NULL;
	tdirp = NULL;

	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, ffhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	/*
	 * Remember our original uid so that we can reset cr_uid before
	 * the second nfs_namei() call, in case it is remapped.
	 */
	saved_uid = cred->cr_uid;
	error = nfs_namei(&fromnd, cred, NLC_RENAME_SRC,
			  NULL, NULL,
			  ffhp, len, slp, nam, &info.md, &info.dpos, &fdirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (fdirp) {
		if (info.v3)
			fdirfor_ret = VOP_GETATTR(fdirp, &fdirfor);
	}
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      2 * NFSX_WCCDATA(info.v3), &error));
		nfsm_srvwcc_data(&info, nfsd, fdirfor_ret, &fdirfor,
				 fdiraft_ret, &fdiraft);
		nfsm_srvwcc_data(&info, nfsd, tdirfor_ret, &tdirfor,
				 tdiraft_ret, &tdiraft);
		error = 0;
		goto nfsmout;
	}

	/*
	 * We have to unlock the from ncp before we can safely lookup
	 * the target ncp.
	 */
	KKASSERT(fromnd.nl_flags & NLC_NCPISLOCKED);
	cache_unlock(&fromnd.nl_nch);
	fromnd.nl_flags &= ~NLC_NCPISLOCKED;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, tfhp, &error));
	NEGATIVEOUT(len2 = nfsm_strsiz(&info, NFS_MAXNAMLEN));
	cred->cr_uid = saved_uid;

	error = nfs_namei(&tond, cred, NLC_RENAME_DST, NULL, NULL,
			  tfhp, len2, slp, nam, &info.md, &info.dpos, &tdirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (tdirp) {
		if (info.v3)
			tdirfor_ret = VOP_GETATTR(tdirp, &tdirfor);
	}
	if (error)
		goto out1;

	/*
	 * relock the source
	 */
	if (cache_lock_nonblock(&fromnd.nl_nch) == 0) {
		cache_resolve(&fromnd.nl_nch, fromnd.nl_cred);
	} else if (fromnd.nl_nch.ncp > tond.nl_nch.ncp) {
		cache_lock(&fromnd.nl_nch);
		cache_resolve(&fromnd.nl_nch, fromnd.nl_cred);
	} else {
		cache_unlock(&tond.nl_nch);
		cache_lock(&fromnd.nl_nch);
		cache_resolve(&fromnd.nl_nch, fromnd.nl_cred);
		cache_lock(&tond.nl_nch);
		cache_resolve(&tond.nl_nch, tond.nl_cred);
	}
	fromnd.nl_flags |= NLC_NCPISLOCKED;

	fvp = fromnd.nl_nch.ncp->nc_vp;
	tvp = tond.nl_nch.ncp->nc_vp;

	/*
	 * Set fdvp and tdvp.  We haven't done all the topology checks
	 * so these can wind up NULL (e.g. if either fvp or tvp is a mount
	 * point).  If we get through the checks these will be guarenteed
	 * to be non-NULL.
	 *
	 * Holding the children ncp's should be sufficient to prevent
	 * fdvp and tdvp ripouts.
	 */
	if (fromnd.nl_nch.ncp->nc_parent)
		fdvp = fromnd.nl_nch.ncp->nc_parent->nc_vp;
	else
		fdvp = NULL;
	if (tond.nl_nch.ncp->nc_parent)
		tdvp = tond.nl_nch.ncp->nc_parent->nc_vp;
	else
		tdvp = NULL;

	if (tvp != NULL) {
		if (fvp->v_type == VDIR && tvp->v_type != VDIR) {
			if (info.v3)
				error = EEXIST;
			else
				error = EISDIR;
			goto out;
		} else if (fvp->v_type != VDIR && tvp->v_type == VDIR) {
			if (info.v3)
				error = EEXIST;
			else
				error = ENOTDIR;
			goto out;
		}
		if (tvp->v_type == VDIR && (tond.nl_nch.ncp->nc_flag & NCF_ISMOUNTPT)) {
			if (info.v3)
				error = EXDEV;
			else
				error = ENOTEMPTY;
			goto out;
		}
	}
	if (fvp->v_type == VDIR && (fromnd.nl_nch.ncp->nc_flag & NCF_ISMOUNTPT)) {
		if (info.v3)
			error = EXDEV;
		else
			error = ENOTEMPTY;
		goto out;
	}
	if (fromnd.nl_nch.mount != tond.nl_nch.mount) {
		if (info.v3)
			error = EXDEV;
		else
			error = ENOTEMPTY;
		goto out;
	}
	if (fromnd.nl_nch.ncp == tond.nl_nch.ncp->nc_parent) {
		if (info.v3)
			error = EINVAL;
		else
			error = ENOTEMPTY;
	}

	/*
	 * You cannot rename a source into itself or a subdirectory of itself.
	 * We check this by travsering the target directory upwards looking
	 * for a match against the source.
	 */
	if (error == 0) {
		for (ncp = tond.nl_nch.ncp; ncp; ncp = ncp->nc_parent) {
			if (fromnd.nl_nch.ncp == ncp) {
				error = EINVAL;
				break;
			}
		}
	}

	/*
	 * If source is the same as the destination (that is the
	 * same vnode with the same name in the same directory),
	 * then there is nothing to do.
	 */
	if (fromnd.nl_nch.ncp == tond.nl_nch.ncp)
		error = -1;
out:
	if (!error) {
		/*
		 * The VOP_NRENAME function releases all vnode references &
		 * locks prior to returning so we need to clear the pointers
		 * to bypass cleanup code later on.
		 */
		error = VOP_NRENAME(&fromnd.nl_nch, &tond.nl_nch,
				    fdvp, tdvp, tond.nl_cred);
	} else {
		if (error == -1)
			error = 0;
	}
	/* fall through */

out1:
	if (fdirp)
		fdiraft_ret = VOP_GETATTR(fdirp, &fdiraft);
	if (tdirp)
		tdiraft_ret = VOP_GETATTR(tdirp, &tdiraft);
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      2 * NFSX_WCCDATA(info.v3), &error));
	if (info.v3) {
		nfsm_srvwcc_data(&info, nfsd, fdirfor_ret, &fdirfor,
				 fdiraft_ret, &fdiraft);
		nfsm_srvwcc_data(&info, nfsd, tdirfor_ret, &tdirfor,
				 tdiraft_ret, &tdiraft);
	}
	error = 0;
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	if (tdirp)
		vrele(tdirp);
	nlookup_done(&tond);
	if (fdirp)
		vrele(fdirp);
	nlookup_done(&fromnd);
	return (error);
}

/*
 * nfs link service
 */
int
nfsrv_link(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	   struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct nlookupdata nd;
	int error = 0, rdonly, len, dirfor_ret = 1, diraft_ret = 1;
	int getret = 1;
	struct vnode *dirp;
	struct vnode *dvp;
	struct vnode *vp;
	struct vnode *xp;
	struct mount *xmp;
	struct vattr dirfor, diraft, at;
	nfsfh_t nfh, dnfh;
	fhandle_t *fhp, *dfhp;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = dvp = vp = xp = NULL;
	xmp = NULL;

	fhp = &nfh.fh_generic;
	dfhp = &dnfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, dfhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	error = nfsrv_fhtovp(fhp, FALSE, &xmp, &xp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_POSTOPATTR(info.v3) +
				      NFSX_WCCDATA(info.v3),
				      &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		xp = NULL;
		error = 0;
		goto nfsmout;
	}
	if (xp->v_type == VDIR) {
		error = EPERM;		/* POSIX */
		goto out1;
	}

	error = nfs_namei(&nd, cred, NLC_CREATE, &dvp, &vp,
			  dfhp, len, slp, nam, &info.md, &info.dpos, &dirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (dirp) {
		if (info.v3)
			dirfor_ret = VOP_GETATTR(dirp, &dirfor);
	}
	if (error)
		goto out1;

	if (vp != NULL) {
		error = EEXIST;
		goto out;
	}
	if (xp->v_mount != dvp->v_mount)
		error = EXDEV;
out:
	if (!error) {
		vn_unlock(dvp);
		error = VOP_NLINK(&nd.nl_nch, dvp, xp, nd.nl_cred);
		vrele(dvp);
		dvp = NULL;
	}
	/* fall through */

out1:
	if (info.v3)
		getret = VOP_GETATTR(xp, &at);
	if (dirp)
		diraft_ret = VOP_GETATTR(dirp, &diraft);
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_POSTOPATTR(info.v3) + NFSX_WCCDATA(info.v3),
			      &error));
	if (info.v3) {
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
	}
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	nlookup_done(&nd);
	if (dirp)
		vrele(dirp);
	if (xp)
		vrele(xp);
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
	}
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs symbolic link service
 */
int
nfsrv_symlink(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	      struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct vattr va, dirfor, diraft;
	struct nlookupdata nd;
	struct vattr *vap = &va;
	struct nfsv2_sattr *sp;
	char *pathcp = NULL;
	struct uio io;
	struct iovec iv;
	int error = 0, len, len2, dirfor_ret = 1, diraft_ret = 1;
	struct vnode *dirp;
	struct vnode *vp;
	struct vnode *dvp;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct nfsm_info info;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = NULL;
	dvp = NULL;
	vp = NULL;

	info.mrep = nfsd->nd_mrep;
	info.mreq =  NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	error = nfs_namei(&nd, cred, NLC_CREATE, &dvp, &vp,
			fhp, len, slp, nam, &info.md, &info.dpos, &dirp,
			td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (dirp) {
		if (info.v3)
			dirfor_ret = VOP_GETATTR(dirp, &dirfor);
	}
	if (error)
		goto out;

	VATTR_NULL(vap);
	if (info.v3) {
		ERROROUT(nfsm_srvsattr(&info, vap));
	}
	NEGATIVEOUT(len2 = nfsm_strsiz(&info, NFS_MAXPATHLEN));
	pathcp = kmalloc(len2 + 1, M_TEMP, M_WAITOK);
	iv.iov_base = pathcp;
	iv.iov_len = len2;
	io.uio_resid = len2;
	io.uio_offset = 0;
	io.uio_iov = &iv;
	io.uio_iovcnt = 1;
	io.uio_segflg = UIO_SYSSPACE;
	io.uio_rw = UIO_READ;
	io.uio_td = NULL;
	ERROROUT(nfsm_mtouio(&info, &io, len2));
	if (info.v3 == 0) {
		NULLOUT(sp = nfsm_dissect(&info, NFSX_V2SATTR));
		vap->va_mode = nfstov_mode(sp->sa_mode);
	}
	*(pathcp + len2) = '\0';
	if (vp) {
		error = EEXIST;
		goto out;
	}

	if (vap->va_mode == (mode_t)VNOVAL)
		vap->va_mode = 0;
	if (dvp != vp)
		vn_unlock(dvp);
	error = VOP_NSYMLINK(&nd.nl_nch, dvp, &vp, nd.nl_cred, vap, pathcp);
	vrele(dvp);
	dvp = NULL;
	if (error == 0) {
		bzero(&fhp->fh_fid, sizeof(fhp->fh_fid));
		error = VFS_VPTOFH(vp, &fhp->fh_fid);
		if (!error)
			error = VOP_GETATTR(vp, vap);
	}

out:
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
	}
	if (vp) {
		vput(vp);
		vp = NULL;
	}
	if (pathcp) {
		kfree(pathcp, M_TEMP);
		pathcp = NULL;
	}
	if (dirp) {
		diraft_ret = VOP_GETATTR(dirp, &diraft);
		vrele(dirp);
		dirp = NULL;
	}
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_SRVFH(info.v3) + NFSX_POSTOPATTR(info.v3) +
			      NFSX_WCCDATA(info.v3),
			      &error));
	if (info.v3) {
		if (!error) {
			nfsm_srvpostop_fh(&info, fhp);
			nfsm_srvpostop_attr(&info, nfsd, 0, vap);
		}
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
	}
	error = 0;
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	nlookup_done(&nd);
	if (vp)
		vput(vp);
	if (dirp)
		vrele(dirp);
	if (pathcp)
		kfree(pathcp, M_TEMP);
	return (error);
}

/*
 * nfs mkdir service
 */
int
nfsrv_mkdir(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct vattr va, dirfor, diraft;
	struct vattr *vap = &va;
	struct nfs_fattr *fp;
	struct nlookupdata nd;
	u_int32_t *tl;
	int error = 0, len, dirfor_ret = 1, diraft_ret = 1;
	struct vnode *dirp;
	struct vnode *dvp;
	struct vnode *vp;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct nfsm_info info;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = NULL;
	dvp = NULL;
	vp = NULL;

	info.dpos = nfsd->nd_dpos;
	info.mrep = nfsd->nd_mrep;
	info.mreq =  NULL;
	info.md = nfsd->nd_md;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	error = nfs_namei(&nd, cred, NLC_CREATE, &dvp, &vp,
			  fhp, len, slp, nam, &info.md, &info.dpos, &dirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (dirp) {
		if (info.v3)
			dirfor_ret = VOP_GETATTR(dirp, &dirfor);
	}
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_WCCDATA(info.v3), &error));
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
		goto nfsmout;
	}
	VATTR_NULL(vap);
	if (info.v3) {
		ERROROUT(nfsm_srvsattr(&info, vap));
	} else {
		NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
		vap->va_mode = nfstov_mode(*tl++);
	}

	/*
	 * At this point nd.ni_dvp is referenced and exclusively locked and
	 * nd.ni_vp, if it exists, is referenced but not locked.
	 */

	vap->va_type = VDIR;
	if (vp != NULL) {
		error = EEXIST;
		goto out;
	}

	/*
	 * Issue mkdir op.  Since SAVESTART is not set, the pathname 
	 * component is freed by the VOP call.  This will fill-in
	 * nd.ni_vp, reference, and exclusively lock it.
	 */
	if (vap->va_mode == (mode_t)VNOVAL)
		vap->va_mode = 0;
	vn_unlock(dvp);
	error = VOP_NMKDIR(&nd.nl_nch, dvp, &vp, nd.nl_cred, vap);
	vrele(dvp);
	dvp = NULL;

	if (error == 0) {
		bzero(&fhp->fh_fid, sizeof(fhp->fh_fid));
		error = VFS_VPTOFH(vp, &fhp->fh_fid);
		if (error == 0)
			error = VOP_GETATTR(vp, vap);
	}
out:
	if (dirp)
		diraft_ret = VOP_GETATTR(dirp, &diraft);
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_SRVFH(info.v3) + NFSX_POSTOPATTR(info.v3) +
			      NFSX_WCCDATA(info.v3),
			      &error));
	if (info.v3) {
		if (!error) {
			nfsm_srvpostop_fh(&info, fhp);
			nfsm_srvpostop_attr(&info, nfsd, 0, vap);
		}
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
	} else {
		nfsm_srvfhtom(&info, fhp);
		fp = nfsm_build(&info, NFSX_V2FATTR);
		nfsm_srvfattr(nfsd, vap, fp);
	}
	error = 0;
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	nlookup_done(&nd);
	if (dirp)
		vrele(dirp);
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
	}
	if (vp)
		vput(vp);
	return (error);
}

/*
 * nfs rmdir service
 */
int
nfsrv_rmdir(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	int error = 0, len, dirfor_ret = 1, diraft_ret = 1;
	struct vnode *dirp;
	struct vnode *dvp;
	struct vnode *vp;
	struct vattr dirfor, diraft;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct nlookupdata nd;
	struct nfsm_info info;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	nlookup_zero(&nd);
	dirp = NULL;
	dvp = NULL;
	vp = NULL;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NEGREPLYOUT(len = nfsm_srvnamesiz(&info, &error));

	error = nfs_namei(&nd, cred, NLC_DELETE, &dvp, &vp,
			  fhp, len, slp, nam, &info.md, &info.dpos, &dirp,
			  td, (nfsd->nd_flag & ND_KERBAUTH), FALSE);
	if (dirp) {
		if (info.v3)
			dirfor_ret = VOP_GETATTR(dirp, &dirfor);
	}
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_WCCDATA(info.v3), &error));
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
		goto nfsmout;
	}
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}

	/*
	 * The root of a mounted filesystem cannot be deleted.
	 */
	if (vp->v_flag & VROOT)
		error = EBUSY;
out:
	/*
	 * Issue or abort op.  Since SAVESTART is not set, path name
	 * component is freed by the VOP after either.
	 */
	if (!error) {
		if (dvp != vp)
			vn_unlock(dvp);
		vput(vp);
		vp = NULL;
		error = VOP_NRMDIR(&nd.nl_nch, dvp, nd.nl_cred);
		vrele(dvp);
		dvp = NULL;
	}
	nlookup_done(&nd);

	if (dirp)
		diraft_ret = VOP_GETATTR(dirp, &diraft);
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_WCCDATA(info.v3), &error));
	if (info.v3) {
		nfsm_srvwcc_data(&info, nfsd, dirfor_ret, &dirfor,
				 diraft_ret, &diraft);
		error = 0;
	}
	/* fall through */

nfsmout:
	*mrq = info.mreq;
	if (dvp) {
		if (dvp == vp)
			vrele(dvp);
		else
			vput(dvp);
	}
	nlookup_done(&nd);
	if (dirp)
		vrele(dirp);
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs readdir service
 * - mallocs what it thinks is enough to read
 *	count rounded up to a multiple of NFS_DIRBLKSIZ <= NFS_MAXREADDIR
 * - calls VOP_READDIR()
 * - loops around building the reply
 *	if the output generated exceeds count break out of loop
 *	The nfsm_clget macro is used here so that the reply will be packed
 *	tightly in mbuf clusters.
 * - it only knows that it has encountered eof when the VOP_READDIR()
 *	reads nothing
 * - as such one readdir rpc will return eof false although you are there
 *	and then the next will return eof
 * - it trims out records with d_fileno == 0
 *	this doesn't matter for Unix clients, but they might confuse clients
 *	for other os'.
 * NB: It is tempting to set eof to true if the VOP_READDIR() reads less
 *	than requested, but this may not apply to all filesystems. For
 *	example, client NFS does not { although it is never remote mounted
 *	anyhow }
 *     The alternate call nfsrv_readdirplus() does lookups as well.
 * PS: The NFS protocol spec. does not clarify what the "count" byte
 *	argument is a count of.. just name strings and file id's or the
 *	entire reply rpc or ...
 *	I tried just file name and id sizes and it confused the Sun client,
 *	so I am using the full rpc size now. The "paranoia.." comment refers
 *	to including the status longwords that are not a part of the dir.
 *	"entry" structures, but are in the rpc.
 */
struct flrep {
	nfsuint64	fl_off;
	u_int32_t	fl_postopok;
	u_int32_t	fl_fattr[NFSX_V3FATTR / sizeof (u_int32_t)];
	u_int32_t	fl_fhok;
	u_int32_t	fl_fhsize;
	u_int32_t	fl_nfh[NFSX_V3FH / sizeof (u_int32_t)];
};

int
nfsrv_readdir(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	      struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	char *bp, *be;
	struct dirent *dp;
	caddr_t cp;
	u_int32_t *tl;
	struct mbuf *mp1, *mp2;
	char *cpos, *cend, *rbuf;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	struct vattr at;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct uio io;
	struct iovec iv;
	int len, nlen, rem, xfer, tsiz, i, error = 0, getret = 1;
	int siz, cnt, fullsiz, eofflag, rdonly, ncookies;
	u_quad_t off, toff;
#if 0
	u_quad_t verf;
#endif
	off_t *cookies = NULL, *cookiep;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	if (info.v3) {
		NULLOUT(tl = nfsm_dissect(&info, 5 * NFSX_UNSIGNED));
		toff = fxdr_hyper(tl);
		tl += 2;
#if 0
		verf = fxdr_hyper(tl);
#endif
		tl += 2;
	} else {
		NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
		toff = fxdr_unsigned(u_quad_t, *tl++);
#if 0
		verf = 0;	/* shut up gcc */
#endif
	}
	off = toff;
	cnt = fxdr_unsigned(int, *tl);
	siz = ((cnt + DIRBLKSIZ - 1) & ~(DIRBLKSIZ - 1));
	xfer = NFS_SRVMAXDATA(nfsd);
	if ((unsigned)cnt > xfer)
		cnt = xfer;
	if ((unsigned)siz > xfer)
		siz = xfer;
	fullsiz = siz;
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (!error && vp->v_type != VDIR) {
		error = ENOTDIR;
		vput(vp);
		vp = NULL;
	}
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}

	/*
	 * Obtain lock on vnode for this section of the code
	 */

	if (info.v3) {
		error = getret = VOP_GETATTR(vp, &at);
#if 0
		/*
		 * XXX This check may be too strict for Solaris 2.5 clients.
		 */
		if (!error && toff && verf && verf != at.va_filerev)
			error = NFSERR_BAD_COOKIE;
#endif
	}
	if (!error)
		error = nfsrv_access(mp, vp, VEXEC, cred, rdonly, td, 0);
	if (error) {
		vput(vp);
		vp = NULL;
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_POSTOPATTR(info.v3), &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	vn_unlock(vp);

	/*
	 * end section.  Allocate rbuf and continue
	 */
	rbuf = kmalloc(siz, M_TEMP, M_WAITOK);
again:
	iv.iov_base = rbuf;
	iv.iov_len = fullsiz;
	io.uio_iov = &iv;
	io.uio_iovcnt = 1;
	io.uio_offset = (off_t)off;
	io.uio_resid = fullsiz;
	io.uio_segflg = UIO_SYSSPACE;
	io.uio_rw = UIO_READ;
	io.uio_td = NULL;
	eofflag = 0;
	if (cookies) {
		kfree((caddr_t)cookies, M_TEMP);
		cookies = NULL;
	}
	error = VOP_READDIR(vp, &io, cred, &eofflag, &ncookies, &cookies);
	off = (off_t)io.uio_offset;
	if (!cookies && !error)
		error = NFSERR_PERM;
	if (info.v3) {
		getret = VOP_GETATTR(vp, &at);
		if (!error)
			error = getret;
	}
	if (error) {
		vrele(vp);
		vp = NULL;
		kfree((caddr_t)rbuf, M_TEMP);
		if (cookies)
			kfree((caddr_t)cookies, M_TEMP);
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_POSTOPATTR(info.v3), &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	if (io.uio_resid) {
		siz -= io.uio_resid;

		/*
		 * If nothing read, return eof
		 * rpc reply
		 */
		if (siz == 0) {
			vrele(vp);
			vp = NULL;
			NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
					      NFSX_POSTOPATTR(info.v3) +
					      NFSX_COOKIEVERF(info.v3) +
					      2 * NFSX_UNSIGNED,
					      &error));
			if (info.v3) {
				nfsm_srvpostop_attr(&info, nfsd, getret, &at);
				tl = nfsm_build(&info, 4 * NFSX_UNSIGNED);
				txdr_hyper(at.va_filerev, tl);
				tl += 2;
			} else
				tl = nfsm_build(&info, 2 * NFSX_UNSIGNED);
			*tl++ = nfs_false;
			*tl = nfs_true;
			kfree((caddr_t)rbuf, M_TEMP);
			kfree((caddr_t)cookies, M_TEMP);
			error = 0;
			goto nfsmout;
		}
	}

	/*
	 * Check for degenerate cases of nothing useful read.
	 * If so go try again
	 */
	cpos = rbuf;
	cend = rbuf + siz;
	dp = (struct dirent *)cpos;
	cookiep = cookies;
	/*
	 * For some reason FreeBSD's ufs_readdir() chooses to back the
	 * directory offset up to a block boundary, so it is necessary to
	 * skip over the records that preceed the requested offset. This
	 * requires the assumption that file offset cookies monotonically
	 * increase.
	 */
	while (cpos < cend && ncookies > 0 &&
		(dp->d_ino == 0 || dp->d_type == DT_WHT ||
		 ((u_quad_t)(*cookiep)) <= toff)) {
		dp = _DIRENT_NEXT(dp);
		cpos = (char *)dp;
		cookiep++;
		ncookies--;
	}
	if (cpos >= cend || ncookies == 0) {
		toff = off;
		siz = fullsiz;
		goto again;
	}

	len = 3 * NFSX_UNSIGNED;	/* paranoia, probably can be 0 */
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_POSTOPATTR(info.v3) +
			      NFSX_COOKIEVERF(info.v3) + siz,
			      &error));
	if (info.v3) {
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		tl = nfsm_build(&info, 2 * NFSX_UNSIGNED);
		txdr_hyper(at.va_filerev, tl);
	}
	mp1 = mp2 = info.mb;
	bp = info.bpos;
	be = bp + M_TRAILINGSPACE(mp1);

	/* Loop through the records and build reply */
	while (cpos < cend && ncookies > 0) {
		if (dp->d_ino != 0 && dp->d_type != DT_WHT) {
			nlen = dp->d_namlen;
			rem = nfsm_rndup(nlen) - nlen;
			len += (4 * NFSX_UNSIGNED + nlen + rem);
			if (info.v3)
				len += 2 * NFSX_UNSIGNED;
			if (len > cnt) {
				eofflag = 0;
				break;
			}
			/*
			 * Build the directory record xdr from
			 * the dirent entry.
			 */
			tl = nfsm_clget(&info, mp1, mp2, bp, be);
			*tl = nfs_true;
			bp += NFSX_UNSIGNED;
			if (info.v3) {
				tl = nfsm_clget(&info, mp1, mp2, bp, be);
				*tl = txdr_unsigned(dp->d_ino >> 32);
				bp += NFSX_UNSIGNED;
			}
			tl = nfsm_clget(&info, mp1, mp2, bp, be);
			*tl = txdr_unsigned(dp->d_ino);
			bp += NFSX_UNSIGNED;
			tl = nfsm_clget(&info, mp1, mp2, bp, be);
			*tl = txdr_unsigned(nlen);
			bp += NFSX_UNSIGNED;

			/* And loop around copying the name */
			xfer = nlen;
			cp = dp->d_name;
			while (xfer > 0) {
				tl = nfsm_clget(&info, mp1, mp2, bp, be);
				if ((bp+xfer) > be)
					tsiz = be-bp;
				else
					tsiz = xfer;
				bcopy(cp, bp, tsiz);
				bp += tsiz;
				xfer -= tsiz;
				if (xfer > 0)
					cp += tsiz;
			}
			/* And null pad to a int32_t boundary */
			for (i = 0; i < rem; i++)
				*bp++ = '\0';
			tl = nfsm_clget(&info, mp1, mp2, bp, be);

			/* Finish off the record */
			if (info.v3) {
				*tl = txdr_unsigned(*cookiep >> 32);
				bp += NFSX_UNSIGNED;
				tl = nfsm_clget(&info, mp1, mp2, bp, be);
			}
			*tl = txdr_unsigned(*cookiep);
			bp += NFSX_UNSIGNED;
		}
		dp = _DIRENT_NEXT(dp);
		cpos = (char *)dp;
		cookiep++;
		ncookies--;
	}
	vrele(vp);
	vp = NULL;
	tl = nfsm_clget(&info, mp1, mp2, bp, be);
	*tl = nfs_false;
	bp += NFSX_UNSIGNED;
	tl = nfsm_clget(&info, mp1, mp2, bp, be);
	if (eofflag)
		*tl = nfs_true;
	else
		*tl = nfs_false;
	bp += NFSX_UNSIGNED;
	if (mp1 != info.mb) {
		if (bp < be)
			mp1->m_len = bp - mtod(mp1, caddr_t);
	} else
		mp1->m_len += bp - info.bpos;
	kfree((caddr_t)rbuf, M_TEMP);
	kfree((caddr_t)cookies, M_TEMP);

nfsmout:
	*mrq = info.mreq;
	if (vp)
		vrele(vp);
	return(error);
}

int
nfsrv_readdirplus(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
		  struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	char *bp, *be;
	struct dirent *dp;
	caddr_t cp;
	u_int32_t *tl;
	struct mbuf *mp1, *mp2;
	char *cpos, *cend, *rbuf;
	struct vnode *vp = NULL, *nvp;
	struct mount *mp = NULL;
	struct flrep fl;
	nfsfh_t nfh;
	fhandle_t *fhp, *nfhp = (fhandle_t *)fl.fl_nfh;
	struct uio io;
	struct iovec iv;
	struct vattr va, at, *vap = &va;
	struct nfs_fattr *fp;
	int len, nlen, rem, xfer, tsiz, i, error = 0, getret = 1;
	int siz, cnt, fullsiz, eofflag, rdonly, dirlen, ncookies;
	u_quad_t off, toff;
#if 0
	u_quad_t verf;
#endif
	off_t *cookies = NULL, *cookiep; /* needs to be int64_t or off_t */
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NULLOUT(tl = nfsm_dissect(&info, 6 * NFSX_UNSIGNED));
	toff = fxdr_hyper(tl);
	tl += 2;
#if 0
	verf = fxdr_hyper(tl);
#endif
	tl += 2;
	siz = fxdr_unsigned(int, *tl++);
	cnt = fxdr_unsigned(int, *tl);
	off = toff;
	siz = ((siz + DIRBLKSIZ - 1) & ~(DIRBLKSIZ - 1));
	xfer = NFS_SRVMAXDATA(nfsd);
	if ((unsigned)cnt > xfer)
		cnt = xfer;
	if ((unsigned)siz > xfer)
		siz = xfer;
	fullsiz = siz;
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
			     &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (!error && vp->v_type != VDIR) {
		error = ENOTDIR;
		vput(vp);
		vp = NULL;
	}
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	error = getret = VOP_GETATTR(vp, &at);
#if 0
	/*
	 * XXX This check may be too strict for Solaris 2.5 clients.
	 */
	if (!error && toff && verf && verf != at.va_filerev)
		error = NFSERR_BAD_COOKIE;
#endif
	if (!error) {
		error = nfsrv_access(mp, vp, VEXEC, cred, rdonly, td, 0);
	}
	if (error) {
		vput(vp);
		vp = NULL;
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_V3POSTOPATTR, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	vn_unlock(vp);
	rbuf = kmalloc(siz, M_TEMP, M_WAITOK);
again:
	iv.iov_base = rbuf;
	iv.iov_len = fullsiz;
	io.uio_iov = &iv;
	io.uio_iovcnt = 1;
	io.uio_offset = (off_t)off;
	io.uio_resid = fullsiz;
	io.uio_segflg = UIO_SYSSPACE;
	io.uio_rw = UIO_READ;
	io.uio_td = NULL;
	eofflag = 0;
	if (cookies) {
		kfree((caddr_t)cookies, M_TEMP);
		cookies = NULL;
	}
	error = VOP_READDIR(vp, &io, cred, &eofflag, &ncookies, &cookies);
	off = (u_quad_t)io.uio_offset;
	getret = VOP_GETATTR(vp, &at);
	if (!cookies && !error)
		error = NFSERR_PERM;
	if (!error)
		error = getret;
	if (error) {
		vrele(vp);
		vp = NULL;
		if (cookies)
			kfree((caddr_t)cookies, M_TEMP);
		kfree((caddr_t)rbuf, M_TEMP);
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_V3POSTOPATTR, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	if (io.uio_resid) {
		siz -= io.uio_resid;

		/*
		 * If nothing read, return eof
		 * rpc reply
		 */
		if (siz == 0) {
			vrele(vp);
			vp = NULL;
			NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
					      NFSX_V3POSTOPATTR +
					      NFSX_V3COOKIEVERF +
					      2 * NFSX_UNSIGNED,
					      &error));
			nfsm_srvpostop_attr(&info, nfsd, getret, &at);
			tl = nfsm_build(&info, 4 * NFSX_UNSIGNED);
			txdr_hyper(at.va_filerev, tl);
			tl += 2;
			*tl++ = nfs_false;
			*tl = nfs_true;
			kfree((caddr_t)cookies, M_TEMP);
			kfree((caddr_t)rbuf, M_TEMP);
			error = 0;
			goto nfsmout;
		}
	}

	/*
	 * Check for degenerate cases of nothing useful read.
	 * If so go try again
	 */
	cpos = rbuf;
	cend = rbuf + siz;
	dp = (struct dirent *)cpos;
	cookiep = cookies;
	/*
	 * For some reason FreeBSD's ufs_readdir() chooses to back the
	 * directory offset up to a block boundary, so it is necessary to
	 * skip over the records that preceed the requested offset. This
	 * requires the assumption that file offset cookies monotonically
	 * increase.
	 */
	while (cpos < cend && ncookies > 0 &&
		(dp->d_ino == 0 || dp->d_type == DT_WHT ||
		 ((u_quad_t)(*cookiep)) <= toff)) {
		dp = _DIRENT_NEXT(dp);
		cpos = (char *)dp;
		cookiep++;
		ncookies--;
	}
	if (cpos >= cend || ncookies == 0) {
		toff = off;
		siz = fullsiz;
		goto again;
	}

	/*
	 * Probe one of the directory entries to see if the filesystem
	 * supports VGET.
	 */
	if (VFS_VGET(vp->v_mount, vp, dp->d_ino, &nvp) == EOPNOTSUPP) {
		error = NFSERR_NOTSUPP;
		vrele(vp);
		vp = NULL;
		kfree((caddr_t)cookies, M_TEMP);
		kfree((caddr_t)rbuf, M_TEMP);
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      NFSX_V3POSTOPATTR, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	if (nvp) {
		vput(nvp);
		nvp = NULL;
	}
	    
	dirlen = len = NFSX_V3POSTOPATTR + NFSX_V3COOKIEVERF +
			2 * NFSX_UNSIGNED;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, cnt, &error));
	nfsm_srvpostop_attr(&info, nfsd, getret, &at);
	tl = nfsm_build(&info, 2 * NFSX_UNSIGNED);
	txdr_hyper(at.va_filerev, tl);
	mp1 = mp2 = info.mb;
	bp = info.bpos;
	be = bp + M_TRAILINGSPACE(mp1);

	/* Loop through the records and build reply */
	while (cpos < cend && ncookies > 0) {
		if (dp->d_ino != 0 && dp->d_type != DT_WHT) {
			nlen = dp->d_namlen;
			rem = nfsm_rndup(nlen) - nlen;

			/*
			 * For readdir_and_lookup get the vnode using
			 * the file number.
			 */
			if (VFS_VGET(vp->v_mount, vp, dp->d_ino, &nvp))
				goto invalid;
			bzero((caddr_t)nfhp, NFSX_V3FH);
			nfhp->fh_fsid = fhp->fh_fsid;
			if (VFS_VPTOFH(nvp, &nfhp->fh_fid)) {
				vput(nvp);
				nvp = NULL;
				goto invalid;
			}
			if (VOP_GETATTR(nvp, vap)) {
				vput(nvp);
				nvp = NULL;
				goto invalid;
			}
			vput(nvp);
			nvp = NULL;

			/*
			 * If either the dircount or maxcount will be
			 * exceeded, get out now. Both of these lengths
			 * are calculated conservatively, including all
			 * XDR overheads.
			 */
			len += (8 * NFSX_UNSIGNED + nlen + rem + NFSX_V3FH +
				NFSX_V3POSTOPATTR);
			dirlen += (6 * NFSX_UNSIGNED + nlen + rem);
			if (len > cnt || dirlen > fullsiz) {
				eofflag = 0;
				break;
			}

			/*
			 * Build the directory record xdr from
			 * the dirent entry.
			 */
			fp = (struct nfs_fattr *)&fl.fl_fattr;
			nfsm_srvfattr(nfsd, vap, fp);
			fl.fl_off.nfsuquad[0] = txdr_unsigned(*cookiep >> 32);
			fl.fl_off.nfsuquad[1] = txdr_unsigned(*cookiep);
			fl.fl_postopok = nfs_true;
			fl.fl_fhok = nfs_true;
			fl.fl_fhsize = txdr_unsigned(NFSX_V3FH);

			tl = nfsm_clget(&info, mp1, mp2, bp, be);
			*tl = nfs_true;
			bp += NFSX_UNSIGNED;
			tl = nfsm_clget(&info, mp1, mp2, bp, be);
			*tl = txdr_unsigned(dp->d_ino >> 32);
			bp += NFSX_UNSIGNED;
			tl = nfsm_clget(&info, mp1, mp2, bp, be);
			*tl = txdr_unsigned(dp->d_ino);
			bp += NFSX_UNSIGNED;
			tl = nfsm_clget(&info, mp1, mp2, bp, be);
			*tl = txdr_unsigned(nlen);
			bp += NFSX_UNSIGNED;

			/* And loop around copying the name */
			xfer = nlen;
			cp = dp->d_name;
			while (xfer > 0) {
				tl = nfsm_clget(&info, mp1, mp2, bp, be);
				if ((bp + xfer) > be)
					tsiz = be - bp;
				else
					tsiz = xfer;
				bcopy(cp, bp, tsiz);
				bp += tsiz;
				xfer -= tsiz;
				cp += tsiz;
			}
			/* And null pad to a int32_t boundary */
			for (i = 0; i < rem; i++)
				*bp++ = '\0';
	
			/*
			 * Now copy the flrep structure out.
			 */
			xfer = sizeof (struct flrep);
			cp = (caddr_t)&fl;
			while (xfer > 0) {
				tl = nfsm_clget(&info, mp1, mp2, bp, be);
				if ((bp + xfer) > be)
					tsiz = be - bp;
				else
					tsiz = xfer;
				bcopy(cp, bp, tsiz);
				bp += tsiz;
				xfer -= tsiz;
				cp += tsiz;
			}
		}
invalid:
		dp = _DIRENT_NEXT(dp);
		cpos = (char *)dp;
		cookiep++;
		ncookies--;
	}
	vrele(vp);
	vp = NULL;
	tl = nfsm_clget(&info, mp1, mp2, bp, be);
	*tl = nfs_false;
	bp += NFSX_UNSIGNED;
	tl = nfsm_clget(&info, mp1, mp2, bp, be);
	if (eofflag)
		*tl = nfs_true;
	else
		*tl = nfs_false;
	bp += NFSX_UNSIGNED;
	if (mp1 != info.mb) {
		if (bp < be)
			mp1->m_len = bp - mtod(mp1, caddr_t);
	} else
		mp1->m_len += bp - info.bpos;
	kfree((caddr_t)cookies, M_TEMP);
	kfree((caddr_t)rbuf, M_TEMP);
nfsmout:
	*mrq = info.mreq;
	if (vp)
		vrele(vp);
	return(error);
}

/*
 * nfs commit service
 */
int
nfsrv_commit(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	     struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct vattr bfor, aft;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	nfsfh_t nfh;
	fhandle_t *fhp;
	u_int32_t *tl;
	int error = 0, rdonly, for_ret = 1, aft_ret = 1, cnt;
	u_quad_t off;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));

	/*
	 * XXX At this time VOP_FSYNC() does not accept offset and byte
	 * count parameters, so these arguments are useless (someday maybe).
	 */
	off = fxdr_hyper(tl);
	tl += 2;
	cnt = fxdr_unsigned(int, *tl);
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
				      2 * NFSX_UNSIGNED, &error));
		nfsm_srvwcc_data(&info, nfsd, for_ret, &bfor,
				 aft_ret, &aft);
		error = 0;
		goto nfsmout;
	}
	for_ret = VOP_GETATTR(vp, &bfor);

	/*
	 * RFC 1813 3.3.21: If count is 0, a flush from offset to the end of
	 * file is done. At this time VOP_FSYNC does not accept offset and
	 * byte count parameters, so call VOP_FSYNC the whole file for now.
	 */
	if (cnt == 0 || cnt > MAX_COMMIT_COUNT) {
		/*
		 * Give up and do the whole thing
		 */
		if (vp->v_object &&
		   (vp->v_object->flags & OBJ_MIGHTBEDIRTY)) {
			vm_object_page_clean(vp->v_object, 0, 0, OBJPC_SYNC);
		}
		error = VOP_FSYNC(vp, MNT_WAIT, 0);
	} else {
		/*
		 * Locate and synchronously write any buffers that fall
		 * into the requested range.  Note:  we are assuming that
		 * f_iosize is a power of 2.
		 */
		int iosize = vp->v_mount->mnt_stat.f_iosize;
		int iomask = iosize - 1;
		off_t loffset;

		/*
		 * Align to iosize boundry, super-align to page boundry.
		 */
		if (off & iomask) {
			cnt += off & iomask;
			off &= ~(u_quad_t)iomask;
		}
		if (off & PAGE_MASK) {
			cnt += off & PAGE_MASK;
			off &= ~(u_quad_t)PAGE_MASK;
		}
		loffset = off;

		if (vp->v_object &&
		   (vp->v_object->flags & OBJ_MIGHTBEDIRTY)) {
			vm_object_page_clean(vp->v_object, off / PAGE_SIZE,
			    (cnt + PAGE_MASK) / PAGE_SIZE, OBJPC_SYNC);
		}

		crit_enter();
		while (error == 0 || cnt > 0) {
			struct buf *bp;

			/*
			 * If we have a buffer and it is marked B_DELWRI we
			 * have to lock and write it.  Otherwise the prior
			 * write is assumed to have already been committed.
			 *
			 * WARNING: FINDBLK_TEST buffers represent stable
			 *	    storage but not necessarily stable
			 *	    content.  It is ok in this case.
			 */
			if ((bp = findblk(vp, loffset, FINDBLK_TEST)) != NULL) {
				if (bp->b_flags & B_DELWRI)
					bp = findblk(vp, loffset, 0);
				else
					bp = NULL;
			}
			if (bp) {
				if (bp->b_flags & B_DELWRI) {
					bremfree(bp);
					error = bwrite(bp);
					++nfs_commit_miss;
				} else {
					BUF_UNLOCK(bp);
				}
			}
			++nfs_commit_blks;
			if (cnt < iosize)
				break;
			cnt -= iosize;
			loffset += iosize;
		}
		crit_exit();
	}

	aft_ret = VOP_GETATTR(vp, &aft);
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_V3WCCDATA + NFSX_V3WRITEVERF,
			      &error));
	nfsm_srvwcc_data(&info, nfsd, for_ret, &bfor,
			 aft_ret, &aft);
	if (!error) {
		tl = nfsm_build(&info, NFSX_V3WRITEVERF);
		if (nfsver.tv_sec == 0)
			nfsver = boottime;
		*tl++ = txdr_unsigned(nfsver.tv_sec);
		*tl = txdr_unsigned(nfsver.tv_nsec / 1000);
	} else {
		error = 0;
	}
nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs statfs service
 */
int
nfsrv_statfs(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	     struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct statfs *sf;
	struct nfs_statfs *sfp;
	int error = 0, rdonly, getret = 1;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	struct vattr at;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct statfs statfs;
	u_quad_t tval;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;
	info.v3 = (nfsd->nd_flag & ND_NFSV3);

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	sf = &statfs;
	error = VFS_STATFS(vp->v_mount, sf, proc0.p_ucred);
	getret = VOP_GETATTR(vp, &at);
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_POSTOPATTR(info.v3) + NFSX_STATFS(info.v3),
			      &error));
	if (info.v3)
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
	if (error) {
		error = 0;
		goto nfsmout;
	}
	sfp = nfsm_build(&info, NFSX_STATFS(info.v3));
	if (info.v3) {
		tval = (u_quad_t)sf->f_blocks;
		tval *= (u_quad_t)sf->f_bsize;
		txdr_hyper(tval, &sfp->sf_tbytes);
		tval = (u_quad_t)sf->f_bfree;
		tval *= (u_quad_t)sf->f_bsize;
		txdr_hyper(tval, &sfp->sf_fbytes);
		tval = (u_quad_t)sf->f_bavail;
		tval *= (u_quad_t)sf->f_bsize;
		txdr_hyper(tval, &sfp->sf_abytes);
		sfp->sf_tfiles.nfsuquad[0] = 0;
		sfp->sf_tfiles.nfsuquad[1] = txdr_unsigned(sf->f_files);
		sfp->sf_ffiles.nfsuquad[0] = 0;
		sfp->sf_ffiles.nfsuquad[1] = txdr_unsigned(sf->f_ffree);
		sfp->sf_afiles.nfsuquad[0] = 0;
		sfp->sf_afiles.nfsuquad[1] = txdr_unsigned(sf->f_ffree);
		sfp->sf_invarsec = 0;
	} else {
		sfp->sf_tsize = txdr_unsigned(NFS_MAXDGRAMDATA);
		sfp->sf_bsize = txdr_unsigned(sf->f_bsize);
		sfp->sf_blocks = txdr_unsigned(sf->f_blocks);
		sfp->sf_bfree = txdr_unsigned(sf->f_bfree);
		sfp->sf_bavail = txdr_unsigned(sf->f_bavail);
	}
nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs fsinfo service
 */
int
nfsrv_fsinfo(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	     struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct nfsv3_fsinfo *sip;
	int error = 0, rdonly, getret = 1, pref;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	struct vattr at;
	nfsfh_t nfh;
	fhandle_t *fhp;
	u_quad_t maxfsize;
	struct statfs sb;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}

	/* XXX Try to make a guess on the max file size. */
	VFS_STATFS(vp->v_mount, &sb, proc0.p_ucred);
	maxfsize = (u_quad_t)0x80000000 * sb.f_bsize - 1;

	getret = VOP_GETATTR(vp, &at);
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_V3POSTOPATTR + NFSX_V3FSINFO, &error));
	nfsm_srvpostop_attr(&info, nfsd, getret, &at);
	sip = nfsm_build(&info, NFSX_V3FSINFO);

	/*
	 * XXX
	 * There should be file system VFS OP(s) to get this information.
	 * For now, assume ufs.
	 */
	if (slp->ns_so->so_type == SOCK_DGRAM)
		pref = NFS_MAXDGRAMDATA;
	else
		pref = NFS_MAXDATA;
	sip->fs_rtmax = txdr_unsigned(NFS_MAXDATA);
	sip->fs_rtpref = txdr_unsigned(pref);
	sip->fs_rtmult = txdr_unsigned(NFS_FABLKSIZE);
	sip->fs_wtmax = txdr_unsigned(NFS_MAXDATA);
	sip->fs_wtpref = txdr_unsigned(pref);
	sip->fs_wtmult = txdr_unsigned(NFS_FABLKSIZE);
	sip->fs_dtpref = txdr_unsigned(pref);
	txdr_hyper(maxfsize, &sip->fs_maxfilesize);
	sip->fs_timedelta.nfsv3_sec = 0;
	sip->fs_timedelta.nfsv3_nsec = txdr_unsigned(1);
	sip->fs_properties = txdr_unsigned(NFSV3FSINFO_LINK |
		NFSV3FSINFO_SYMLINK | NFSV3FSINFO_HOMOGENEOUS |
		NFSV3FSINFO_CANSETTIME);
nfsmout:
	*mrq = info.mreq;
	if (vp)
		vput(vp);
	return(error);
}

/*
 * nfs pathconf service
 */
int
nfsrv_pathconf(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	       struct thread *td, struct mbuf **mrq)
{
	struct sockaddr *nam = nfsd->nd_nam;
	struct ucred *cred = &nfsd->nd_cr;
	struct nfsv3_pathconf *pc;
	int error = 0, rdonly, getret = 1;
	register_t linkmax, namemax, chownres, notrunc;
	struct vnode *vp = NULL;
	struct mount *mp = NULL;
	struct vattr at;
	nfsfh_t nfh;
	fhandle_t *fhp;
	struct nfsm_info info;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;
	info.md = nfsd->nd_md;
	info.dpos = nfsd->nd_dpos;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	fhp = &nfh.fh_generic;
	NEGREPLYOUT(nfsm_srvmtofh(&info, nfsd, fhp, &error));
	error = nfsrv_fhtovp(fhp, 1, &mp, &vp, cred, slp, nam,
		 &rdonly, (nfsd->nd_flag & ND_KERBAUTH), TRUE);
	if (error) {
		NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, NFSX_UNSIGNED, &error));
		nfsm_srvpostop_attr(&info, nfsd, getret, &at);
		error = 0;
		goto nfsmout;
	}
	error = VOP_PATHCONF(vp, _PC_LINK_MAX, &linkmax);
	if (!error)
		error = VOP_PATHCONF(vp, _PC_NAME_MAX, &namemax);
	if (!error)
		error = VOP_PATHCONF(vp, _PC_CHOWN_RESTRICTED, &chownres);
	if (!error)
		error = VOP_PATHCONF(vp, _PC_NO_TRUNC, &notrunc);
	getret = VOP_GETATTR(vp, &at);
	vput(vp);
	vp = NULL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp,
			      NFSX_V3POSTOPATTR + NFSX_V3PATHCONF,
			      &error));
	nfsm_srvpostop_attr(&info, nfsd, getret, &at);
	if (error) {
		error = 0;
		goto nfsmout;
	}
	pc = nfsm_build(&info, NFSX_V3PATHCONF);

	pc->pc_linkmax = txdr_unsigned(linkmax);
	pc->pc_namemax = txdr_unsigned(namemax);
	pc->pc_notrunc = txdr_unsigned(notrunc);
	pc->pc_chownrestricted = txdr_unsigned(chownres);

	/*
	 * These should probably be supported by VOP_PATHCONF(), but
	 * until msdosfs is exportable (why would you want to?), the
	 * Unix defaults should be ok.
	 */
	pc->pc_caseinsensitive = nfs_false;
	pc->pc_casepreserving = nfs_true;
nfsmout:
	*mrq = info.mreq;
	if (vp)	
		vput(vp);
	return(error);
}

/*
 * Null operation, used by clients to ping server
 */
/* ARGSUSED */
int
nfsrv_null(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	   struct thread *td, struct mbuf **mrq)
{
	struct nfsm_info info;
	int error = NFSERR_RETVOID;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, 0, &error));
nfsmout:
	*mrq = info.mreq;
	return (error);
}

/*
 * No operation, used for obsolete procedures
 */
/* ARGSUSED */
int
nfsrv_noop(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	   struct thread *td, struct mbuf **mrq)
{
	struct nfsm_info info;
	int error;

	info.mrep = nfsd->nd_mrep;
	info.mreq = NULL;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	if (nfsd->nd_repstat)
		error = nfsd->nd_repstat;
	else
		error = EPROCUNAVAIL;
	NEGKEEPOUT(nfsm_reply(&info, nfsd, slp, 0, &error));
	error = 0;
nfsmout:
	*mrq = info.mreq;
	return (error);
}

/*
 * Perform access checking for vnodes obtained from file handles that would
 * refer to files already opened by a Unix client. You cannot just use
 * vn_writechk() and VOP_ACCESS() for two reasons.
 * 1 - You must check for exported rdonly as well as MNT_RDONLY for the write case
 * 2 - The owner is to be given access irrespective of mode bits for some
 *     operations, so that processes that chmod after opening a file don't
 *     break. I don't like this because it opens a security hole, but since
 *     the nfs server opens a security hole the size of a barn door anyhow,
 *     what the heck.
 *
 * The exception to rule 2 is EPERM. If a file is IMMUTABLE, VOP_ACCESS()
 * will return EPERM instead of EACCESS. EPERM is always an error.
 */
static int
nfsrv_access(struct mount *mp, struct vnode *vp, int flags, struct ucred *cred,
	     int rdonly, struct thread *td, int override)
{
	struct vattr vattr;
	int error;

	nfsdbprintf(("%s %d\n", __FILE__, __LINE__));
	if (flags & VWRITE) {
		/* Just vn_writechk() changed to check rdonly */
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket or a block or character
		 * device resident on the file system.
		 */
		if (rdonly || 
		    ((mp->mnt_flag | vp->v_mount->mnt_flag) & MNT_RDONLY)) {
			switch (vp->v_type) {
			case VREG:
			case VDIR:
			case VLNK:
				return (EROFS);
			default:
				break;
			}
		}
		/*
		 * If there's shared text associated with
		 * the inode, we can't allow writing.
		 */
		if (vp->v_flag & VTEXT)
			return (ETXTBSY);
	}
	error = VOP_GETATTR(vp, &vattr);
	if (error)
		return (error);
	error = VOP_ACCESS(vp, flags, cred);	/* XXX ruid/rgid vs uid/gid */
	/*
	 * Allow certain operations for the owner (reads and writes
	 * on files that are already open).
	 */
	if (override && error == EACCES && cred->cr_uid == vattr.va_uid)
		error = 0;
	return error;
}
#endif /* NFS_NOSERVER */
