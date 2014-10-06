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
 *	@(#)nfs_vnops.c	8.16 (Berkeley) 5/27/95
 * $FreeBSD: src/sys/nfs/nfs_vnops.c,v 1.150.2.5 2001/12/20 19:56:28 dillon Exp $
 */


/*
 * vnode op calls for Sun NFS version 2 and 3
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/socket.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/lockf.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/conf.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>

#include <vfs/fifofs/fifo.h>
#include <vfs/ufs/dir.h>

#undef DIRBLKSIZ

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>

#include <sys/thread2.h>

/* Defs */
#define	TRUE	1
#define	FALSE	0

static int	nfsfifo_read (struct vop_read_args *);
static int	nfsfifo_write (struct vop_write_args *);
static int	nfsfifo_close (struct vop_close_args *);
static int	nfs_setattrrpc (struct vnode *,struct vattr *,struct ucred *,struct thread *);
static	int	nfs_lookup (struct vop_old_lookup_args *);
static	int	nfs_create (struct vop_old_create_args *);
static	int	nfs_mknod (struct vop_old_mknod_args *);
static	int	nfs_open (struct vop_open_args *);
static	int	nfs_close (struct vop_close_args *);
static	int	nfs_access (struct vop_access_args *);
static	int	nfs_getattr (struct vop_getattr_args *);
static	int	nfs_setattr (struct vop_setattr_args *);
static	int	nfs_read (struct vop_read_args *);
static	int	nfs_mmap (struct vop_mmap_args *);
static	int	nfs_fsync (struct vop_fsync_args *);
static	int	nfs_remove (struct vop_old_remove_args *);
static	int	nfs_link (struct vop_old_link_args *);
static	int	nfs_rename (struct vop_old_rename_args *);
static	int	nfs_mkdir (struct vop_old_mkdir_args *);
static	int	nfs_rmdir (struct vop_old_rmdir_args *);
static	int	nfs_symlink (struct vop_old_symlink_args *);
static	int	nfs_readdir (struct vop_readdir_args *);
static	int	nfs_bmap (struct vop_bmap_args *);
static	int	nfs_strategy (struct vop_strategy_args *);
static	int	nfs_lookitup (struct vnode *, const char *, int,
			struct ucred *, struct thread *, struct nfsnode **);
static	int	nfs_sillyrename (struct vnode *,struct vnode *,struct componentname *);
static int	nfs_laccess (struct vop_access_args *);
static int	nfs_readlink (struct vop_readlink_args *);
static int	nfs_print (struct vop_print_args *);
static int	nfs_advlock (struct vop_advlock_args *);

static	int	nfs_nresolve (struct vop_nresolve_args *);
/*
 * Global vfs data structures for nfs
 */
struct vop_ops nfsv2_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		nfs_access,
	.vop_advlock =		nfs_advlock,
	.vop_bmap =		nfs_bmap,
	.vop_close =		nfs_close,
	.vop_old_create =	nfs_create,
	.vop_fsync =		nfs_fsync,
	.vop_getattr =		nfs_getattr,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_inactive =		nfs_inactive,
	.vop_old_link =		nfs_link,
	.vop_old_lookup =	nfs_lookup,
	.vop_old_mkdir =	nfs_mkdir,
	.vop_old_mknod =	nfs_mknod,
	.vop_mmap =		nfs_mmap,
	.vop_open =		nfs_open,
	.vop_print =		nfs_print,
	.vop_read =		nfs_read,
	.vop_readdir =		nfs_readdir,
	.vop_readlink =		nfs_readlink,
	.vop_reclaim =		nfs_reclaim,
	.vop_old_remove =	nfs_remove,
	.vop_old_rename =	nfs_rename,
	.vop_old_rmdir =	nfs_rmdir,
	.vop_setattr =		nfs_setattr,
	.vop_strategy =		nfs_strategy,
	.vop_old_symlink =	nfs_symlink,
	.vop_write =		nfs_write,
	.vop_nresolve =		nfs_nresolve
};

/*
 * Special device vnode ops
 */
struct vop_ops nfsv2_spec_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		nfs_laccess,
	.vop_close =		nfs_close,
	.vop_fsync =		nfs_fsync,
	.vop_getattr =		nfs_getattr,
	.vop_inactive =		nfs_inactive,
	.vop_print =		nfs_print,
	.vop_read =		vop_stdnoread,
	.vop_reclaim =		nfs_reclaim,
	.vop_setattr =		nfs_setattr,
	.vop_write =		vop_stdnowrite
};

struct vop_ops nfsv2_fifo_vops = {
	.vop_default =		fifo_vnoperate,
	.vop_access =		nfs_laccess,
	.vop_close =		nfsfifo_close,
	.vop_fsync =		nfs_fsync,
	.vop_getattr =		nfs_getattr,
	.vop_inactive =		nfs_inactive,
	.vop_print =		nfs_print,
	.vop_read =		nfsfifo_read,
	.vop_reclaim =		nfs_reclaim,
	.vop_setattr =		nfs_setattr,
	.vop_write =		nfsfifo_write
};

static int	nfs_mknodrpc (struct vnode *dvp, struct vnode **vpp,
				  struct componentname *cnp,
				  struct vattr *vap);
static int	nfs_removerpc (struct vnode *dvp, const char *name,
				   int namelen,
				   struct ucred *cred, struct thread *td);
static int	nfs_renamerpc (struct vnode *fdvp, const char *fnameptr,
				   int fnamelen, struct vnode *tdvp,
				   const char *tnameptr, int tnamelen,
				   struct ucred *cred, struct thread *td);
static int	nfs_renameit (struct vnode *sdvp,
				  struct componentname *scnp,
				  struct sillyrename *sp);

SYSCTL_DECL(_vfs_nfs);

static int nfs_flush_on_rename = 1;
SYSCTL_INT(_vfs_nfs, OID_AUTO, flush_on_rename, CTLFLAG_RW, 
	   &nfs_flush_on_rename, 0, "flush fvp prior to rename");
static int nfs_flush_on_hlink = 0;
SYSCTL_INT(_vfs_nfs, OID_AUTO, flush_on_hlink, CTLFLAG_RW, 
	   &nfs_flush_on_hlink, 0, "flush fvp prior to hard link");

static int	nfsaccess_cache_timeout = NFS_DEFATTRTIMO;
SYSCTL_INT(_vfs_nfs, OID_AUTO, access_cache_timeout, CTLFLAG_RW, 
	   &nfsaccess_cache_timeout, 0, "NFS ACCESS cache timeout");

static int	nfsneg_cache_timeout = NFS_MINATTRTIMO;
SYSCTL_INT(_vfs_nfs, OID_AUTO, neg_cache_timeout, CTLFLAG_RW, 
	   &nfsneg_cache_timeout, 0, "NFS NEGATIVE NAMECACHE timeout");

static int	nfspos_cache_timeout = NFS_MINATTRTIMO;
SYSCTL_INT(_vfs_nfs, OID_AUTO, pos_cache_timeout, CTLFLAG_RW, 
	   &nfspos_cache_timeout, 0, "NFS POSITIVE NAMECACHE timeout");

static int	nfsv3_commit_on_close = 0;
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfsv3_commit_on_close, CTLFLAG_RW, 
	   &nfsv3_commit_on_close, 0, "write+commit on close, else only write");
#if 0
SYSCTL_INT(_vfs_nfs, OID_AUTO, access_cache_hits, CTLFLAG_RD, 
	   &nfsstats.accesscache_hits, 0, "NFS ACCESS cache hit count");

SYSCTL_INT(_vfs_nfs, OID_AUTO, access_cache_misses, CTLFLAG_RD, 
	   &nfsstats.accesscache_misses, 0, "NFS ACCESS cache miss count");
#endif

#define	NFSV3ACCESS_ALL (NFSV3ACCESS_READ | NFSV3ACCESS_MODIFY		\
			 | NFSV3ACCESS_EXTEND | NFSV3ACCESS_EXECUTE	\
			 | NFSV3ACCESS_DELETE | NFSV3ACCESS_LOOKUP)

/*
 * Returns whether a name component is a degenerate '.' or '..'.
 */
static __inline
int
nlcdegenerate(struct nlcomponent *nlc)
{
	if (nlc->nlc_namelen == 1 && nlc->nlc_nameptr[0] == '.')
		return(1);
	if (nlc->nlc_namelen == 2 &&
	    nlc->nlc_nameptr[0] == '.' && nlc->nlc_nameptr[1] == '.')
		return(1);
	return(0);
}

static int
nfs3_access_otw(struct vnode *vp, int wmode,
		struct thread *td, struct ucred *cred)
{
	struct nfsnode *np = VTONFS(vp);
	int attrflag;
	int error = 0;
	u_int32_t *tl;
	u_int32_t rmode;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = 1;

	nfsstats.rpccnt[NFSPROC_ACCESS]++;
	nfsm_reqhead(&info, vp, NFSPROC_ACCESS,
		     NFSX_FH(info.v3) + NFSX_UNSIGNED);
	ERROROUT(nfsm_fhtom(&info, vp));
	tl = nfsm_build(&info, NFSX_UNSIGNED);
	*tl = txdr_unsigned(wmode); 
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_ACCESS, td, cred, &error));
	ERROROUT(nfsm_postop_attr(&info, vp, &attrflag, NFS_LATTR_NOSHRINK));
	if (error == 0) {
		NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
		rmode = fxdr_unsigned(u_int32_t, *tl);
		np->n_mode = rmode;
		np->n_modeuid = cred->cr_uid;
		np->n_modestamp = mycpu->gd_time_seconds;
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	return error;
}

/*
 * nfs access vnode op.
 * For nfs version 2, just return ok. File accesses may fail later.
 * For nfs version 3, use the access rpc to check accessibility. If file modes
 * are changed on the server, accesses might still fail later.
 *
 * nfs_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
static int
nfs_access(struct vop_access_args *ap)
{
	struct ucred *cred;
	struct vnode *vp = ap->a_vp;
	thread_t td = curthread;
	int error = 0;
	u_int32_t mode, wmode;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int v3 = NFS_ISV3(vp);

	lwkt_gettoken(&nmp->nm_token);

	/*
	 * Disallow write attempts on filesystems mounted read-only;
	 * unless the file is a socket, fifo, or a block or character
	 * device resident on the filesystem.
	 */
	if ((ap->a_mode & VWRITE) && (vp->v_mount->mnt_flag & MNT_RDONLY)) {
		switch (vp->v_type) {
		case VREG:
		case VDIR:
		case VLNK:
			lwkt_reltoken(&nmp->nm_token);
			return (EROFS);
		default:
			break;
		}
	}

	/*
	 * The NFS protocol passes only the effective uid/gid over the wire but
	 * we need to check access against real ids if AT_EACCESS not set.
	 * Handle this case by cloning the credentials and setting the
	 * effective ids to the real ones.
	 */
	if (ap->a_flags & AT_EACCESS) {
		cred = crhold(ap->a_cred);
	} else {
		cred = crdup(ap->a_cred);
		cred->cr_uid = cred->cr_ruid;
		cred->cr_gid = cred->cr_rgid;
	}

	/*
	 * For nfs v3, check to see if we have done this recently, and if
	 * so return our cached result instead of making an ACCESS call.
	 * If not, do an access rpc, otherwise you are stuck emulating
	 * ufs_access() locally using the vattr. This may not be correct,
	 * since the server may apply other access criteria such as
	 * client uid-->server uid mapping that we do not know about.
	 */
	if (v3) {
		if (ap->a_mode & VREAD)
			mode = NFSV3ACCESS_READ;
		else
			mode = 0;
		if (vp->v_type != VDIR) {
			if (ap->a_mode & VWRITE)
				mode |= (NFSV3ACCESS_MODIFY | NFSV3ACCESS_EXTEND);
			if (ap->a_mode & VEXEC)
				mode |= NFSV3ACCESS_EXECUTE;
		} else {
			if (ap->a_mode & VWRITE)
				mode |= (NFSV3ACCESS_MODIFY | NFSV3ACCESS_EXTEND |
					 NFSV3ACCESS_DELETE);
			if (ap->a_mode & VEXEC)
				mode |= NFSV3ACCESS_LOOKUP;
		}
		/* XXX safety belt, only make blanket request if caching */
		if (nfsaccess_cache_timeout > 0) {
			wmode = NFSV3ACCESS_READ | NFSV3ACCESS_MODIFY | 
				NFSV3ACCESS_EXTEND | NFSV3ACCESS_EXECUTE | 
				NFSV3ACCESS_DELETE | NFSV3ACCESS_LOOKUP;
		} else {
			wmode = mode;
		}

		/*
		 * Does our cached result allow us to give a definite yes to
		 * this request?
		 */
		if (np->n_modestamp && 
		   (mycpu->gd_time_seconds < (np->n_modestamp + nfsaccess_cache_timeout)) &&
		   (cred->cr_uid == np->n_modeuid) &&
		   ((np->n_mode & mode) == mode)) {
			nfsstats.accesscache_hits++;
		} else {
			/*
			 * Either a no, or a don't know.  Go to the wire.
			 */
			nfsstats.accesscache_misses++;
		        error = nfs3_access_otw(vp, wmode, td, cred);
			if (!error) {
				if ((np->n_mode & mode) != mode) {
					error = EACCES;
				}
			}
		}
	} else {
		if ((error = nfs_laccess(ap)) != 0) {
			crfree(cred);
			lwkt_reltoken(&nmp->nm_token);
			return (error);
		}

		/*
		 * Attempt to prevent a mapped root from accessing a file
		 * which it shouldn't.  We try to read a byte from the file
		 * if the user is root and the file is not zero length.
		 * After calling nfs_laccess, we should have the correct
		 * file size cached.
		 */
		if (cred->cr_uid == 0 && (ap->a_mode & VREAD)
		    && VTONFS(vp)->n_size > 0) {
			struct iovec aiov;
			struct uio auio;
			char buf[1];

			aiov.iov_base = buf;
			aiov.iov_len = 1;
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			auio.uio_offset = 0;
			auio.uio_resid = 1;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_READ;
			auio.uio_td = td;

			if (vp->v_type == VREG) {
				error = nfs_readrpc_uio(vp, &auio);
			} else if (vp->v_type == VDIR) {
				char* bp;
				bp = kmalloc(NFS_DIRBLKSIZ, M_TEMP, M_WAITOK);
				aiov.iov_base = bp;
				aiov.iov_len = auio.uio_resid = NFS_DIRBLKSIZ;
				error = nfs_readdirrpc_uio(vp, &auio);
				kfree(bp, M_TEMP);
			} else if (vp->v_type == VLNK) {
				error = nfs_readlinkrpc_uio(vp, &auio);
			} else {
				error = EACCES;
			}
		}
	}
	/*
	 * [re]record creds for reading and/or writing if access
	 * was granted.  Assume the NFS server will grant read access
	 * for execute requests.
	 */
	if (error == 0) {
		if ((ap->a_mode & (VREAD|VEXEC)) && cred != np->n_rucred) {
			crhold(cred);
			if (np->n_rucred)
				crfree(np->n_rucred);
			np->n_rucred = cred;
		}
		if ((ap->a_mode & VWRITE) && cred != np->n_wucred) {
			crhold(cred);
			if (np->n_wucred)
				crfree(np->n_wucred);
			np->n_wucred = cred;
		}
	}
	lwkt_reltoken(&nmp->nm_token);
	crfree(cred);
	return(error);
}

