/*	$NetBSD: ntfs_vnops.c,v 1.23 1999/10/31 19:45:27 jdolecek Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * John Heidemann of the UCLA Ficus project.
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
 * $FreeBSD: src/sys/ntfs/ntfs_vnops.c,v 1.9.2.4 2002/08/06 19:35:18 semenu Exp $
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_extern.h>

#include <sys/sysctl.h>

#include <sys/buf2.h>

#include "ntfs.h"
#include "ntfs_inode.h"
#include "ntfs_subr.h"

#include <sys/unistd.h> /* for pathconf(2) constants */

static int	ntfs_read (struct vop_read_args *);
static int	ntfs_write (struct vop_write_args *ap);
static int	ntfs_getattr (struct vop_getattr_args *ap);
static int	ntfs_inactive (struct vop_inactive_args *ap);
static int	ntfs_print (struct vop_print_args *ap);
static int	ntfs_reclaim (struct vop_reclaim_args *ap);
static int	ntfs_strategy (struct vop_strategy_args *ap);
static int	ntfs_access (struct vop_access_args *ap);
static int	ntfs_open (struct vop_open_args *ap);
static int	ntfs_close (struct vop_close_args *ap);
static int	ntfs_readdir (struct vop_readdir_args *ap);
static int	ntfs_lookup (struct vop_old_lookup_args *ap);
static int	ntfs_bmap (struct vop_bmap_args *ap);
static int	ntfs_fsync (struct vop_fsync_args *ap);
static int	ntfs_pathconf (struct vop_pathconf_args *);

int	ntfs_prtactive = 1;	/* 1 => print out reclaim of active vnodes */

/*
 * This is a noop, simply returning what one has been given.
 *
 * ntfs_bmap(struct vnode *a_vp, off_t a_loffset,
 *	     daddr_t *a_doffsetp, int *a_runp, int *a_runb)
 */
int
ntfs_bmap(struct vop_bmap_args *ap)
{
	dprintf(("ntfs_bmap: vn: %p, blk: %u\n", ap->a_vp,
		(u_int32_t)ap->a_loffset));
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

/*
 * ntfs_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
static int
ntfs_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	struct buf *bp;
	daddr_t cn;
	int resid, off, toread;
	int error;

	dprintf(("ntfs_read: ino: %ju, off: %u resid: %zd, segflg: %d\n",
		(uintmax_t)ip->i_number, (uint32_t)uio->uio_offset,
		uio->uio_resid, uio->uio_segflg));

	dprintf(("ntfs_read: filesize: %ju", (uintmax_t)fp->f_size));

	/* don't allow reading after end of file */
	if (uio->uio_offset > fp->f_size)
		return (0);

	resid = (int)szmin(uio->uio_resid, fp->f_size - uio->uio_offset);

	dprintf((", resid: %d\n", resid));

	error = 0;
	while (resid) {
		cn = ntfs_btocn(uio->uio_offset);
		off = ntfs_btocnoff(uio->uio_offset);

		toread = min(off + resid, ntfs_cntob(1));

		error = bread(vp, ntfs_cntodoff(cn), ntfs_cntob(1), &bp);
		if (error) {
			brelse(bp);
			break;
		}

		error = uiomovebp(bp, bp->b_data + off, toread - off, uio);
		if(error) {
			brelse(bp);
			break;
		}
		brelse(bp);

		resid -= toread - off;
	}

	return (error);
}

/*
 * ntfs_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
ntfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct vattr *vap = ap->a_vap;

	dprintf(("ntfs_getattr: %ju, flags: %d\n", (uintmax_t)ip->i_number,
		ip->i_flag));

	vap->va_fsid = dev2udev(ip->i_dev);
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mp->ntm_mode;
	vap->va_nlink = ip->i_nlink;
	vap->va_uid = ip->i_mp->ntm_uid;
	vap->va_gid = ip->i_mp->ntm_gid;
	vap->va_rmajor = VNOVAL;
	vap->va_rminor = VNOVAL;
	vap->va_size = fp->f_size;
	vap->va_bytes = fp->f_allocated;
	vap->va_atime = ntfs_nttimetounix(fp->f_times.t_access);
	vap->va_mtime = ntfs_nttimetounix(fp->f_times.t_write);
	vap->va_ctime = ntfs_nttimetounix(fp->f_times.t_create);
	vap->va_flags = ip->i_flag;
	vap->va_gen = 0;
	vap->va_blocksize = ip->i_mp->ntm_spc * ip->i_mp->ntm_bps;
	vap->va_type = vp->v_type;
	vap->va_filerev = 0;
	return (0);
}


/*
 * Last reference to an ntnode.  If necessary, write or delete it.
 *
 * ntfs_inactive(struct vnode *a_vp)
 */
