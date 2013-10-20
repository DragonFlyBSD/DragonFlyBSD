/*
 * Copyright (c) 1999, 2000 Boris Popov
 * All rights reserved.
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
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/nwfs/nwfs_vnops.c,v 1.6.2.3 2001/03/14 11:26:59 bp Exp $
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <netproto/ncp/ncp.h>
#include <netproto/ncp/ncp_conn.h>
#include <netproto/ncp/ncp_subr.h>
#include <netproto/ncp/nwerror.h>
#include <netproto/ncp/ncp_nls.h>

#include "nwfs.h"
#include "nwfs_node.h"
#include "nwfs_subr.h"

/*
 * Prototypes for NWFS vnode operations
 */
static int nwfs_create(struct vop_old_create_args *);
static int nwfs_mknod(struct vop_old_mknod_args *);
static int nwfs_open(struct vop_open_args *);
static int nwfs_close(struct vop_close_args *);
static int nwfs_access(struct vop_access_args *);
static int nwfs_getattr(struct vop_getattr_args *);
static int nwfs_setattr(struct vop_setattr_args *);
static int nwfs_read(struct vop_read_args *);
static int nwfs_write(struct vop_write_args *);
static int nwfs_fsync(struct vop_fsync_args *);
static int nwfs_remove(struct vop_old_remove_args *);
static int nwfs_link(struct vop_old_link_args *);
static int nwfs_lookup(struct vop_old_lookup_args *);
static int nwfs_rename(struct vop_old_rename_args *);
static int nwfs_mkdir(struct vop_old_mkdir_args *);
static int nwfs_rmdir(struct vop_old_rmdir_args *);
static int nwfs_symlink(struct vop_old_symlink_args *);
static int nwfs_readdir(struct vop_readdir_args *);
static int nwfs_bmap(struct vop_bmap_args *);
static int nwfs_strategy(struct vop_strategy_args *);
static int nwfs_print(struct vop_print_args *);
static int nwfs_pathconf(struct vop_pathconf_args *ap);

/* Global vfs data structures for nwfs */
struct vop_ops nwfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		nwfs_access,
	.vop_bmap =		nwfs_bmap,
	.vop_open =		nwfs_open,
	.vop_close =		nwfs_close,
	.vop_old_create =	nwfs_create,
	.vop_fsync =		nwfs_fsync,
	.vop_getattr =		nwfs_getattr,
	.vop_getpages =		nwfs_getpages,
	.vop_putpages =		nwfs_putpages,
	.vop_ioctl =		nwfs_ioctl,
	.vop_inactive =		nwfs_inactive,
	.vop_old_link =		nwfs_link,
	.vop_old_lookup =	nwfs_lookup,
	.vop_old_mkdir =	nwfs_mkdir,
	.vop_old_mknod =	nwfs_mknod,
	.vop_pathconf =		nwfs_pathconf,
	.vop_print =		nwfs_print,
	.vop_read =		nwfs_read,
	.vop_readdir =		nwfs_readdir,
	.vop_reclaim =		nwfs_reclaim,
	.vop_old_remove =	nwfs_remove,
	.vop_old_rename =	nwfs_rename,
	.vop_old_rmdir =	nwfs_rmdir,
	.vop_setattr =		nwfs_setattr,
	.vop_strategy =		nwfs_strategy,
	.vop_old_symlink =	nwfs_symlink,
	.vop_write =		nwfs_write
};

/*
 * nwfs_access vnode op
 *
 * nwfs_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
static int
nwfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nwmount *nmp = VTONWFS(vp);
	int error;
	int mode;

	NCPVNDEBUG("\n");
	mode = ((vp->v_type == VREG) ?  nmp->m.file_mode : nmp->m.dir_mode);
	error = vop_helper_access(ap, nmp->m.uid, nmp->m.gid, mode, 0);
	return (error);
}

/*
 * nwfs_open vnode op
 *
 * nwfs_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	     struct file *a_fp)
 */