/*
 * nfs open vnode op
 * Check to see if the type is ok
 * and that deletion is not in progress.
 * For paged in text files, you will need to flush the page cache
 * if consistency is lost.
 *
 * nfs_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	    struct file *a_fp)
 */
/* ARGSUSED */
static int
nfs_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct vattr vattr;
	int error;

	lwkt_gettoken(&nmp->nm_token);

	if (vp->v_type != VREG && vp->v_type != VDIR && vp->v_type != VLNK) {
#ifdef DIAGNOSTIC
		kprintf("open eacces vtyp=%d\n",vp->v_type);
#endif
		lwkt_reltoken(&nmp->nm_token);
		return (EOPNOTSUPP);
	}

	/*
	 * Save valid creds for reading and writing for later RPCs.
	 */
	if ((ap->a_mode & FREAD) && ap->a_cred != np->n_rucred) {
		crhold(ap->a_cred);
		if (np->n_rucred)
			crfree(np->n_rucred);
		np->n_rucred = ap->a_cred;
	}
	if ((ap->a_mode & FWRITE) && ap->a_cred != np->n_wucred) {
		crhold(ap->a_cred);
		if (np->n_wucred)
			crfree(np->n_wucred);
		np->n_wucred = ap->a_cred;
	}

	/*
	 * Clear the attribute cache only if opening with write access.  It
	 * is unclear if we should do this at all here, but we certainly
	 * should not clear the cache unconditionally simply because a file
	 * is being opened.
	 */
	if (ap->a_mode & FWRITE)
		np->n_attrstamp = 0;

	/*
	 * For normal NFS, reconcile changes made locally verses 
	 * changes made remotely.  Note that VOP_GETATTR only goes
	 * to the wire if the cached attribute has timed out or been
	 * cleared.
	 *
	 * If local modifications have been made clear the attribute
	 * cache to force an attribute and modified time check.  If
	 * GETATTR detects that the file has been changed by someone
	 * other then us it will set NRMODIFIED.
	 *
	 * If we are opening a directory and local changes have been
	 * made we have to invalidate the cache in order to ensure
	 * that we get the most up-to-date information from the
	 * server.  XXX
	 */
	if (np->n_flag & NLMODIFIED) {
		np->n_attrstamp = 0;
		if (vp->v_type == VDIR) {
			error = nfs_vinvalbuf(vp, V_SAVE, 1);
			if (error == EINTR) {
				lwkt_reltoken(&nmp->nm_token);
				return (error);
			}
			nfs_invaldir(vp);
		}
	}
	error = VOP_GETATTR(vp, &vattr);
	if (error) {
		lwkt_reltoken(&nmp->nm_token);
		return (error);
	}
	if (np->n_flag & NRMODIFIED) {
		if (vp->v_type == VDIR)
			nfs_invaldir(vp);
		error = nfs_vinvalbuf(vp, V_SAVE, 1);
		if (error == EINTR) {
			lwkt_reltoken(&nmp->nm_token);
			return (error);
		}
		np->n_flag &= ~NRMODIFIED;
	}
	error = vop_stdopen(ap);
	lwkt_reltoken(&nmp->nm_token);

	return error;
}

/*
 * nfs close vnode op
 * What an NFS client should do upon close after writing is a debatable issue.
 * Most NFS clients push delayed writes to the server upon close, basically for
 * two reasons:
 * 1 - So that any write errors may be reported back to the client process
 *     doing the close system call. By far the two most likely errors are
 *     NFSERR_NOSPC and NFSERR_DQUOT to indicate space allocation failure.
 * 2 - To put a worst case upper bound on cache inconsistency between
 *     multiple clients for the file.
 * There is also a consistency problem for Version 2 of the protocol w.r.t.
 * not being able to tell if other clients are writing a file concurrently,
 * since there is no way of knowing if the changed modify time in the reply
 * is only due to the write for this client.
 * (NFS Version 3 provides weak cache consistency data in the reply that
 *  should be sufficient to detect and handle this case.)
 *
 * The current code does the following:
 * for NFS Version 2 - play it safe and flush/invalidate all dirty buffers
 * for NFS Version 3 - flush dirty buffers to the server but don't invalidate
 *                     or commit them (this satisfies 1 and 2 except for the
 *                     case where the server crashes after this close but
 *                     before the commit RPC, which is felt to be "good
 *                     enough". Changing the last argument to nfs_flush() to
 *                     a 1 would force a commit operation, if it is felt a
 *                     commit is necessary now.
 * for NQNFS         - do nothing now, since 2 is dealt with via leases and
 *                     1 should be dealt with via an fsync() system call for
 *                     cases where write errors are important.
 *
 * nfs_close(struct vnode *a_vp, int a_fflag)
 */
/* ARGSUSED */
static int
nfs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0;
	thread_t td = curthread;

	vn_lock(vp, LK_UPGRADE | LK_RETRY); /* XXX */
	lwkt_gettoken(&nmp->nm_token);

	if (vp->v_type == VREG) {
	    if (np->n_flag & NLMODIFIED) {
		if (NFS_ISV3(vp)) {
		    /*
		     * Under NFSv3 we have dirty buffers to dispose of.  We
		     * must flush them to the NFS server.  We have the option
		     * of waiting all the way through the commit rpc or just
		     * waiting for the initial write.  The default is to only
		     * wait through the initial write so the data is in the
		     * server's cache, which is roughly similar to the state
		     * a standard disk subsystem leaves the file in on close().
		     *
		     * We cannot clear the NLMODIFIED bit in np->n_flag due to
		     * potential races with other processes, and certainly
		     * cannot clear it if we don't commit.
		     */
		    int cm = nfsv3_commit_on_close ? 1 : 0;
		    error = nfs_flush(vp, MNT_WAIT, td, cm);
		    /* np->n_flag &= ~NLMODIFIED; */
		} else {
		    error = nfs_vinvalbuf(vp, V_SAVE, 1);
		}
		np->n_attrstamp = 0;
	    }
	    if (np->n_flag & NWRITEERR) {
		np->n_flag &= ~NWRITEERR;
		error = np->n_error;
	    }
	}
	vop_stdclose(ap);
	lwkt_reltoken(&nmp->nm_token);

	return (error);
}

/*
 * nfs getattr call from vfs.
 *
 * nfs_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
nfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp;
	int error = 0;
	thread_t td = curthread;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(vp);
	nmp = VFSTONFS(vp->v_mount);

	lwkt_gettoken(&nmp->nm_token);
	
	/*
	 * Update local times for special files.
	 */
	if (np->n_flag & (NACC | NUPD))
		np->n_flag |= NCHG;
	/*
	 * First look in the cache.
	 */
	if (nfs_getattrcache(vp, ap->a_vap) == 0)
		goto done;

	if (info.v3 && nfsaccess_cache_timeout > 0) {
		nfsstats.accesscache_misses++;
		nfs3_access_otw(vp, NFSV3ACCESS_ALL, td, nfs_vpcred(vp, ND_CHECK));
		if (nfs_getattrcache(vp, ap->a_vap) == 0)
			goto done;
	}

	nfsstats.rpccnt[NFSPROC_GETATTR]++;
	nfsm_reqhead(&info, vp, NFSPROC_GETATTR, NFSX_FH(info.v3));
	ERROROUT(nfsm_fhtom(&info, vp));
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_GETATTR, td,
				nfs_vpcred(vp, ND_CHECK), &error));
	if (error == 0) {
		ERROROUT(nfsm_loadattr(&info, vp, ap->a_vap));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
done:
	/*
	 * NFS doesn't support chflags flags.  If the nfs mount was
	 * made -o cache set the UF_CACHE bit for swapcache.
	 */
	if ((nmp->nm_flag & NFSMNT_CACHE) && (vp->v_flag & VROOT))
		ap->a_vap->va_flags |= UF_CACHE;
nfsmout:
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

/*
 * nfs setattr call.
 *
 * nfs_setattr(struct vnode *a_vp, struct vattr *a_vap, struct ucred *a_cred)
 */
static int
nfs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct vattr *vap = ap->a_vap;
	int error = 0;
	off_t tsize;
	thread_t td = curthread;

#ifndef nolint
	tsize = (off_t)0;
#endif
	/*
	 * Setting of flags is not supported.
	 */
	if (vap->va_flags != VNOVAL)
		return (EOPNOTSUPP);

	/*
	 * Disallow write attempts if the filesystem is mounted read-only.
	 */
  	if ((vap->va_flags != VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);

	lwkt_gettoken(&nmp->nm_token);

	if (vap->va_size != VNOVAL) {
		/*
		 * truncation requested
		 */
 		switch (vp->v_type) {
 		case VDIR:
			lwkt_reltoken(&nmp->nm_token);
 			return (EISDIR);
 		case VCHR:
 		case VBLK:
 		case VSOCK:
 		case VFIFO:
			if (vap->va_mtime.tv_sec == VNOVAL &&
			    vap->va_atime.tv_sec == VNOVAL &&
			    vap->va_mode == (mode_t)VNOVAL &&
			    vap->va_uid == (uid_t)VNOVAL &&
			    vap->va_gid == (gid_t)VNOVAL) {
				lwkt_reltoken(&nmp->nm_token);
				return (0);
			}
 			vap->va_size = VNOVAL;
 			break;
 		default:
			/*
			 * Disallow write attempts if the filesystem is
			 * mounted read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY) {
				lwkt_reltoken(&nmp->nm_token);
				return (EROFS);
			}

			tsize = np->n_size;
again:
			error = nfs_meta_setsize(vp, td, vap->va_size, 0);

#if 0
 			if (np->n_flag & NLMODIFIED) {
 			    if (vap->va_size == 0)
 				error = nfs_vinvalbuf(vp, 0, 1);
 			    else
 				error = nfs_vinvalbuf(vp, V_SAVE, 1);
 			}
#endif
			/*
			 * note: this loop case almost always happens at 
			 * least once per truncation.
			 */
			if (error == 0 && np->n_size != vap->va_size)
				goto again;
			np->n_vattr.va_size = vap->va_size;
			break;
		}
	} else if ((np->n_flag & NLMODIFIED) && vp->v_type == VREG) {
		/*
		 * What to do.  If we are modifying the mtime we lose
		 * mtime detection of changes made by the server or other
		 * clients.  But programs like rsync/rdist/cpdup are going
		 * to call utimes a lot.  We don't want to piecemeal sync.
		 *
		 * For now sync if any prior remote changes were detected,
		 * but allow us to lose track of remote changes made during
		 * the utimes operation.
		 */
		if (np->n_flag & NRMODIFIED)
			error = nfs_vinvalbuf(vp, V_SAVE, 1);
		if (error == EINTR) {
			lwkt_reltoken(&nmp->nm_token);
			return (error);
		}
		if (error == 0) {
			if (vap->va_mtime.tv_sec != VNOVAL) {
				np->n_mtime = vap->va_mtime.tv_sec;
			}
		}
	}
	error = nfs_setattrrpc(vp, vap, ap->a_cred, td);

	/*
	 * Sanity check if a truncation was issued.  This should only occur
	 * if multiple processes are racing on the same file.
	 */
	if (error == 0 && vap->va_size != VNOVAL && 
	    np->n_size != vap->va_size) {
		kprintf("NFS ftruncate: server disagrees on the file size: "
			"%jd/%jd/%jd\n",
			(intmax_t)tsize,
			(intmax_t)vap->va_size,
			(intmax_t)np->n_size);
		goto again;
	}
	if (error && vap->va_size != VNOVAL) {
		np->n_size = np->n_vattr.va_size = tsize;
		nfs_meta_setsize(vp, td, np->n_size, 0);
	}
	lwkt_reltoken(&nmp->nm_token);

	return (error);
}

/*
 * Do an nfs setattr rpc.
 */
static int
nfs_setattrrpc(struct vnode *vp, struct vattr *vap,
	       struct ucred *cred, struct thread *td)
{
	struct nfsv2_sattr *sp;
	struct nfsnode *np = VTONFS(vp);
	u_int32_t *tl;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(vp);

	nfsstats.rpccnt[NFSPROC_SETATTR]++;
	nfsm_reqhead(&info, vp, NFSPROC_SETATTR,
		     NFSX_FH(info.v3) + NFSX_SATTR(info.v3));
	ERROROUT(nfsm_fhtom(&info, vp));
	if (info.v3) {
		nfsm_v3attrbuild(&info, vap, TRUE);
		tl = nfsm_build(&info, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		sp = nfsm_build(&info, NFSX_V2SATTR);
		if (vap->va_mode == (mode_t)VNOVAL)
			sp->sa_mode = nfs_xdrneg1;
		else
			sp->sa_mode = vtonfsv2_mode(vp->v_type, vap->va_mode);
		if (vap->va_uid == (uid_t)VNOVAL)
			sp->sa_uid = nfs_xdrneg1;
		else
			sp->sa_uid = txdr_unsigned(vap->va_uid);
		if (vap->va_gid == (gid_t)VNOVAL)
			sp->sa_gid = nfs_xdrneg1;
		else
			sp->sa_gid = txdr_unsigned(vap->va_gid);
		sp->sa_size = txdr_unsigned(vap->va_size);
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_SETATTR, td, cred, &error));
	if (info.v3) {
		np->n_modestamp = 0;
		ERROROUT(nfsm_wcc_data(&info, vp, &wccflag));
	} else {
		ERROROUT(nfsm_loadattr(&info, vp, NULL));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	return (error);
}

static
void
nfs_cache_setvp(struct nchandle *nch, struct vnode *vp, int nctimeout)
{
	if (nctimeout == 0)
		nctimeout = 1;
	else
		nctimeout *= hz;
	cache_setvp(nch, vp);
	cache_settimeout(nch, nctimeout);
}

/*
 * NEW API CALL - replaces nfs_lookup().  However, we cannot remove 
 * nfs_lookup() until all remaining new api calls are implemented.
 *
 * Resolve a namecache entry.  This function is passed a locked ncp and
 * must call nfs_cache_setvp() on it as appropriate to resolve the entry.
 */
static int
nfs_nresolve(struct vop_nresolve_args *ap)
{
	struct thread *td = curthread;
	struct namecache *ncp;
	struct nfsmount *nmp;
	struct nfsnode *np;
	struct vnode *dvp;
	struct vnode *nvp;
	nfsfh_t *fhp;
	int attrflag;
	int fhsize;
	int error;
	int tmp_error;
	int len;
	struct nfsm_info info;

	dvp = ap->a_dvp;
	nmp = VFSTONFS(dvp->v_mount);

	lwkt_gettoken(&nmp->nm_token);

	if ((error = vget(dvp, LK_SHARED)) != 0) {
		lwkt_reltoken(&nmp->nm_token);
		return (error);
	}

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);

	nvp = NULL;
	nfsstats.lookupcache_misses++;
	nfsstats.rpccnt[NFSPROC_LOOKUP]++;
	ncp = ap->a_nch->ncp;
	len = ncp->nc_nlen;
	nfsm_reqhead(&info, dvp, NFSPROC_LOOKUP,
		     NFSX_FH(info.v3) + NFSX_UNSIGNED + nfsm_rndup(len));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, ncp->nc_name, len, NFS_MAXNAMLEN));
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_LOOKUP, td,
				ap->a_cred, &error));
	if (error) {
		/*
		 * Cache negatve lookups to reduce NFS traffic, but use
		 * a fast timeout.  Otherwise use a timeout of 1 tick.
		 * XXX we should add a namecache flag for no-caching
		 * to uncache the negative hit as soon as possible, but
		 * we cannot simply destroy the entry because it is used
		 * as a placeholder by the caller.
		 *
		 * The refactored nfs code will overwrite a non-zero error
		 * with 0 when we use ERROROUT(), so don't here.
		 */
		if (error == ENOENT)
			nfs_cache_setvp(ap->a_nch, NULL, nfsneg_cache_timeout);
		tmp_error = nfsm_postop_attr(&info, dvp, &attrflag,
					     NFS_LATTR_NOSHRINK);
		if (tmp_error) {
			error = tmp_error;
			goto nfsmout;
		}
		m_freem(info.mrep);
		info.mrep = NULL;
		goto nfsmout;
	}

	/*
	 * Success, get the file handle, do various checks, and load 
	 * post-operation data from the reply packet.  Theoretically
	 * we should never be looking up "." so, theoretically, we
	 * should never get the same file handle as our directory.  But
	 * we check anyway. XXX
	 *
	 * Note that no timeout is set for the positive cache hit.  We
	 * assume, theoretically, that ESTALE returns will be dealt with
	 * properly to handle NFS races and in anycase we cannot depend
	 * on a timeout to deal with NFS open/create/excl issues so instead
	 * of a bad hack here the rest of the NFS client code needs to do
	 * the right thing.
	 */
	NEGATIVEOUT(fhsize = nfsm_getfh(&info, &fhp));

	np = VTONFS(dvp);
	if (NFS_CMPFH(np, fhp, fhsize)) {
		vref(dvp);
		nvp = dvp;
	} else {
		error = nfs_nget(dvp->v_mount, fhp, fhsize, &np, NULL);
		if (error) {
			m_freem(info.mrep);
			info.mrep = NULL;
			vput(dvp);
			lwkt_reltoken(&nmp->nm_token);
			return (error);
		}
		nvp = NFSTOV(np);
	}
	if (info.v3) {
		ERROROUT(nfsm_postop_attr(&info, nvp, &attrflag,
					  NFS_LATTR_NOSHRINK));
		ERROROUT(nfsm_postop_attr(&info, dvp, &attrflag,
					  NFS_LATTR_NOSHRINK));
	} else {
		ERROROUT(nfsm_loadattr(&info, nvp, NULL));
	}
	nfs_cache_setvp(ap->a_nch, nvp, nfspos_cache_timeout);
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	lwkt_reltoken(&nmp->nm_token);
	vput(dvp);
	if (nvp) {
		if (nvp == dvp)
			vrele(nvp);
		else
			vput(nvp);
	}
	return (error);
}

