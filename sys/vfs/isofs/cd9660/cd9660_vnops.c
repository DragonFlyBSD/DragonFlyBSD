/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
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
 *	@(#)cd9660_vnops.c	8.19 (Berkeley) 5/27/95
 * $FreeBSD: src/sys/isofs/cd9660/cd9660_vnops.c,v 1.62 1999/12/15 23:01:51 eivind Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <vfs/fifofs/fifo.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/unistd.h>
#include <sys/filio.h>
#include <sys/lockf.h>
#include <sys/objcache.h>

#include <vm/vm.h>
#include <vm/vnode_pager.h>

#include <sys/buf2.h>

#include "iso.h"
#include "cd9660_node.h"
#include "iso_rrip.h"

static int cd9660_access (struct vop_access_args *);
static int cd9660_advlock (struct vop_advlock_args *);
static int cd9660_getattr (struct vop_getattr_args *);
static int cd9660_ioctl (struct vop_ioctl_args *);
static int cd9660_pathconf (struct vop_pathconf_args *);
static int cd9660_open (struct vop_open_args *);
static int cd9660_read (struct vop_read_args *);
static int cd9660_setattr (struct vop_setattr_args *);
struct isoreaddir;
static int iso_uiodir (struct isoreaddir *idp, struct dirent *dp,
			   off_t off);
static int iso_shipdir (struct isoreaddir *idp);
static int cd9660_readdir (struct vop_readdir_args *);
static int cd9660_readlink (struct vop_readlink_args *ap);
static int cd9660_strategy (struct vop_strategy_args *);
static int cd9660_print (struct vop_print_args *);

/*
 * Setattr call. Only allowed for block and character special devices.
 *
 * cd9660_setattr(struct vnode *a_vp, struct vattr *a_vap,
 *		  struct ucred *a_cred, struct proc *a_p)
 */
int
cd9660_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;

  	if (vap->va_flags != (u_long)VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL)
		return (EROFS);
	if (vap->va_size != (u_quad_t)VNOVAL) {
 		switch (vp->v_type) {
 		case VDIR:
 			return (EISDIR);
		case VLNK:
		case VREG:
		case VDATABASE:
			return (EROFS);
 		case VCHR:
 		case VBLK:
 		case VSOCK:
 		case VFIFO:
		default:
			return (0);
		}
	}
	return (0);
}

/*
 * Check mode permission on inode pointer. Mode is READ, WRITE or EXEC.
 * The mode is shifted to select the owner/group/other fields. The
 * super user is granted all permissions.
 *
 * cd9660_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *		 struct proc *a_p)
 */
/* ARGSUSED */
static int
cd9660_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip = VTOI(vp);

	KKASSERT(vp->v_mount->mnt_flag & MNT_RDONLY);
	return (vop_helper_access(ap, ip->inode.iso_uid, ip->inode.iso_gid,
	    		ip->inode.iso_mode, 0));
}

/*
 * cd9660_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
cd9660_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct iso_node *ip = VTOI(vp);

	vap->va_fsid	= dev2udev(ip->i_dev);
	vap->va_fileid	= ip->i_number;

	vap->va_mode	= ip->inode.iso_mode;
	vap->va_nlink	= ip->inode.iso_links;
	vap->va_uid	= ip->inode.iso_uid;
	vap->va_gid	= ip->inode.iso_gid;
	vap->va_atime	= ip->inode.iso_atime;
	vap->va_mtime	= ip->inode.iso_mtime;
	vap->va_ctime	= ip->inode.iso_ctime;
	vap->va_rdev	= makedev( umajor(ip->inode.iso_rdev), uminor(ip->inode.iso_rdev) );

	vap->va_size	= (u_quad_t)(unsigned long)ip->i_size;
	if (ip->i_size == 0 && (vap->va_mode & S_IFMT) == S_IFLNK) {
		struct vop_readlink_args rdlnk;
		struct iovec aiov;
		struct uio auio;
		char *cp;

		cp = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);
		aiov.iov_base = cp;
		aiov.iov_len = MAXPATHLEN;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_td = curthread;
		auio.uio_resid = MAXPATHLEN;
		rdlnk.a_uio = &auio;
		rdlnk.a_vp = ap->a_vp;
		rdlnk.a_cred = proc0.p_ucred; /* use root cred */
		if (cd9660_readlink(&rdlnk) == 0)
			vap->va_size = MAXPATHLEN - auio.uio_resid;
		kfree(cp, M_TEMP);
	}
	vap->va_flags	= 0;
	vap->va_gen = 1;
	vap->va_blocksize = ip->i_mnt->logical_block_size;
	vap->va_bytes	= (u_quad_t) ip->i_size;
	vap->va_type	= vp->v_type;
	vap->va_filerev	= 0;
	return (0);
}

