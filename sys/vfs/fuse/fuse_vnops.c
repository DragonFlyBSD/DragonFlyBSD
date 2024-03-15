/*-
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
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
 */

#include "fuse.h"

#include <sys/fcntl.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/mountctl.h>
#include <sys/kern_syscall.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vnode_pager.h>
#include <vm/vm_pageout.h>

#include <sys/buf2.h>
#include <vm/vm_page2.h>

static int fuse_reg_resize(struct vnode *vp, off_t newsize, int trivial);
static void fuse_io_execute(struct fuse_mount *fmp, struct bio *bio);

static int
fuse_set_attr(struct fuse_node *fnp, struct fuse_attr *fat)
{
	struct vattr *vap = &fnp->attr;
	int error = 0;

	vattr_null(vap);

	vap->va_type = IFTOVT(fat->mode);
	vap->va_size = (fnp->sizeoverride ? fnp->size : fat->size);
	vap->va_bytes = fat->blocks * S_BLKSIZE;
	vap->va_mode = fat->mode & ~S_IFMT;
	if (!fat->nlink) /* XXX .fuse_hidden* has 0 link */
		vap->va_nlink = 1;
	else
		vap->va_nlink = fat->nlink;
	vap->va_uid = fat->uid;
	vap->va_gid = fat->gid;
	vap->va_fsid = fnp->fmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = fat->ino;
	vap->va_blocksize = FUSE_BLKSIZE;
	vap->va_rmajor = VNOVAL;
	vap->va_rminor = VNOVAL;
	vap->va_atime.tv_sec = fat->atime;
	vap->va_atime.tv_nsec = fat->atimensec;
	vap->va_mtime.tv_sec = fat->mtime;
	vap->va_mtime.tv_nsec = fat->mtimensec;
	vap->va_ctime.tv_sec = fat->ctime;
	vap->va_ctime.tv_nsec = fat->ctimensec;
	vap->va_flags = 0;
	vap->va_gen = VNOVAL;
	vap->va_vaflags = 0;

	KKASSERT(vap->va_type == fnp->type);

	/*
	 * NOTE: fnp->nlink just founds fep entries, it is not the
	 *	 nlink file attribute.
	 */
	if (fnp->vp->v_object && fnp->sizeoverride == 0 &&
	    fnp->size != vap->va_size)
	{
		error = fuse_node_truncate(fnp, fnp->size, vap->va_size);
	}

	fnp->attrgood = 1;

	return error;
}

static int
fuse_vop_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	mode_t mode = ap->a_mode;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_ipc *fip;
	struct fuse_access_in *fai;
	uint32_t mask;
	int error;

	if (fuse_test_dead(fmp))
		return 0;

	if (fuse_test_nosys(fmp, FUSE_ACCESS))
		return 0;

	switch (vp->v_type) {
	case VDIR:
	case VLNK:
	case VREG:
		if ((mode & VWRITE) && (vp->v_mount->mnt_flag & MNT_RDONLY))
			return EROFS;
		break;
	case VBLK:
	case VCHR:
	case VSOCK:
	case VFIFO:
		break;
	default:
		return EINVAL;
	}

	mask = F_OK;
	if (mode & VEXEC)
		mask |= X_OK;
	if (mode & VWRITE)
		mask |= W_OK;
	if (mode & VREAD)
		mask |= R_OK;

	fip = fuse_ipc_get(fmp, sizeof(*fai));
	fai = fuse_ipc_fill(fip, FUSE_ACCESS, VTOI(vp)->ino, ap->a_cred);
	fai->mask = mask;

	error = fuse_ipc_tx(fip);
	if (error) {
		if (error == ENOSYS)
			error = 0;
		if (error == ENOTCONN && (vp->v_flag & VROOT))
			error = 0;
		return error;
	}

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp = VTOI(vp);
	struct fuse_ipc *fip;
	struct fuse_open_in *foi;
	struct fuse_open_out *foo;
	int error, op;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_OPEN))
		return EOPNOTSUPP;

	/*
	 * Reopen with userland process if the vnode doesn't have a
	 * file-handle.  This can occur if the vnode is new or if it
	 * was previously deactivated.
	 */
	if (fnp->fh == 0) {
		if (vp->v_type == VDIR)
			op = FUSE_OPENDIR;
		else
			op = FUSE_OPEN;

		fip = fuse_ipc_get(fmp, sizeof(*foi));
		foi = fuse_ipc_fill(fip, op, fnp->ino, ap->a_cred);
		foi->flags = OFLAGS(ap->a_mode);
		fuse_dbg("flags=%X\n", foi->flags);
		if (foi->flags & O_CREAT) {
			fuse_dbg("drop O_CREAT\n");
			foi->flags &= ~O_CREAT;
		}

		error = fuse_ipc_tx(fip);
		if (error)
			return error;

		/* XXX unused */
		foo = fuse_out_data(fip);
		if (foo->open_flags & FOPEN_DIRECT_IO)
			;
		else if (foo->open_flags & FOPEN_KEEP_CACHE)
			;
		else if (foo->open_flags & FOPEN_NONSEEKABLE)
			;
		else if (foo->open_flags & FOPEN_CACHE_DIR)
			;

		fnp->closed = false;
		fnp->fh = foo->fh;
		fuse_ipc_put(fip);
	}

	return vop_stdopen(ap);
}

/*
 * NOTE: We do not release the file-handle on close() as the vnode
 *	 may still be in active use as an active directory or memory-mapped.
 *
 *	 However, to reduce overhead we issue vfinalize() to tell the kernel
 *	 to attempt to finalize (deactivate) the vnode as soon as it can.
 */
static int
fuse_vop_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);

	if (fuse_test_dead(fmp))
		return 0;

	if (fuse_test_nosys(fmp, FUSE_RELEASE) ||
	    fuse_test_nosys(fmp, FUSE_RELEASEDIR))
	{
		return EOPNOTSUPP;
	}

	/*
	 * Finalize immediately if not dirty, otherwise we will check
	 * during the fsync and try to finalize then.
	 */
	if ((vp->v_flag & VISDIRTY) == 0 &&
	    RB_EMPTY(&vp->v_rbdirty_tree))
	{
		vfinalize(vp);
	}

	return vop_stdclose(ap);
}