int
ntfs_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
#ifdef NTFS_DEBUG
	struct ntnode *ip = VTONT(vp);
#endif

	dprintf(("ntfs_inactive: vnode: %p, ntnode: %ju\n", vp, (uintmax_t)ip->i_number));

	if (ntfs_prtactive && VREFCNT(vp) > 1)
		vprint("ntfs_inactive: pushing active", vp);

	/*
	 * XXX since we don't support any filesystem changes
	 * right now, nothing more needs to be done
	 */
	return (0);
}

/*
 * Reclaim an fnode/ntnode so that it can be used for other purposes.
 *
 * ntfs_reclaim(struct vnode *a_vp)
 */
int
ntfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	int error;

	dprintf(("ntfs_reclaim: vnode: %p, ntnode: %ju\n", vp, (uintmax_t)ip->i_number));

	if (ntfs_prtactive && VREFCNT(vp) > 1)
		vprint("ntfs_reclaim: pushing active", vp);

	if ((error = ntfs_ntget(ip)) != 0)
		return (error);
	
	ntfs_frele(fp);
	ntfs_ntput(ip);
	vp->v_data = NULL;

	return (0);
}

/*
 * ntfs_print(struct vnode *a_vp)
 */
static int
ntfs_print(struct vop_print_args *ap)
{
	return (0);
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * ntfs_strategy(struct vnode *a_vp, struct bio *a_bio)
 */
int
ntfs_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct ntfsmount *ntmp = ip->i_mp;
	u_int32_t toread;
	u_int32_t towrite;
	size_t tmp;
	int error;

	dprintf(("ntfs_strategy: loffset: %u, doffset: %u\n",
		(uint32_t)bp->b_loffset, (uint32_t)bio->bio_offset));

	dprintf(("strategy: bcount: %u flags: 0x%x\n",
		bp->b_bcount, bp->b_flags));

	bp->b_error = 0;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		if (bio->bio_offset >= fp->f_size) {
			clrbuf(bp);
			error = 0;
		} else {
			toread = min(bp->b_bcount,
				 fp->f_size - bio->bio_offset);
			dprintf(("ntfs_strategy: toread: %u, fsize: %ju\n",
				toread, (uintmax_t)fp->f_size));

			error = ntfs_readattr(ntmp, ip, fp->f_attrtype,
				fp->f_attrname, bio->bio_offset,
				toread, bp->b_data, NULL);

			if (error) {
				kprintf("ntfs_strategy: ntfs_readattr failed\n");
				bp->b_error = error;
				bp->b_flags |= B_ERROR;
			}

			bzero(bp->b_data + toread, bp->b_bcount - toread);
		}
		break;
	case BUF_CMD_WRITE:
		if (bio->bio_offset + bp->b_bcount >= fp->f_size) {
			kprintf("ntfs_strategy: CAN'T EXTEND FILE\n");
			bp->b_error = error = EFBIG;
			bp->b_flags |= B_ERROR;
		} else {
			towrite = min(bp->b_bcount,
				      fp->f_size - bio->bio_offset);
			dprintf(("ntfs_strategy: towrite: %d, fsize: %ju\n",
				towrite, (uintmax_t)fp->f_size));

			error = ntfs_writeattr_plain(ntmp, ip, fp->f_attrtype,	
				fp->f_attrname, bio->bio_offset,towrite,
				bp->b_data, &tmp, NULL);

			if (error) {
				kprintf("ntfs_strategy: ntfs_writeattr fail\n");
				bp->b_error = error;
				bp->b_flags |= B_ERROR;
			}
		}
		break;
	default:
		panic("ntfs: bad b_cmd %d", bp->b_cmd);
	}
	biodone(bio);
	return (error);
}