/*
 * Vnode op for ioctl.
 *
 * cd9660_ioctl(struct vnode *a_vp, int a_command, caddr_t a_data,
 *		int a_fflag, struct ucred *a_cred, struct proc *a_p)
 */
static int
cd9660_ioctl(struct vop_ioctl_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip = VTOI(vp);

        switch (ap->a_command) {

        case FIOGETLBA:
		*(int *)(ap->a_data) = ip->iso_start;
		return 0;
        default:
                return (ENOTTY);
        }
}

/*
 * open is called when the kernel intends to read or memory map a vnode.
 */
static int
cd9660_open(struct vop_open_args *ap)
{
	return(vop_stdopen(ap));
}

/*
 * Vnode op for reading.
 *
 * cd9660_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		struct ucred *a_cred)
 */
static int
cd9660_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct iso_node *ip = VTOI(vp);
	struct iso_mnt *imp;
	struct buf *bp;
	daddr_t lbn, rablock;
	off_t raoffset;
	off_t loffset;
	off_t diff;
	int rasize, error = 0;
	int seqcount;
	long size, n, on;

	seqcount = ap->a_ioflag >> 16;

	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);
	ip->i_flag |= IN_ACCESS;
	imp = ip->i_mnt;
	do {
		lbn = lblkno(imp, uio->uio_offset);
		loffset = lblktooff(imp, lbn);
		on = blkoff(imp, uio->uio_offset);
		n = szmin((u_int)(imp->logical_block_size - on),
			  uio->uio_resid);
		diff = (off_t)ip->i_size - uio->uio_offset;
		if (diff <= 0)
			return (0);
		if (diff < n)
			n = diff;
		size = blksize(imp, ip, lbn);
		rablock = lbn + 1;
		raoffset = lblktooff(imp, rablock);
		if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0) {
			if (raoffset < ip->i_size) {
				error = cluster_read(vp, (off_t)ip->i_size,
						     loffset, size,
						     uio->uio_resid,
						     (ap->a_ioflag >> 16) *
						      BKVASIZE,
						     &bp);
			} else {
				error = bread(vp, loffset, size, &bp);
			}
		} else {
			if (seqcount > 1 &&
			    lblktosize(imp, rablock) < ip->i_size) {
				rasize = blksize(imp, ip, rablock);
				error = breadn(vp, loffset, size, &raoffset,
					       &rasize, 1, &bp);
			} else
				error = bread(vp, loffset, size, &bp);
		}
		n = imin(n, size - bp->b_resid);
		if (error) {
			brelse(bp);
			return (error);
		}

		error = uiomove(bp->b_data + on, (int)n, uio);
		brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n != 0);
	return (error);
}

/* struct dirent + enough space for the maximum supported size */
struct iso_dirent {
	struct dirent de;
	char de_name[_DIRENT_RECLEN(NAME_MAX) - sizeof(struct dirent)];
};

/*
 * Structure for reading directories
 */
struct isoreaddir {
	struct iso_dirent saveent;
	struct iso_dirent assocent;
	struct iso_dirent current;
	off_t saveoff;
	off_t assocoff;
	off_t curroff;
	struct uio *uio;
	off_t uio_off;
	int eofflag;
	off_t *cookies;
	int ncookies;
};

int
iso_uiodir(struct isoreaddir *idp, struct dirent *dp, off_t off)
{
	int error;

	dp->d_name[dp->d_namlen] = 0;

	if (idp->uio->uio_resid < _DIRENT_DIRSIZ(dp)) {
		idp->eofflag = 0;
		return (-1);
	}

	if (idp->cookies) {
		if (idp->ncookies <= 0) {
			idp->eofflag = 0;
			return (-1);
		}

		*idp->cookies++ = off;
		--idp->ncookies;
	}

	if ((error = uiomove((caddr_t) dp,_DIRENT_DIRSIZ(dp),idp->uio)) != 0)
		return (error);
	idp->uio_off = off;
	return (0);
}