static int
fuse_vop_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_ipc *fip;
	struct fuse_fsync_in *fsi;
	struct fuse_node *fnp = VTOI(vp);
	int error, op;

	if (fuse_test_dead(fmp))
		return 0;

	if (fuse_test_nosys(fmp, FUSE_FSYNC))
		return 0;

	/*
	 * fsync any dirty buffers, wait for completion.
	 */
	vclrisdirty(vp);
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);
	bio_track_wait(&vp->v_track_write, 0, 0);
	fnp->sizeoverride = 0;

	/*
	 * Ask DragonFly to deactivate the vnode ASAP if it is no longer
	 * open.
	 */
	if (vp->v_opencount == 0)
		vfinalize(vp);

	if (vp->v_type == VDIR)
		op = FUSE_FSYNCDIR;
	else
		op = FUSE_FSYNC;

	fip = fuse_ipc_get(fmp, sizeof(*fsi));
	fsi = fuse_ipc_fill(fip, op, VTOI(vp)->ino, NULL);
	fsi->fh = VTOI(vp)->fh;
	fsi->fsync_flags = 1; /* datasync */

	error = fuse_ipc_tx(fip);
	if (error)
		return error;
	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp = VTOI(vp);
	struct fuse_ipc *fip;
	struct fuse_getattr_in *fgi;
	struct fuse_attr_out *fao;
	int error;

	if (fuse_test_dead(fmp))
		return 0;

	if (fuse_test_nosys(fmp, FUSE_GETATTR))
		return 0;

	if (fnp->attrgood == 0) {
		/*
		 * Acquire new attribute
		 */
		fip = fuse_ipc_get(fmp, sizeof(*fgi));
		fgi = fuse_ipc_fill(fip, FUSE_GETATTR, fnp->ino, NULL);
#if 0
		/* this may be called before open when fh is 0 */
		fgi->getattr_flags |= FUSE_GETATTR_FH;
		fgi->fh = fnp->fh;
#endif
		error = fuse_ipc_tx(fip);
		if (error) {
			if (error == ENOSYS)
				error = 0;
			if (error == ENOTCONN && (vp->v_flag & VROOT)) {
				memset(vap, 0, sizeof(*vap));
				vap->va_type = vp->v_type;
				error = 0;
			}
			return error;
		}

		fao = fuse_out_data(fip);
		mtx_lock(&fnp->node_lock);
		fuse_set_attr(fnp, &fao->attr);
		memcpy(vap, &fnp->attr, sizeof(*vap));
		/* unused */
		//fao->attr_valid;
		//fao->attr_valid_nsec;
		mtx_unlock(&fnp->node_lock);

		fuse_ipc_put(fip);
	} else {
		/*
		 * Use cached attribute
		 */
		memcpy(vap, &fnp->attr, sizeof(*vap));
	}

	if (vap->va_type != vp->v_type)
		return EINVAL;

	return 0;
}

static int
fuse_vop_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp = VTOI(vp);
	struct fuse_ipc *fip;
	struct fuse_setattr_in *fsi, arg;
	struct fuse_attr_out *fao;
	int kflags = 0;
	int error = 0;

	if (fuse_test_dead(fmp))
		return 0;

	if (fuse_test_nosys(fmp, FUSE_SETATTR))
		return 0;

	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	memset(&arg, 0, sizeof(arg));
	mtx_lock(&fnp->node_lock);

	if (!error && (vap->va_flags != VNOVAL)) {
		mtx_unlock(&fnp->node_lock);
		kflags |= NOTE_ATTRIB;
		return EOPNOTSUPP; /* XXX */
	}

	if (!error && (vap->va_size != VNOVAL)) {
		if (vp->v_type == VDIR) {
			mtx_unlock(&fnp->node_lock);
			return EISDIR;
		}
		if (vp->v_type == VREG &&
		    (vp->v_mount->mnt_flag & MNT_RDONLY)) {
			mtx_unlock(&fnp->node_lock);
			return EROFS;
		}
		arg.size = vap->va_size;
		arg.valid |= FATTR_SIZE;
		if (vap->va_size > fnp->size)
			kflags |= NOTE_WRITE | NOTE_EXTEND;
		else
			kflags |= NOTE_WRITE;
	}

	if (!error && (vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL)) {
		mode_t mode;
		error = vop_helper_chown(vp, vap->va_uid, vap->va_gid,
		    ap->a_cred, &arg.uid, &arg.gid, &mode);
		arg.valid |= FATTR_UID;
		arg.valid |= FATTR_GID;
		kflags |= NOTE_ATTRIB;
	}

	if (!error && (vap->va_mode != (mode_t)VNOVAL)) {
		error = vop_helper_chmod(vp, vap->va_mode, ap->a_cred,
		    vap->va_uid, vap->va_gid, (mode_t*)&arg.mode);
		arg.valid |= FATTR_MODE;
		kflags |= NOTE_ATTRIB;
	}

	if (!error && (vap->va_atime.tv_sec != VNOVAL &&
	    vap->va_atime.tv_nsec != VNOVAL)) {
		arg.atime = vap->va_atime.tv_sec;
		arg.atimensec = vap->va_atime.tv_nsec;
		arg.valid |= FATTR_ATIME;
		kflags |= NOTE_ATTRIB;
	}

	if (!error && (vap->va_mtime.tv_sec != VNOVAL &&
	    vap->va_mtime.tv_nsec != VNOVAL)) {
		arg.mtime = vap->va_mtime.tv_sec;
		arg.mtimensec = vap->va_mtime.tv_nsec;
		arg.valid |= FATTR_MTIME;
		kflags |= NOTE_ATTRIB;
	}

	if (!error && (vap->va_ctime.tv_sec != VNOVAL &&
	    vap->va_ctime.tv_nsec != VNOVAL)) {
		arg.ctime = vap->va_ctime.tv_sec;
		arg.ctimensec = vap->va_ctime.tv_nsec;
		arg.valid |= FATTR_CTIME;
		kflags |= NOTE_ATTRIB;
	}

	mtx_unlock(&fnp->node_lock);

	if (error)
		return error;
	if (!arg.valid)
		return 0;

	fip = fuse_ipc_get(fmp, sizeof(*fsi));
	fsi = fuse_ipc_fill(fip, FUSE_SETATTR, fnp->ino, ap->a_cred);
	memcpy(fsi, &arg, sizeof(arg));
#if 0
	fsi->valid |= FATTR_FH;
	fsi->fh = fnp->fh;
#endif
	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	fao = fuse_out_data(fip);
	if (IFTOVT(fao->attr.mode) != vp->v_type) {
		fuse_ipc_put(fip);
		return EINVAL;
	}
	mtx_lock(&fnp->node_lock);
	fuse_set_attr(fnp, &fao->attr);
	/* unused */
	//fao->attr_valid;
	//fao->attr_valid_nsec;
	mtx_unlock(&fnp->node_lock);

	fuse_ipc_put(fip);
	fuse_knote(vp, kflags);

	return 0;
}