/*
 * ntfs_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
static int
ntfs_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	size_t towrite;
	size_t written;
	int error;

	dprintf(("ntfs_write: ino: %ju, off: %u resid: %zd, segflg: %d\n",
		(uintmax_t)ip->i_number, (uint32_t)uio->uio_offset,
		uio->uio_resid, uio->uio_segflg));
	dprintf(("ntfs_write: filesize: %ju ", (uintmax_t)fp->f_size));

	if (uio->uio_resid + uio->uio_offset > fp->f_size) {
		kprintf("ntfs_write: CAN'T WRITE BEYOND END OF FILE\n");
		return (EFBIG);
	}
	if (uio->uio_offset > fp->f_size)
		return (EFBIG);

	towrite = szmin(uio->uio_resid, fp->f_size - uio->uio_offset);

	dprintf((", towrite: %zd\n", towrite));

	error = ntfs_writeattr_plain(ntmp, ip, fp->f_attrtype,
		fp->f_attrname, uio->uio_offset, towrite, NULL, &written, uio);
#ifdef NTFS_DEBUG
	if (error)
		kprintf("ntfs_write: ntfs_writeattr failed: %d\n", error);
#endif

	return (error);
}

/*
 * ntfs_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
int
ntfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);
	struct ucred *cred = ap->a_cred;
	mode_t mask, mode = ap->a_mode;
	gid_t *gp;
	int i;
#ifdef QUOTA
	int error;
#endif

	dprintf(("ntfs_access: %ju\n", (uintmax_t)ip->i_number));

	/*
	 * Disallow write attempts on read-only file systems;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 */
	if (mode & VWRITE) {
		switch ((int)vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
#ifdef QUOTA
			if (error = getinoquota(ip))
				return (error);
#endif
			break;
		}
	}

	/* Otherwise, user id 0 always gets access. */
	if (cred->cr_uid == 0)
		return (0);

	mask = 0;

	/* Otherwise, check the owner. */
	if (cred->cr_uid == ip->i_mp->ntm_uid) {
		if (mode & VEXEC)
			mask |= S_IXUSR;
		if (mode & VREAD)
			mask |= S_IRUSR;
		if (mode & VWRITE)
			mask |= S_IWUSR;
		return ((ip->i_mp->ntm_mode & mask) == mask ? 0 : EACCES);
	}

	/* Otherwise, check the groups. */
	for (i = 0, gp = cred->cr_groups; i < cred->cr_ngroups; i++, gp++)
		if (ip->i_mp->ntm_gid == *gp) {
			if (mode & VEXEC)
				mask |= S_IXGRP;
			if (mode & VREAD)
				mask |= S_IRGRP;
			if (mode & VWRITE)
				mask |= S_IWGRP;
			return ((ip->i_mp->ntm_mode&mask) == mask ? 0 : EACCES);
		}

	/* Otherwise, check everyone else. */
	if (mode & VEXEC)
		mask |= S_IXOTH;
	if (mode & VREAD)
		mask |= S_IROTH;
	if (mode & VWRITE)
		mask |= S_IWOTH;
	return ((ip->i_mp->ntm_mode & mask) == mask ? 0 : EACCES);
}

/*
 * Open called.
 *
 * Nothing to do.
 *
 * ntfs_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	     struct file *a_fp)
 */
/* ARGSUSED */
static int
ntfs_open(struct vop_open_args *ap)
{
	return (vop_stdopen(ap));
}

/*
 * Close called.
 *
 * Update the times on the inode.
 *
 * ntfs_close(struct vnode *a_vp, int a_fflag)
 */
/* ARGSUSED */
static int
ntfs_close(struct vop_close_args *ap)
{
#if NTFS_DEBUG
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);

	kprintf("ntfs_close: %ju\n", (uintmax_t)ip->i_number);
#endif

	return (vop_stdclose(ap));
}

/*
 * ntfs_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred,
 *		int *a_ncookies, off_t **cookies)
 */