/*
 * 'cached' nfs directory lookup
 *
 * NOTE: cannot be removed until NFS implements all the new n*() API calls.
 *
 * nfs_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *	      struct componentname *a_cnp)
 */
static int
nfs_lookup(struct vop_old_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	int flags = cnp->cn_flags;
	struct vnode *newvp;
	struct vnode *notvp;
	struct nfsmount *nmp;
	long len;
	nfsfh_t *fhp;
	struct nfsnode *np;
	int lockparent, wantparent, attrflag, fhsize;
	int error;
	int tmp_error;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);
	error = 0;

	notvp = (cnp->cn_flags & CNP_NOTVP) ? cnp->cn_notvp : NULL;

	/*
	 * Read-only mount check and directory check.
	 */
	*vpp = NULLVP;
	if ((dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == NAMEI_DELETE || cnp->cn_nameiop == NAMEI_RENAME))
		return (EROFS);

	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	/*
	 * Look it up in the cache.  Note that ENOENT is only returned if we
	 * previously entered a negative hit (see later on).  The additional
	 * nfsneg_cache_timeout check causes previously cached results to
	 * be instantly ignored if the negative caching is turned off.
	 */
	lockparent = flags & CNP_LOCKPARENT;
	wantparent = flags & (CNP_LOCKPARENT|CNP_WANTPARENT);
	nmp = VFSTONFS(dvp->v_mount);
	np = VTONFS(dvp);

	lwkt_gettoken(&nmp->nm_token);

	/*
	 * Go to the wire.
	 */
	error = 0;
	newvp = NULLVP;
	nfsstats.lookupcache_misses++;
	nfsstats.rpccnt[NFSPROC_LOOKUP]++;
	len = cnp->cn_namelen;
	nfsm_reqhead(&info, dvp, NFSPROC_LOOKUP,
		     NFSX_FH(info.v3) + NFSX_UNSIGNED + nfsm_rndup(len));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, cnp->cn_nameptr, len, NFS_MAXNAMLEN));
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_LOOKUP, cnp->cn_td,
				cnp->cn_cred, &error));
	if (error) {
		tmp_error = nfsm_postop_attr(&info, dvp, &attrflag,
					     NFS_LATTR_NOSHRINK);
		if (tmp_error) {
			error = tmp_error;
			goto nfsmout;
		}

		m_freem(info.mrep);
		info.mrep = NULL;
		goto nfsmout;
	}
	NEGATIVEOUT(fhsize = nfsm_getfh(&info, &fhp));

	/*
	 * Handle RENAME case...
	 */
	if (cnp->cn_nameiop == NAMEI_RENAME && wantparent) {
		if (NFS_CMPFH(np, fhp, fhsize)) {
			m_freem(info.mrep);
			info.mrep = NULL;
			lwkt_reltoken(&nmp->nm_token);
			return (EISDIR);
		}
		error = nfs_nget(dvp->v_mount, fhp, fhsize, &np, notvp);
		if (error) {
			m_freem(info.mrep);
			info.mrep = NULL;
			lwkt_reltoken(&nmp->nm_token);
			return (error);
		}
		newvp = NFSTOV(np);
		if (info.v3) {
			ERROROUT(nfsm_postop_attr(&info, newvp, &attrflag,
						  NFS_LATTR_NOSHRINK));
			ERROROUT(nfsm_postop_attr(&info, dvp, &attrflag,
						  NFS_LATTR_NOSHRINK));
		} else {
			ERROROUT(nfsm_loadattr(&info, newvp, NULL));
		}
		*vpp = newvp;
		m_freem(info.mrep);
		info.mrep = NULL;
		if (!lockparent) {
			vn_unlock(dvp);
			cnp->cn_flags |= CNP_PDIRUNLOCK;
		}
		lwkt_reltoken(&nmp->nm_token);
		return (0);
	}

	if (flags & CNP_ISDOTDOT) {
		vn_unlock(dvp);
		cnp->cn_flags |= CNP_PDIRUNLOCK;
		error = nfs_nget(dvp->v_mount, fhp, fhsize, &np, notvp);
		if (error) {
			vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
			cnp->cn_flags &= ~CNP_PDIRUNLOCK;
			lwkt_reltoken(&nmp->nm_token);
			return (error); /* NOTE: return error from nget */
		}
		newvp = NFSTOV(np);
		if (lockparent) {
			error = vn_lock(dvp, LK_EXCLUSIVE | LK_FAILRECLAIM);
			if (error) {
				vput(newvp);
				lwkt_reltoken(&nmp->nm_token);
				return (error);
			}
			cnp->cn_flags |= CNP_PDIRUNLOCK;
		}
	} else if (NFS_CMPFH(np, fhp, fhsize)) {
		vref(dvp);
		newvp = dvp;
	} else {
		error = nfs_nget(dvp->v_mount, fhp, fhsize, &np, notvp);
		if (error) {
			m_freem(info.mrep);
			info.mrep = NULL;
			lwkt_reltoken(&nmp->nm_token);
			return (error);
		}
		if (!lockparent) {
			vn_unlock(dvp);
			cnp->cn_flags |= CNP_PDIRUNLOCK;
		}
		newvp = NFSTOV(np);
	}
	if (info.v3) {
		ERROROUT(nfsm_postop_attr(&info, newvp, &attrflag,
					  NFS_LATTR_NOSHRINK));
		ERROROUT(nfsm_postop_attr(&info, dvp, &attrflag,
					  NFS_LATTR_NOSHRINK));
	} else {
		ERROROUT(nfsm_loadattr(&info, newvp, NULL));
	}
#if 0
	/* XXX MOVE TO nfs_nremove() */
	if ((cnp->cn_flags & CNP_MAKEENTRY) &&
	    cnp->cn_nameiop != NAMEI_DELETE) {
		np->n_ctime = np->n_vattr.va_ctime.tv_sec; /* XXX */
	}
#endif
	*vpp = newvp;
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	if (error) {
		if (newvp != NULLVP) {
			vrele(newvp);
			*vpp = NULLVP;
		}
		if ((cnp->cn_nameiop == NAMEI_CREATE || 
		     cnp->cn_nameiop == NAMEI_RENAME) &&
		    error == ENOENT) {
			if (!lockparent) {
				vn_unlock(dvp);
				cnp->cn_flags |= CNP_PDIRUNLOCK;
			}
			if (dvp->v_mount->mnt_flag & MNT_RDONLY)
				error = EROFS;
			else
				error = EJUSTRETURN;
		}
	}
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

/*
 * nfs read call.
 * Just call nfs_bioread() to do the work.
 *
 * nfs_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	    struct ucred *a_cred)
 */
static int
nfs_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error;

	lwkt_gettoken(&nmp->nm_token);
	error = nfs_bioread(vp, ap->a_uio, ap->a_ioflag);
	lwkt_reltoken(&nmp->nm_token);

	return error;
}

/*
 * nfs readlink call
 *
 * nfs_readlink(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred)
 */
static int
nfs_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error;

	if (vp->v_type != VLNK)
		return (EINVAL);

	lwkt_gettoken(&nmp->nm_token);
	error = nfs_bioread(vp, ap->a_uio, 0);
	lwkt_reltoken(&nmp->nm_token);

	return error;
}

/*
 * Do a readlink rpc.
 * Called by nfs_doio() from below the buffer cache.
 */
int
nfs_readlinkrpc_uio(struct vnode *vp, struct uio *uiop)
{
	int error = 0, len, attrflag;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(vp);

	nfsstats.rpccnt[NFSPROC_READLINK]++;
	nfsm_reqhead(&info, vp, NFSPROC_READLINK, NFSX_FH(info.v3));
	ERROROUT(nfsm_fhtom(&info, vp));
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_READLINK, uiop->uio_td,
				nfs_vpcred(vp, ND_CHECK), &error));
	if (info.v3) {
		ERROROUT(nfsm_postop_attr(&info, vp, &attrflag,
					  NFS_LATTR_NOSHRINK));
	}
	if (!error) {
		NEGATIVEOUT(len = nfsm_strsiz(&info, NFS_MAXPATHLEN));
		if (len == NFS_MAXPATHLEN) {
			struct nfsnode *np = VTONFS(vp);
			if (np->n_size && np->n_size < NFS_MAXPATHLEN)
				len = np->n_size;
		}
		ERROROUT(nfsm_mtouio(&info, uiop, len));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	return (error);
}

/*
 * nfs synchronous read rpc using UIO
 */
int
nfs_readrpc_uio(struct vnode *vp, struct uio *uiop)
{
	u_int32_t *tl;
	struct nfsmount *nmp;
	int error = 0, len, retlen, tsiz, eof, attrflag;
	struct nfsm_info info;
	off_t tmp_off;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(vp);

#ifndef nolint
	eof = 0;
#endif
	nmp = VFSTONFS(vp->v_mount);

	tsiz = uiop->uio_resid;
	tmp_off = uiop->uio_offset + tsiz;
	if (tmp_off > nmp->nm_maxfilesize || tmp_off < uiop->uio_offset)
		return (EFBIG);
	tmp_off = uiop->uio_offset;
	while (tsiz > 0) {
		nfsstats.rpccnt[NFSPROC_READ]++;
		len = (tsiz > nmp->nm_rsize) ? nmp->nm_rsize : tsiz;
		nfsm_reqhead(&info, vp, NFSPROC_READ,
			     NFSX_FH(info.v3) + NFSX_UNSIGNED * 3);
		ERROROUT(nfsm_fhtom(&info, vp));
		tl = nfsm_build(&info, NFSX_UNSIGNED * 3);
		if (info.v3) {
			txdr_hyper(uiop->uio_offset, tl);
			*(tl + 2) = txdr_unsigned(len);
		} else {
			*tl++ = txdr_unsigned(uiop->uio_offset);
			*tl++ = txdr_unsigned(len);
			*tl = 0;
		}
		NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_READ, uiop->uio_td,
					nfs_vpcred(vp, ND_READ), &error));
		if (info.v3) {
			ERROROUT(nfsm_postop_attr(&info, vp, &attrflag,
						 NFS_LATTR_NOSHRINK));
			NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
			eof = fxdr_unsigned(int, *(tl + 1));
		} else {
			ERROROUT(nfsm_loadattr(&info, vp, NULL));
		}
		NEGATIVEOUT(retlen = nfsm_strsiz(&info, len));
		ERROROUT(nfsm_mtouio(&info, uiop, retlen));
		m_freem(info.mrep);
		info.mrep = NULL;

		/*
		 * Handle short-read from server (NFSv3).  If EOF is not
		 * flagged (and no error occurred), but retlen is less
		 * then the request size, we must zero-fill the remainder.
		 */
		if (retlen < len && info.v3 && eof == 0) {
			ERROROUT(uiomovez(len - retlen, uiop));
			retlen = len;
		}
		tsiz -= retlen;

		/*
		 * Terminate loop on EOF or zero-length read.
		 *
		 * For NFSv2 a short-read indicates EOF, not zero-fill,
		 * and also terminates the loop.
		 */
		if (info.v3) {
			if (eof || retlen == 0)
				tsiz = 0;
		} else if (retlen < len) {
			tsiz = 0;
		}
	}