static int
fuse_vop_nresolve(struct vop_nresolve_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(dvp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_ipc *fip;
	struct fuse_entry_out *feo;
	char *p, tmp[1024];
	uint32_t mode;
	enum vtype vtyp;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_LOOKUP))
		return EOPNOTSUPP;

	fip = fuse_ipc_get(fmp, ncp->nc_nlen + 1);
	p = fuse_ipc_fill(fip, FUSE_LOOKUP, dfnp->ino, ap->a_cred);

	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';
	strlcpy(tmp, p, sizeof(tmp));

	error = fuse_ipc_tx(fip);
	if (error == ENOENT) {
		cache_setvp(ap->a_nch, NULL);
		fuse_dbg("lookup \"%s\" ENOENT\n", tmp);
		return ENOENT;
	} else if (error) {
		fuse_dbg("lookup \"%s\" error=%d\n", tmp, error);
		return error;
	}

	feo = fuse_out_data(fip);
	fuse_dbg("lookup \"%s\" ino=%ju/%ju\n", p, feo->nodeid, feo->attr.ino);

	mode = feo->attr.mode;
	if (S_ISREG(mode))
		vtyp = VREG;
	else if (S_ISDIR(mode))
		vtyp = VDIR;
	else if (S_ISBLK(mode))
		vtyp = VBLK;
	else if (S_ISCHR(mode))
		vtyp = VCHR;
	else if (S_ISLNK(mode))
		vtyp = VLNK;
	else if (S_ISSOCK(mode))
		vtyp = VSOCK;
	else if (S_ISFIFO(mode))
		vtyp = VFIFO;
	else
		vtyp = VBAD;

	error = fuse_alloc_node(dfnp, feo->nodeid, p, strlen(p), vtyp, &vp);
	if (error) {
		fuse_ipc_put(fip);
		return error;
	}
	KKASSERT(vp);
	KKASSERT(vn_islocked(vp));

	vn_unlock(vp);
	cache_setvp(ap->a_nch, vp);
	vrele(vp);

	/* unused */
	//feo->generation;
	//feo->entry_valid;
	//feo->attr_valid;
	//feo->entry_valid_nsec;
	//feo->attr_valid_nsec;

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_nlink(struct vop_nlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_node *fnp = VTOI(vp);
	struct fuse_dent *fep;
	struct fuse_ipc *fip;
	struct fuse_link_in *fli;
	struct fuse_entry_out *feo;
	char *p;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_LINK))
		return EOPNOTSUPP;

	if (vp->v_type == VDIR)
		return EPERM;
	if (dvp->v_mount != vp->v_mount)
		return EXDEV;
	if (fnp->nlink >= LINK_MAX)
		return EMLINK;

	fip = fuse_ipc_get(fmp, sizeof(fli) + ncp->nc_nlen + 1);
	fli = fuse_ipc_fill(fip, FUSE_LINK, dfnp->ino, ap->a_cred);
	fli->oldnodeid = fnp->ino;

	p = (char*)(fli + 1);
	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';

	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	feo = fuse_out_data(fip);
	if (IFTOVT(feo->attr.mode) != vp->v_type) {
		fuse_ipc_put(fip);
		return EINVAL;
	}

	mtx_lock(&dfnp->node_lock);
	mtx_lock(&fnp->node_lock);
	fuse_dent_new(fnp, p, strlen(p), &fep);
	fuse_dent_attach(dfnp, fep);
	fuse_set_attr(fnp, &feo->attr);
	mtx_unlock(&fnp->node_lock);
	mtx_unlock(&dfnp->node_lock);

	cache_setunresolved(ap->a_nch);
	cache_setvp(ap->a_nch, vp);
	fuse_knote(dvp, NOTE_WRITE);
	fuse_knote(vp, NOTE_LINK);

	/* unused */
	//feo->nodeid;
	//feo->generation;
	//feo->entry_valid;
	//feo->attr_valid;
	//feo->entry_valid_nsec;
	//feo->attr_valid_nsec;

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_ncreate(struct vop_ncreate_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(dvp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_node *fnp;
	struct fuse_ipc *fip;
	struct fuse_create_in *fci;
	struct fuse_entry_out *feo;
	struct fuse_open_out *foo;
	enum vtype vtyp;
	char *p;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_CREATE))
		return EOPNOTSUPP;

	fip = fuse_ipc_get(fmp, sizeof(*fci) + ncp->nc_nlen + 1);
	fci = fuse_ipc_fill(fip, FUSE_CREATE, dfnp->ino, ap->a_cred);
	fci->flags = OFLAGS(ap->a_vap->va_fuseflags);
	fci->mode = MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode);
	/* unused */
	//fci->umask = ...;
	fuse_dbg("flags=%X mode=%X\n", fci->flags, fci->mode);

	p = (char*)(fci + 1);
	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';

	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	feo = fuse_out_data(fip);
	foo = (struct fuse_open_out*)(feo + 1);
	vtyp = IFTOVT(feo->attr.mode);
	if (vtyp != VREG && vtyp != VSOCK) {
		fuse_ipc_put(fip);
		return EINVAL;
	}

	error = fuse_alloc_node(dfnp, feo->nodeid, p, strlen(p), VREG, &vp);
	if (error) {
		fuse_ipc_put(fip);
		return error;
	}
	KKASSERT(vp);
	KKASSERT(vn_islocked(vp));

	fnp = VTOI(vp);
	mtx_lock(&fnp->node_lock);
	fuse_set_attr(fnp, &feo->attr);
	mtx_unlock(&fnp->node_lock);

	cache_setunresolved(ap->a_nch);
	cache_setvp(ap->a_nch, vp);
	*(ap->a_vpp) = vp;
	fuse_knote(dvp, NOTE_WRITE);

	/* unused */
	//feo->generation;
	//feo->entry_valid;
	//feo->attr_valid;
	//feo->entry_valid_nsec;
	//feo->attr_valid_nsec;
	/* unused */
	//foo->open_flags;

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_nmknod(struct vop_nmknod_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(dvp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_node *fnp;
	struct fuse_ipc *fip;
	struct fuse_mknod_in *fmi;
	struct fuse_entry_out *feo;
	enum vtype vtyp;
	char *p;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_MKNOD))
		return EOPNOTSUPP;

	fip = fuse_ipc_get(fmp, sizeof(*fmi) + ncp->nc_nlen + 1);
	fmi = fuse_ipc_fill(fip, FUSE_MKNOD, dfnp->ino, ap->a_cred);
	fmi->mode = MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode);
	/* unused */
	//fmi->rdev = ...;
	//fmi->umask = ...;

	p = (char*)(fmi + 1);
	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';

	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	feo = fuse_out_data(fip);
	vtyp = IFTOVT(feo->attr.mode);
	if (vtyp != VBLK && vtyp != VCHR && vtyp != VFIFO) {
		fuse_ipc_put(fip);
		return EINVAL;
	}

	error = fuse_alloc_node(dfnp, feo->nodeid, p, strlen(p),
	    ap->a_vap->va_type, &vp);
	if (error) {
		fuse_ipc_put(fip);
		return error;
	}
	KKASSERT(vp);
	KKASSERT(vn_islocked(vp));

	fnp = VTOI(vp);
	mtx_lock(&fnp->node_lock);
	fuse_set_attr(fnp, &feo->attr);
	mtx_unlock(&fnp->node_lock);

	cache_setunresolved(ap->a_nch);
	cache_setvp(ap->a_nch, vp);
	*(ap->a_vpp) = vp;
	fuse_knote(dvp, NOTE_WRITE);

	/* unused */
	//feo->generation;
	//feo->entry_valid;
	//feo->attr_valid;
	//feo->entry_valid_nsec;
	//feo->attr_valid_nsec;

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_nremove(struct vop_nremove_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(dvp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_node *fnp;
	struct fuse_dent *fep;
	struct fuse_ipc *fip;
	char *p;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_UNLINK))
		return EOPNOTSUPP;

	error = cache_vget(ap->a_nch, ap->a_cred, LK_SHARED, &vp);
	KKASSERT(vp->v_mount == dvp->v_mount);
	KKASSERT(!error); /* from tmpfs */
	vn_unlock(vp);

	fip = fuse_ipc_get(fmp, ncp->nc_nlen + 1);
	p = fuse_ipc_fill(fip, FUSE_UNLINK, dfnp->ino, ap->a_cred);

	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';

	error = fuse_ipc_tx(fip);
	if (error) {
		vrele(vp);
		return error;
	}

	fnp = VTOI(vp);
	mtx_lock(&dfnp->node_lock);
	mtx_lock(&fnp->node_lock);
	error = fuse_dent_find(dfnp, p, strlen(p), &fep);
	if (error == ENOENT) {
		mtx_unlock(&fnp->node_lock);
		mtx_unlock(&dfnp->node_lock);
		fuse_ipc_put(fip);
		vrele(vp);
		return error;
	}
	fuse_dent_detach(dfnp, fep);
	fuse_dent_free(fep);
	mtx_unlock(&fnp->node_lock);
	mtx_unlock(&dfnp->node_lock);

	cache_unlink(ap->a_nch);
	fuse_knote(dvp, NOTE_WRITE);
	fuse_knote(vp, NOTE_DELETE);

	fuse_ipc_put(fip);
	vrele(vp);

	return 0;
}