int
ntfs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	int i, j, error = 0;
	wchar c;
	u_int32_t faked = 0, num, off;
	int ncookies = 0;
	char convname[NTFS_MAXFILENAME + 1];

	dprintf(("ntfs_readdir %ju off: %u resid: %zd\n",
		(uintmax_t)ip->i_number, (uint32_t)uio->uio_offset,
		uio->uio_resid));

	if (uio->uio_offset < 0 || uio->uio_offset > INT_MAX)
		return (EINVAL);
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error)
		return (error);

	/*
	 * uio->uio_offset carries the number of the entry
	 * where we should start returning dirents.
	 *
	 * We fake up "." if we're not reading the FS root
	 * and we always fake up "..".
	 *
	 * off contains the entry we are starting at,
	 * num increments while we are reading.
	 */

	off = num = uio->uio_offset;
	faked = (ip->i_number == NTFS_ROOTINO) ? 1 : 2;

	/* Simulate . in every dir except ROOT */
	if (ip->i_number != NTFS_ROOTINO && num == 0) {
		if (vop_write_dirent(&error, uio, ip->i_number,
		    DT_DIR, 1, "."))
			goto done;
		if (error)
			goto done;

		num++;
		ncookies++;
	}

	/* Simulate .. in every dir including ROOT */
	if (num == faked - 1) {
		/* XXX NTFS_ROOTINO seems to be wrong here */
		if (vop_write_dirent(&error, uio, NTFS_ROOTINO,
		    DT_DIR, 2, ".."))
			goto readdone;
		if (error)
			goto done;

		num++;
		ncookies++;
	}

	for (;;) {
		struct attr_indexentry *iep;

		/*
		 * num is the number of the entry we will return,
		 * but ntfs_ntreaddir takes the entry number of the
		 * ntfs directory listing, so subtract the faked
		 * . and .. entries.
		 */
		error = ntfs_ntreaddir(ntmp, fp, num - faked, &iep);

		if (error)
			goto done;

		if( NULL == iep )
			break;

		for (; !(iep->ie_flag & NTFS_IEFLAG_LAST);
			iep = NTFS_NEXTREC(iep, struct attr_indexentry *))
		{
			if(!ntfs_isnamepermitted(ntmp,iep))
				continue;
			for(i=0, j=0; i < iep->ie_fnamelen; i++, j++) {
				c = NTFS_U28(iep->ie_fname[i]);
				if (c&0xFF00)
					convname[j++] = (char)(c>>8);
				convname[j] = (char)c&0xFF;
			}
			convname[j] = '\0';
			if (vop_write_dirent(&error, uio, iep->ie_number,
			    (iep->ie_fflag & NTFS_FFLAG_DIR) ? DT_DIR : DT_REG,
			    j, convname))
				goto readdone;

			dprintf(("ntfs_readdir: elem: %d, fname:[%s] type: %d, "
				 "flag: %d, %s\n",
				 ncookies, convname, iep->ie_fnametype,
				 iep->ie_flag,
				 (iep->ie_fflag & NTFS_FFLAG_DIR) ?
					"dir" : "reg"));

			if (error)
				goto done;

			ncookies++;
			num++;
		}
	}

readdone:
	uio->uio_offset = num;

	dprintf(("ntfs_readdir: %d entries (%d bytes) read\n",
		ncookies,(u_int)(uio->uio_offset - off)));
	dprintf(("ntfs_readdir: off: %u resid: %zd\n",
		(uint32_t)uio->uio_offset, uio->uio_resid));

	if (!error && ap->a_ncookies != NULL) {
		off_t *cookies;
		off_t *cookiep;

		ddprintf(("ntfs_readdir: %d cookies\n",ncookies));
		if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1)
			panic("ntfs_readdir: unexpected uio from NFS server");
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
		cookiep = cookies;
		while (off < num)
			*cookiep++ = ++off;

		*ap->a_ncookies = ncookies;
		*ap->a_cookies = cookies;
	}
/*
	if (ap->a_eofflag)
	    *ap->a_eofflag = VTONT(vp)->i_size <= uio->uio_offset;
*/
done:
	vn_unlock(vp);
	return (error);
}

/*
 * ntfs_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp)
 */