int
iso_shipdir(struct isoreaddir *idp)
{
	struct dirent *dp;
	int cl, sl, assoc;
	int error;
	char *cname, *sname;

	cl = idp->current.de.d_namlen;
	cname = idp->current.de.d_name;
assoc = (cl > 1) && (*cname == ASSOCCHAR);
	if (assoc) {
		cl--;
		cname++;
	}

	dp = &idp->saveent.de;
	sname = dp->d_name;
	if (!(sl = dp->d_namlen)) {
		dp = &idp->assocent.de;
		sname = dp->d_name + 1;
		sl = dp->d_namlen - 1;
	}
	if (sl > 0) {
		if (sl != cl
		    || bcmp(sname,cname,sl)) {
			if (idp->assocent.de.d_namlen) {
				if ((error = iso_uiodir(idp,&idp->assocent.de,idp->assocoff)) != 0)
					return (error);
				idp->assocent.de.d_namlen = 0;
			}
			if (idp->saveent.de.d_namlen) {
				if ((error = iso_uiodir(idp,&idp->saveent.de,idp->saveoff)) != 0)
					return (error);
				idp->saveent.de.d_namlen = 0;
			}
		}
	}
	if (assoc) {
		idp->assocoff = idp->curroff;
		bcopy(&idp->current,&idp->assocent,_DIRENT_DIRSIZ(&idp->current.de));
	} else {
		idp->saveoff = idp->curroff;
		bcopy(&idp->current,&idp->saveent,_DIRENT_DIRSIZ(&idp->current.de));
	}
	return (0);
}

/*
 * Vnode op for readdir
 *
 * cd9660_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred,
 *		  int *a_eofflag, int *a_ncookies, off_t *a_cookies)
 */
static int
cd9660_readdir(struct vop_readdir_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct isoreaddir *idp;
	struct vnode *vdp = ap->a_vp;
	struct iso_node *dp;
	struct iso_mnt *imp;
	struct buf *bp = NULL;
	struct iso_directory_record *ep;
	int entryoffsetinblock;
	doff_t endsearch;
	u_long bmask;
	int error = 0;
	int reclen;
	u_short namelen;
	int ncookies = 0;
	off_t *cookies = NULL;

	dp = VTOI(vdp);
	imp = dp->i_mnt;
	bmask = imp->im_bmask;

	error = vn_lock(vdp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error)
		return (error);

	idp = kmalloc(sizeof(*idp), M_TEMP, M_WAITOK);
	idp->saveent.de.d_namlen = idp->assocent.de.d_namlen = 0;
	/*
	 * XXX
	 * Is it worth trying to figure out the type?
	 */
	idp->saveent.de.d_type = DT_UNKNOWN;
	idp->assocent.de.d_type = DT_UNKNOWN;
	idp->current.de.d_type = DT_UNKNOWN;
	idp->uio = uio;
	if (ap->a_ncookies == NULL) {
		idp->cookies = NULL;
	} else {
		/*
		 * Guess the number of cookies needed.  Guess at least
		 * 1 to avoid a degenerate case in malloc, and cap at
		 * a reasonable limit.
		 */
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
		idp->cookies = cookies;
		idp->ncookies = ncookies;
	}
	idp->eofflag = 1;
	idp->curroff = uio->uio_offset;

	if ((entryoffsetinblock = idp->curroff & bmask) &&
	    (error = cd9660_devblkatoff(vdp, idp->curroff, NULL, &bp))) {
		kfree(idp, M_TEMP);
		goto done;
	}
	endsearch = dp->i_size;

	while (idp->curroff < endsearch) {
		/*
		 * If offset is on a block boundary,
		 * read the next directory block.
		 * Release previous if it exists.
		 */
		if ((idp->curroff & bmask) == 0) {
			if (bp != NULL)
				brelse(bp);
			if ((error =
			    cd9660_devblkatoff(vdp, idp->curroff, NULL, &bp)) != 0)
				break;
			entryoffsetinblock = 0;
		}
		/*
		 * Get pointer to next entry.
		 */
		ep = (struct iso_directory_record *)
			((char *)bp->b_data + entryoffsetinblock);

		reclen = isonum_711(ep->length);
		if (reclen == 0) {
			/* skip to next block, if any */
			idp->curroff =
			    (idp->curroff & ~bmask) + imp->logical_block_size;
			continue;
		}

		if (reclen < ISO_DIRECTORY_RECORD_SIZE) {
			error = EINVAL;
			/* illegal entry, stop */
			break;
		}

		if (entryoffsetinblock + reclen > imp->logical_block_size) {
			error = EINVAL;
			/* illegal directory, so stop looking */
			break;
		}

		idp->current.de.d_namlen = isonum_711(ep->name_len);

		if (reclen < ISO_DIRECTORY_RECORD_SIZE + idp->current.de.d_namlen) {
			error = EINVAL;
			/* illegal entry, stop */
			break;
		}

		if (isonum_711(ep->flags)&2)
			idp->current.de.d_ino = isodirino(ep, imp);
		else
			idp->current.de.d_ino = bp->b_bio1.bio_offset +
						entryoffsetinblock;

		idp->curroff += reclen;

		switch (imp->iso_ftype) {
		case ISO_FTYPE_RRIP:
		{
			ino_t cur_fileno = idp->current.de.d_ino;	
			cd9660_rrip_getname(ep,idp->current.de.d_name, &namelen,
					   &cur_fileno,imp);
			idp->current.de.d_ino = cur_fileno;
			idp->current.de.d_namlen = namelen;
			if (idp->current.de.d_namlen)
				error = iso_uiodir(idp,&idp->current.de,idp->curroff);
			break;
		}
		default: /* ISO_FTYPE_DEFAULT || ISO_FTYPE_9660 || ISO_FTYPE_HIGH_SIERRA*/
			strcpy(idp->current.de.d_name,"..");
			if (idp->current.de.d_namlen == 1 && ep->name[0] == 0) {
				idp->current.de.d_namlen = 1;
				error = iso_uiodir(idp,&idp->current.de,idp->curroff);
			} else if (idp->current.de.d_namlen == 1 && ep->name[0] == 1) {
				idp->current.de.d_namlen = 2;
				error = iso_uiodir(idp,&idp->current.de,idp->curroff);
			} else {
                                isofntrans(ep->name,idp->current.de.d_namlen,
                                           idp->current.de.d_name, &namelen,
                                           imp->iso_ftype == ISO_FTYPE_9660,
                                           isonum_711(ep->flags)&4,
                                           imp->joliet_level,
                                           imp->im_flags,
                                           imp->im_d2l);
				idp->current.de.d_namlen = namelen;
				if (imp->iso_ftype == ISO_FTYPE_DEFAULT)
					error = iso_shipdir(idp);
				else
					error = iso_uiodir(idp,&idp->current.de,idp->curroff);
			}
		}
		if (error)
			break;

		entryoffsetinblock += reclen;
	}

	if (!error && imp->iso_ftype == ISO_FTYPE_DEFAULT) {
		idp->current.de.d_namlen = 0;
		error = iso_shipdir(idp);
	}
	if (error < 0)
		error = 0;

	if (ap->a_ncookies != NULL) {
		if (error)
			kfree(cookies, M_TEMP);
		else {
			/*
			 * Work out the number of cookies actually used.
			 */
			*ap->a_ncookies = ncookies - idp->ncookies;
			*ap->a_cookies = cookies;
		}
	}

	if (bp)
		brelse (bp);

	uio->uio_offset = idp->uio_off;
	*ap->a_eofflag = idp->eofflag;

	kfree(idp, M_TEMP);

done:
	vn_unlock(vdp);
	return (error);
}

