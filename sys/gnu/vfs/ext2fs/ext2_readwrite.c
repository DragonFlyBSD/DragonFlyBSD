/*
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ufs_readwrite.c	8.7 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_readwrite.c,v 1.18.2.2 2000/12/22 18:44:33 dillon Exp $
 */

#define	BLKSIZE(a, b, c)	blksize(a, b, c)

/*
 * Vnode op for reading.
 *
 * ext2_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
/* ARGSUSED */
static int
ext2_read(struct vop_read_args *ap)
{
	struct vnode *vp;
	struct inode *ip;
	struct uio *uio;
	struct ext2_sb_info *fs;
	struct buf *bp;
	daddr_t lbn, nextlbn;
	off_t nextloffset;
	off_t bytesinfile;
	long size, xfersize, blkoffset;
	int error, orig_resid;
	int seqcount = ap->a_ioflag >> 16;

	vp = ap->a_vp;
	ip = VTOI(vp);
	uio = ap->a_uio;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("ext2_read: mode");

	if (vp->v_type == VLNK) {
		if ((int)ip->i_size < vp->v_mount->mnt_maxsymlinklen)
			panic("ext2_read: short symlink");
	} else if (vp->v_type != VREG && vp->v_type != VDIR)
		panic("ext2_read: type %d", vp->v_type);
#endif
	fs = ip->i_e2fs;
#if 0
	if ((u_quad_t)uio->uio_offset > fs->fs_maxfilesize)
		return (EFBIG);
#endif

	orig_resid = uio->uio_resid;
	for (error = 0, bp = NULL; uio->uio_resid > 0; bp = NULL) {
		if ((bytesinfile = ip->i_size - uio->uio_offset) <= 0)
			break;
		lbn = lblkno(fs, uio->uio_offset);
		nextlbn = lbn + 1;
		nextloffset = lblktodoff(fs, nextlbn);
		size = BLKSIZE(fs, ip, lbn);
		blkoffset = blkoff(fs, uio->uio_offset);

		xfersize = fs->s_frag_size - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		if (bytesinfile < xfersize)
			xfersize = bytesinfile;

		if (nextloffset >= ip->i_size) {
			error = bread(vp, lblktodoff(fs, lbn), size, &bp);
		} else if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0) {
			error = cluster_read(vp, (off_t)ip->i_size,
					     lblktodoff(fs, lbn), size,
					     uio->uio_resid,
					     (ap->a_ioflag >> 16) * BKVASIZE,
					     &bp);
		} else if (seqcount > 1) {
			int nextsize = BLKSIZE(fs, ip, nextlbn);
			error = breadn(vp, lblktodoff(fs, lbn),
					size, &nextloffset, &nextsize, 1, &bp);
		} else {
			error = bread(vp, lblktodoff(fs, lbn), size, &bp);
		}
		if (error) {
			brelse(bp);
			bp = NULL;
			break;
		}

		/*
		 * We should only get non-zero b_resid when an I/O error
		 * has occurred, which should cause us to break above.
		 * However, if the short read did not cause an error,
		 * then we want to ensure that we do not uiomove bad
		 * or uninitialized data.
		 */
		size -= bp->b_resid;
		if (size < xfersize) {
			if (size == 0)
				break;
			xfersize = size;
		}
		error = uiomove((char *)bp->b_data + blkoffset,
				(int)xfersize, uio);
		if (error)
			break;

		bqrelse(bp);
	}
	if (bp != NULL)
		bqrelse(bp);
	if (orig_resid > 0 && (error == 0 || uio->uio_resid != orig_resid) &&
	    (vp->v_mount->mnt_flag & MNT_NOATIME) == 0)
		ip->i_flag |= IN_ACCESS;
	return (error);
}