static int
fuse_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(dvp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_node *fnp;
	struct fuse_ipc *fip;
	struct fuse_mkdir_in *fmi;
	struct fuse_entry_out *feo;
	char *p;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_MKDIR))
		return EOPNOTSUPP;

	fip = fuse_ipc_get(fmp, sizeof(*fmi) + ncp->nc_nlen + 1);
	fmi = fuse_ipc_fill(fip, FUSE_MKDIR, dfnp->ino, ap->a_cred);
	fmi->mode = MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode);

	p = (char*)(fmi + 1);
	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';

	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	feo = fuse_out_data(fip);
	if (IFTOVT(feo->attr.mode) != VDIR) {
		fuse_ipc_put(fip);
		return EINVAL;
	}

	error = fuse_alloc_node(dfnp, feo->nodeid, p, strlen(p), VDIR, &vp);
	if (error) {
		fuse_ipc_put(fip);
		return error;
	}
	KKASSERT(vp);
	KKASSERT(vn_islocked(vp));

	fnp = VTOI(vp);
	mtx_lock(&fnp->node_lock);
	fuse_set_attr(fnp, &feo->attr);
	mtx_unlock(&fnp->node_lock);

	cache_setunresolved(ap->a_nch);
	cache_setvp(ap->a_nch, vp);
	*(ap->a_vpp) = vp;
	fuse_knote(dvp, NOTE_WRITE | NOTE_LINK);

	/* unused */
	//feo->generation;
	//feo->entry_valid;
	//feo->attr_valid;
	//feo->entry_valid_nsec;
	//feo->attr_valid_nsec;

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(dvp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_node *fnp;
	struct fuse_dent *fep;
	struct fuse_ipc *fip;
	char *p;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_RMDIR))
		return EOPNOTSUPP;

	error = cache_vget(ap->a_nch, ap->a_cred, LK_SHARED, &vp);
	KKASSERT(vp->v_mount == dvp->v_mount);
	KKASSERT(!error); /* from tmpfs */
	vn_unlock(vp);

	fip = fuse_ipc_get(fmp, ncp->nc_nlen + 1);
	p = fuse_ipc_fill(fip, FUSE_RMDIR, dfnp->ino, ap->a_cred);

	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';

	error = fuse_ipc_tx(fip);
	if (error) {
		vrele(vp);
		return error;
	}

	fnp = VTOI(vp);
	mtx_lock(&dfnp->node_lock);
	mtx_lock(&fnp->node_lock);
	error = fuse_dent_find(dfnp, p, strlen(p), &fep);
	if (error == ENOENT) {
		mtx_unlock(&fnp->node_lock);
		mtx_unlock(&dfnp->node_lock);
		fuse_ipc_put(fip);
		vrele(vp);
		return error;
	}
	fuse_dent_detach(dfnp, fep);
	fuse_dent_free(fep);
	mtx_unlock(&fnp->node_lock);
	mtx_unlock(&dfnp->node_lock);

	cache_unlink(ap->a_nch);
	fuse_knote(dvp, NOTE_WRITE | NOTE_LINK);

	fuse_ipc_put(fip);
	vrele(vp);

	return 0;
}

static int
fuse_vop_pathconf(struct vop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_FILESIZEBITS:
		*ap->a_retval = 64;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		break;
	default:
		return vop_stdpathconf(ap);
	}

	return 0;
}

static int
fuse_vop_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_ipc *fip;
	struct fuse_read_in *fri;
	const char *buf;
	size_t len;
	off_t cur_offset = 0;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_READDIR))
		return EOPNOTSUPP;

	fip = fuse_ipc_get(fmp, sizeof(*fri));
	fri = fuse_ipc_fill(fip, FUSE_READDIR, VTOI(vp)->ino, ap->a_cred);
	fri->fh = VTOI(vp)->fh;
	fri->offset = 0;
	/*
	 * XXX This needs to be large enough to read all entries at once.
	 * FUSE filesystems typically just opendir/readdir and return entries.
	 */
	fri->size = FUSE_BLKSIZE * 10;
	/* unused */
	//fri->read_flags = ...;
	//fri->lock_owner = ...;
	//fri->flags = ...;

	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	buf = fuse_out_data(fip);
	len = fuse_out_data_size(fip);

	while (1) {
		const struct fuse_dirent *fde;
		size_t freclen;

		fuse_dbg("uio_offset=%ju uio_resid=%ju\n",
		    uio->uio_offset, uio->uio_resid);

		if (len < FUSE_NAME_OFFSET) {
			if (ap->a_eofflag)
				*ap->a_eofflag = 1;
			break;
		}
		if (uio->uio_resid < FUSE_NAME_OFFSET)
			break;

		fde = (const struct fuse_dirent*)buf;
		if (!fde->namelen) {
			error = EINVAL;
			break;
		}
		freclen = FUSE_DIRENT_SIZE(fde);

		/*
		 * Also see
		 * getdirentries(2) in sys/kern/vfs_syscalls.c
		 * readdir(3) in lib/libc/gen/readdir.c
		 */
		if (cur_offset >= uio->uio_offset) {
			error = 0;
			if (vop_write_dirent(&error, uio, fde->ino, fde->type,
			    fde->namelen, fde->name))
				break;
			if (error)
				break;
			fuse_dbg("ino=%ju type=%d name=%s len=%u\n",
			    fde->ino, fde->type, fde->name, fde->namelen);
		}

		cur_offset += _DIRENT_RECLEN(fde->namelen);
		buf += freclen;
		len -= freclen;
	}
	fuse_ipc_put(fip);

	return error;
}

static int
fuse_vop_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_ipc *fip;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_READLINK))
		return EOPNOTSUPP;

	if (vp->v_type != VLNK)
		return EINVAL;

	fip = fuse_ipc_get(fmp, 0);
	fuse_ipc_fill(fip, FUSE_READLINK, VTOI(vp)->ino, ap->a_cred);

	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	error = uiomove(fuse_out_data(fip), fuse_out_data_size(fip), ap->a_uio);

	fuse_ipc_put(fip);

	return error;
}