nfsmout:
	return (error);
}

/*
 * nfs write call
 */
int
nfs_writerpc_uio(struct vnode *vp, struct uio *uiop,
		 int *iomode, int *must_commit)
{
	u_int32_t *tl;
	int32_t backup;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, len, tsiz, wccflag = NFSV3_WCCRATTR, rlen, commit;
	int  committed = NFSV3WRITE_FILESYNC;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(vp);

#ifndef DIAGNOSTIC
	if (uiop->uio_iovcnt != 1)
		panic("nfs: writerpc iovcnt > 1");
#endif
	*must_commit = 0;
	tsiz = uiop->uio_resid;
	if (uiop->uio_offset + tsiz > nmp->nm_maxfilesize)
		return (EFBIG);
	while (tsiz > 0) {
		nfsstats.rpccnt[NFSPROC_WRITE]++;
		len = (tsiz > nmp->nm_wsize) ? nmp->nm_wsize : tsiz;
		nfsm_reqhead(&info, vp, NFSPROC_WRITE,
			     NFSX_FH(info.v3) + 5 * NFSX_UNSIGNED +
			     nfsm_rndup(len));
		ERROROUT(nfsm_fhtom(&info, vp));
		if (info.v3) {
			tl = nfsm_build(&info, 5 * NFSX_UNSIGNED);
			txdr_hyper(uiop->uio_offset, tl);
			tl += 2;
			*tl++ = txdr_unsigned(len);
			*tl++ = txdr_unsigned(*iomode);
			*tl = txdr_unsigned(len);
		} else {
			u_int32_t x;

			tl = nfsm_build(&info, 4 * NFSX_UNSIGNED);
			/* Set both "begin" and "current" to non-garbage. */
			x = txdr_unsigned((u_int32_t)uiop->uio_offset);
			*tl++ = x;	/* "begin offset" */
			*tl++ = x;	/* "current offset" */
			x = txdr_unsigned(len);
			*tl++ = x;	/* total to this offset */
			*tl = x;	/* size of this write */
		}
		ERROROUT(nfsm_uiotom(&info, uiop, len));
		NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_WRITE, uiop->uio_td,
					nfs_vpcred(vp, ND_WRITE), &error));
		if (info.v3) {
			/*
			 * The write RPC returns a before and after mtime.  The
			 * nfsm_wcc_data() macro checks the before n_mtime
			 * against the before time and stores the after time
			 * in the nfsnode's cached vattr and n_mtime field.
			 * The NRMODIFIED bit will be set if the before
			 * time did not match the original mtime.
			 */
			wccflag = NFSV3_WCCCHK;
			ERROROUT(nfsm_wcc_data(&info, vp, &wccflag));
			if (error == 0) {
				NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED + NFSX_V3WRITEVERF));
				rlen = fxdr_unsigned(int, *tl++);
				if (rlen == 0) {
					error = NFSERR_IO;
					m_freem(info.mrep);
					info.mrep = NULL;
					break;
				} else if (rlen < len) {
					backup = len - rlen;
					uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base - backup;
					uiop->uio_iov->iov_len += backup;
					uiop->uio_offset -= backup;
					uiop->uio_resid += backup;
					len = rlen;
				}
				commit = fxdr_unsigned(int, *tl++);

				/*
				 * Return the lowest committment level
				 * obtained by any of the RPCs.
				 */
				if (committed == NFSV3WRITE_FILESYNC)
					committed = commit;
				else if (committed == NFSV3WRITE_DATASYNC &&
					commit == NFSV3WRITE_UNSTABLE)
					committed = commit;
				if ((nmp->nm_state & NFSSTA_HASWRITEVERF) == 0){
				    bcopy((caddr_t)tl, (caddr_t)nmp->nm_verf,
					NFSX_V3WRITEVERF);
				    nmp->nm_state |= NFSSTA_HASWRITEVERF;
				} else if (bcmp((caddr_t)tl,
				    (caddr_t)nmp->nm_verf, NFSX_V3WRITEVERF)) {
				    *must_commit = 1;
				    bcopy((caddr_t)tl, (caddr_t)nmp->nm_verf,
					NFSX_V3WRITEVERF);
				}
			}
		} else {
			ERROROUT(nfsm_loadattr(&info, vp, NULL));
		}
		m_freem(info.mrep);
		info.mrep = NULL;
		if (error)
			break;
		tsiz -= len;
	}
nfsmout:
	if (vp->v_mount->mnt_flag & MNT_ASYNC)
		committed = NFSV3WRITE_FILESYNC;
	*iomode = committed;
	if (error)
		uiop->uio_resid = tsiz;
	return (error);
}

/*
 * nfs mknod rpc
 * For NFS v2 this is a kludge. Use a create rpc but with the IFMT bits of the
 * mode set to specify the file type and the size field for rdev.
 */
static int
nfs_mknodrpc(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp,
	     struct vattr *vap)
{
	struct nfsv2_sattr *sp;
	u_int32_t *tl;
	struct vnode *newvp = NULL;
	struct nfsnode *np = NULL;
	struct vattr vattr;
	int error = 0, wccflag = NFSV3_WCCRATTR, gotvp = 0;
	int rmajor, rminor;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);

	if (vap->va_type == VCHR || vap->va_type == VBLK) {
		rmajor = txdr_unsigned(major(vap->va_rdev));
		rminor = txdr_unsigned(minor(vap->va_rdev));
	} else if (vap->va_type == VFIFO || vap->va_type == VSOCK) {
		rmajor = nfs_xdrneg1;
		rminor = nfs_xdrneg1;
	} else {
		return (EOPNOTSUPP);
	}
	if ((error = VOP_GETATTR(dvp, &vattr)) != 0) {
		return (error);
	}
	nfsstats.rpccnt[NFSPROC_MKNOD]++;
	nfsm_reqhead(&info, dvp, NFSPROC_MKNOD,
		     NFSX_FH(info.v3) + 4 * NFSX_UNSIGNED +
		     nfsm_rndup(cnp->cn_namelen) + NFSX_SATTR(info.v3));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, cnp->cn_nameptr, cnp->cn_namelen,
			     NFS_MAXNAMLEN));
	if (info.v3) {
		tl = nfsm_build(&info, NFSX_UNSIGNED);
		*tl++ = vtonfsv3_type(vap->va_type);
		nfsm_v3attrbuild(&info, vap, FALSE);
		if (vap->va_type == VCHR || vap->va_type == VBLK) {
			tl = nfsm_build(&info, 2 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(major(vap->va_rdev));
			*tl = txdr_unsigned(minor(vap->va_rdev));
		}
	} else {
		sp = nfsm_build(&info, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(vap->va_type, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = makeudev(rmajor, rminor);
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_MKNOD, cnp->cn_td,
				cnp->cn_cred, &error));
	if (!error) {
		ERROROUT(nfsm_mtofh(&info, dvp, &newvp, &gotvp));
		if (!gotvp) {
			if (newvp) {
				vput(newvp);
				newvp = NULL;
			}
			error = nfs_lookitup(dvp, cnp->cn_nameptr,
			    cnp->cn_namelen, cnp->cn_cred, cnp->cn_td, &np);
			if (!error)
				newvp = NFSTOV(np);
		}
	}
	if (info.v3) {
		ERROROUT(nfsm_wcc_data(&info, dvp, &wccflag));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	if (error) {
		if (newvp)
			vput(newvp);
	} else {
		*vpp = newvp;
	}
	VTONFS(dvp)->n_flag |= NLMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs mknod vop
 * just call nfs_mknodrpc() to do the work.
 *
 * nfs_mknod(struct vnode *a_dvp, struct vnode **a_vpp,
 *	     struct componentname *a_cnp, struct vattr *a_vap)
 */
/* ARGSUSED */
static int
nfs_mknod(struct vop_old_mknod_args *ap)
{
	struct nfsmount *nmp = VFSTONFS(ap->a_dvp->v_mount);
	int error;

	lwkt_gettoken(&nmp->nm_token);
	error = nfs_mknodrpc(ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap);
	lwkt_reltoken(&nmp->nm_token);

	return error;
}

static u_long create_verf;
/*
 * nfs file create call
 *
 * nfs_create(struct vnode *a_dvp, struct vnode **a_vpp,
 *	      struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
nfs_create(struct vop_old_create_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct nfsmount *nmp = VFSTONFS(dvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	struct nfsv2_sattr *sp;
	u_int32_t *tl;
	struct nfsnode *np = NULL;
	struct vnode *newvp = NULL;
	int error = 0, wccflag = NFSV3_WCCRATTR, gotvp = 0, fmode = 0;
	struct vattr vattr;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);
	lwkt_gettoken(&nmp->nm_token);

	/*
	 * Oops, not for me..
	 */
	if (vap->va_type == VSOCK) {
		error = nfs_mknodrpc(dvp, ap->a_vpp, cnp, vap);
		lwkt_reltoken(&nmp->nm_token);
		return error;
	}

	if ((error = VOP_GETATTR(dvp, &vattr)) != 0) {
		lwkt_reltoken(&nmp->nm_token);
		return (error);
	}
	if (vap->va_vaflags & VA_EXCLUSIVE)
		fmode |= O_EXCL;
again:
	nfsstats.rpccnt[NFSPROC_CREATE]++;
	nfsm_reqhead(&info, dvp, NFSPROC_CREATE,
		     NFSX_FH(info.v3) + 2 * NFSX_UNSIGNED +
		     nfsm_rndup(cnp->cn_namelen) + NFSX_SATTR(info.v3));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, cnp->cn_nameptr, cnp->cn_namelen,
			     NFS_MAXNAMLEN));
	if (info.v3) {
		tl = nfsm_build(&info, NFSX_UNSIGNED);
		if (fmode & O_EXCL) {
			*tl = txdr_unsigned(NFSV3CREATE_EXCLUSIVE);
			tl = nfsm_build(&info, NFSX_V3CREATEVERF);
#ifdef INET
			if (!TAILQ_EMPTY(&in_ifaddrheads[mycpuid]))
				*tl++ = IA_SIN(TAILQ_FIRST(&in_ifaddrheads[mycpuid])->ia)->sin_addr.s_addr;
			else
#endif
				*tl++ = create_verf;
			*tl = ++create_verf;
		} else {
			*tl = txdr_unsigned(NFSV3CREATE_UNCHECKED);
			nfsm_v3attrbuild(&info, vap, FALSE);
		}
	} else {
		sp = nfsm_build(&info, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(vap->va_type, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = 0;
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_CREATE, cnp->cn_td,
				cnp->cn_cred, &error));
	if (error == 0) {
		ERROROUT(nfsm_mtofh(&info, dvp, &newvp, &gotvp));
		if (!gotvp) {
			if (newvp) {
				vput(newvp);
				newvp = NULL;
			}
			error = nfs_lookitup(dvp, cnp->cn_nameptr,
			    cnp->cn_namelen, cnp->cn_cred, cnp->cn_td, &np);
			if (!error)
				newvp = NFSTOV(np);
		}
	}
	if (info.v3) {
		if (error == 0)
			error = nfsm_wcc_data(&info, dvp, &wccflag);
		else
			(void)nfsm_wcc_data(&info, dvp, &wccflag);
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	if (error) {
		if (info.v3 && (fmode & O_EXCL) && error == NFSERR_NOTSUPP) {
			KKASSERT(newvp == NULL);
			fmode &= ~O_EXCL;
			goto again;
		}
	} else if (info.v3 && (fmode & O_EXCL)) {
		/*
		 * We are normally called with only a partially initialized
		 * VAP.  Since the NFSv3 spec says that server may use the
		 * file attributes to store the verifier, the spec requires
		 * us to do a SETATTR RPC. FreeBSD servers store the verifier
		 * in atime, but we can't really assume that all servers will
		 * so we ensure that our SETATTR sets both atime and mtime.
		 */
		if (vap->va_mtime.tv_sec == VNOVAL)
			vfs_timestamp(&vap->va_mtime);
		if (vap->va_atime.tv_sec == VNOVAL)
			vap->va_atime = vap->va_mtime;
		error = nfs_setattrrpc(newvp, vap, cnp->cn_cred, cnp->cn_td);
	}
	if (error == 0) {
		/*
		 * The new np may have enough info for access
		 * checks, make sure rucred and wucred are
		 * initialized for read and write rpc's.
		 */
		np = VTONFS(newvp);
		if (np->n_rucred == NULL)
			np->n_rucred = crhold(cnp->cn_cred);
		if (np->n_wucred == NULL)
			np->n_wucred = crhold(cnp->cn_cred);
		*ap->a_vpp = newvp;
	} else if (newvp) {
		vput(newvp);
	}
	VTONFS(dvp)->n_flag |= NLMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

/*
 * nfs file remove call
 * To try and make nfs semantics closer to ufs semantics, a file that has
 * other processes using the vnode is renamed instead of removed and then
 * removed later on the last close.
 * - If v_refcnt > 1
 *	  If a rename is not already in the works
 *	     call nfs_sillyrename() to set it up
 *     else
 *	  do the remove rpc
 *
 * nfs_remove(struct vnode *a_dvp, struct vnode *a_vp,
 *	      struct componentname *a_cnp)
 */
static int
nfs_remove(struct vop_old_remove_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct nfsmount *nmp = VFSTONFS(dvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	struct nfsnode *np = VTONFS(vp);
	int error = 0;
	struct vattr vattr;

	lwkt_gettoken(&nmp->nm_token);
#ifndef DIAGNOSTIC
	if (VREFCNT(vp) < 1)
		panic("nfs_remove: bad v_refcnt");
#endif
	if (vp->v_type == VDIR) {
		error = EPERM;
	} else if (VREFCNT(vp) == 1 || (np->n_sillyrename &&
		   VOP_GETATTR(vp, &vattr) == 0 && vattr.va_nlink > 1)) {
		/*
		 * throw away biocache buffers, mainly to avoid
		 * unnecessary delayed writes later.
		 */
		error = nfs_vinvalbuf(vp, 0, 1);
		/* Do the rpc */
		if (error != EINTR)
			error = nfs_removerpc(dvp, cnp->cn_nameptr,
				cnp->cn_namelen, cnp->cn_cred, cnp->cn_td);
		/*
		 * Kludge City: If the first reply to the remove rpc is lost..
		 *   the reply to the retransmitted request will be ENOENT
		 *   since the file was in fact removed
		 *   Therefore, we cheat and return success.
		 */
		if (error == ENOENT)
			error = 0;
	} else if (!np->n_sillyrename) {
		error = nfs_sillyrename(dvp, vp, cnp);
	}
	np->n_attrstamp = 0;
	lwkt_reltoken(&nmp->nm_token);

	return (error);
}

/*
 * nfs file remove rpc called from nfs_inactive
 *
 * NOTE: s_dvp can be VBAD during a forced unmount.
 */
int
nfs_removeit(struct sillyrename *sp)
{
	if (sp->s_dvp->v_type == VBAD)
		return(0);
	return (nfs_removerpc(sp->s_dvp, sp->s_name, sp->s_namlen,
		sp->s_cred, NULL));
}

/*
 * Nfs remove rpc, called from nfs_remove() and nfs_removeit().
 */
static int
nfs_removerpc(struct vnode *dvp, const char *name, int namelen,
	      struct ucred *cred, struct thread *td)
{
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);

	nfsstats.rpccnt[NFSPROC_REMOVE]++;
	nfsm_reqhead(&info, dvp, NFSPROC_REMOVE,
		     NFSX_FH(info.v3) + NFSX_UNSIGNED + nfsm_rndup(namelen));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, name, namelen, NFS_MAXNAMLEN));
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_REMOVE, td, cred, &error));
	if (info.v3) {
		ERROROUT(nfsm_wcc_data(&info, dvp, &wccflag));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	VTONFS(dvp)->n_flag |= NLMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs file rename call
 *
 * nfs_rename(struct vnode *a_fdvp, struct vnode *a_fvp,
 *	      struct componentname *a_fcnp, struct vnode *a_tdvp,
 *	      struct vnode *a_tvp, struct componentname *a_tcnp)
 */
static int
nfs_rename(struct vop_old_rename_args *ap)
{
	struct vnode *fvp = ap->a_fvp;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct nfsmount *nmp = VFSTONFS(fdvp->v_mount);
	int error;

	lwkt_gettoken(&nmp->nm_token);

	/* Check for cross-device rename */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
		goto out;
	}

	/*
	 * We shouldn't have to flush fvp on rename for most server-side
	 * filesystems as the file handle should not change.  Unfortunately
	 * the inode for some filesystems (msdosfs) might be tied to the
	 * file name or directory position so to be completely safe
	 * vfs.nfs.flush_on_rename is set by default.  Clear to improve
	 * performance.
	 *
	 * We must flush tvp on rename because it might become stale on the
	 * server after the rename.
	 */
	if (nfs_flush_on_rename)
	    VOP_FSYNC(fvp, MNT_WAIT, 0);
	if (tvp)
	    VOP_FSYNC(tvp, MNT_WAIT, 0);

	/*
	 * If the tvp exists and is in use, sillyrename it before doing the
	 * rename of the new file over it.
	 *
	 * XXX Can't sillyrename a directory.
	 *
	 * We do not attempt to do any namecache purges in this old API
	 * routine.  The new API compat functions have access to the actual
	 * namecache structures and will do it for us.
	 */
	if (tvp && VREFCNT(tvp) > 1 && !VTONFS(tvp)->n_sillyrename &&
		tvp->v_type != VDIR && !nfs_sillyrename(tdvp, tvp, tcnp)) {
		vput(tvp);
		tvp = NULL;
	} else if (tvp) {
		;
	}

	error = nfs_renamerpc(fdvp, fcnp->cn_nameptr, fcnp->cn_namelen,
		tdvp, tcnp->cn_nameptr, tcnp->cn_namelen, tcnp->cn_cred,
		tcnp->cn_td);

out:
	lwkt_reltoken(&nmp->nm_token);
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);
	/*
	 * Kludge: Map ENOENT => 0 assuming that it is a reply to a retry.
	 */
	if (error == ENOENT)
		error = 0;
	return (error);
}