/*
 * Return target name of a symbolic link
 * Shouldn't we get the parent vnode and read the data from there?
 * This could eventually result in deadlocks in cd9660_lookup.
 * But otherwise the block read here is in the block buffer two times.
 */
typedef struct iso_directory_record ISODIR;
typedef struct iso_node		    ISONODE;
typedef struct iso_mnt		    ISOMNT;
/*
 * cd9660_readlink(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred)
 */
static int
cd9660_readlink(struct vop_readlink_args *ap)
{
	ISONODE	*ip;
	ISODIR	*dirp;
	ISOMNT	*imp;
	struct	buf *bp;
	struct	uio *uio;
	u_short	symlen;
	int	error;
	char	*symname;

	ip  = VTOI(ap->a_vp);
	imp = ip->i_mnt;
	uio = ap->a_uio;

	if (imp->iso_ftype != ISO_FTYPE_RRIP)
		return (EINVAL);

	/*
	 * Get parents directory record block that this inode included.
	 */
	error = bread(imp->im_devvp,
			(off_t)ip->i_number & ~((1 << imp->im_bshift) - 1),
		      imp->logical_block_size, &bp);
	if (error) {
		brelse(bp);
		return (EINVAL);
	}

	/*
	 * Setup the directory pointer for this inode
	 */
	dirp = (ISODIR *)(bp->b_data + (ip->i_number & imp->im_bmask));

	/*
	 * Just make sure, we have a right one....
	 *   1: Check not cross boundary on block
	 */
	if ((ip->i_number & imp->im_bmask) + isonum_711(dirp->length)
	    > (unsigned)imp->logical_block_size) {
		brelse(bp);
		return (EINVAL);
	}

	/*
	 * Now get a buffer
	 * Abuse a namei buffer for now.
	 */
	if (uio->uio_segflg == UIO_SYSSPACE)
		symname = uio->uio_iov->iov_base;
	else
		symname = objcache_get(namei_oc, M_WAITOK);
	
	/*
	 * Ok, we just gathering a symbolic name in SL record.
	 */
	if (cd9660_rrip_getsymname(dirp, symname, &symlen, imp) == 0) {
		if (uio->uio_segflg != UIO_SYSSPACE)
			objcache_put(namei_oc, symname);
		brelse(bp);
		return (EINVAL);
	}
	/*
	 * Don't forget before you leave from home ;-)
	 */
	brelse(bp);

	/*
	 * return with the symbolic name to caller's.
	 */
	if (uio->uio_segflg != UIO_SYSSPACE) {
		error = uiomove(symname, symlen, uio);
		objcache_put(namei_oc, symname);
		return (error);
	}
	uio->uio_resid -= symlen;
	uio->uio_iov->iov_base = (char *)uio->uio_iov->iov_base + symlen;
	uio->uio_iov->iov_len -= symlen;
	return (0);
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * cd9660_strategy(struct buf *a_vp, struct buf *a_bio)
 */
static int
cd9660_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip;
	int error;

	ip = VTOI(vp);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("cd9660_strategy: spec");
	nbio = push_bio(bio);
	if (nbio->bio_offset == NOOFFSET) {
		error = VOP_BMAP(vp, bio->bio_offset,
				 &nbio->bio_offset, NULL, NULL, bp->b_cmd);
		if (error) {
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
			/* I/O was never started on nbio, must biodone(bio) */
			biodone(bio);
			return (error);
		}
		if (nbio->bio_offset == NOOFFSET)
			clrbuf(bp);
	}
	if (nbio->bio_offset == NOOFFSET) {
		/* I/O was never started on nbio, must biodone(bio) */
		biodone(bio);
		return (0);
	}
	vp = ip->i_devvp;
	vn_strategy(vp, nbio);
	return (0);
}