static int
fuse_vop_nrename(struct vop_nrename_args *ap)
{
	struct namecache *fncp = ap->a_fnch->ncp;
	struct namecache *tncp = ap->a_tnch->ncp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *fvp = fncp->nc_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *tvp;
	struct fuse_mount *fmp = VFSTOFUSE(fdvp->v_mount);
	struct fuse_node *fdfnp = VTOI(fdvp);
	struct fuse_node *ffnp = VTOI(fvp);
	struct fuse_node *tdfnp = VTOI(tdvp);
	struct fuse_node *tfnp;
	struct fuse_dent *ffep;
	struct fuse_dent *tfep;
	struct fuse_ipc *fip;
	struct fuse_rename_in *fri;
	char *p, *newname, *oldname;
	int error;

	KKASSERT(fdvp->v_mount == fvp->v_mount);

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_RENAME))
		return EOPNOTSUPP;

	error = cache_vget(ap->a_tnch, ap->a_cred, LK_SHARED, &tvp);
	if (!error) {
		tfnp = VTOI(tvp);
		vn_unlock(tvp);
	} else
		tfnp = NULL;

	/* Disallow cross-device renames.
	 * Why isn't this done by the caller? */
	if (fvp->v_mount != tdvp->v_mount ||
	    (tvp && fvp->v_mount != tvp->v_mount)) {
		error = EXDEV;
		goto out;
	}

	if (fvp == tvp) {
		error = 0;
		goto out;
	}
	error = fuse_dent_find(fdfnp, fncp->nc_name, fncp->nc_nlen, &ffep);
	if (error == ENOENT)
		goto out;
	KKASSERT(ffep->fnp == ffnp);

	if (tvp) {
		KKASSERT(tfnp);
		if (ffnp->type == VDIR && tfnp->type == VDIR) {
			if (!RB_EMPTY(&tfnp->dent_head)) {
				error = ENOTEMPTY;
				goto out;
			}
		} else if (ffnp->type == VDIR && tfnp->type != VDIR) {
			error = ENOTDIR;
			goto out;
		} else if (ffnp->type != VDIR && tfnp->type == VDIR) {
			error = EISDIR;
			goto out;
		} else
			KKASSERT(ffnp->type != VDIR && tfnp->type != VDIR);
	}

	fip = fuse_ipc_get(fmp,
	    sizeof(*fri) + fncp->nc_nlen + tncp->nc_nlen + 2);
	/* There is also fuse_rename2_in with flags. */
	fri = fuse_ipc_fill(fip, FUSE_RENAME, fdfnp->ino, ap->a_cred);
	fri->newdir = tdfnp->ino;

	p = (char*)(fri + 1);
	memcpy(p, fncp->nc_name, fncp->nc_nlen);
	p[fncp->nc_nlen] = '\0';
	memcpy(p + fncp->nc_nlen + 1, tncp->nc_name, tncp->nc_nlen);
	p[fncp->nc_nlen + 1 + tncp->nc_nlen] = '\0';

	error = fuse_ipc_tx(fip);
	if (error)
		goto out;
	fuse_ipc_put(fip);

	if (fncp->nc_nlen != tncp->nc_nlen ||
	    memcmp(fncp->nc_name, tncp->nc_name, fncp->nc_nlen)) {
		newname = kmalloc(tncp->nc_nlen + 1, M_TEMP, M_WAITOK | M_ZERO);
		KKASSERT(newname);
		memcpy(newname, tncp->nc_name, tncp->nc_nlen);
		newname[tncp->nc_nlen] = '\0';
		fuse_dbg("newname=\"%s\"\n", newname);
	} else
		newname = NULL;

	mtx_lock(&tdfnp->node_lock);
	mtx_lock(&fdfnp->node_lock);
	mtx_lock(&ffnp->node_lock);

	fuse_dbg("detach from_dent=\"%s\"\n", ffep->name);
	fuse_dent_detach(fdfnp, ffep);

	if (newname) {
		oldname = ffep->name;
		ffep->name = newname;
		newname = oldname;
	}

	if (tvp) {
		mtx_lock(&tfnp->node_lock);
		error = fuse_dent_find(tdfnp, tncp->nc_name, tncp->nc_nlen,
		    &tfep);
		KKASSERT(!error);
		fuse_dbg("detach/free to_dent=\"%s\"\n", tfep->name);
		fuse_dent_detach(tdfnp, tfep);
		fuse_dent_free(tfep);
		mtx_unlock(&tfnp->node_lock);
		fuse_knote(tdvp, NOTE_DELETE);
	}

	fuse_dbg("attach from_dent=\"%s\"\n", ffep->name);
	fuse_dent_attach(tdfnp, ffep);

	mtx_unlock(&ffnp->node_lock);
	mtx_unlock(&fdfnp->node_lock);
	mtx_unlock(&tdfnp->node_lock);

	if (newname)
		kfree(newname, M_TEMP);

	cache_rename(ap->a_fnch, ap->a_tnch);
	fuse_knote(fdvp, NOTE_WRITE);
	fuse_knote(tdvp, NOTE_WRITE);
	fuse_knote(fvp, NOTE_RENAME);
out:
	if (tvp)
		vrele(tvp);

	return error;
}

static int
fuse_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct fuse_mount *fmp = VFSTOFUSE(dvp->v_mount);
	struct fuse_node *dfnp = VTOI(dvp);
	struct fuse_node *fnp;
	struct fuse_ipc *fip;
	struct fuse_entry_out *feo;
	char *p;
	int error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_SYMLINK))
		return EOPNOTSUPP;

	fip = fuse_ipc_get(fmp, strlen(ap->a_target) + 1 + ncp->nc_nlen + 1);
	p = fuse_ipc_fill(fip, FUSE_SYMLINK, dfnp->ino, ap->a_cred);

	memcpy(p, ncp->nc_name, ncp->nc_nlen);
	p[ncp->nc_nlen] = '\0';
	memcpy(p + ncp->nc_nlen + 1, ap->a_target, strlen(ap->a_target) + 1);

	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	feo = fuse_out_data(fip);
	if (IFTOVT(feo->attr.mode) != VLNK) {
		fuse_ipc_put(fip);
		return EINVAL;
	}

	error = fuse_alloc_node(dfnp, feo->nodeid, p, strlen(p), VLNK, &vp);
	if (error) {
		fuse_ipc_put(fip);
		return error;
	}
	KKASSERT(vp);
	KKASSERT(vn_islocked(vp));

	fnp = VTOI(vp);
	mtx_lock(&fnp->node_lock);
	fuse_set_attr(fnp, &feo->attr);
	mtx_unlock(&fnp->node_lock);

	cache_setunresolved(ap->a_nch);
	cache_setvp(ap->a_nch, vp);
	*(ap->a_vpp) = vp;
	fuse_knote(vp, NOTE_WRITE);

	/* unused */
	//feo->generation;
	//feo->entry_valid;
	//feo->attr_valid;
	//feo->entry_valid_nsec;
	//feo->attr_valid_nsec;

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_vop_read(struct vop_read_args *ap)
{
	struct buf *bp;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp;
	off_t base_offset;
	size_t offset;
	size_t len;
	size_t resid;
	int error;
	int seqcount;

	/*
	 * Check the basics
	 */
	if (fuse_test_dead(fmp))
		return ENOTCONN;
	if (fuse_test_nosys(fmp, FUSE_READ))
		return EOPNOTSUPP;
	if (uio->uio_offset < 0)
		return EINVAL;
	if (vp->v_type != VREG)
		return EINVAL;

	/*
	 * Extract node, try to shortcut the operation through
	 * the vM page cache, allowing us to avoid buffer cache
	 * overheads.
	 */
	fnp = VTOI(vp);
	resid = uio->uio_resid;
	seqcount = ap->a_ioflag >> IO_SEQSHIFT;
	error = vop_helper_read_shortcut(ap);
	if (error)
		return error;
	if (uio->uio_resid == 0) {
		if (resid)
			goto finished;
		return error;
	}

	/*
	 * Fall-through to our normal read code.
	 */
	while (uio->uio_resid > 0 && uio->uio_offset < fnp->size) {
		/*
		 * Use buffer cache I/O (via fuse_vop_strategy)
		 */
		offset = (size_t)uio->uio_offset & FUSE_BLKMASK64;
		base_offset = (off_t)uio->uio_offset - offset;
		bp = getcacheblk(vp, base_offset,
				 FUSE_BLKSIZE, GETBLK_KVABIO);
		if (bp == NULL) {
			if (1 /* fuse_cluster_rd_enable XXX sysctl */) {
				error = cluster_readx(vp, fnp->size,
						     base_offset,
						     FUSE_BLKSIZE,
						     B_NOTMETA | B_KVABIO,
						     uio->uio_resid,
						     seqcount * MAXBSIZE,
						     &bp);
			} else {
				error = bread_kvabio(vp, base_offset,
						     FUSE_BLKSIZE, &bp);
			}
			if (error) {
				brelse(bp);
				kprintf("fuse_vop_read bread error %d\n",
					error);
				break;
			}

			/*
			 * Only do this if the VOP is coming from a normal
			 * read/write.  The VM system handles the case for
			 * UIO_NOCOPY.
			 */
			if (uio->uio_segflg != UIO_NOCOPY)
				vm_wait_nominal();
		}
		bp->b_flags |= B_CLUSTEROK;
		bkvasync(bp);

		/*
		 * Figure out how many bytes we can actually copy this loop.
		 */
		len = FUSE_BLKSIZE - offset;
		if (len > uio->uio_resid)
			len = uio->uio_resid;
		if (len > fnp->size - uio->uio_offset)
			len = (size_t)(fnp->size - uio->uio_offset);

		error = uiomovebp(bp, (char *)bp->b_data + offset, len, uio);
		bqrelse(bp);
		if (error) {
			kprintf("fuse_vop_read uiomove error %d\n", error);
			break;
		}
	}

finished:
	if (fnp->accessed == 0) {
		mtx_lock(&fnp->node_lock);
		fnp->accessed = 1;
		mtx_unlock(&fnp->node_lock);
	}
	return (error);
}