int
ntfs_lookup(struct vop_old_lookup_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct ntnode *dip = VTONT(dvp);
	struct ntfsmount *ntmp = dip->i_mp;
	struct componentname *cnp = ap->a_cnp;
	int error;
	int lockparent = cnp->cn_flags & CNP_LOCKPARENT;
#if NTFS_DEBUG
	int wantparent = cnp->cn_flags & (CNP_LOCKPARENT | CNP_WANTPARENT);
#endif
	dprintf(("ntfs_lookup: \"%.*s\" (%ld bytes) in %ju, lp: %d, wp: %d \n",
		(int)cnp->cn_namelen, cnp->cn_nameptr, cnp->cn_namelen,
		(uintmax_t)dip->i_number, lockparent, wantparent));

	*ap->a_vpp = NULL;

	if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		dprintf(("ntfs_lookup: faking . directory in %u\n",
			(uint32_t)dip->i_number));

		vref(dvp);
		*ap->a_vpp = dvp;
		error = 0;
	} else if (cnp->cn_flags & CNP_ISDOTDOT) {
		struct ntvattr *vap;

		dprintf(("ntfs_lookup: faking .. directory in %d\n",
			(uint32_t)dip->i_number));

		error = ntfs_ntvattrget(ntmp, dip, NTFS_A_NAME, NULL, 0, &vap);
		if(error)
			return (error);

		VOP__UNLOCK(dvp, 0);
		cnp->cn_flags |= CNP_PDIRUNLOCK;

		dprintf(("ntfs_lookup: parentdir: %d\n",
			 vap->va_a_name->n_pnumber));
		error = VFS_VGET(ntmp->ntm_mountp, NULL,
				 vap->va_a_name->n_pnumber,ap->a_vpp); 
		ntfs_ntvattrrele(vap);
		if (error) {
			if (VOP_LOCK(dvp, LK_EXCLUSIVE | LK_RETRY) == 0)
				cnp->cn_flags &= ~CNP_PDIRUNLOCK;
			return (error);
		}

		if (lockparent) {
			error = VOP_LOCK(dvp, LK_EXCLUSIVE);
			if (error) {
				vput(*ap->a_vpp);
				*ap->a_vpp = NULL;
				return (error);
			}
			cnp->cn_flags &= ~CNP_PDIRUNLOCK;
		}
	} else {
		error = ntfs_ntlookupfile(ntmp, dvp, cnp, ap->a_vpp);
		if (error) {
			dprintf(("ntfs_ntlookupfile: returned %d\n", error));
			return (error);
		}

		dprintf(("ntfs_lookup: found ino: %u\n",
			(uint32_t)VTONT(*ap->a_vpp)->i_number));

		if (!lockparent) {
			VOP__UNLOCK(dvp, 0);
			cnp->cn_flags |= CNP_PDIRUNLOCK;
		}
	}
	return (error);
}

/*
 * Flush the blocks of a file to disk.
 *
 * This function is worthless for vnodes that represent directories. Maybe we
 * could just do a sync if they try an fsync on a directory file.
 *
 * ntfs_fsync(struct vnode *a_vp, int a_waitfor)
 */
static int
ntfs_fsync(struct vop_fsync_args *ap)
{
	return (0);
}

/*
 * Return POSIX pathconf information applicable to NTFS filesystem
 */
int
ntfs_pathconf(struct vop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = NTFS_MAXFILENAME;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Global vfs data structures
 */
struct vop_ops ntfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_getattr =		ntfs_getattr,
	.vop_inactive =		ntfs_inactive,
	.vop_reclaim =		ntfs_reclaim,
	.vop_print =		ntfs_print,
	.vop_pathconf =		ntfs_pathconf,
	.vop_old_lookup =	ntfs_lookup,
	.vop_access =		ntfs_access,
	.vop_close =		ntfs_close,
	.vop_open =		ntfs_open,
	.vop_readdir =		ntfs_readdir,
	.vop_fsync =		ntfs_fsync,
	.vop_bmap =		ntfs_bmap,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_strategy =		ntfs_strategy,
	.vop_read =		ntfs_read,
	.vop_write =		ntfs_write
};