/* ARGSUSED */
static int
nwfs_open(struct vop_open_args *ap)
{
 	thread_t td = curthread; /* XXX */
	struct vnode *vp = ap->a_vp;
	int mode = ap->a_mode;
	struct nwnode *np = VTONW(vp);
	struct ncp_open_info no;
	struct nwmount *nmp = VTONWFS(vp);
	struct vattr vattr;
	int error, nwm;

	NCPVNDEBUG("%s,%d\n",np->n_name, np->opened);
	if (vp->v_type != VREG && vp->v_type != VDIR) { 
		NCPFATAL("open vtype = %d\n", vp->v_type);
		return (EACCES);
	}
	if (vp->v_type == VDIR) return 0;	/* nothing to do now */
	if (np->n_flag & NMODIFIED) {
		if ((error = nwfs_vinvalbuf(vp, V_SAVE, 1)) == EINTR)
			return (error);
		np->n_atime = 0;
		error = VOP_GETATTR(vp, &vattr);
		if (error) 
			return (error);
		np->n_mtime = vattr.va_mtime.tv_sec;
	} else {
		error = VOP_GETATTR(vp, &vattr);
		if (error) 
			return (error);
		if (np->n_mtime != vattr.va_mtime.tv_sec) {
			if ((error = nwfs_vinvalbuf(vp, V_SAVE,	1)) == EINTR)
				return (error);
			np->n_mtime = vattr.va_mtime.tv_sec;
		}
	}
	if (np->opened) {
		np->opened++;
		return (vop_stdopen(ap));
	}
	nwm = AR_READ;
	if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0)
		nwm |= AR_WRITE;
	error = ncp_open_create_file_or_subdir(nmp, vp, 0, NULL, OC_MODE_OPEN,
					       0, nwm, &no, td, ap->a_cred);
	if (error) {
		if (mode & FWRITE)
			return EACCES;
		nwm = AR_READ;
		error = ncp_open_create_file_or_subdir(nmp, vp, 0, NULL, OC_MODE_OPEN, 0,
						   nwm, &no, td, ap->a_cred);
	}
	np->n_atime = 0;
	if (error == 0) {
		np->opened++;
		np->n_fh = no.fh;
		np->n_origfh = no.origfh;
		error = vop_stdopen(ap);
	}
	return (error);
}

/*
 * nwfs_close(struct vnode *a_vp, int a_fflag)
 */
static int
nwfs_close(struct vop_close_args *ap)
{
	thread_t td = curthread; /* XXX */
	struct vnode *vp = ap->a_vp;
	struct nwnode *np = VTONW(vp);
	int error;

	NCPVNDEBUG("name=%s,td=%p,c=%d\n",np->n_name,ap->a_td,np->opened);

	error = 0;
	if (vp->v_type == VDIR)
		goto done;
	if (np->opened == 0)
		goto done;
	error = nwfs_vinvalbuf(vp, V_SAVE, 1);
	if (np->opened == 0) {
		error = 0;	/* huh? */
		goto done;
	}
	if (--np->opened == 0) {
		error = ncp_close_file(NWFSTOCONN(VTONWFS(vp)), &np->n_fh, 
		   td, proc0.p_ucred);
	} 
	np->n_atime = 0;
done:
	vop_stdclose(ap);
	return (error);
}

/*
 * nwfs_getattr call from vfs.
 *
 * nwfs_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
nwfs_getattr(struct vop_getattr_args *ap)
{
	thread_t td = curthread; /* XXX */
	struct vnode *vp = ap->a_vp;
	struct nwnode *np = VTONW(vp);
	struct vattr *va=ap->a_vap;
	struct nwmount *nmp = VTONWFS(vp);
	struct nw_entry_info fattr;
	int error;
	u_int32_t oldsize;

	NCPVNDEBUG("%lx:%d: '%s' %d\n", (long)vp, nmp->n_volume, np->n_name, (vp->v_flag & VROOT) != 0);
	error = nwfs_attr_cachelookup(vp,va);
	if (!error) return 0;
	NCPVNDEBUG("not in cache\n");
	oldsize = np->n_size;
	if (np->n_flag & NVOLUME) {
		error = ncp_obtain_info(nmp, np->n_fid.f_id, 0, NULL, &fattr,
		    td,proc0.p_ucred);
	} else {
		error = ncp_obtain_info(nmp, np->n_fid.f_parent, np->n_nmlen, 
		    np->n_name, &fattr, td, proc0.p_ucred);
	}
	if (error) {
		NCPVNDEBUG("error %d\n", error);
		return error;
	}
	nwfs_attr_cacheenter(vp, &fattr);
	*va = np->n_vattr;
	if (np->opened)
		np->n_size = oldsize;
	return (0);
}
/*
 * nwfs_setattr call from vfs.
 *
 * nwfs_setattr(struct vnode *a_vp, struct vattr *a_vap, struct ucred *a_cred)
 */