/*
 * Vnode op for writing.
 *
 * ext2_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
static int
ext2_write(struct vop_write_args *ap)
{
	struct vnode *vp;
	struct uio *uio;
	struct inode *ip;
	struct ext2_sb_info *fs;
	struct buf *bp;
	struct thread *td;
	daddr_t lbn;
	off_t osize;
	int seqcount;
	int blkoffset, error, flags, ioflag, resid, size, xfersize;

	ioflag = ap->a_ioflag;
	seqcount = ap->a_ioflag >> 16;
	uio = ap->a_uio;
	vp = ap->a_vp;
	ip = VTOI(vp);

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_WRITE)
		panic("ext2_write: mode");
#endif

	switch (vp->v_type) {
	case VREG:
		if (ioflag & IO_APPEND)
			uio->uio_offset = ip->i_size;
		if ((ip->i_flags & APPEND) && uio->uio_offset != ip->i_size)
			return (EPERM);
		/* FALLTHROUGH */
	case VLNK:
		break;
	case VDIR:
		if ((ioflag & IO_SYNC) == 0)
			panic("ext2_write: nonsync dir write");
		break;
	default:
		panic("ext2_write: type");
	}

	fs = ip->i_e2fs;
#if 0
	if (uio->uio_offset < 0 ||
	    (u_quad_t)uio->uio_offset + uio->uio_resid > fs->fs_maxfilesize)
		return (EFBIG);
#endif
	/*
	 * Maybe this should be above the vnode op call, but so long as
	 * file servers have no limits, I don't think it matters.
	 */
	td = uio->uio_td;
	if (vp->v_type == VREG && td && td->td_proc &&
	    uio->uio_offset + uio->uio_resid >
	    td->td_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		lwpsignal(td->td_proc, td->td_lwp, SIGXFSZ);
		return (EFBIG);
	}

	resid = uio->uio_resid;
	osize = ip->i_size;
	flags = ioflag & IO_SYNC ? B_SYNC : 0;

	for (error = 0; uio->uio_resid > 0;) {
		lbn = lblkno(fs, uio->uio_offset);
		blkoffset = blkoff(fs, uio->uio_offset);
		xfersize = fs->s_frag_size - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;

		if (uio->uio_offset + xfersize > ip->i_size)
			vnode_pager_setsize(vp, uio->uio_offset + xfersize);

		/*
		 * Avoid a data-consistency race between write() and mmap()
		 * by ensuring that newly allocated blocks are zerod.  The
		 * race can occur even in the case where the write covers
		 * the entire block.
		 *
		 * Just set B_CLRBUF unconditionally, even if the write
		 * covers the whole block.  This also handles the UIO_NOCOPY
		 * case.  See ffs_write() in ufs for a more sophisticated
		 * version.
		 */
		flags |= B_CLRBUF;
		error = ext2_balloc(ip, lbn, blkoffset + xfersize,
				    ap->a_cred, &bp, flags);
		if (error)
			break;

		if (uio->uio_offset + xfersize > ip->i_size) {
			ip->i_size = uio->uio_offset + xfersize;
		}

		size = BLKSIZE(fs, ip, lbn) - bp->b_resid;
		if (size < xfersize)
			xfersize = size;

		error = uiomove((char *)bp->b_data + blkoffset, xfersize, uio);
		if ((ioflag & IO_VMIO) &&
		    LIST_FIRST(&bp->b_dep) == NULL) /* in ext2fs? */
			bp->b_flags |= B_RELBUF;

		if (ioflag & IO_SYNC) {
			bwrite(bp);
		} else if (xfersize + blkoffset == fs->s_frag_size) {
			if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERW) == 0) {
				bp->b_flags |= B_CLUSTEROK;
				cluster_write(bp, (off_t)ip->i_size, vp->v_mount->mnt_stat.f_iosize, seqcount);
			} else {
				bawrite(bp);
			}
		} else {
			bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
		if (error || xfersize == 0)
			break;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	/*
	 * If we successfully wrote any data, and we are not the superuser
	 * we clear the setuid and setgid bits as a precaution against
	 * tampering.
	 */
	if (resid > uio->uio_resid && ap->a_cred && ap->a_cred->cr_uid != 0)
		ip->i_mode &= ~(ISUID | ISGID);
	if (error) {
		if (ioflag & IO_UNIT) {
			EXT2_TRUNCATE(vp, osize, ioflag & IO_SYNC, ap->a_cred);
			uio->uio_offset -= resid - uio->uio_resid;
			uio->uio_resid = resid;
		}
	} else if (resid > uio->uio_resid && (ioflag & IO_SYNC))
		error = EXT2_UPDATE(vp, 1);
	return (error);
}