/*
 * nfs file rename rpc called from nfs_remove() above
 */
static int
nfs_renameit(struct vnode *sdvp, struct componentname *scnp,
	     struct sillyrename *sp)
{
	return (nfs_renamerpc(sdvp, scnp->cn_nameptr, scnp->cn_namelen,
		sdvp, sp->s_name, sp->s_namlen, scnp->cn_cred, scnp->cn_td));
}

/*
 * Do an nfs rename rpc. Called from nfs_rename() and nfs_renameit().
 */
static int
nfs_renamerpc(struct vnode *fdvp, const char *fnameptr, int fnamelen,
	      struct vnode *tdvp, const char *tnameptr, int tnamelen,
	      struct ucred *cred, struct thread *td)
{
	int error = 0, fwccflag = NFSV3_WCCRATTR, twccflag = NFSV3_WCCRATTR;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(fdvp);

	nfsstats.rpccnt[NFSPROC_RENAME]++;
	nfsm_reqhead(&info, fdvp, NFSPROC_RENAME,
		    (NFSX_FH(info.v3) + NFSX_UNSIGNED)*2 +
		    nfsm_rndup(fnamelen) + nfsm_rndup(tnamelen));
	ERROROUT(nfsm_fhtom(&info, fdvp));
	ERROROUT(nfsm_strtom(&info, fnameptr, fnamelen, NFS_MAXNAMLEN));
	ERROROUT(nfsm_fhtom(&info, tdvp));
	ERROROUT(nfsm_strtom(&info, tnameptr, tnamelen, NFS_MAXNAMLEN));
	NEGKEEPOUT(nfsm_request(&info, fdvp, NFSPROC_RENAME, td, cred, &error));
	if (info.v3) {
		ERROROUT(nfsm_wcc_data(&info, fdvp, &fwccflag));
		ERROROUT(nfsm_wcc_data(&info, tdvp, &twccflag));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	VTONFS(fdvp)->n_flag |= NLMODIFIED;
	VTONFS(tdvp)->n_flag |= NLMODIFIED;
	if (!fwccflag)
		VTONFS(fdvp)->n_attrstamp = 0;
	if (!twccflag)
		VTONFS(tdvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs hard link create call
 *
 * nfs_link(struct vnode *a_tdvp, struct vnode *a_vp,
 *	    struct componentname *a_cnp)
 */
static int
nfs_link(struct vop_old_link_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct nfsmount *nmp = VFSTONFS(tdvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	int error = 0, wccflag = NFSV3_WCCRATTR, attrflag = 0;
	struct nfsm_info info;

	if (vp->v_mount != tdvp->v_mount) {
		return (EXDEV);
	}
	lwkt_gettoken(&nmp->nm_token);

	/*
	 * The attribute cache may get out of sync with the server on link.
	 * Pushing writes to the server before handle was inherited from
	 * long long ago and it is unclear if we still need to do this.
	 * Defaults to off.
	 */
	if (nfs_flush_on_hlink)
		VOP_FSYNC(vp, MNT_WAIT, 0);

	info.mrep = NULL;
	info.v3 = NFS_ISV3(vp);

	nfsstats.rpccnt[NFSPROC_LINK]++;
	nfsm_reqhead(&info, vp, NFSPROC_LINK,
		     NFSX_FH(info.v3) * 2 + NFSX_UNSIGNED +
		     nfsm_rndup(cnp->cn_namelen));
	ERROROUT(nfsm_fhtom(&info, vp));
	ERROROUT(nfsm_fhtom(&info, tdvp));
	ERROROUT(nfsm_strtom(&info, cnp->cn_nameptr, cnp->cn_namelen,
			     NFS_MAXNAMLEN));
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_LINK, cnp->cn_td,
				cnp->cn_cred, &error));
	if (info.v3) {
		ERROROUT(nfsm_postop_attr(&info, vp, &attrflag,
					 NFS_LATTR_NOSHRINK));
		ERROROUT(nfsm_wcc_data(&info, tdvp, &wccflag));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	VTONFS(tdvp)->n_flag |= NLMODIFIED;
	if (!attrflag)
		VTONFS(vp)->n_attrstamp = 0;
	if (!wccflag)
		VTONFS(tdvp)->n_attrstamp = 0;
	/*
	 * Kludge: Map EEXIST => 0 assuming that it is a reply to a retry.
	 */
	if (error == EEXIST)
		error = 0;
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

/*
 * nfs symbolic link create call
 *
 * nfs_symlink(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp, struct vattr *a_vap,
 *		char *a_target)
 */
static int
nfs_symlink(struct vop_old_symlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct nfsmount *nmp = VFSTONFS(dvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	struct nfsv2_sattr *sp;
	int slen, error = 0, wccflag = NFSV3_WCCRATTR, gotvp;
	struct vnode *newvp = NULL;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);
	lwkt_gettoken(&nmp->nm_token);

	nfsstats.rpccnt[NFSPROC_SYMLINK]++;
	slen = strlen(ap->a_target);
	nfsm_reqhead(&info, dvp, NFSPROC_SYMLINK,
		     NFSX_FH(info.v3) + 2*NFSX_UNSIGNED +
		     nfsm_rndup(cnp->cn_namelen) +
		     nfsm_rndup(slen) + NFSX_SATTR(info.v3));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, cnp->cn_nameptr, cnp->cn_namelen,
			     NFS_MAXNAMLEN));
	if (info.v3) {
		nfsm_v3attrbuild(&info, vap, FALSE);
	}
	ERROROUT(nfsm_strtom(&info, ap->a_target, slen, NFS_MAXPATHLEN));
	if (info.v3 == 0) {
		sp = nfsm_build(&info, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(VLNK, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = nfs_xdrneg1;
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}

	/*
	 * Issue the NFS request and get the rpc response.
	 *
	 * Only NFSv3 responses returning an error of 0 actually return
	 * a file handle that can be converted into newvp without having
	 * to do an extra lookup rpc.
	 */
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_SYMLINK, cnp->cn_td,
				cnp->cn_cred, &error));
	if (info.v3) {
		if (error == 0) {
		       ERROROUT(nfsm_mtofh(&info, dvp, &newvp, &gotvp));
		}
		ERROROUT(nfsm_wcc_data(&info, dvp, &wccflag));
	}

	/*
	 * out code jumps -> here, mrep is also freed.
	 */

	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:

	/*
	 * If we get an EEXIST error, silently convert it to no-error
	 * in case of an NFS retry.
	 */
	if (error == EEXIST)
		error = 0;

	/*
	 * If we do not have (or no longer have) an error, and we could
	 * not extract the newvp from the response due to the request being
	 * NFSv2 or the error being EEXIST.  We have to do a lookup in order
	 * to obtain a newvp to return.  
	 */
	if (error == 0 && newvp == NULL) {
		struct nfsnode *np = NULL;

		error = nfs_lookitup(dvp, cnp->cn_nameptr, cnp->cn_namelen,
				     cnp->cn_cred, cnp->cn_td, &np);
		if (!error)
			newvp = NFSTOV(np);
	}
	if (error) {
		if (newvp)
			vput(newvp);
	} else {
		*ap->a_vpp = newvp;
	}
	VTONFS(dvp)->n_flag |= NLMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	lwkt_reltoken(&nmp->nm_token);

	return (error);
}

/*
 * nfs make dir call
 *
 * nfs_mkdir(struct vnode *a_dvp, struct vnode **a_vpp,
 *	     struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
nfs_mkdir(struct vop_old_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct nfsmount *nmp = VFSTONFS(dvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	struct nfsv2_sattr *sp;
	struct nfsnode *np = NULL;
	struct vnode *newvp = NULL;
	struct vattr vattr;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	int gotvp = 0;
	int len;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);
	lwkt_gettoken(&nmp->nm_token);

	if ((error = VOP_GETATTR(dvp, &vattr)) != 0) {
		lwkt_reltoken(&nmp->nm_token);
		return (error);
	}
	len = cnp->cn_namelen;
	nfsstats.rpccnt[NFSPROC_MKDIR]++;
	nfsm_reqhead(&info, dvp, NFSPROC_MKDIR,
		     NFSX_FH(info.v3) + NFSX_UNSIGNED +
		     nfsm_rndup(len) + NFSX_SATTR(info.v3));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, cnp->cn_nameptr, len, NFS_MAXNAMLEN));
	if (info.v3) {
		nfsm_v3attrbuild(&info, vap, FALSE);
	} else {
		sp = nfsm_build(&info, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(VDIR, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = nfs_xdrneg1;
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_MKDIR, cnp->cn_td,
		    cnp->cn_cred, &error));
	if (error == 0) {
		ERROROUT(nfsm_mtofh(&info, dvp, &newvp, &gotvp));
	}
	if (info.v3) {
		ERROROUT(nfsm_wcc_data(&info, dvp, &wccflag));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	VTONFS(dvp)->n_flag |= NLMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	/*
	 * Kludge: Map EEXIST => 0 assuming that you have a reply to a retry
	 * if we can succeed in looking up the directory.
	 */
	if (error == EEXIST || (!error && !gotvp)) {
		if (newvp) {
			vrele(newvp);
			newvp = NULL;
		}
		error = nfs_lookitup(dvp, cnp->cn_nameptr, len, cnp->cn_cred,
			cnp->cn_td, &np);
		if (!error) {
			newvp = NFSTOV(np);
			if (newvp->v_type != VDIR)
				error = EEXIST;
		}
	}
	if (error) {
		if (newvp)
			vrele(newvp);
	} else {
		*ap->a_vpp = newvp;
	}
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

/*
 * nfs remove directory call
 *
 * nfs_rmdir(struct vnode *a_dvp, struct vnode *a_vp,
 *	     struct componentname *a_cnp)
 */
static int
nfs_rmdir(struct vop_old_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct nfsmount *nmp = VFSTONFS(dvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);

	if (dvp == vp)
		return (EINVAL);

	lwkt_gettoken(&nmp->nm_token);

	nfsstats.rpccnt[NFSPROC_RMDIR]++;
	nfsm_reqhead(&info, dvp, NFSPROC_RMDIR,
		     NFSX_FH(info.v3) + NFSX_UNSIGNED +
		     nfsm_rndup(cnp->cn_namelen));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, cnp->cn_nameptr, cnp->cn_namelen,
		 NFS_MAXNAMLEN));
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_RMDIR, cnp->cn_td,
				cnp->cn_cred, &error));
	if (info.v3) {
		ERROROUT(nfsm_wcc_data(&info, dvp, &wccflag));
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	VTONFS(dvp)->n_flag |= NLMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	/*
	 * Kludge: Map ENOENT => 0 assuming that you have a reply to a retry.
	 */
	if (error == ENOENT)
		error = 0;
	lwkt_reltoken(&nmp->nm_token);

	return (error);
}