static int
nwfs_setattr(struct vop_setattr_args *ap)
{
	thread_t td = curthread; /* XXX */
	struct vnode *vp = ap->a_vp;
	struct nwnode *np = VTONW(vp);
	struct vattr *vap = ap->a_vap;
	u_quad_t tsize=0;
	int error = 0;

	NCPVNDEBUG("\n");
	if (vap->va_flags != VNOVAL)
		return (EOPNOTSUPP);
	/*
	 * Disallow write attempts if the filesystem is mounted read-only.
	 */
  	if ((vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL || 
	     vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL ||
	     vap->va_mode != (mode_t)VNOVAL) &&(vp->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);
	if (vap->va_size != VNOVAL) {
 		switch (vp->v_type) {
 		case VDIR:
 			return (EISDIR);
 		case VREG:
			/*
			 * Disallow write attempts if the filesystem is
			 * mounted read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			vnode_pager_setsize(vp, (u_long)vap->va_size);
 			tsize = np->n_size;
 			np->n_size = vap->va_size;
			break;
 		default:
			return EINVAL;
  		};
  	}
	error = ncp_setattr(vp, vap, ap->a_cred, td);
	if (error && vap->va_size != VNOVAL) {
		np->n_size = tsize;
		vnode_pager_setsize(vp, (u_long)tsize);
	}
	np->n_atime = 0;	/* invalidate cache */
	VOP_GETATTR(vp, vap);
	np->n_mtime = vap->va_mtime.tv_sec;
	return (0);
}

/*
 * nwfs_read call.
 *
 * nwfs_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
static int
nwfs_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio=ap->a_uio;
	int error;
	NCPVNDEBUG("nwfs_read:\n");

	if (vp->v_type != VREG && vp->v_type != VDIR)
		return (EPERM);
	error = nwfs_readvnode(vp, uio, ap->a_cred);
	return error;
}

/*
 * nwfs_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
static int
nwfs_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int error;

	NCPVNDEBUG("%d,ofs=%d,sz=%d\n",vp->v_type, (int)uio->uio_offset, uio->uio_resid);

	if (vp->v_type != VREG)
		return (EPERM);
	error = nwfs_writevnode(vp, uio, ap->a_cred,ap->a_ioflag);
	return(error);
}
/*
 * nwfs_create call
 * Create a regular file. On entry the directory to contain the file being
 * created is locked.  We must release before we return. 
 *
 * nwfs_create(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnpl, struct vattr *a_vap)
 */
static int
nwfs_create(struct vop_old_create_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct vnode **vpp=ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vnode *vp = NULL;
	int error = 0, fmode;
	struct vattr vattr;
	struct nwnode *np;
	struct ncp_open_info no;
	struct nwmount *nmp=VTONWFS(dvp);
	ncpfid fid;
	

	NCPVNDEBUG("\n");
	*vpp = NULL;
	if (vap->va_type == VSOCK)
		return (EOPNOTSUPP);
	if ((error = VOP_GETATTR(dvp, &vattr))) {
		return (error);
	}
	fmode = AR_READ | AR_WRITE;
/*	if (vap->va_vaflags & VA_EXCLUSIVE)
		fmode |= AR_DENY_READ | AR_DENY_WRITE;*/
	
	error = ncp_open_create_file_or_subdir(nmp,dvp,cnp->cn_namelen,cnp->cn_nameptr, 
			   OC_MODE_CREATE | OC_MODE_OPEN | OC_MODE_REPLACE,
			   0, fmode, &no, cnp->cn_td, cnp->cn_cred);
	if (!error) {
		error = ncp_close_file(NWFSTOCONN(nmp), &no.fh, cnp->cn_td,cnp->cn_cred);
		fid.f_parent = VTONW(dvp)->n_fid.f_id;
		fid.f_id = no.fattr.dirEntNum;
		error = nwfs_nget(VTOVFS(dvp), fid, &no.fattr, dvp, &vp);
		if (!error) {
			np = VTONW(vp);
			np->opened = 0;
			*vpp = vp;
		}
	}
	return (error);
}

/*
 * nwfs_remove call. It isn't possible to emulate UFS behaivour because
 * NetWare doesn't allow delete/rename operations on an opened file.
 *
 * nwfs_remove(struct vnode *a_dvp,
 *	       struct vnode *a_vp, struct componentname *a_cnp)
 */
static int
nwfs_remove(struct vop_old_remove_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct nwnode *np = VTONW(vp);
	struct nwmount *nmp = VTONWFS(vp);
	int error;

	if (vp->v_type == VDIR || np->opened || VREFCNT(vp) > 1) {
		error = EPERM;
	} else if (!ncp_conn_valid(NWFSTOCONN(nmp))) {
		error = EIO;
	} else {
		error = ncp_DeleteNSEntry(nmp, VTONW(dvp)->n_fid.f_id,
		    cnp->cn_namelen,cnp->cn_nameptr,cnp->cn_td,cnp->cn_cred);
		if (error == 0)
			np->n_flag |= NSHOULDFREE;
		else if (error == 0x899c)
			error = EACCES;
	}
	return (error);
}

/*
 * nwfs_file rename call
 *
 * nwfs_rename(struct vnode *a_fdvp, struct vnode *a_fvp,
 *		struct componentname *a_fcnp, struct vnode *a_tdvp,
 *		struct vnode *a_tvp, struct componentname *a_tcnp)
 */
static int
nwfs_rename(struct vop_old_rename_args *ap)
{
	struct vnode *fvp = ap->a_fvp;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct nwmount *nmp=VTONWFS(fvp);
	u_int16_t oldtype = 6;
	int error=0;

	/* Check for cross-device rename */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
		goto out;
	}

	if (tvp && VREFCNT(tvp) > 1) {
		error = EBUSY;
		goto out;
	}
	if (tvp && tvp != fvp) {
		error = ncp_DeleteNSEntry(nmp, VTONW(tdvp)->n_fid.f_id,
		    tcnp->cn_namelen, tcnp->cn_nameptr, 
		    tcnp->cn_td, tcnp->cn_cred);
		if (error == 0x899c) error = EACCES;
		if (error)
			goto out;
	}
	if (fvp->v_type == VDIR) {
		oldtype |= NW_TYPE_SUBDIR;
	} else if (fvp->v_type == VREG) {
		oldtype |= NW_TYPE_FILE;
	} else
		return EINVAL;
	error = ncp_nsrename(NWFSTOCONN(nmp), nmp->n_volume, nmp->name_space, 
		oldtype, &nmp->m.nls,
		VTONW(fdvp)->n_fid.f_id, fcnp->cn_nameptr, fcnp->cn_namelen,
		VTONW(tdvp)->n_fid.f_id, tcnp->cn_nameptr, tcnp->cn_namelen,
		tcnp->cn_td,tcnp->cn_cred);

	if (error == 0x8992)
		error = EEXIST;
out:
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);
	nwfs_attr_cacheremove(fdvp);
	nwfs_attr_cacheremove(tdvp);
	/*
	 * Need to get rid of old vnodes, because netware will change
	 * file id on rename
	 */
	vgone_vxlocked(fvp);
	if (tvp)
		vgone_vxlocked(tvp);
	/*
	 * Kludge: Map ENOENT => 0 assuming that it is a reply to a retry.
	 */
	if (error == ENOENT)
		error = 0;
	return (error);
}

/*
 * nwfs hard link create call
 * Netware filesystems don't know what links are.
 *
 * nwfs_link(struct vnode *a_tdvp, struct vnode *a_vp,
 *	     struct componentname *a_cnp)
 */
static int
nwfs_link(struct vop_old_link_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * nwfs_symlink link create call
 * Netware filesystems don't know what symlinks are.
 *
 * nwfs_symlink(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp, struct vattr *a_vap,
 *		char *a_target)
 */
static int
nwfs_symlink(struct vop_old_symlink_args *ap)
{
	return (EOPNOTSUPP);
}

static int nwfs_mknod(struct vop_old_mknod_args *ap)
{
	return (EOPNOTSUPP);
}

/*
 * nwfs_mkdir call
 *
 * nwfs_mkdir(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
nwfs_mkdir(struct vop_old_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
/*	struct vattr *vap = ap->a_vap;*/
	struct componentname *cnp = ap->a_cnp;
	int len=cnp->cn_namelen;
	struct ncp_open_info no;
	struct vnode *newvp = NULL;
	ncpfid fid;
	int error = 0;
	struct vattr vattr;
	char *name=cnp->cn_nameptr;

	if ((error = VOP_GETATTR(dvp, &vattr))) {
		return (error);
	}	
	if ((name[0] == '.') && ((len == 1) || ((len == 2) && (name[1] == '.')))) {
		return EEXIST;
	}
	if (ncp_open_create_file_or_subdir(VTONWFS(dvp),dvp, cnp->cn_namelen,
			cnp->cn_nameptr,OC_MODE_CREATE, aDIR, 0xffff,
			&no, cnp->cn_td, cnp->cn_cred) != 0) {
		error = EACCES;
	} else {
		error = 0;
        }
	if (!error) {
		fid.f_parent = VTONW(dvp)->n_fid.f_id;
		fid.f_id = no.fattr.dirEntNum;
		error = nwfs_nget(VTOVFS(dvp), fid, &no.fattr, dvp, &newvp);
		if (!error) {
			newvp->v_type = VDIR;
			*ap->a_vpp = newvp;
		}
	}
	return (error);
}

/*
 * nwfs_remove directory call
 *
 * nwfs_rmdir(struct vnode *a_dvp, struct vnode *a_vp,
 *	      struct componentname *a_cnp)
 */
static int
nwfs_rmdir(struct vop_old_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct nwnode *np = VTONW(vp);
	struct nwmount *nmp = VTONWFS(vp);
	struct nwnode *dnp = VTONW(dvp);
	int error = EIO;

	if (dvp == vp)
		return EINVAL;

	error = ncp_DeleteNSEntry(nmp, dnp->n_fid.f_id, 
		cnp->cn_namelen, cnp->cn_nameptr,cnp->cn_td,cnp->cn_cred);
	if (error == 0)
		np->n_flag |= NSHOULDFREE;
	else if (error == NWE_DIR_NOT_EMPTY)
		error = ENOTEMPTY;
	dnp->n_flag |= NMODIFIED;
	nwfs_attr_cacheremove(dvp);
	return (error);
}

/*
 * nwfs_readdir call
 *
 * nwfs_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred,
 *		int *a_eofflag, off_t *a_cookies, int a_ncookies)
 */
static int
nwfs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int error;

	if (vp->v_type != VDIR)
		return (EPERM);
	if (ap->a_ncookies) {
		kprintf("nwfs_readdir: no support for cookies now...");
		return (EOPNOTSUPP);
	}
	if ((error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY)) != 0)
		return (error);
	error = nwfs_readvnode(vp, uio, ap->a_cred);
	vn_unlock(vp);
	return error;
}