/*
 * Print out the contents of an inode.
 *
 * cd9660_print(struct vnode *a_vp)
 */
static int
cd9660_print(struct vop_print_args *ap)
{
	kprintf("tag VT_ISOFS, isofs vnode\n");
	return (0);
}

/*
 * Return POSIX pathconf information applicable to cd9660 filesystems.
 *
 * cd9660_pathconf(struct vnode *a_vp, int a_name, register_t *a_retval)
 */
static int
cd9660_pathconf(struct vop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		return (0);
	case _PC_NAME_MAX:
		if (VTOI(ap->a_vp)->i_mnt->iso_ftype == ISO_FTYPE_RRIP)
			*ap->a_retval = NAME_MAX;
		else
			*ap->a_retval = 37;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Advisory lock support
 */
static int
cd9660_advlock(struct vop_advlock_args *ap)
{
	struct iso_node *ip = VTOI(ap->a_vp);
	return (lf_advlock(ap, &(ip->i_lockf), ip->i_size));
}


/*
 * Global vfs data structures for cd9660
 */
struct vop_ops cd9660_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_open =		cd9660_open,
	.vop_access =		cd9660_access,
	.vop_advlock =		cd9660_advlock,
	.vop_bmap =		cd9660_bmap,
	.vop_old_lookup =	cd9660_lookup,
	.vop_getattr =		cd9660_getattr,
	.vop_inactive =		cd9660_inactive,
	.vop_ioctl =		cd9660_ioctl,
	.vop_pathconf =		cd9660_pathconf,
	.vop_print =		cd9660_print,
	.vop_read =		cd9660_read,
	.vop_readdir =		cd9660_readdir,
	.vop_readlink =		cd9660_readlink,
	.vop_reclaim =		cd9660_reclaim,
	.vop_setattr =		cd9660_setattr,
	.vop_strategy =		cd9660_strategy,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages
};

/*
 * Special device vnode ops
 */
struct vop_ops cd9660_spec_vops = {
	.vop_default =		vop_defaultop,
	.vop_read =		vop_stdnoread,
	.vop_access =		cd9660_access,
	.vop_getattr =		cd9660_getattr,
	.vop_inactive =		cd9660_inactive,
	.vop_print =		cd9660_print,
	.vop_reclaim =		cd9660_reclaim,
	.vop_setattr =		cd9660_setattr,
};

struct vop_ops cd9660_fifo_vops = {
	.vop_default =		fifo_vnoperate,
	.vop_access =		cd9660_access,
	.vop_getattr =		cd9660_getattr,
	.vop_inactive =		cd9660_inactive,
	.vop_print =		cd9660_print,
	.vop_reclaim =		cd9660_reclaim,
	.vop_setattr =		cd9660_setattr,
};