/*
 * nfs readdir call
 *
 * nfs_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred)
 */
static int
nfs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct uio *uio = ap->a_uio;
	int tresid, error;
	struct vattr vattr;

	if (vp->v_type != VDIR)
		return (EPERM);

	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error)
		return (error);

	lwkt_gettoken(&nmp->nm_token);

	/*
	 * If we have a valid EOF offset cache we must call VOP_GETATTR()
	 * and then check that is still valid, or if this is an NQNFS mount
	 * we call NQNFS_CKCACHEABLE() instead of VOP_GETATTR().  Note that
	 * VOP_GETATTR() does not necessarily go to the wire.
	 */
	if (np->n_direofoffset > 0 && uio->uio_offset >= np->n_direofoffset &&
	    (np->n_flag & (NLMODIFIED|NRMODIFIED)) == 0) {
		if (VOP_GETATTR(vp, &vattr) == 0 &&
		    (np->n_flag & (NLMODIFIED|NRMODIFIED)) == 0
		) {
			nfsstats.direofcache_hits++;
			goto done;
		}
	}

	/*
	 * Call nfs_bioread() to do the real work.  nfs_bioread() does its
	 * own cache coherency checks so we do not have to.
	 */
	tresid = uio->uio_resid;
	error = nfs_bioread(vp, uio, 0);

	if (!error && uio->uio_resid == tresid)
		nfsstats.direofcache_misses++;
done:
	lwkt_reltoken(&nmp->nm_token);
	vn_unlock(vp);

	return (error);
}

/*
 * Readdir rpc call.  nfs_bioread->nfs_doio->nfs_readdirrpc.
 *
 * Note that for directories, nfs_bioread maintains the underlying nfs-centric
 * offset/block and converts the nfs formatted directory entries for userland
 * consumption as well as deals with offsets into the middle of blocks.
 * nfs_doio only deals with logical blocks.  In particular, uio_offset will
 * be block-bounded.  It must convert to cookies for the actual RPC.
 */
int
nfs_readdirrpc_uio(struct vnode *vp, struct uio *uiop)
{
	int len, left;
	struct nfs_dirent *dp = NULL;
	u_int32_t *tl;
	nfsuint64 *cookiep;
	caddr_t cp;
	nfsuint64 cookie;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct nfsnode *dnp = VTONFS(vp);
	u_quad_t fileno;
	int error = 0, tlen, more_dirs = 1, blksiz = 0, bigenough = 1;
	int attrflag;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(vp);

#ifndef DIAGNOSTIC
	if (uiop->uio_iovcnt != 1 || (uiop->uio_offset & (DIRBLKSIZ - 1)) ||
		(uiop->uio_resid & (DIRBLKSIZ - 1)))
		panic("nfs readdirrpc bad uio");
#endif

	/*
	 * If there is no cookie, assume directory was stale.
	 */
	cookiep = nfs_getcookie(dnp, uiop->uio_offset, 0);
	if (cookiep)
		cookie = *cookiep;
	else
		return (NFSERR_BAD_COOKIE);
	/*
	 * Loop around doing readdir rpc's of size nm_readdirsize
	 * truncated to a multiple of DIRBLKSIZ.
	 * The stopping criteria is EOF or buffer full.
	 */
	while (more_dirs && bigenough) {
		nfsstats.rpccnt[NFSPROC_READDIR]++;
		nfsm_reqhead(&info, vp, NFSPROC_READDIR,
			     NFSX_FH(info.v3) + NFSX_READDIR(info.v3));
		ERROROUT(nfsm_fhtom(&info, vp));
		if (info.v3) {
			tl = nfsm_build(&info, 5 * NFSX_UNSIGNED);
			*tl++ = cookie.nfsuquad[0];
			*tl++ = cookie.nfsuquad[1];
			*tl++ = dnp->n_cookieverf.nfsuquad[0];
			*tl++ = dnp->n_cookieverf.nfsuquad[1];
		} else {
			/*
			 * WARNING!  HAMMER DIRECTORIES WILL NOT WORK WELL
			 * WITH NFSv2!!!  There's nothing I can really do
			 * about it other than to hope the server supports
			 * rdirplus w/NFSv2.
			 */
			tl = nfsm_build(&info, 2 * NFSX_UNSIGNED);
			*tl++ = cookie.nfsuquad[0];
		}
		*tl = txdr_unsigned(nmp->nm_readdirsize);
		NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_READDIR,
					uiop->uio_td,
					nfs_vpcred(vp, ND_READ), &error));
		if (info.v3) {
			ERROROUT(nfsm_postop_attr(&info, vp, &attrflag,
						  NFS_LATTR_NOSHRINK));
			NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
			dnp->n_cookieverf.nfsuquad[0] = *tl++;
			dnp->n_cookieverf.nfsuquad[1] = *tl;
		}
		NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
		more_dirs = fxdr_unsigned(int, *tl);
	
		/* loop thru the dir entries, converting them to std form */
		while (more_dirs && bigenough) {
			if (info.v3) {
				NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));
				fileno = fxdr_hyper(tl);
				len = fxdr_unsigned(int, *(tl + 2));
			} else {
				NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
				fileno = fxdr_unsigned(u_quad_t, *tl++);
				len = fxdr_unsigned(int, *tl);
			}
			if (len <= 0 || len > NFS_MAXNAMLEN) {
				error = EBADRPC;
				m_freem(info.mrep);
				info.mrep = NULL;
				goto nfsmout;
			}

			/*
			 * len is the number of bytes in the path element
			 * name, not including the \0 termination.
			 *
			 * tlen is the number of bytes w have to reserve for
			 * the path element name.
			 */
			tlen = nfsm_rndup(len);
			if (tlen == len)
				tlen += 4;	/* To ensure null termination */

			/*
			 * If the entry would cross a DIRBLKSIZ boundary, 
			 * extend the previous nfs_dirent to cover the
			 * remaining space.
			 */
			left = DIRBLKSIZ - blksiz;
			if ((tlen + sizeof(struct nfs_dirent)) > left) {
				dp->nfs_reclen += left;
				uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + left;
				uiop->uio_iov->iov_len -= left;
				uiop->uio_offset += left;
				uiop->uio_resid -= left;
				blksiz = 0;
			}
			if ((tlen + sizeof(struct nfs_dirent)) > uiop->uio_resid)
				bigenough = 0;
			if (bigenough) {
				dp = (struct nfs_dirent *)uiop->uio_iov->iov_base;
				dp->nfs_ino = fileno;
				dp->nfs_namlen = len;
				dp->nfs_reclen = tlen + sizeof(struct nfs_dirent);
				dp->nfs_type = DT_UNKNOWN;
				blksiz += dp->nfs_reclen;
				if (blksiz == DIRBLKSIZ)
					blksiz = 0;
				uiop->uio_offset += sizeof(struct nfs_dirent);
				uiop->uio_resid -= sizeof(struct nfs_dirent);
				uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + sizeof(struct nfs_dirent);
				uiop->uio_iov->iov_len -= sizeof(struct nfs_dirent);
				ERROROUT(nfsm_mtouio(&info, uiop, len));

				/*
				 * The uiop has advanced by nfs_dirent + len
				 * but really needs to advance by
				 * nfs_dirent + tlen
				 */
				cp = uiop->uio_iov->iov_base;
				tlen -= len;
				*cp = '\0';	/* null terminate */
				uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + tlen;
				uiop->uio_iov->iov_len -= tlen;
				uiop->uio_offset += tlen;
				uiop->uio_resid -= tlen;
			} else {
				/*
				 * NFS strings must be rounded up (nfsm_myouio
				 * handled that in the bigenough case).
				 */
				ERROROUT(nfsm_adv(&info, nfsm_rndup(len)));
			}
			if (info.v3) {
				NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));
			} else {
				NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
			}

			/*
			 * If we were able to accomodate the last entry,
			 * get the cookie for the next one.  Otherwise
			 * hold-over the cookie for the one we were not
			 * able to accomodate.
			 */
			if (bigenough) {
				cookie.nfsuquad[0] = *tl++;
				if (info.v3)
					cookie.nfsuquad[1] = *tl++;
			} else if (info.v3) {
				tl += 2;
			} else {
				tl++;
			}
			more_dirs = fxdr_unsigned(int, *tl);
		}
		/*
		 * If at end of rpc data, get the eof boolean
		 */
		if (!more_dirs) {
			NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
			more_dirs = (fxdr_unsigned(int, *tl) == 0);
		}
		m_freem(info.mrep);
		info.mrep = NULL;
	}
	/*
	 * Fill last record, iff any, out to a multiple of DIRBLKSIZ
	 * by increasing d_reclen for the last record.
	 */
	if (blksiz > 0) {
		left = DIRBLKSIZ - blksiz;
		dp->nfs_reclen += left;
		uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + left;
		uiop->uio_iov->iov_len -= left;
		uiop->uio_offset += left;
		uiop->uio_resid -= left;
	}

	if (bigenough) {
		/*
		 * We hit the end of the directory, update direofoffset.
		 */
		dnp->n_direofoffset = uiop->uio_offset;
	} else {
		/*
		 * There is more to go, insert the link cookie so the
		 * next block can be read.
		 */
		if (uiop->uio_resid > 0)
			kprintf("EEK! readdirrpc resid > 0\n");
		cookiep = nfs_getcookie(dnp, uiop->uio_offset, 1);
		*cookiep = cookie;
	}
nfsmout:
	return (error);
}

/*
 * NFS V3 readdir plus RPC. Used in place of nfs_readdirrpc().
 */