/*
 * nwfs_fsync(struct vnode *a_vp, int a_waitfor)
 */
/* ARGSUSED */
static int
nwfs_fsync(struct vop_fsync_args *ap)
{
/*	return (nfs_flush(ap->a_vp, ap->a_waitfor, curthread, 1));*/
    return (0);
}

/*
 * nwfs_print(struct vnode *a_vp)
 */
/* ARGSUSED */
static int
nwfs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nwnode *np = VTONW(vp);

	kprintf("nwfs node: name = '%s', fid = %d, pfid = %d\n",
	    np->n_name, np->n_fid.f_id, np->n_fid.f_parent);
	return (0);
}

/*
 * nwfs_pathconf(struct vnode *vp, int name, register_t *retval)
 */
static int
nwfs_pathconf(struct vop_pathconf_args *ap)
{
	int name=ap->a_name, error=0;
	register_t *retval=ap->a_retval;
	
	switch(name){
		case _PC_LINK_MAX:
		        *retval=0;
			break;
		case _PC_NAME_MAX:
			*retval=NCP_MAX_FILENAME; /* XXX from nwfsnode */
			break;
		case _PC_PATH_MAX:
			*retval=NCP_MAXPATHLEN; /* XXX from nwfsnode */
			break;
		default:
			error=EINVAL;
	}
	return(error);
}