static int
fuse_vop_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct thread *td = uio->uio_td;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp;
	boolean_t extended;
	off_t oldsize;
	off_t newsize;
	int error;
	off_t base_offset;
	size_t offset;
	size_t len;
	struct rlimit limit;
	int trivial = 0;
	int kflags = 0;
	int ioflag = ap->a_ioflag;
	int seqcount;
	int endofblk;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	if (fuse_test_nosys(fmp, FUSE_WRITE))
		return EOPNOTSUPP;

	error = 0;
	if (uio->uio_resid == 0)
		return error;

	fnp = VTOI(vp);

	if (vp->v_type != VREG)
		return (EINVAL);
	seqcount = ioflag >> IO_SEQSHIFT;

	mtx_lock(&fnp->node_lock);

	oldsize = fnp->size;
	newsize = uio->uio_offset + uio->uio_resid;
	if (newsize < oldsize)
		newsize = oldsize;
	if (ioflag & IO_APPEND)
		uio->uio_offset = fnp->size;

	/*
	 * Check for illegal write offsets.
	 */
	if (newsize > FUSE_MAXFILESIZE) {
		error = EFBIG;
		goto done;
	}

	/*
	 * NOTE: Ignore if UIO does not come from a user thread (e.g. VN).
	 */
	if (vp->v_type == VREG && td != NULL && td->td_lwp != NULL) {
		error = kern_getrlimit(RLIMIT_FSIZE, &limit);
		if (error)
			goto done;
		if (newsize > limit.rlim_cur) {
			ksignal(td->td_proc, SIGXFSZ);
			error = EFBIG;
			goto done;
		}
	}

	/*
	 * Extend the file's size if necessary
	 */
	extended = (newsize > fnp->size);

	while (uio->uio_resid > 0) {
		struct buf *bp;

		/*
		 * Don't completely blow out running buffer I/O
		 * when being hit from the pageout daemon.
		 */
		if (uio->uio_segflg == UIO_NOCOPY &&
		    (ioflag & IO_RECURSE) == 0)
		{
			bwillwrite(FUSE_BLKSIZE);
		}

		/*
		 * Use buffer cache I/O (via fuse_vop_strategy)
		 *
		 * Calculate the maximum bytes we can write to the buffer at
		 * this offset (after resizing).
		 */
		offset = (size_t)uio->uio_offset & FUSE_BLKMASK64;
		base_offset = (off_t)uio->uio_offset - offset;
		len = uio->uio_resid;
		if (len > FUSE_BLKSIZE - offset)
			len = FUSE_BLKSIZE - offset;

		endofblk = 0;
		trivial = 0;
		if ((uio->uio_offset + len) > fnp->size) {
			trivial = (uio->uio_offset <= fnp->size);
			error = fuse_reg_resize(vp, uio->uio_offset + len,
						trivial);
			kflags |= NOTE_EXTEND;
			if (error)
				break;
		}
		if (base_offset + len == FUSE_BLKSIZE)
			endofblk = 1;

		/*
		 * Get the buffer
		 */
		error = 0;
		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Issue a write with the same data backing
			 * the buffer
			 */
			bp = getblk(vp,
				    base_offset, FUSE_BLKSIZE,
				    GETBLK_BHEAVY | GETBLK_KVABIO, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread_kvabio(vp,
					      base_offset, FUSE_BLKSIZE,
					      &bp);
			}
		} else if (trivial) {
			/*
			 * We are entirely overwriting the buffer, but
			 * may still have to zero it.
			 */
			bp = getblk(vp,
				    base_offset, FUSE_BLKSIZE,
				    GETBLK_BHEAVY | GETBLK_KVABIO, 0);
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits
			 * then replace the portion being overwritten.
			 */
			error = bread_kvabio(vp, base_offset, FUSE_BLKSIZE, &bp);
			if (error == 0)
				bheavy(bp);
		}

		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Ok, copy the data in
		 */
		bkvasync(bp);
		error = uiomovebp(bp, (char *)bp->b_data + offset, len, uio);
		kflags |= NOTE_WRITE;

		if (error) {
			kprintf("fuse_vop_write uiomove error %d\n", error);
			brelse(bp);
			break;
		}

		if (ioflag & IO_SYNC) {
			bwrite(bp);
		} else if ((ioflag & IO_DIRECT) && endofblk) {
			bawrite(bp);
		} else if (ioflag & IO_ASYNC) {
			bawrite(bp);
		} else if (vp->v_mount->mnt_flag & MNT_NOCLUSTERW) {
			bdwrite(bp);
		} else {
			bp->b_flags |= B_CLUSTEROK;
			cluster_write(bp, fnp->size, FUSE_BLKSIZE, seqcount);
			//bdwrite(bp);
		}
	}
	vsetisdirty(vp);

	if (error) {
		if (extended) {
			(void)fuse_reg_resize(vp, oldsize, trivial);
			kflags &= ~NOTE_EXTEND;
		}
		goto done;
	}

	/*
	 * Currently we don't set the mtime on files modified via mmap()
	 * because we can't tell the difference between those modifications
	 * and an attempt by the pageout daemon to flush fuse pages to
	 * swap.
	 */
	if (uio->uio_segflg == UIO_NOCOPY) {
		if (vp->v_flag & VLASTWRITETS) {
			fnp->attr.va_mtime.tv_sec = vp->v_lastwrite_ts.tv_sec;
			fnp->attr.va_mtime.tv_nsec = vp->v_lastwrite_ts.tv_nsec;
		}
	} else {
		fnp->modified = 1;
		vclrflags(vp, VLASTWRITETS);
	}

	if (extended)
		fnp->changed = 1;

	if (fnp->attr.va_mode & (S_ISUID | S_ISGID)) {
		if (caps_priv_check(ap->a_cred, SYSCAP_NOVFS_RETAINSUGID))
			fnp->attr.va_mode &= ~(S_ISUID | S_ISGID);
	}