int
nfs_readdirplusrpc_uio(struct vnode *vp, struct uio *uiop)
{
	int len, left;
	struct nfs_dirent *dp;
	u_int32_t *tl;
	struct vnode *newvp;
	nfsuint64 *cookiep;
	caddr_t dpossav1, dpossav2;
	caddr_t cp;
	struct mbuf *mdsav1, *mdsav2;
	nfsuint64 cookie;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct nfsnode *dnp = VTONFS(vp), *np;
	nfsfh_t *fhp;
	u_quad_t fileno;
	int error = 0, tlen, more_dirs = 1, blksiz = 0, doit, bigenough = 1, i;
	int attrflag, fhsize;
	struct nchandle nch;
	struct nchandle dnch;
	struct nlcomponent nlc;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = 1;

#ifndef nolint
	dp = NULL;
#endif
#ifndef DIAGNOSTIC
	if (uiop->uio_iovcnt != 1 || (uiop->uio_offset & (DIRBLKSIZ - 1)) ||
		(uiop->uio_resid & (DIRBLKSIZ - 1)))
		panic("nfs readdirplusrpc bad uio");
#endif
	/*
	 * Obtain the namecache record for the directory so we have something
	 * to use as a basis for creating the entries.  This function will
	 * return a held (but not locked) ncp.  The ncp may be disconnected
	 * from the tree and cannot be used for upward traversals, and the
	 * ncp may be unnamed.  Note that other unrelated operations may 
	 * cause the ncp to be named at any time.
	 *
	 * We have to lock the ncp to prevent a lock order reversal when
	 * rdirplus does nlookups of the children, because the vnode is
	 * locked and has to stay that way.
	 */
	cache_fromdvp(vp, NULL, 0, &dnch);
	bzero(&nlc, sizeof(nlc));
	newvp = NULLVP;

	/*
	 * If there is no cookie, assume directory was stale.
	 */
	cookiep = nfs_getcookie(dnp, uiop->uio_offset, 0);
	if (cookiep) {
		cookie = *cookiep;
	} else {
		if (dnch.ncp)
			cache_drop(&dnch);
		return (NFSERR_BAD_COOKIE);
	}

	/*
	 * Loop around doing readdir rpc's of size nm_readdirsize
	 * truncated to a multiple of DIRBLKSIZ.
	 * The stopping criteria is EOF or buffer full.
	 */
	while (more_dirs && bigenough) {
		nfsstats.rpccnt[NFSPROC_READDIRPLUS]++;
		nfsm_reqhead(&info, vp, NFSPROC_READDIRPLUS,
			     NFSX_FH(info.v3) + 6 * NFSX_UNSIGNED);
		ERROROUT(nfsm_fhtom(&info, vp));
		tl = nfsm_build(&info, 6 * NFSX_UNSIGNED);
		*tl++ = cookie.nfsuquad[0];
		*tl++ = cookie.nfsuquad[1];
		*tl++ = dnp->n_cookieverf.nfsuquad[0];
		*tl++ = dnp->n_cookieverf.nfsuquad[1];
		*tl++ = txdr_unsigned(nmp->nm_readdirsize);
		*tl = txdr_unsigned(nmp->nm_rsize);
		NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_READDIRPLUS,
					uiop->uio_td,
					nfs_vpcred(vp, ND_READ), &error));
		ERROROUT(nfsm_postop_attr(&info, vp, &attrflag,
					  NFS_LATTR_NOSHRINK));
		NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));
		dnp->n_cookieverf.nfsuquad[0] = *tl++;
		dnp->n_cookieverf.nfsuquad[1] = *tl++;
		more_dirs = fxdr_unsigned(int, *tl);

		/* loop thru the dir entries, doctoring them to 4bsd form */
		while (more_dirs && bigenough) {
			NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));
			fileno = fxdr_hyper(tl);
			len = fxdr_unsigned(int, *(tl + 2));
			if (len <= 0 || len > NFS_MAXNAMLEN) {
				error = EBADRPC;
				m_freem(info.mrep);
				info.mrep = NULL;
				goto nfsmout;
			}
			tlen = nfsm_rndup(len);
			if (tlen == len)
				tlen += 4;	/* To ensure null termination*/
			left = DIRBLKSIZ - blksiz;
			if ((tlen + sizeof(struct nfs_dirent)) > left) {
				dp->nfs_reclen += left;
				uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + left;
				uiop->uio_iov->iov_len -= left;
				uiop->uio_offset += left;
				uiop->uio_resid -= left;
				blksiz = 0;
			}
			if ((tlen + sizeof(struct nfs_dirent)) > uiop->uio_resid)
				bigenough = 0;
			if (bigenough) {
				dp = (struct nfs_dirent *)uiop->uio_iov->iov_base;
				dp->nfs_ino = fileno;
				dp->nfs_namlen = len;
				dp->nfs_reclen = tlen + sizeof(struct nfs_dirent);
				dp->nfs_type = DT_UNKNOWN;
				blksiz += dp->nfs_reclen;
				if (blksiz == DIRBLKSIZ)
					blksiz = 0;
				uiop->uio_offset += sizeof(struct nfs_dirent);
				uiop->uio_resid -= sizeof(struct nfs_dirent);
				uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + sizeof(struct nfs_dirent);
				uiop->uio_iov->iov_len -= sizeof(struct nfs_dirent);
				nlc.nlc_nameptr = uiop->uio_iov->iov_base;
				nlc.nlc_namelen = len;
				ERROROUT(nfsm_mtouio(&info, uiop, len));
				cp = uiop->uio_iov->iov_base;
				tlen -= len;
				*cp = '\0';
				uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + tlen;
				uiop->uio_iov->iov_len -= tlen;
				uiop->uio_offset += tlen;
				uiop->uio_resid -= tlen;
			} else {
				ERROROUT(nfsm_adv(&info, nfsm_rndup(len)));
			}
			NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));
			if (bigenough) {
				cookie.nfsuquad[0] = *tl++;
				cookie.nfsuquad[1] = *tl++;
			} else {
				tl += 2;
			}

			/*
			 * Since the attributes are before the file handle
			 * (sigh), we must skip over the attributes and then
			 * come back and get them.
			 */
			attrflag = fxdr_unsigned(int, *tl);
			if (attrflag) {
			    dpossav1 = info.dpos;
			    mdsav1 = info.md;
			    ERROROUT(nfsm_adv(&info, NFSX_V3FATTR));
			    NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
			    doit = fxdr_unsigned(int, *tl);
			    if (doit) {
				NEGATIVEOUT(fhsize = nfsm_getfh(&info, &fhp));
			    }
			    if (doit && bigenough && !nlcdegenerate(&nlc) &&
				!NFS_CMPFH(dnp, fhp, fhsize)
			    ) {
				if (dnch.ncp) {
#if 0
				    kprintf("NFS/READDIRPLUS, ENTER %*.*s\n",
					nlc.nlc_namelen, nlc.nlc_namelen,
					nlc.nlc_nameptr);
#endif
				    /*
				     * This is a bit hokey but there isn't
				     * much we can do about it.  We can't
				     * hold the directory vp locked while
				     * doing lookups and gets.
				     */
				    nch = cache_nlookup_nonblock(&dnch, &nlc);
				    if (nch.ncp == NULL)
					goto rdfail;
				    cache_setunresolved(&nch);
				    error = nfs_nget_nonblock(vp->v_mount, fhp,
							      fhsize, &np,
							      NULL);
				    if (error) {
					cache_put(&nch);
					goto rdfail;
				    }
				    newvp = NFSTOV(np);
				    dpossav2 = info.dpos;
				    info.dpos = dpossav1;
				    mdsav2 = info.md;
				    info.md = mdsav1;
				    ERROROUT(nfsm_loadattr(&info, newvp, NULL));
				    info.dpos = dpossav2;
				    info.md = mdsav2;
				    dp->nfs_type =
					    IFTODT(VTTOIF(np->n_vattr.va_type));
				    nfs_cache_setvp(&nch, newvp,
						    nfspos_cache_timeout);
				    vput(newvp);
				    newvp = NULLVP;
				    cache_put(&nch);
				} else {
rdfail:
				    ;
#if 0
				    kprintf("Warning: NFS/rddirplus, "
					    "UNABLE TO ENTER %*.*s\n",
					nlc.nlc_namelen, nlc.nlc_namelen,
					nlc.nlc_nameptr);
#endif
				}
			    }
			} else {
			    /* Just skip over the file handle */
			    NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
			    i = fxdr_unsigned(int, *tl);
			    ERROROUT(nfsm_adv(&info, nfsm_rndup(i)));
			}
			NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
			more_dirs = fxdr_unsigned(int, *tl);
		}
		/*
		 * If at end of rpc data, get the eof boolean
		 */
		if (!more_dirs) {
			NULLOUT(tl = nfsm_dissect(&info, NFSX_UNSIGNED));
			more_dirs = (fxdr_unsigned(int, *tl) == 0);
		}
		m_freem(info.mrep);
		info.mrep = NULL;
	}
	/*
	 * Fill last record, iff any, out to a multiple of DIRBLKSIZ
	 * by increasing d_reclen for the last record.
	 */
	if (blksiz > 0) {
		left = DIRBLKSIZ - blksiz;
		dp->nfs_reclen += left;
		uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base + left;
		uiop->uio_iov->iov_len -= left;
		uiop->uio_offset += left;
		uiop->uio_resid -= left;
	}

	/*
	 * We are now either at the end of the directory or have filled the
	 * block.
	 */
	if (bigenough) {
		dnp->n_direofoffset = uiop->uio_offset;
	} else {
		if (uiop->uio_resid > 0)
			kprintf("EEK! readdirplusrpc resid > 0\n");
		cookiep = nfs_getcookie(dnp, uiop->uio_offset, 1);
		*cookiep = cookie;
	}
nfsmout:
	if (newvp != NULLVP) {
	        if (newvp == vp)
			vrele(newvp);
		else
			vput(newvp);
		newvp = NULLVP;
	}
	if (dnch.ncp)
		cache_drop(&dnch);
	return (error);
}

/*
 * Silly rename. To make the NFS filesystem that is stateless look a little
 * more like the "ufs" a remove of an active vnode is translated to a rename
 * to a funny looking filename that is removed by nfs_inactive on the
 * nfsnode. There is the potential for another process on a different client
 * to create the same funny name between the nfs_lookitup() fails and the
 * nfs_rename() completes, but...
 */
static int
nfs_sillyrename(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
	struct sillyrename *sp;
	struct nfsnode *np;
	int error;

	/*
	 * We previously purged dvp instead of vp.  I don't know why, it
	 * completely destroys performance.  We can't do it anyway with the
	 * new VFS API since we would be breaking the namecache topology.
	 */
	cache_purge(vp);	/* XXX */
	np = VTONFS(vp);
#ifndef DIAGNOSTIC
	if (vp->v_type == VDIR)
		panic("nfs: sillyrename dir");
#endif
	sp = kmalloc(sizeof(struct sillyrename), M_NFSREQ, M_WAITOK);
	sp->s_cred = crdup(cnp->cn_cred);
	sp->s_dvp = dvp;
	vref(dvp);

	/* Fudge together a funny name */
	sp->s_namlen = ksprintf(sp->s_name, ".nfsA%08x4.4",
				(int)(intptr_t)cnp->cn_td);

	/* Try lookitups until we get one that isn't there */
	while (nfs_lookitup(dvp, sp->s_name, sp->s_namlen, sp->s_cred,
		cnp->cn_td, NULL) == 0) {
		sp->s_name[4]++;
		if (sp->s_name[4] > 'z') {
			error = EINVAL;
			goto bad;
		}
	}
	error = nfs_renameit(dvp, cnp, sp);
	if (error)
		goto bad;
	error = nfs_lookitup(dvp, sp->s_name, sp->s_namlen, sp->s_cred,
		cnp->cn_td, &np);
	np->n_sillyrename = sp;
	return (0);
bad:
	vrele(sp->s_dvp);
	crfree(sp->s_cred);
	kfree((caddr_t)sp, M_NFSREQ);
	return (error);
}

/*
 * Look up a file name and optionally either update the file handle or
 * allocate an nfsnode, depending on the value of npp.
 * npp == NULL	--> just do the lookup
 * *npp == NULL --> allocate a new nfsnode and make sure attributes are
 *			handled too
 * *npp != NULL --> update the file handle in the vnode
 */
static int
nfs_lookitup(struct vnode *dvp, const char *name, int len, struct ucred *cred,
	     struct thread *td, struct nfsnode **npp)
{
	struct vnode *newvp = NULL;
	struct nfsnode *np, *dnp = VTONFS(dvp);
	int error = 0, fhlen, attrflag;
	nfsfh_t *nfhp;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = NFS_ISV3(dvp);

	nfsstats.rpccnt[NFSPROC_LOOKUP]++;
	nfsm_reqhead(&info, dvp, NFSPROC_LOOKUP,
		     NFSX_FH(info.v3) + NFSX_UNSIGNED + nfsm_rndup(len));
	ERROROUT(nfsm_fhtom(&info, dvp));
	ERROROUT(nfsm_strtom(&info, name, len, NFS_MAXNAMLEN));
	NEGKEEPOUT(nfsm_request(&info, dvp, NFSPROC_LOOKUP, td, cred, &error));
	if (npp && !error) {
		NEGATIVEOUT(fhlen = nfsm_getfh(&info, &nfhp));
		if (*npp) {
		    np = *npp;
		    if (np->n_fhsize > NFS_SMALLFH && fhlen <= NFS_SMALLFH) {
			kfree((caddr_t)np->n_fhp, M_NFSBIGFH);
			np->n_fhp = &np->n_fh;
		    } else if (np->n_fhsize <= NFS_SMALLFH && fhlen>NFS_SMALLFH)
			np->n_fhp =(nfsfh_t *)kmalloc(fhlen,M_NFSBIGFH,M_WAITOK);
		    bcopy((caddr_t)nfhp, (caddr_t)np->n_fhp, fhlen);
		    np->n_fhsize = fhlen;
		    newvp = NFSTOV(np);
		} else if (NFS_CMPFH(dnp, nfhp, fhlen)) {
		    vref(dvp);
		    newvp = dvp;
		} else {
		    error = nfs_nget(dvp->v_mount, nfhp, fhlen, &np, NULL);
		    if (error) {
			m_freem(info.mrep);
			info.mrep = NULL;
			return (error);
		    }
		    newvp = NFSTOV(np);
		}
		if (info.v3) {
			ERROROUT(nfsm_postop_attr(&info, newvp, &attrflag,
						  NFS_LATTR_NOSHRINK));
			if (!attrflag && *npp == NULL) {
				m_freem(info.mrep);
				info.mrep = NULL;
				if (newvp == dvp)
					vrele(newvp);
				else
					vput(newvp);
				return (ENOENT);
			}
		} else {
			ERROROUT(nfsm_loadattr(&info, newvp, NULL));
		}
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	if (npp && *npp == NULL) {
		if (error) {
			if (newvp) {
				if (newvp == dvp)
					vrele(newvp);
				else
					vput(newvp);
			}
		} else
			*npp = np;
	}
	return (error);
}

/*
 * Nfs Version 3 commit rpc
 *
 * We call it 'uio' to distinguish it from 'bio' but there is no real uio
 * involved.
 */
int
nfs_commitrpc_uio(struct vnode *vp, u_quad_t offset, int cnt, struct thread *td)
{
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct nfsm_info info;
	u_int32_t *tl;

	info.mrep = NULL;
	info.v3 = 1;
	
	if ((nmp->nm_state & NFSSTA_HASWRITEVERF) == 0)
		return (0);
	nfsstats.rpccnt[NFSPROC_COMMIT]++;
	nfsm_reqhead(&info, vp, NFSPROC_COMMIT, NFSX_FH(1));
	ERROROUT(nfsm_fhtom(&info, vp));
	tl = nfsm_build(&info, 3 * NFSX_UNSIGNED);
	txdr_hyper(offset, tl);
	tl += 2;
	*tl = txdr_unsigned(cnt);
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_COMMIT, td,
				nfs_vpcred(vp, ND_WRITE), &error));
	ERROROUT(nfsm_wcc_data(&info, vp, &wccflag));
	if (!error) {
		NULLOUT(tl = nfsm_dissect(&info, NFSX_V3WRITEVERF));
		if (bcmp((caddr_t)nmp->nm_verf, (caddr_t)tl,
			NFSX_V3WRITEVERF)) {
			bcopy((caddr_t)tl, (caddr_t)nmp->nm_verf,
				NFSX_V3WRITEVERF);
			error = NFSERR_STALEWRITEVERF;
		}
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	return (error);
}

/*
 * Kludge City..
 * - make nfs_bmap() essentially a no-op that does no translation
 * - do nfs_strategy() by doing I/O with nfs_readrpc/nfs_writerpc
 *   (Maybe I could use the process's page mapping, but I was concerned that
 *    Kernel Write might not be enabled and also figured copyout() would do
 *    a lot more work than bcopy() and also it currently happens in the
 *    context of the swapper process (2).
 *
 * nfs_bmap(struct vnode *a_vp, off_t a_loffset,
 *	    off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
static int
nfs_bmap(struct vop_bmap_args *ap)
{
	/* no token lock required */
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

/*
 * Strategy routine.
 */