/*
 * nwfs_strategy(struct vnode *a_vp, struct bio *a_bio)
 */
static int
nwfs_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	int error = 0;
	struct thread *td = NULL;

	NCPVNDEBUG("\n");
	if ((bio->bio_flags & BIO_SYNC))
		td = curthread;		/* YYY dunno if this is legal */
	/*
	 * If the op is asynchronous and an i/o daemon is waiting
	 * queue the request, wake it up and wait for completion
	 * otherwise just do it ourselves.
	 */
	if (bio->bio_flags & BIO_SYNC)
		error = nwfs_doio(ap->a_vp, bio, proc0.p_ucred, td);
	return (error);
}

/*
 * nwfs_bmap(struct vnode *a_vp, off_t a_loffset,
 *	     off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
static int
nwfs_bmap(struct vop_bmap_args *ap)
{
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

int
nwfs_nget(struct mount *mp, ncpfid fid, const struct nw_entry_info *fap,
	  struct vnode *dvp, struct vnode **vpp)
{
	int error;
	struct nwnode *newnp;
	struct vnode *vp;

	*vpp = NULL;
	error = nwfs_allocvp(mp, fid, &vp);
	if (error)
		return error;
	newnp = VTONW(vp);
	if (fap) {
		newnp->n_attr = fap->attributes;
		vp->v_type = newnp->n_attr & aDIR ? VDIR : VREG;
		nwfs_attr_cacheenter(vp, fap);
	}
	if (dvp) {
		newnp->n_parent = VTONW(dvp)->n_fid;
		if ((newnp->n_flag & NNEW) && vp->v_type == VDIR) {
			if ((dvp->v_flag & VROOT) == 0) {
				newnp->n_refparent = 1;
				vref(dvp);	/* vhold */
			}
		}
	} else {
		if ((newnp->n_flag & NNEW) && vp->v_type == VREG)
			kprintf("new vnode '%s' borned without parent ?\n",newnp->n_name);
	}
	newnp->n_flag &= ~NNEW;
	*vpp = vp;
	return 0;
}