done:
	mtx_unlock(&fnp->node_lock);

	if (kflags)
		fuse_knote(vp, kflags);

	return(error);
}

/*
 * Issue I/O RPC to support thread.  This can be issued from sensitive
 * kernel threads such as the pageout daemon, so we have to queue the
 * I/O to our support thread and return.  We cannot block in here.
 */
static int
fuse_vop_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	//struct fuse_node *fnp = VTOI(vp);

	fuse_dbg("ino=%ju b_cmd=%d\n", VTOI(ap->a_vp)->ino, bp->b_cmd);

	if (vp->v_type != VREG) {
		bp->b_resid = bp->b_bcount;
		bp->b_flags |= B_ERROR | B_INVAL;
		bp->b_error = EINVAL;
		biodone(bio);
		return 0;
	}

	bp->b_flags &= ~(B_ERROR | B_INVAL);

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		if (vn_cache_strategy(vp, bio) == 0) {
			bio->bio_driver_info = vp;
			spin_lock(&fmp->helper_spin);
			TAILQ_INSERT_TAIL(&fmp->bioq, bio, bio_act);
			spin_unlock(&fmp->helper_spin);
			wakeup(&fmp->helper_td);
		}
		break;
	case BUF_CMD_WRITE:
		bio->bio_driver_info = vp;
		spin_lock(&fmp->helper_spin);
		TAILQ_INSERT_TAIL(&fmp->bioq, bio, bio_act);
		spin_unlock(&fmp->helper_spin);
		wakeup(&fmp->helper_td);
		break;
	default:
		bp->b_flags |= B_INVAL;
		bp->b_error = EINVAL;
		biodone(bio);
		break;
	}
	return 0;
}

/*
 * Just make the backing store appear to be contiguous so write clustering
 * works.  The strategy function will take it from there.  Use MAXBSIZE
 * chunks as a micro-optimization to make random flushes use reasonable
 * block writes.
 */
static int
fuse_bmap(struct vop_bmap_args *ap)
{
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = MAXBSIZE - (ap->a_loffset & (MAXBSIZE - 1));
	if (ap->a_runb != NULL)
		*ap->a_runb = ap->a_loffset & (MAXBSIZE - 1);

	return 0;
}

static int
fuse_advlock(struct vop_advlock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fuse_node *fnp = VTOI(vp);
	int error;

	error = lf_advlock(ap, &fnp->advlock, fnp->size);

	return error;
}

static int
fuse_vop_print(struct vop_print_args *ap)
{
	struct fuse_node *fnp = VTOI(ap->a_vp);

	fuse_print("tag VT_FUSE, node %p, ino %ju, parent ino %ju\n",
	    fnp, VTOI(ap->a_vp)->ino, VTOI(fnp->pfnp->vp)->ino);

	return 0;
}

static int
fuse_vop_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct mount *mp = vp->v_mount;
	struct fuse_node *fnp = VTOI(vp);
	struct fuse_mount *fmp = VFSTOFUSE(mp);
	struct fuse_ipc *fip;
	struct fuse_release_in *fri;
	int error, op;

	if (!fnp) {
		vrecycle(vp);
		return 0;
	}

	fuse_dbg("ino=%ju nlink=%d\n", fnp->ino, fnp->nlink);
	vinvalbuf(vp, V_SAVE, 0, 0);

	if (fnp->fh) {
		/*
		 * Release the file-handle to clean-up the userland side.
		 */
		if (vp->v_type == VDIR)
			op = FUSE_RELEASEDIR;
		else
			op = FUSE_RELEASE;

		fip = fuse_ipc_get(fmp, sizeof(*fri));
		fri = fuse_ipc_fill(fip, op, fnp->ino, NULL);
		/* unused */
		//fri->flags = ...;
		//fri->release_flags = ...;
		//fri->lock_owner = ...;
		fri->fh = fnp->fh;

		error = fuse_ipc_tx(fip);
		if (error)
			return error;
		fuse_ipc_put(fip);
	}

	fnp->closed = true;
	fnp->fh = 0;

	vrecycle(vp);

	return 0;
}

/*
 * Reclaim inactive vnode and destroy the related fuse_node.  We
 * never destroy the root fuse_node here.
 */
static int
fuse_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp = VTOI(vp);

	if (fnp) {
		vp->v_data = NULL;
		fnp->vp = NULL;
		fuse_dbg("ino=%ju\n", fnp->ino);

		if (fnp != fmp->rfnp)
			fuse_node_free(fnp);
		vclrisdirty(vp);
	}

	return 0;
}

static int
fuse_vop_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	int res = 0;

	mp = ap->a_head.a_ops->head.vv_mount;
	lwkt_gettoken(&mp->mnt_token);

	switch (ap->a_op) {
	//case MOUNTCTL_MOUNTFLAGS:
	//	...
	//	break;
	default:
		res = vop_stdmountctl(ap);
		break;
	}

	lwkt_reltoken(&mp->mnt_token);
	return res;
}

static void filt_fusedetach(struct knote*);
static int filt_fuseread(struct knote*, long);
static int filt_fusewrite(struct knote*, long);
static int filt_fusevnode(struct knote*, long);

static struct filterops fuseread_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_fusedetach, filt_fuseread };
static struct filterops fusewrite_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_fusedetach, filt_fusewrite };
static struct filterops fusevnode_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_fusedetach, filt_fusevnode };

static int
fuse_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &fuseread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &fusewrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &fusevnode_filtops;
		break;
	default:
		return EOPNOTSUPP;
	}

	kn->kn_hook = (caddr_t)vp;
	knote_insert(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);

	return 0;
}

static void
filt_fusedetach(struct knote *kn)
{
	struct vnode *vp = (void*)kn->kn_hook;

	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
}

static int
filt_fuseread(struct knote *kn, long hint)
{
	struct vnode *vp = (void*)kn->kn_hook;
	struct fuse_node *fnp = VTOI(vp);
	off_t off;

	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
		return 1;
	}

	/*
	 * Interlock against MP races when performing this function.
	 */
	mtx_lock(&fnp->node_lock);
	off = fnp->size - kn->kn_fp->f_offset;
	kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	if (kn->kn_sfflags & NOTE_OLDAPI) {
		mtx_unlock(&fnp->node_lock);
		return 1;
	}
	if (!kn->kn_data)
		kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	mtx_unlock(&fnp->node_lock);

	return kn->kn_data != 0;
}

static int
filt_fusewrite(struct knote *kn, long hint)
{
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
	kn->kn_data = 0;

	return 1;
}

static int
filt_fusevnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return 1;
	}

	return kn->kn_fflags != 0;
}

static int
fuse_vop_getpages(struct vop_getpages_args *ap)
{
	if (!ap->a_vp->v_mount)
		return VM_PAGER_BAD;

	return vnode_pager_generic_getpages(ap->a_vp, ap->a_m, ap->a_count,
	    ap->a_reqpage, ap->a_seqaccess);
}