static int
nfs_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp __debugvar = bio->bio_buf;
	struct nfsmount *nmp = VFSTONFS(ap->a_vp->v_mount);
	struct thread *td;
	int error;

	KASSERT(bp->b_cmd != BUF_CMD_DONE,
		("nfs_strategy: buffer %p unexpectedly marked done", bp));
	KASSERT(BUF_REFCNT(bp) > 0,
		("nfs_strategy: buffer %p not locked", bp));

	if (bio->bio_flags & BIO_SYNC)
		td = curthread;	/* XXX */
	else
		td = NULL;

	lwkt_gettoken(&nmp->nm_token);

        /*
	 * We probably don't need to push an nbio any more since no
	 * block conversion is required due to the use of 64 bit byte
	 * offsets, but do it anyway.
	 *
	 * NOTE: When NFS callers itself via this strategy routines and
	 *	 sets up a synchronous I/O, it expects the I/O to run
	 *	 synchronously (its bio_done routine just assumes it),
	 *	 so for now we have to honor the bit.
         */
	nbio = push_bio(bio);
	nbio->bio_offset = bio->bio_offset;
	nbio->bio_flags = bio->bio_flags & BIO_SYNC;

	/*
	 * If the op is asynchronous and an i/o daemon is waiting
	 * queue the request, wake it up and wait for completion
	 * otherwise just do it ourselves.
	 */
	if (bio->bio_flags & BIO_SYNC) {
		error = nfs_doio(ap->a_vp, nbio, td);
	} else {
		nfs_asyncio(ap->a_vp, nbio);
		error = 0;
	}
	lwkt_reltoken(&nmp->nm_token);

	return (error);
}

/*
 * Mmap a file
 *
 * NB Currently unsupported.
 *
 * nfs_mmap(struct vnode *a_vp, int a_fflags, struct ucred *a_cred)
 */
/* ARGSUSED */
static int
nfs_mmap(struct vop_mmap_args *ap)
{
	/* no token lock required */
	return (EINVAL);
}

/*
 * fsync vnode op. Just call nfs_flush() with commit == 1.
 *
 * nfs_fsync(struct vnode *a_vp, int a_waitfor)
 */
/* ARGSUSED */
static int
nfs_fsync(struct vop_fsync_args *ap)
{
	struct nfsmount *nmp = VFSTONFS(ap->a_vp->v_mount);
	int error;

	lwkt_gettoken(&nmp->nm_token);

	/*
	 * NOTE: Because attributes are set synchronously we currently
	 *	 do not have to implement vsetisdirty()/vclrisdirty().
	 */
	error = nfs_flush(ap->a_vp, ap->a_waitfor, curthread, 1);

	lwkt_reltoken(&nmp->nm_token);

	return error;
}

/*
 * Flush all the blocks associated with a vnode.   Dirty NFS buffers may be
 * in one of two states:  If B_NEEDCOMMIT is clear then the buffer contains
 * new NFS data which needs to be written to the server.  If B_NEEDCOMMIT is
 * set the buffer contains data that has already been written to the server
 * and which now needs a commit RPC.
 *
 * If commit is 0 we only take one pass and only flush buffers containing new
 * dirty data.
 *
 * If commit is 1 we take two passes, issuing a commit RPC in the second
 * pass.
 *
 * If waitfor is MNT_WAIT and commit is 1, we loop as many times as required
 * to completely flush all pending data.
 *
 * Note that the RB_SCAN code properly handles the case where the
 * callback might block and directly or indirectly (another thread) cause
 * the RB tree to change.
 */

#ifndef NFS_COMMITBVECSIZ
#define NFS_COMMITBVECSIZ	16
#endif

struct nfs_flush_info {
	enum { NFI_FLUSHNEW, NFI_COMMIT } mode;
	struct thread *td;
	struct vnode *vp;
	int waitfor;
	int slpflag;
	int slptimeo;
	int loops;
	struct buf *bvary[NFS_COMMITBVECSIZ];
	int bvsize;
	off_t beg_off;
	off_t end_off;
};

static int nfs_flush_bp(struct buf *bp, void *data);
static int nfs_flush_docommit(struct nfs_flush_info *info, int error);

int
nfs_flush(struct vnode *vp, int waitfor, struct thread *td, int commit)
{
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct nfs_flush_info info;
	int error;

	bzero(&info, sizeof(info));
	info.td = td;
	info.vp = vp;
	info.waitfor = waitfor;
	info.slpflag = (nmp->nm_flag & NFSMNT_INT) ? PCATCH : 0;
	info.loops = 0;
	lwkt_gettoken(&vp->v_token);

	do {
		/*
		 * Flush mode
		 */
		info.mode = NFI_FLUSHNEW;
		error = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, NULL, 
				nfs_flush_bp, &info);

		/*
		 * Take a second pass if committing and no error occured.  
		 * Clean up any left over collection (whether an error 
		 * occurs or not).
		 */
		if (commit && error == 0) {
			info.mode = NFI_COMMIT;
			error = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, NULL, 
					nfs_flush_bp, &info);
			if (info.bvsize)
				error = nfs_flush_docommit(&info, error);
		}

		/*
		 * Wait for pending I/O to complete before checking whether
		 * any further dirty buffers exist.
		 */
		while (waitfor == MNT_WAIT &&
		       bio_track_active(&vp->v_track_write)) {
			error = bio_track_wait(&vp->v_track_write,
					       info.slpflag, info.slptimeo);
			if (error) {
				/*
				 * We have to be able to break out if this 
				 * is an 'intr' mount.
				 */
				if (nfs_sigintr(nmp, NULL, td)) {
					error = -EINTR;
					break;
				}

				/*
				 * Since we do not process pending signals,
				 * once we get a PCATCH our tsleep() will no
				 * longer sleep, switch to a fixed timeout
				 * instead.
				 */
				if (info.slpflag == PCATCH) {
					info.slpflag = 0;
					info.slptimeo = 2 * hz;
				}
				error = 0;
			}
		}
		++info.loops;
		/*
		 * Loop if we are flushing synchronous as well as committing,
		 * and dirty buffers are still present.  Otherwise we might livelock.
		 */
	} while (waitfor == MNT_WAIT && commit && 
		 error == 0 && !RB_EMPTY(&vp->v_rbdirty_tree));

	/*
	 * The callbacks have to return a negative error to terminate the
	 * RB scan.
	 */
	if (error < 0)
		error = -error;

	/*
	 * Deal with any error collection
	 */
	if (np->n_flag & NWRITEERR) {
		error = np->n_error;
		np->n_flag &= ~NWRITEERR;
	}
	lwkt_reltoken(&vp->v_token);
	return (error);
}

static
int
nfs_flush_bp(struct buf *bp, void *data)
{
	struct nfs_flush_info *info = data;
	int lkflags;
	int error;
	off_t toff;

	error = 0;
	switch(info->mode) {
	case NFI_FLUSHNEW:
		error = BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT);
		if (error && info->loops && info->waitfor == MNT_WAIT) {
			error = BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT);
			if (error) {
				lkflags = LK_EXCLUSIVE | LK_SLEEPFAIL;
				if (info->slpflag & PCATCH)
					lkflags |= LK_PCATCH;
				error = BUF_TIMELOCK(bp, lkflags, "nfsfsync",
						     info->slptimeo);
			}
		}

		/*
		 * Ignore locking errors
		 */
		if (error) {
			error = 0;
			break;
		}

		/*
		 * The buffer may have changed out from under us, even if
		 * we did not block (MPSAFE).  Check again now that it is
		 * locked.
		 */
		if (bp->b_vp == info->vp &&
		    (bp->b_flags & (B_DELWRI | B_NEEDCOMMIT)) == B_DELWRI) {
			bremfree(bp);
			bawrite(bp);
		} else {
			BUF_UNLOCK(bp);
		}
		break;
	case NFI_COMMIT:
		/*
		 * Only process buffers in need of a commit which we can
		 * immediately lock.  This may prevent a buffer from being
		 * committed, but the normal flush loop will block on the
		 * same buffer so we shouldn't get into an endless loop.
		 */
		if ((bp->b_flags & (B_DELWRI | B_NEEDCOMMIT)) != 
		    (B_DELWRI | B_NEEDCOMMIT)) {
			break;
		}
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT))
			break;

		/*
		 * We must recheck after successfully locking the buffer.
		 */
		if (bp->b_vp != info->vp ||
		    (bp->b_flags & (B_DELWRI | B_NEEDCOMMIT)) !=
		    (B_DELWRI | B_NEEDCOMMIT)) {
			BUF_UNLOCK(bp);
			break;
		}

		/*
		 * NOTE: storing the bp in the bvary[] basically sets
		 * it up for a commit operation.
		 *
		 * We must call vfs_busy_pages() now so the commit operation
		 * is interlocked with user modifications to memory mapped
		 * pages.  The b_dirtyoff/b_dirtyend range is not correct
		 * until after the pages have been busied.
		 *
		 * Note: to avoid loopback deadlocks, we do not
		 * assign b_runningbufspace.
		 */
		bremfree(bp);
		bp->b_cmd = BUF_CMD_WRITE;
		vfs_busy_pages(bp->b_vp, bp);
		info->bvary[info->bvsize] = bp;
		toff = bp->b_bio2.bio_offset + bp->b_dirtyoff;
		if (info->bvsize == 0 || toff < info->beg_off)
			info->beg_off = toff;
		toff += (off_t)(bp->b_dirtyend - bp->b_dirtyoff);
		if (info->bvsize == 0 || toff > info->end_off)
			info->end_off = toff;
		++info->bvsize;
		if (info->bvsize == NFS_COMMITBVECSIZ) {
			error = nfs_flush_docommit(info, 0);
			KKASSERT(info->bvsize == 0);
		}
	}
	return (error);
}

static
int
nfs_flush_docommit(struct nfs_flush_info *info, int error)
{
	struct vnode *vp;
	struct buf *bp;
	off_t bytes;
	int retv;
	int i;

	vp = info->vp;

	if (info->bvsize > 0) {
		/*
		 * Commit data on the server, as required.  Note that
		 * nfs_commit will use the vnode's cred for the commit.
		 * The NFSv3 commit RPC is limited to a 32 bit byte count.
		 */
		bytes = info->end_off - info->beg_off;
		if (bytes > 0x40000000)
			bytes = 0x40000000;
		if (error) {
			retv = -error;
		} else {
			retv = nfs_commitrpc_uio(vp, info->beg_off,
						 (int)bytes, info->td);
			if (retv == NFSERR_STALEWRITEVERF)
				nfs_clearcommit(vp->v_mount);
		}

		/*
		 * Now, either mark the blocks I/O done or mark the
		 * blocks dirty, depending on whether the commit
		 * succeeded.
		 */
		for (i = 0; i < info->bvsize; ++i) {
			bp = info->bvary[i];
			if (retv || (bp->b_flags & B_NEEDCOMMIT) == 0) {
				/*
				 * Either an error or the original
				 * vfs_busy_pages() cleared B_NEEDCOMMIT
				 * due to finding new dirty VM pages in
				 * the buffer.
				 *
				 * Leave B_DELWRI intact.
				 */
				bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
				vfs_unbusy_pages(bp);
				bp->b_cmd = BUF_CMD_DONE;
				bqrelse(bp);
			} else {
				/*
				 * Success, remove B_DELWRI ( bundirty() ).
				 *
				 * b_dirtyoff/b_dirtyend seem to be NFS 
				 * specific.  We should probably move that
				 * into bundirty(). XXX
				 *
				 * We are faking an I/O write, we have to 
				 * start the transaction in order to
				 * immediately biodone() it.
				 */
				bundirty(bp);
				bp->b_flags &= ~B_ERROR;
				bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
				bp->b_dirtyoff = bp->b_dirtyend = 0;
				biodone(&bp->b_bio1);
			}
		}
		info->bvsize = 0;
	}
	return (error);
}

/*
 * NFS advisory byte-level locks.
 * Currently unsupported.
 *
 * nfs_advlock(struct vnode *a_vp, caddr_t a_id, int a_op, struct flock *a_fl,
 *		int a_flags)
 */
static int
nfs_advlock(struct vop_advlock_args *ap)
{
	struct nfsnode *np = VTONFS(ap->a_vp);

	/* no token lock currently required */
	/*
	 * The following kludge is to allow diskless support to work
	 * until a real NFS lockd is implemented. Basically, just pretend
	 * that this is a local lock.
	 */
	return (lf_advlock(ap, &(np->n_lockf), np->n_size));
}

/*
 * Print out the contents of an nfsnode.
 *
 * nfs_print(struct vnode *a_vp)
 */
static int
nfs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);

	kprintf("tag VT_NFS, fileid %lld fsid 0x%x",
		(long long)np->n_vattr.va_fileid, np->n_vattr.va_fsid);
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
	kprintf("\n");
	return (0);
}

/*
 * nfs special file access vnode op.
 *
 * nfs_laccess(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
static int
nfs_laccess(struct vop_access_args *ap)
{
	struct nfsmount *nmp = VFSTONFS(ap->a_vp->v_mount);
	struct vattr vattr;
	int error;

	lwkt_gettoken(&nmp->nm_token);
	error = VOP_GETATTR(ap->a_vp, &vattr);
	if (error == 0) {
		error = vop_helper_access(ap, vattr.va_uid, vattr.va_gid, 
					  vattr.va_mode, 0);
	}
	lwkt_reltoken(&nmp->nm_token);

	return (error);
}

/*
 * Read wrapper for fifos.
 *
 * nfsfifo_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		struct ucred *a_cred)
 */
static int
nfsfifo_read(struct vop_read_args *ap)
{
	struct nfsnode *np = VTONFS(ap->a_vp);

	/* no token access required */
	/*
	 * Set access flag.
	 */
	np->n_flag |= NACC;
	getnanotime(&np->n_atim);
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

/*
 * Write wrapper for fifos.
 *
 * nfsfifo_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		 struct ucred *a_cred)
 */
static int
nfsfifo_write(struct vop_write_args *ap)
{
	struct nfsnode *np = VTONFS(ap->a_vp);

	/* no token access required */
	/*
	 * Set update flag.
	 */
	np->n_flag |= NUPD;
	getnanotime(&np->n_mtim);
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

/*
 * Close wrapper for fifos.
 *
 * Update the times on the nfsnode then do fifo close.
 *
 * nfsfifo_close(struct vnode *a_vp, int a_fflag)
 */
static int
nfsfifo_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct vattr vattr;
	struct timespec ts;

	/* no token access required */

	vn_lock(vp, LK_UPGRADE | LK_RETRY); /* XXX */
	if (np->n_flag & (NACC | NUPD)) {
		getnanotime(&ts);
		if (np->n_flag & NACC)
			np->n_atim = ts;
		if (np->n_flag & NUPD)
			np->n_mtim = ts;
		np->n_flag |= NCHG;
		if (VREFCNT(vp) == 1 &&
		    (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			VATTR_NULL(&vattr);
			if (np->n_flag & NACC)
				vattr.va_atime = np->n_atim;
			if (np->n_flag & NUPD)
				vattr.va_mtime = np->n_mtim;
			(void)VOP_SETATTR(vp, &vattr, nfs_vpcred(vp, ND_WRITE));
		}
	}
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