/*
 * How to keep the brain busy ...
 * Currently lookup routine can make two lookup for vnode. This can be
 * avoided by reorg the code.
 *
 * nwfs_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *	       struct componentname *a_cnp)
 */
int
nwfs_lookup(struct vop_old_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	int flags = cnp->cn_flags;
	struct vnode *vp;
	struct nwmount *nmp;
	struct mount *mp = dvp->v_mount;
	struct nwnode *dnp, *npp;
	struct nw_entry_info fattr, *fap;
	ncpfid fid;
	int nameiop=cnp->cn_nameiop;
	int lockparent, wantparent, error = 0, notfound;
	struct thread *td = cnp->cn_td;
	char _name[cnp->cn_namelen+1];
	bcopy(cnp->cn_nameptr,_name,cnp->cn_namelen);
	_name[cnp->cn_namelen]=0;
	
	if (dvp->v_type != VDIR)
		return (ENOTDIR);
	if ((flags & CNP_ISDOTDOT) && (dvp->v_flag & VROOT)) {
		kprintf("nwfs_lookup: invalid '..'\n");
		return EIO;
	}

	NCPVNDEBUG("%d '%s' in '%s' id=d\n", nameiop, _name, 
		VTONW(dvp)->n_name/*, VTONW(dvp)->n_name*/);

	if ((mp->mnt_flag & MNT_RDONLY) && nameiop != NAMEI_LOOKUP)
		return (EROFS);
	if ((error = VOP_EACCESS(dvp, VEXEC, cnp->cn_cred)))
		return (error);
	lockparent = flags & CNP_LOCKPARENT;
	wantparent = flags & (CNP_LOCKPARENT | CNP_WANTPARENT);
	nmp = VFSTONWFS(mp);
	dnp = VTONW(dvp);
/*
kprintf("dvp %d:%d:%d\n", (int)mp, (int)dvp->v_flag & VROOT, (int)flags & CNP_ISDOTDOT);
*/
	error = ncp_pathcheck(cnp->cn_nameptr, cnp->cn_namelen, &nmp->m.nls, 
	    (nameiop == NAMEI_CREATE || nameiop == NAMEI_RENAME) && (nmp->m.nls.opt & NWHP_NOSTRICT) == 0);
	if (error) 
	    return ENOENT;

	error = 0;
	*vpp = NULLVP;
	fap = NULL;
	if (flags & CNP_ISDOTDOT) {
		if (NWCMPF(&dnp->n_parent, &nmp->n_rootent)) {
			fid = nmp->n_rootent;
			fap = NULL;
			notfound = 0;
		} else {
			error = nwfs_lookupnp(nmp, dnp->n_parent, td, &npp);
			if (error) {
				return error;
			}
			fid = dnp->n_parent;
			fap = &fattr;
			/*np = *npp;*/
			notfound = ncp_obtain_info(nmp, npp->n_dosfid,
			    0, NULL, fap, td, cnp->cn_cred);
		}
	} else {
		fap = &fattr;
		notfound = ncp_lookup(dvp, cnp->cn_namelen, cnp->cn_nameptr,
			fap, td, cnp->cn_cred);
		fid.f_id = fap->dirEntNum;
		if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
			fid.f_parent = dnp->n_fid.f_parent;
		} else
			fid.f_parent = dnp->n_fid.f_id;
		NCPVNDEBUG("call to ncp_lookup returned=%d\n",notfound);
	}
	if (notfound && notfound < 0x80 )
		return (notfound);	/* hard error */
	if (notfound) { /* entry not found */
		/* Handle RENAME or CREATE case... */
		if ((nameiop == NAMEI_CREATE || nameiop == NAMEI_RENAME) && wantparent) {
			if (!lockparent)
				vn_unlock(dvp);
			return (EJUSTRETURN);
		}
		return ENOENT;
	}/* else {
		NCPVNDEBUG("Found entry %s with id=%d\n", fap->entryName, fap->dirEntNum);
	}*/
	/* handle DELETE case ... */
	if (nameiop == NAMEI_DELETE) { 	/* delete last component */
		error = VOP_EACCESS(dvp, VWRITE, cnp->cn_cred);
		if (error) return (error);
		if (NWCMPF(&dnp->n_fid, &fid)) {	/* we found ourselfs */
			vref(dvp);
			*vpp = dvp;
			return 0;
		}
		error = nwfs_nget(mp, fid, fap, dvp, &vp);
		if (error) return (error);
		*vpp = vp;
		if (!lockparent) vn_unlock(dvp);
		return (0);
	}
	if (nameiop == NAMEI_RENAME && wantparent) {
		error = VOP_EACCESS(dvp, VWRITE, cnp->cn_cred);
		if (error) return (error);
		if (NWCMPF(&dnp->n_fid, &fid)) return EISDIR;
		error = nwfs_nget(mp, fid, fap, dvp, &vp);
		if (error) return (error);
		*vpp = vp;
		if (!lockparent)
			vn_unlock(dvp);
		return (0);
	}
	if (flags & CNP_ISDOTDOT) {
		vn_unlock(dvp);	/* race to get the inode */
		error = nwfs_nget(mp, fid, NULL, NULL, &vp);
		if (error) {
			vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
			return (error);
		}
		if (lockparent && (error = vn_lock(dvp, LK_EXCLUSIVE))) {
		    	vput(vp);
			return (error);
		}
		*vpp = vp;
	} else if (NWCMPF(&dnp->n_fid, &fid)) {
		vref(dvp);
		*vpp = dvp;
	} else {
		error = nwfs_nget(mp, fid, fap, dvp, &vp);
		if (error) return (error);
		*vpp = vp;
		NCPVNDEBUG("lookup: getnewvp!\n");
		if (!lockparent)
			vn_unlock(dvp);
	}
#if 0
	/* XXX MOVE TO NREMOVE */
	if ((cnp->cn_flags & CNP_MAKEENTRY)) {
		VTONW(*vpp)->n_ctime = VTONW(*vpp)->n_vattr.va_ctime.tv_sec;
		/* XXX */
	}
#endif
	return (0);
}