static int
fuse_vop_putpages(struct vop_putpages_args *ap)
{
	if (!ap->a_vp->v_mount)
		return VM_PAGER_BAD;

	return vnode_pager_generic_putpages(ap->a_vp, ap->a_m, ap->a_count,
	    ap->a_flags, ap->a_rtvals);
}

/*
 * Resizes the object associated to the regular file pointed to by vp to
 * the size newsize.  'vp' must point to a vnode that represents a regular
 * file.  'newsize' must be positive.
 *
 * pass NVEXTF_TRIVIAL when buf content will be overwritten, otherwise set 0
 * to be zero filled.
 *
 * Returns zero on success or an appropriate error code on failure.
 *
 * Caller must hold the node exclusively locked.
 */
static int
fuse_reg_resize(struct vnode *vp, off_t newsize, int trivial)
{
	struct fuse_node *fnp;
	off_t oldsize;
	int nvextflags;
	int error;

#ifdef INVARIANTS
	KKASSERT(vp->v_type == VREG);
	KKASSERT(newsize >= 0);
#endif

	fnp = VTOI(vp);

	oldsize = fnp->size;
	fnp->size = newsize;
	fnp->attr.va_size = newsize;
	fnp->sizeoverride = 1;

	nvextflags = 0;

	/*
	 * The backing VM object may contain VM pages as well as swap
	 * assignments if we previously renamed main object pages into
	 * it during deactivation.
	 */
	if (newsize < oldsize) {
		error = nvtruncbuf(vp, newsize, FUSE_BLKSIZE, -1, nvextflags);
	} else {
		int nblksize;

		nblksize = FUSE_BLKSIZE;

		if (trivial)
			nvextflags |= NVEXTF_TRIVIAL;

		error = nvextendbuf(vp, oldsize, newsize,
				    FUSE_BLKSIZE, nblksize,
				    -1, -1, nvextflags);
	}
	return error;
}

/*
 * Fuse strategy helper thread
 */
void
fuse_io_thread(void *arg)
{
	struct fuse_mount *fmp = arg;
	struct bio *bio;

	while (fmp->dead == 0) {
		tsleep(&fmp->helper_td, 0, "fuse_wio", 0);
		spin_lock(&fmp->helper_spin);
		while ((bio = TAILQ_FIRST(&fmp->bioq)) != NULL) {
			TAILQ_REMOVE(&fmp->bioq, bio, bio_act);
			spin_unlock(&fmp->helper_spin);
			fuse_io_execute(fmp, bio);
			spin_lock(&fmp->helper_spin);
		}
		spin_unlock(&fmp->helper_spin);
	}
	fmp->helper_td = NULL;
	wakeup(&fmp->helper_td);
}

/*
 * Execute BIO
 */
static void
fuse_io_execute(struct fuse_mount *fmp, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = bio->bio_driver_info;
	struct fuse_node *fnp = VTOI(vp);
	struct fuse_ipc *fip;
	struct fuse_read_in *fri;
	struct fuse_write_in *fwi;
	struct fuse_write_out *fwo;
	int error;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		fip = fuse_ipc_get(fmp, sizeof(*fri));
		fri = fuse_ipc_fill(fip, FUSE_READ, fnp->ino, proc0.p_ucred);
		fri->offset = bp->b_loffset;
		fri->size = bp->b_bcount;
		fri->fh = fnp->fh;

		error = fuse_ipc_tx(fip);

		if (error == 0) {
			memcpy(bp->b_data, fuse_out_data(fip),
			       fuse_out_data_size(fip));
			fuse_ipc_put(fip);
			bp->b_resid = 0;
			bp->b_error = 0;
		} else {
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR | B_INVAL;
			bp->b_error = EINVAL;
		}
		biodone(bio);
		break;
	case BUF_CMD_WRITE:
		fip = fuse_ipc_get(fmp, sizeof(*fwi) + bp->b_bcount);
		fwi = fuse_ipc_fill(fip, FUSE_WRITE, fnp->ino, proc0.p_ucred);
		fwi->offset = bp->b_loffset;
		fwi->size = bp->b_bcount;
		fwi->fh = fnp->fh;

		/*
		 * Handle truncated buffer at file EOF
		 */
		if (fwi->offset + fwi->size > fnp->size) {
			if (fwi->offset >= fnp->size) {
				error = EINVAL;
				goto write_failed;
			}
			fwi->size = fnp->size - fwi->offset;
		}

		memcpy((void *)(fwi + 1), bp->b_data, bp->b_bcount);

		error = fuse_ipc_tx(fip);

		fwo = fuse_out_data(fip);
		if (error == 0) {
			bp->b_resid = bp->b_bcount - fwo->size;
			bp->b_error = 0;
			fuse_ipc_put(fip);
		} else {
write_failed:
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR | B_INVAL;
			bp->b_error = EINVAL;
		}
		biodone(bio);
		break;
	default:
		bp->b_resid = bp->b_bcount;
		bp->b_flags |= B_ERROR | B_INVAL;
		bp->b_error = EINVAL;
		biodone(bio);
		break;
	}
}

#if 0
	bp->b_resid = bp->b_bcount;
	bp->b_flags |= B_ERROR | B_INVAL;
	bp->b_error = EINVAL;
	biodone(bio);
#endif

struct vop_ops fuse_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		fuse_vop_access,
	.vop_open =		fuse_vop_open,
	.vop_close =		fuse_vop_close,
	.vop_fsync =		fuse_vop_fsync,
	.vop_getattr =		fuse_vop_getattr,
	.vop_setattr =		fuse_vop_setattr,
	.vop_nresolve =		fuse_vop_nresolve,
	//.vop_nlookupdotdot =	fuse_nlookupdotdot,
	.vop_nlink =		fuse_vop_nlink,
	.vop_ncreate =		fuse_vop_ncreate,
	.vop_nmknod =		fuse_vop_nmknod,
	.vop_nremove =		fuse_vop_nremove,
	.vop_nmkdir =		fuse_vop_nmkdir,
	.vop_nrmdir =		fuse_vop_nrmdir,
	.vop_pathconf =		fuse_vop_pathconf,
	.vop_readdir =		fuse_vop_readdir,
	.vop_readlink =		fuse_vop_readlink,
	.vop_nrename =		fuse_vop_nrename,
	.vop_nsymlink =		fuse_vop_nsymlink,
	.vop_read =		fuse_vop_read,
	.vop_write =		fuse_vop_write,
	.vop_strategy =		fuse_vop_strategy,
	.vop_bmap =		fuse_bmap,
	.vop_advlock =		fuse_advlock,
	.vop_print =		fuse_vop_print,
	.vop_inactive =		fuse_vop_inactive,
	.vop_reclaim =		fuse_vop_reclaim,
	.vop_mountctl =		fuse_vop_mountctl,
	.vop_kqfilter =		fuse_kqfilter,
	.vop_getpages =		fuse_vop_getpages,
	.vop_putpages =		fuse_vop_putpages,
};

struct vop_ops fuse_spec_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		fuse_vop_access,
	.vop_close =		fuse_vop_close,
	.vop_fsync =		fuse_vop_fsync,
	.vop_getattr =		fuse_vop_getattr,
	.vop_setattr =		fuse_vop_setattr,
	.vop_read =		vop_stdnoread,
	.vop_write =		vop_stdnowrite,
	//.vop_markatime =	fuse_vop_markatime,
	.vop_print =		fuse_vop_print,
	.vop_inactive =		fuse_vop_inactive,
	.vop_reclaim =		fuse_vop_reclaim,
};
