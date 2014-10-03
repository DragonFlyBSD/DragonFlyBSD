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
 *	@(#)nfs_bio.c	8.9 (Berkeley) 3/30/95
 * $FreeBSD: /repoman/r/ncvs/src/sys/nfsclient/nfs_bio.c,v 1.130 2004/04/14 23:23:55 peadar Exp $
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

#include <sys/buf2.h>
#include <sys/thread2.h>
#include <vm/vm_page2.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"


static struct buf *nfs_getcacheblk(struct vnode *vp, off_t loffset,
				   int size, struct thread *td);
static int nfs_check_dirent(struct nfs_dirent *dp, int maxlen);
static void nfsiodone_sync(struct bio *bio);
static void nfs_readrpc_bio_done(nfsm_info_t info);
static void nfs_writerpc_bio_done(nfsm_info_t info);
static void nfs_commitrpc_bio_done(nfsm_info_t info);

/*
 * Vnode op for read using bio
 */
int
nfs_bioread(struct vnode *vp, struct uio *uio, int ioflag)
{
	struct nfsnode *np = VTONFS(vp);
	int biosize, i;
	struct buf *bp, *rabp;
	struct vattr vattr;
	struct thread *td;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	off_t lbn, rabn;
	off_t raoffset;
	off_t loffset;
	int seqcount;
	int nra, error = 0;
	int boff = 0;
	size_t n;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("nfs_read mode");
#endif
	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)	/* XXX VDIR cookies can be negative */
		return (EINVAL);
	td = uio->uio_td;

	if ((nmp->nm_flag & NFSMNT_NFSV3) != 0 &&
	    (nmp->nm_state & NFSSTA_GOTFSINFO) == 0)
		(void)nfs_fsinfo(nmp, vp, td);
	if (vp->v_type != VDIR &&
	    (uio->uio_offset + uio->uio_resid) > nmp->nm_maxfilesize)
		return (EFBIG);
	biosize = vp->v_mount->mnt_stat.f_iosize;
	seqcount = (int)((off_t)(ioflag >> IO_SEQSHIFT) * biosize / BKVASIZE);

	/*
	 * For nfs, cache consistency can only be maintained approximately.
	 * Although RFC1094 does not specify the criteria, the following is
	 * believed to be compatible with the reference port.
	 *
	 * NFS:		If local changes have been made and this is a
	 *		directory, the directory must be invalidated and
	 *		the attribute cache must be cleared.
	 *
	 *		GETATTR is called to synchronize the file size.
	 *
	 *		If remote changes are detected local data is flushed
	 *		and the cache is invalidated.
	 *
	 *		NOTE: In the normal case the attribute cache is not
	 *		cleared which means GETATTR may use cached data and
	 *		not immediately detect changes made on the server.
	 */
	if ((np->n_flag & NLMODIFIED) && vp->v_type == VDIR) {
		nfs_invaldir(vp);
		error = nfs_vinvalbuf(vp, V_SAVE, 1);
		if (error)
			return (error);
		np->n_attrstamp = 0;
	}
	error = VOP_GETATTR(vp, &vattr);
	if (error)
		return (error);

	/*
	 * This can deadlock getpages/putpages for regular
	 * files.  Only do it for directories.
	 */
	if (np->n_flag & NRMODIFIED) {
		if (vp->v_type == VDIR) {
			nfs_invaldir(vp);
			error = nfs_vinvalbuf(vp, V_SAVE, 1);
			if (error)
				return (error);
			np->n_flag &= ~NRMODIFIED;
		}
	}

	/*
	 * Loop until uio exhausted or we hit EOF
	 */
	do {
	    bp = NULL;

	    switch (vp->v_type) {
	    case VREG:
		nfsstats.biocache_reads++;
		lbn = uio->uio_offset / biosize;
		boff = uio->uio_offset & (biosize - 1);
		loffset = lbn * biosize;

		/*
		 * Start the read ahead(s), as required.
		 */
		if (nmp->nm_readahead > 0 && nfs_asyncok(nmp)) {
		    for (nra = 0; nra < nmp->nm_readahead && nra < seqcount &&
			(off_t)(lbn + 1 + nra) * biosize < np->n_size; nra++) {
			rabn = lbn + 1 + nra;
			raoffset = rabn * biosize;
			if (findblk(vp, raoffset, FINDBLK_TEST) == NULL) {
			    rabp = nfs_getcacheblk(vp, raoffset, biosize, td);
			    if (!rabp)
				return (EINTR);
			    if ((rabp->b_flags & (B_CACHE|B_DELWRI)) == 0) {
				rabp->b_cmd = BUF_CMD_READ;
				vfs_busy_pages(vp, rabp);
				nfs_asyncio(vp, &rabp->b_bio2);
			    } else {
				brelse(rabp);
			    }
			}
		    }
		}

		/*
		 * Obtain the buffer cache block.  Figure out the buffer size
		 * when we are at EOF.  If we are modifying the size of the
		 * buffer based on an EOF condition we need to hold 
		 * nfs_rslock() through obtaining the buffer to prevent
		 * a potential writer-appender from messing with n_size.
		 * Otherwise we may accidently truncate the buffer and
		 * lose dirty data.
		 *
		 * Note that bcount is *not* DEV_BSIZE aligned.
		 */
		if (loffset + boff >= np->n_size) {
			n = 0;
			break;
		}
		bp = nfs_getcacheblk(vp, loffset, biosize, td);

		if (bp == NULL)
			return (EINTR);

		/*
		 * If B_CACHE is not set, we must issue the read.  If this
		 * fails, we return an error.
		 */
		if ((bp->b_flags & B_CACHE) == 0) {
			bp->b_cmd = BUF_CMD_READ;
			bp->b_bio2.bio_done = nfsiodone_sync;
			bp->b_bio2.bio_flags |= BIO_SYNC;
			vfs_busy_pages(vp, bp);
			error = nfs_doio(vp, &bp->b_bio2, td);
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		/*
		 * on is the offset into the current bp.  Figure out how many
		 * bytes we can copy out of the bp.  Note that bcount is
		 * NOT DEV_BSIZE aligned.
		 *
		 * Then figure out how many bytes we can copy into the uio.
		 */
		n = biosize - boff;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (loffset + boff + n > np->n_size)
			n = np->n_size - loffset - boff;
		break;
	    case VLNK:
		biosize = min(NFS_MAXPATHLEN, np->n_size);
		nfsstats.biocache_readlinks++;
		bp = nfs_getcacheblk(vp, (off_t)0, biosize, td);
		if (bp == NULL)
			return (EINTR);
		if ((bp->b_flags & B_CACHE) == 0) {
			bp->b_cmd = BUF_CMD_READ;
			bp->b_bio2.bio_done = nfsiodone_sync;
			bp->b_bio2.bio_flags |= BIO_SYNC;
			vfs_busy_pages(vp, bp);
			error = nfs_doio(vp, &bp->b_bio2, td);
			if (error) {
				bp->b_flags |= B_ERROR | B_INVAL;
				brelse(bp);
				return (error);
			}
		}
		n = szmin(uio->uio_resid, (size_t)bp->b_bcount - bp->b_resid);
		boff = 0;
		break;
	    case VDIR:
		nfsstats.biocache_readdirs++;
		if (np->n_direofoffset &&
		    uio->uio_offset >= np->n_direofoffset
		) {
			return (0);
		}
		lbn = (uoff_t)uio->uio_offset / NFS_DIRBLKSIZ;
		boff = uio->uio_offset & (NFS_DIRBLKSIZ - 1);
		loffset = uio->uio_offset - boff;
		bp = nfs_getcacheblk(vp, loffset, NFS_DIRBLKSIZ, td);
		if (bp == NULL)
			return (EINTR);

		if ((bp->b_flags & B_CACHE) == 0) {
		    bp->b_cmd = BUF_CMD_READ;
		    bp->b_bio2.bio_done = nfsiodone_sync;
		    bp->b_bio2.bio_flags |= BIO_SYNC;
		    vfs_busy_pages(vp, bp);
		    error = nfs_doio(vp, &bp->b_bio2, td);
		    if (error)
			    brelse(bp);
		    while (error == NFSERR_BAD_COOKIE) {
			kprintf("got bad cookie vp %p bp %p\n", vp, bp);
			nfs_invaldir(vp);
			error = nfs_vinvalbuf(vp, 0, 1);
			/*
			 * Yuck! The directory has been modified on the
			 * server. The only way to get the block is by
			 * reading from the beginning to get all the
			 * offset cookies.
			 *
			 * Leave the last bp intact unless there is an error.
			 * Loop back up to the while if the error is another
			 * NFSERR_BAD_COOKIE (double yuch!).
			 */
			for (i = 0; i <= lbn && !error; i++) {
			    if (np->n_direofoffset
				&& (i * NFS_DIRBLKSIZ) >= np->n_direofoffset)
				    return (0);
			    bp = nfs_getcacheblk(vp, (off_t)i * NFS_DIRBLKSIZ,
						 NFS_DIRBLKSIZ, td);
			    if (!bp)
				return (EINTR);
			    if ((bp->b_flags & B_CACHE) == 0) {
				    bp->b_cmd = BUF_CMD_READ;
				    bp->b_bio2.bio_done = nfsiodone_sync;
				    bp->b_bio2.bio_flags |= BIO_SYNC;
				    vfs_busy_pages(vp, bp);
				    error = nfs_doio(vp, &bp->b_bio2, td);
				    /*
				     * no error + B_INVAL == directory EOF,
				     * use the block.
				     */
				    if (error == 0 && (bp->b_flags & B_INVAL))
					    break;
			    }
			    /*
			     * An error will throw away the block and the
			     * for loop will break out.  If no error and this
			     * is not the block we want, we throw away the
			     * block and go for the next one via the for loop.
			     */
			    if (error || i < lbn)
				    brelse(bp);
			}
		    }
		    /*
		     * The above while is repeated if we hit another cookie
		     * error.  If we hit an error and it wasn't a cookie error,
		     * we give up.
		     */
		    if (error)
			    return (error);
		}

		/*
		 * If not eof and read aheads are enabled, start one.
		 * (You need the current block first, so that you have the
		 *  directory offset cookie of the next block.)
		 */
		if (nmp->nm_readahead > 0 && nfs_asyncok(nmp) &&
		    (bp->b_flags & B_INVAL) == 0 &&
		    (np->n_direofoffset == 0 ||
		    loffset + NFS_DIRBLKSIZ < np->n_direofoffset) &&
		    findblk(vp, loffset + NFS_DIRBLKSIZ, FINDBLK_TEST) == NULL
		) {
			rabp = nfs_getcacheblk(vp, loffset + NFS_DIRBLKSIZ,
					       NFS_DIRBLKSIZ, td);
			if (rabp) {
			    if ((rabp->b_flags & (B_CACHE|B_DELWRI)) == 0) {
				rabp->b_cmd = BUF_CMD_READ;
				vfs_busy_pages(vp, rabp);
				nfs_asyncio(vp, &rabp->b_bio2);
			    } else {
				brelse(rabp);
			    }
			}
		}
		/*
		 * Unlike VREG files, whos buffer size ( bp->b_bcount ) is
		 * chopped for the EOF condition, we cannot tell how large
		 * NFS directories are going to be until we hit EOF.  So
		 * an NFS directory buffer is *not* chopped to its EOF.  Now,
		 * it just so happens that b_resid will effectively chop it
		 * to EOF.  *BUT* this information is lost if the buffer goes
		 * away and is reconstituted into a B_CACHE state ( due to
		 * being VMIO ) later.  So we keep track of the directory eof
		 * in np->n_direofoffset and chop it off as an extra step 
		 * right here.
		 *
		 * NOTE: boff could already be beyond EOF.
		 */
		if ((size_t)boff > NFS_DIRBLKSIZ - bp->b_resid) {
			n = 0;
		} else {
			n = szmin(uio->uio_resid,
				  NFS_DIRBLKSIZ - bp->b_resid - (size_t)boff);
		}
		if (np->n_direofoffset &&
		    n > (size_t)(np->n_direofoffset - uio->uio_offset)) {
			n = (size_t)(np->n_direofoffset - uio->uio_offset);
		}
		break;
	    default:
		kprintf(" nfs_bioread: type %x unexpected\n",vp->v_type);
		n = 0;
		break;
	    }

	    switch (vp->v_type) {
	    case VREG:
		if (n > 0)
		    error = uiomovebp(bp, bp->b_data + boff, n, uio);
		break;
	    case VLNK:
		if (n > 0)
		    error = uiomovebp(bp, bp->b_data + boff, n, uio);
		n = 0;
		break;
	    case VDIR:
		if (n > 0) {
		    off_t old_off = uio->uio_offset;
		    caddr_t cpos, epos;
		    struct nfs_dirent *dp;

		    /*
		     * We are casting cpos to nfs_dirent, it must be
		     * int-aligned.
		     */
		    if (boff & 3) {
			error = EINVAL;
			break;
		    }

		    cpos = bp->b_data + boff;
		    epos = bp->b_data + boff + n;
		    while (cpos < epos && error == 0 && uio->uio_resid > 0) {
			    dp = (struct nfs_dirent *)cpos;
			    error = nfs_check_dirent(dp, (int)(epos - cpos));
			    if (error)
				    break;
			    if (vop_write_dirent(&error, uio, dp->nfs_ino,
				dp->nfs_type, dp->nfs_namlen, dp->nfs_name)) {
				    break;
			    }
			    cpos += dp->nfs_reclen;
		    }
		    n = 0;
		    if (error == 0) {
			    uio->uio_offset = old_off + cpos -
					      bp->b_data - boff;
		    }
		}
		break;
	    default:
		kprintf(" nfs_bioread: type %x unexpected\n",vp->v_type);
	    }
	    if (bp)
		    brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n > 0);
	return (error);
}

/*
 * Userland can supply any 'seek' offset when reading a NFS directory.
 * Validate the structure so we don't panic the kernel.  Note that
 * the element name is nul terminated and the nul is not included
 * in nfs_namlen.
 */
static
int
nfs_check_dirent(struct nfs_dirent *dp, int maxlen)
{
	int nfs_name_off = offsetof(struct nfs_dirent, nfs_name[0]);

	if (nfs_name_off >= maxlen)
		return (EINVAL);
	if (dp->nfs_reclen < nfs_name_off || dp->nfs_reclen > maxlen)
		return (EINVAL);
	if (nfs_name_off + dp->nfs_namlen >= dp->nfs_reclen)
		return (EINVAL);
	if (dp->nfs_reclen & 3)
		return (EINVAL);
	return (0);
}

/*
 * Vnode op for write using bio
 *
 * nfs_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
int
nfs_write(struct vop_write_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct thread *td = uio->uio_td;
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	int ioflag = ap->a_ioflag;
	struct buf *bp;
	struct vattr vattr;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	off_t loffset;
	int boff, bytes;
	int error = 0;
	int haverslock = 0;
	int bcount;
	int biosize;
	int trivial;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_WRITE)
		panic("nfs_write mode");
	if (uio->uio_segflg == UIO_USERSPACE && uio->uio_td != curthread)
		panic("nfs_write proc");
#endif
	if (vp->v_type != VREG)
		return (EIO);

	lwkt_gettoken(&nmp->nm_token);

	if (np->n_flag & NWRITEERR) {
		np->n_flag &= ~NWRITEERR;
		lwkt_reltoken(&nmp->nm_token);
		return (np->n_error);
	}
	if ((nmp->nm_flag & NFSMNT_NFSV3) != 0 &&
	    (nmp->nm_state & NFSSTA_GOTFSINFO) == 0) {
		(void)nfs_fsinfo(nmp, vp, td);
	}

	/*
	 * Synchronously flush pending buffers if we are in synchronous
	 * mode or if we are appending.
	 */
	if (ioflag & (IO_APPEND | IO_SYNC)) {
		if (np->n_flag & NLMODIFIED) {
			np->n_attrstamp = 0;
			error = nfs_flush(vp, MNT_WAIT, td, 0);
			/* error = nfs_vinvalbuf(vp, V_SAVE, 1); */
			if (error)
				goto  done;
		}
	}

	/*
	 * If IO_APPEND then load uio_offset.  We restart here if we cannot
	 * get the append lock.
	 */
restart:
	if (ioflag & IO_APPEND) {
		np->n_attrstamp = 0;
		error = VOP_GETATTR(vp, &vattr);
		if (error)
			goto done;
		uio->uio_offset = np->n_size;
	}

	if (uio->uio_offset < 0) {
		error = EINVAL;
		goto done;
	}
	if ((uio->uio_offset + uio->uio_resid) > nmp->nm_maxfilesize) {
		error = EFBIG;
		goto done;
	}
	if (uio->uio_resid == 0) {
		error = 0;
		goto done;
	}

	/*
	 * We need to obtain the rslock if we intend to modify np->n_size
	 * in order to guarentee the append point with multiple contending
	 * writers, to guarentee that no other appenders modify n_size
	 * while we are trying to obtain a truncated buffer (i.e. to avoid
	 * accidently truncating data written by another appender due to
	 * the race), and to ensure that the buffer is populated prior to
	 * our extending of the file.  We hold rslock through the entire
	 * operation.
	 *
	 * Note that we do not synchronize the case where someone truncates
	 * the file while we are appending to it because attempting to lock
	 * this case may deadlock other parts of the system unexpectedly.
	 */
	if ((ioflag & IO_APPEND) ||
	    uio->uio_offset + uio->uio_resid > np->n_size) {
		switch(nfs_rslock(np)) {
		case ENOLCK:
			goto restart;
			/* not reached */
		case EINTR:
		case ERESTART:
			error = EINTR;
			goto done;
			/* not reached */
		default:
			break;
		}
		haverslock = 1;
	}

	/*
	 * Maybe this should be above the vnode op call, but so long as
	 * file servers have no limits, i don't think it matters
	 */
	if (td && td->td_proc && uio->uio_offset + uio->uio_resid >
	      td->td_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		lwpsignal(td->td_proc, td->td_lwp, SIGXFSZ);
		if (haverslock)
			nfs_rsunlock(np);
		error = EFBIG;
		goto done;
	}

	biosize = vp->v_mount->mnt_stat.f_iosize;

	do {
		nfsstats.biocache_writes++;
		boff = uio->uio_offset & (biosize-1);
		loffset = uio->uio_offset - boff;
		bytes = (int)szmin((unsigned)(biosize - boff), uio->uio_resid);
again:
		/*
		 * Handle direct append and file extension cases, calculate
		 * unaligned buffer size.  When extending B_CACHE will be
		 * set if possible.  See UIO_NOCOPY note below.
		 */
		if (uio->uio_offset + bytes > np->n_size) {
			np->n_flag |= NLMODIFIED;
			trivial = (uio->uio_segflg != UIO_NOCOPY &&
				   uio->uio_offset <= np->n_size);
			nfs_meta_setsize(vp, td, uio->uio_offset + bytes,
					 trivial);
		}
		bp = nfs_getcacheblk(vp, loffset, biosize, td);
		if (bp == NULL) {
			error = EINTR;
			break;
		}

		/*
		 * Actual bytes in buffer which we care about
		 */
		if (loffset + biosize < np->n_size)
			bcount = biosize;
		else
			bcount = (int)(np->n_size - loffset);

		/*
		 * Avoid a read by setting B_CACHE where the data we
		 * intend to write covers the entire buffer.  Note
		 * that the buffer may have been set to B_CACHE by
		 * nfs_meta_setsize() above or otherwise inherited the
		 * flag, but if B_CACHE isn't set the buffer may be
		 * uninitialized and must be zero'd to accomodate
		 * future seek+write's.
		 *
		 * See the comments in kern/vfs_bio.c's getblk() for
		 * more information.
		 *
		 * When doing a UIO_NOCOPY write the buffer is not
		 * overwritten and we cannot just set B_CACHE unconditionally
		 * for full-block writes.
		 */
		if (boff == 0 && bytes == biosize &&
		    uio->uio_segflg != UIO_NOCOPY) {
			bp->b_flags |= B_CACHE;
			bp->b_flags &= ~(B_ERROR | B_INVAL);
		}

		/*
		 * b_resid may be set due to file EOF if we extended out.
		 * The NFS bio code will zero the difference anyway so
		 * just acknowledged the fact and set b_resid to 0.
		 */
		if ((bp->b_flags & B_CACHE) == 0) {
			bp->b_cmd = BUF_CMD_READ;
			bp->b_bio2.bio_done = nfsiodone_sync;
			bp->b_bio2.bio_flags |= BIO_SYNC;
			vfs_busy_pages(vp, bp);
			error = nfs_doio(vp, &bp->b_bio2, td);
			if (error) {
				brelse(bp);
				break;
			}
			bp->b_resid = 0;
		}
		np->n_flag |= NLMODIFIED;

		/*
		 * If dirtyend exceeds file size, chop it down.  This should
		 * not normally occur but there is an append race where it
		 * might occur XXX, so we log it. 
		 *
		 * If the chopping creates a reverse-indexed or degenerate
		 * situation with dirtyoff/end, we 0 both of them.
		 */
		if (bp->b_dirtyend > bcount) {
			kprintf("NFS append race @%08llx:%d\n", 
			    (long long)bp->b_bio2.bio_offset,
			    bp->b_dirtyend - bcount);
			bp->b_dirtyend = bcount;
		}

		if (bp->b_dirtyoff >= bp->b_dirtyend)
			bp->b_dirtyoff = bp->b_dirtyend = 0;

		/*
		 * If the new write will leave a contiguous dirty
		 * area, just update the b_dirtyoff and b_dirtyend,
		 * otherwise force a write rpc of the old dirty area.
		 *
		 * While it is possible to merge discontiguous writes due to 
		 * our having a B_CACHE buffer ( and thus valid read data
		 * for the hole), we don't because it could lead to 
		 * significant cache coherency problems with multiple clients,
		 * especially if locking is implemented later on.
		 *
		 * as an optimization we could theoretically maintain
		 * a linked list of discontinuous areas, but we would still
		 * have to commit them separately so there isn't much
		 * advantage to it except perhaps a bit of asynchronization.
		 */
		if (bp->b_dirtyend > 0 &&
		    (boff > bp->b_dirtyend ||
		     (boff + bytes) < bp->b_dirtyoff)
		) {
			if (bwrite(bp) == EINTR) {
				error = EINTR;
				break;
			}
			goto again;
		}

		error = uiomovebp(bp, bp->b_data + boff, bytes, uio);

		/*
		 * Since this block is being modified, it must be written
		 * again and not just committed.  Since write clustering does
		 * not work for the stage 1 data write, only the stage 2
		 * commit rpc, we have to clear B_CLUSTEROK as well.
		 */
		bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);

		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Only update dirtyoff/dirtyend if not a degenerate 
		 * condition.
		 *
		 * The underlying VM pages have been marked valid by
		 * virtue of acquiring the bp.  Because the entire buffer
		 * is marked dirty we do not have to worry about cleaning
		 * out the related dirty bits (and wouldn't really know
		 * how to deal with byte ranges anyway)
		 */
		if (bytes) {
			if (bp->b_dirtyend > 0) {
				bp->b_dirtyoff = imin(boff, bp->b_dirtyoff);
				bp->b_dirtyend = imax(boff + bytes,
						      bp->b_dirtyend);
			} else {
				bp->b_dirtyoff = boff;
				bp->b_dirtyend = boff + bytes;
			}
		}

		/*
		 * If the lease is non-cachable or IO_SYNC do bwrite().
		 *
		 * IO_INVAL appears to be unused.  The idea appears to be
		 * to turn off caching in this case.  Very odd.  XXX
		 *
		 * If nfs_async is set bawrite() will use an unstable write
		 * (build dirty bufs on the server), so we might as well
		 * push it out with bawrite().  If nfs_async is not set we
		 * use bdwrite() to cache dirty bufs on the client.
		 */
		if (ioflag & IO_SYNC) {
			if (ioflag & IO_INVAL)
				bp->b_flags |= B_NOCACHE;
			error = bwrite(bp);
			if (error)
				break;
		} else if (boff + bytes == biosize && nfs_async) {
			bawrite(bp);
		} else {
			bdwrite(bp);
		}
	} while (uio->uio_resid > 0 && bytes > 0);

	if (haverslock)
		nfs_rsunlock(np);

done:
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

/*
 * Get an nfs cache block.
 *
 * Allocate a new one if the block isn't currently in the cache
 * and return the block marked busy. If the calling process is
 * interrupted by a signal for an interruptible mount point, return
 * NULL.
 *
 * The caller must carefully deal with the possible B_INVAL state of
 * the buffer.  nfs_startio() clears B_INVAL (and nfs_asyncio() clears it
 * indirectly), so synchronous reads can be issued without worrying about
 * the B_INVAL state.  We have to be a little more careful when dealing
 * with writes (see comments in nfs_write()) when extending a file past
 * its EOF.
 */
static struct buf *
nfs_getcacheblk(struct vnode *vp, off_t loffset, int size, struct thread *td)
{
	struct buf *bp;
	struct mount *mp;
	struct nfsmount *nmp;

	mp = vp->v_mount;
	nmp = VFSTONFS(mp);

	if (nmp->nm_flag & NFSMNT_INT) {
		bp = getblk(vp, loffset, size, GETBLK_PCATCH, 0);
		while (bp == NULL) {
			if (nfs_sigintr(nmp, NULL, td))
				return (NULL);
			bp = getblk(vp, loffset, size, 0, 2 * hz);
		}
	} else {
		bp = getblk(vp, loffset, size, 0, 0);
	}

	/*
	 * bio2, the 'device' layer.  Since BIOs use 64 bit byte offsets
	 * now, no translation is necessary.
	 */
	bp->b_bio2.bio_offset = loffset;
	return (bp);
}

/*
 * Flush and invalidate all dirty buffers. If another process is already
 * doing the flush, just wait for completion.
 */
int
nfs_vinvalbuf(struct vnode *vp, int flags, int intrflg)
{
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, slpflag, slptimeo;
	thread_t td = curthread;

	if (vp->v_flag & VRECLAIMED)
		return (0);

	if ((nmp->nm_flag & NFSMNT_INT) == 0)
		intrflg = 0;
	if (intrflg) {
		slpflag = PCATCH;
		slptimeo = 2 * hz;
	} else {
		slpflag = 0;
		slptimeo = 0;
	}
	/*
	 * First wait for any other process doing a flush to complete.
	 */
	while (np->n_flag & NFLUSHINPROG) {
		np->n_flag |= NFLUSHWANT;
		error = tsleep((caddr_t)&np->n_flag, 0, "nfsvinval", slptimeo);
		if (error && intrflg && nfs_sigintr(nmp, NULL, td))
			return (EINTR);
	}

	/*
	 * Now, flush as required.
	 */
	np->n_flag |= NFLUSHINPROG;
	error = vinvalbuf(vp, flags, slpflag, 0);
	while (error) {
		if (intrflg && nfs_sigintr(nmp, NULL, td)) {
			np->n_flag &= ~NFLUSHINPROG;
			if (np->n_flag & NFLUSHWANT) {
				np->n_flag &= ~NFLUSHWANT;
				wakeup((caddr_t)&np->n_flag);
			}
			return (EINTR);
		}
		error = vinvalbuf(vp, flags, 0, slptimeo);
	}
	np->n_flag &= ~(NLMODIFIED | NFLUSHINPROG);
	if (np->n_flag & NFLUSHWANT) {
		np->n_flag &= ~NFLUSHWANT;
		wakeup((caddr_t)&np->n_flag);
	}
	return (0);
}

/*
 * Return true (non-zero) if the txthread and rxthread are operational
 * and we do not already have too many not-yet-started BIO's built up.
 */
int
nfs_asyncok(struct nfsmount *nmp)
{
	return (nmp->nm_bioqlen < nfs_maxasyncbio &&
		nmp->nm_bioqlen < nmp->nm_maxasync_scaled / NFS_ASYSCALE &&
		nmp->nm_rxstate <= NFSSVC_PENDING &&
		nmp->nm_txstate <= NFSSVC_PENDING);
}

/*
 * The read-ahead code calls this to queue a bio to the txthread.
 *
 * We don't touch the bio otherwise... that is, we do not even
 * construct or send the initial rpc.  The txthread will do it
 * for us.
 *
 * NOTE!  nm_bioqlen is not decremented until the request completes,
 *	  so it does not reflect the number of bio's on bioq.
 */
void
nfs_asyncio(struct vnode *vp, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);

	KKASSERT(vp->v_tag == VT_NFS);
	BUF_KERNPROC(bp);

	/*
	 * Shortcut swap cache (not done automatically because we are not
	 * using bread()).
	 */
	if (vn_cache_strategy(vp, bio))
		return;

	bio->bio_driver_info = vp;
	crit_enter();
	TAILQ_INSERT_TAIL(&nmp->nm_bioq, bio, bio_act);
	atomic_add_int(&nmp->nm_bioqlen, 1);
	crit_exit();
	nfssvc_iod_writer_wakeup(nmp);
}

/*
 * nfs_doio()	- Execute a BIO operation synchronously.  The BIO will be
 *		  completed and its error returned.  The caller is responsible
 *		  for brelse()ing it.  ONLY USE FOR BIO_SYNC IOs!  Otherwise
 *		  our error probe will be against an invalid pointer.
 *
 * nfs_startio()- Execute a BIO operation assynchronously.
 *
 * NOTE: nfs_asyncio() is used to initiate an asynchronous BIO operation,
 *	 which basically just queues it to the txthread.  nfs_startio()
 *	 actually initiates the I/O AFTER it has gotten to the txthread.
 *
 * NOTE: td might be NULL.
 *
 * NOTE: Caller has already busied the I/O.
 */
void
nfs_startio(struct vnode *vp, struct bio *bio, struct thread *td)
{
	struct buf *bp = bio->bio_buf;

	KKASSERT(vp->v_tag == VT_NFS);

	/*
	 * clear B_ERROR and B_INVAL state prior to initiating the I/O.  We
	 * do this here so we do not have to do it in all the code that
	 * calls us.
	 */
	bp->b_flags &= ~(B_ERROR | B_INVAL);

	KASSERT(bp->b_cmd != BUF_CMD_DONE,
		("nfs_doio: bp %p already marked done!", bp));

	if (bp->b_cmd == BUF_CMD_READ) {
	    switch (vp->v_type) {
	    case VREG:
		nfsstats.read_bios++;
		nfs_readrpc_bio(vp, bio);
		break;
	    case VLNK:
#if 0
		bio->bio_offset = 0;
		nfsstats.readlink_bios++;
		nfs_readlinkrpc_bio(vp, bio);
#else
		nfs_doio(vp, bio, td);
#endif
		break;
	    case VDIR:
		/*
		 * NOTE: If nfs_readdirplusrpc_bio() is requested but
		 *	 not supported, it will chain to
		 *	 nfs_readdirrpc_bio().
		 */
#if 0
		nfsstats.readdir_bios++;
		uiop->uio_offset = bio->bio_offset;
		if (nmp->nm_flag & NFSMNT_RDIRPLUS)
			nfs_readdirplusrpc_bio(vp, bio);
		else
			nfs_readdirrpc_bio(vp, bio);
#else
		nfs_doio(vp, bio, td);
#endif
		break;
	    default:
		kprintf("nfs_doio:  type %x unexpected\n",vp->v_type);
		bp->b_flags |= B_ERROR;
		bp->b_error = EINVAL;
		biodone(bio);
		break;
	    }
	} else {
	    /*
	     * If we only need to commit, try to commit.  If this fails
	     * it will chain through to the write.  Basically all the logic
	     * in nfs_doio() is replicated.
	     */
	    KKASSERT(bp->b_cmd == BUF_CMD_WRITE);
	    if (bp->b_flags & B_NEEDCOMMIT)
		nfs_commitrpc_bio(vp, bio);
	    else
		nfs_writerpc_bio(vp, bio);
	}
}

int
nfs_doio(struct vnode *vp, struct bio *bio, struct thread *td)
{
	struct buf *bp = bio->bio_buf;
	struct uio *uiop;
	struct nfsnode *np;
	struct nfsmount *nmp;
	int error = 0;
	int iomode, must_commit;
	size_t n;
	struct uio uio;
	struct iovec io;

#if 0
	/*
	 * Shortcut swap cache (not done automatically because we are not
	 * using bread()).
	 *
	 * XXX The biowait is a hack until we can figure out how to stop a
	 * biodone chain when a middle element is BIO_SYNC.  BIO_SYNC is
	 * set so the bp shouldn't get ripped out from under us.  The only
	 * use-cases are fully synchronous I/O cases.
	 *
	 * XXX This is having problems, give up for now.
	 */
	if (vn_cache_strategy(vp, bio)) {
		error = biowait(&bio->bio_buf->b_bio1, "nfsrsw");
		return (error);
	}
#endif

	KKASSERT(vp->v_tag == VT_NFS);
	np = VTONFS(vp);
	nmp = VFSTONFS(vp->v_mount);
	uiop = &uio;
	uiop->uio_iov = &io;
	uiop->uio_iovcnt = 1;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_td = td;

	/*
	 * clear B_ERROR and B_INVAL state prior to initiating the I/O.  We
	 * do this here so we do not have to do it in all the code that
	 * calls us.
	 */
	bp->b_flags &= ~(B_ERROR | B_INVAL);

	KASSERT(bp->b_cmd != BUF_CMD_DONE, 
		("nfs_doio: bp %p already marked done!", bp));

	if (bp->b_cmd == BUF_CMD_READ) {
	    io.iov_len = uiop->uio_resid = (size_t)bp->b_bcount;
	    io.iov_base = bp->b_data;
	    uiop->uio_rw = UIO_READ;

	    switch (vp->v_type) {
	    case VREG:
		/*
		 * When reading from a regular file zero-fill any residual.
		 * Note that this residual has nothing to do with NFS short
		 * reads, which nfs_readrpc_uio() will handle for us.
		 *
		 * We have to do this because when we are write extending
		 * a file the server may not have the same notion of
		 * filesize as we do.  Our BIOs should already be sized
		 * (b_bcount) to account for the file EOF.
		 */
		nfsstats.read_bios++;
		uiop->uio_offset = bio->bio_offset;
		error = nfs_readrpc_uio(vp, uiop);
		if (error == 0 && uiop->uio_resid) {
			n = (size_t)bp->b_bcount - uiop->uio_resid;
			bzero(bp->b_data + n, bp->b_bcount - n);
			uiop->uio_resid = 0;
		}
		if (td && td->td_proc && (vp->v_flag & VTEXT) &&
		    np->n_mtime != np->n_vattr.va_mtime.tv_sec) {
			uprintf("Process killed due to text file modification\n");
			ksignal(td->td_proc, SIGKILL);
		}
		break;
	    case VLNK:
		uiop->uio_offset = 0;
		nfsstats.readlink_bios++;
		error = nfs_readlinkrpc_uio(vp, uiop);
		break;
	    case VDIR:
		nfsstats.readdir_bios++;
		uiop->uio_offset = bio->bio_offset;
		if (nmp->nm_flag & NFSMNT_RDIRPLUS) {
			error = nfs_readdirplusrpc_uio(vp, uiop);
			if (error == NFSERR_NOTSUPP)
				nmp->nm_flag &= ~NFSMNT_RDIRPLUS;
		}
		if ((nmp->nm_flag & NFSMNT_RDIRPLUS) == 0)
			error = nfs_readdirrpc_uio(vp, uiop);
		/*
		 * end-of-directory sets B_INVAL but does not generate an
		 * error.
		 */
		if (error == 0 && uiop->uio_resid == bp->b_bcount)
			bp->b_flags |= B_INVAL;
		break;
	    default:
		kprintf("nfs_doio:  type %x unexpected\n",vp->v_type);
		break;
	    }
	    if (error) {
		bp->b_flags |= B_ERROR;
		bp->b_error = error;
	    }
	    bp->b_resid = uiop->uio_resid;
	} else {
	    /* 
	     * If we only need to commit, try to commit.
	     *
	     * NOTE: The I/O has already been staged for the write and
	     *	     its pages busied, so b_dirtyoff/end is valid.
	     */
	    KKASSERT(bp->b_cmd == BUF_CMD_WRITE);
	    if (bp->b_flags & B_NEEDCOMMIT) {
		    int retv;
		    off_t off;

		    off = bio->bio_offset + bp->b_dirtyoff;
		    retv = nfs_commitrpc_uio(vp, off,
					     bp->b_dirtyend - bp->b_dirtyoff,
					     td);
		    if (retv == 0) {
			    bp->b_dirtyoff = bp->b_dirtyend = 0;
			    bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
			    bp->b_resid = 0;
			    biodone(bio);
			    return(0);
		    }
		    if (retv == NFSERR_STALEWRITEVERF) {
			    nfs_clearcommit(vp->v_mount);
		    }
	    }

	    /*
	     * Setup for actual write
	     */
	    if (bio->bio_offset + bp->b_dirtyend > np->n_size)
		bp->b_dirtyend = np->n_size - bio->bio_offset;

	    if (bp->b_dirtyend > bp->b_dirtyoff) {
		io.iov_len = uiop->uio_resid = bp->b_dirtyend
		    - bp->b_dirtyoff;
		uiop->uio_offset = bio->bio_offset + bp->b_dirtyoff;
		io.iov_base = (char *)bp->b_data + bp->b_dirtyoff;
		uiop->uio_rw = UIO_WRITE;
		nfsstats.write_bios++;

		if ((bp->b_flags & (B_NEEDCOMMIT | B_NOCACHE | B_CLUSTER)) == 0)
		    iomode = NFSV3WRITE_UNSTABLE;
		else
		    iomode = NFSV3WRITE_FILESYNC;

		must_commit = 0;
		error = nfs_writerpc_uio(vp, uiop, &iomode, &must_commit);

		/*
		 * We no longer try to use kern/vfs_bio's cluster code to
		 * cluster commits, so B_CLUSTEROK is no longer set with
		 * B_NEEDCOMMIT.  The problem is that a vfs_busy_pages()
		 * may have to clear B_NEEDCOMMIT if it finds underlying
		 * pages have been redirtied through a memory mapping
		 * and doing this on a clustered bp will probably cause
		 * a panic, plus the flag in the underlying NFS bufs
		 * making up the cluster bp will not be properly cleared.
		 */
		if (!error && iomode == NFSV3WRITE_UNSTABLE) {
		    bp->b_flags |= B_NEEDCOMMIT;
#if 0
		    /* XXX do not enable commit clustering */
		    if (bp->b_dirtyoff == 0
			&& bp->b_dirtyend == bp->b_bcount)
			bp->b_flags |= B_CLUSTEROK;
#endif
		} else {
		    bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
		}

		/*
		 * For an interrupted write, the buffer is still valid
		 * and the write hasn't been pushed to the server yet,
		 * so we can't set B_ERROR and report the interruption
		 * by setting B_EINTR. For the async case, B_EINTR
		 * is not relevant, so the rpc attempt is essentially
		 * a noop.  For the case of a V3 write rpc not being
		 * committed to stable storage, the block is still
		 * dirty and requires either a commit rpc or another
		 * write rpc with iomode == NFSV3WRITE_FILESYNC before
		 * the block is reused. This is indicated by setting
		 * the B_DELWRI and B_NEEDCOMMIT flags.
		 *
		 * If the buffer is marked B_PAGING, it does not reside on
		 * the vp's paging queues so we cannot call bdirty().  The
		 * bp in this case is not an NFS cache block so we should
		 * be safe. XXX
		 */
    		if (error == EINTR
		    || (!error && (bp->b_flags & B_NEEDCOMMIT))) {
			crit_enter();
			bp->b_flags &= ~(B_INVAL|B_NOCACHE);
			if ((bp->b_flags & B_PAGING) == 0)
			    bdirty(bp);
			if (error)
			    bp->b_flags |= B_EINTR;
			crit_exit();
	    	} else {
		    if (error) {
			bp->b_flags |= B_ERROR;
			bp->b_error = np->n_error = error;
			np->n_flag |= NWRITEERR;
		    }
		    bp->b_dirtyoff = bp->b_dirtyend = 0;
		}
		if (must_commit)
		    nfs_clearcommit(vp->v_mount);
		bp->b_resid = uiop->uio_resid;
	    } else {
		bp->b_resid = 0;
	    }
	}

	/*
	 * I/O was run synchronously, biodone() it and calculate the
	 * error to return.
	 */
	biodone(bio);
	KKASSERT(bp->b_cmd == BUF_CMD_DONE);
	if (bp->b_flags & B_EINTR)
		return (EINTR);
	if (bp->b_flags & B_ERROR)
		return (bp->b_error ? bp->b_error : EIO);
	return (0);
}

/*
 * Handle all truncation, write-extend, and ftruncate()-extend operations
 * on the NFS lcient side.
 *
 * We use the new API in kern/vfs_vm.c to perform these operations in a
 * VM-friendly way.  With this API VM pages are properly zerod and pages
 * still mapped into the buffer straddling EOF are not invalidated.
 */
int
nfs_meta_setsize(struct vnode *vp, struct thread *td, off_t nsize, int trivial)
{
	struct nfsnode *np = VTONFS(vp);
	off_t osize;
	int biosize = vp->v_mount->mnt_stat.f_iosize;
	int error;

	osize = np->n_size;
	np->n_size = nsize;

	if (nsize < osize) {
		error = nvtruncbuf(vp, nsize, biosize, -1, 0);
	} else {
		error = nvextendbuf(vp, osize, nsize,
				    biosize, biosize, -1, -1,
				    trivial);
	}
	return(error);
}

/*
 * Synchronous completion for nfs_doio.  Call bpdone() with elseit=FALSE.
 * Caller is responsible for brelse()'ing the bp.
 */
static void
nfsiodone_sync(struct bio *bio)
{
	bio->bio_flags = 0;
	bpdone(bio->bio_buf, 0);
}

/*
 * nfs read rpc - BIO version
 */
void
nfs_readrpc_bio(struct vnode *vp, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	u_int32_t *tl;
	struct nfsmount *nmp;
	int error = 0, len, tsiz;
	struct nfsm_info *info;

	info = kmalloc(sizeof(*info), M_NFSREQ, M_WAITOK);
	info->mrep = NULL;
	info->v3 = NFS_ISV3(vp);

	nmp = VFSTONFS(vp->v_mount);
	tsiz = bp->b_bcount;
	KKASSERT(tsiz <= nmp->nm_rsize);
	if (bio->bio_offset + tsiz > nmp->nm_maxfilesize) {
		error = EFBIG;
		goto nfsmout;
	}
	nfsstats.rpccnt[NFSPROC_READ]++;
	len = tsiz;
	nfsm_reqhead(info, vp, NFSPROC_READ,
		     NFSX_FH(info->v3) + NFSX_UNSIGNED * 3);
	ERROROUT(nfsm_fhtom(info, vp));
	tl = nfsm_build(info, NFSX_UNSIGNED * 3);
	if (info->v3) {
		txdr_hyper(bio->bio_offset, tl);
		*(tl + 2) = txdr_unsigned(len);
	} else {
		*tl++ = txdr_unsigned(bio->bio_offset);
		*tl++ = txdr_unsigned(len);
		*tl = 0;
	}
	info->bio = bio;
	info->done = nfs_readrpc_bio_done;
	nfsm_request_bio(info, vp, NFSPROC_READ, NULL,
			 nfs_vpcred(vp, ND_READ));
	return;
nfsmout:
	kfree(info, M_NFSREQ);
	bp->b_error = error;
	bp->b_flags |= B_ERROR;
	biodone(bio);
}

static void
nfs_readrpc_bio_done(nfsm_info_t info)
{
	struct nfsmount *nmp = VFSTONFS(info->vp->v_mount);
	struct bio *bio = info->bio;
	struct buf *bp = bio->bio_buf;
	u_int32_t *tl;
	int attrflag;
	int retlen;
	int eof;
	int error = 0;

	KKASSERT(info->state == NFSM_STATE_DONE);

	lwkt_gettoken(&nmp->nm_token);

	ERROROUT(info->error);
	if (info->v3) {
		ERROROUT(nfsm_postop_attr(info, info->vp, &attrflag,
					 NFS_LATTR_NOSHRINK));
		NULLOUT(tl = nfsm_dissect(info, 2 * NFSX_UNSIGNED));
		eof = fxdr_unsigned(int, *(tl + 1));
	} else {
		ERROROUT(nfsm_loadattr(info, info->vp, NULL));
		eof = 0;
	}
	NEGATIVEOUT(retlen = nfsm_strsiz(info, nmp->nm_rsize));
	ERROROUT(nfsm_mtobio(info, bio, retlen));
	m_freem(info->mrep);
	info->mrep = NULL;

	/*
	 * No error occured, if retlen is less then bcount and no EOF
	 * and NFSv3 a zero-fill short read occured.
	 *
	 * For NFSv2 a short-read indicates EOF.
	 */
	if (retlen < bp->b_bcount && info->v3 && eof == 0) {
		bzero(bp->b_data + retlen, bp->b_bcount - retlen);
		retlen = bp->b_bcount;
	}

	/*
	 * If we hit an EOF we still zero-fill, but return the expected
	 * b_resid anyway.  This should normally not occur since async
	 * BIOs are not used for read-before-write case.  Races against
	 * the server can cause it though and we don't want to leave
	 * garbage in the buffer.
	 */
	if (retlen < bp->b_bcount) {
		bzero(bp->b_data + retlen, bp->b_bcount - retlen);
	}
	bp->b_resid = 0;
	/* bp->b_resid = bp->b_bcount - retlen; */
nfsmout:
	lwkt_reltoken(&nmp->nm_token);
	kfree(info, M_NFSREQ);
	if (error) {
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
	}
	biodone(bio);
}

/*
 * nfs write call - BIO version
 *
 * NOTE: Caller has already busied the I/O.
 */
void
nfs_writerpc_bio(struct vnode *vp, struct bio *bio)
{
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct nfsnode *np = VTONFS(vp);
	struct buf *bp = bio->bio_buf;
	u_int32_t *tl;
	int len;
	int iomode;
	int error = 0;
	struct nfsm_info *info;
	off_t offset;

	/*
	 * Setup for actual write.  Just clean up the bio if there
	 * is nothing to do.  b_dirtyoff/end have already been staged
	 * by the bp's pages getting busied.
	 */
	if (bio->bio_offset + bp->b_dirtyend > np->n_size)
		bp->b_dirtyend = np->n_size - bio->bio_offset;

	if (bp->b_dirtyend <= bp->b_dirtyoff) {
		bp->b_resid = 0;
		biodone(bio);
		return;
	}
	len = bp->b_dirtyend - bp->b_dirtyoff;
	offset = bio->bio_offset + bp->b_dirtyoff;
	if (offset + len > nmp->nm_maxfilesize) {
		bp->b_flags |= B_ERROR;
		bp->b_error = EFBIG;
		biodone(bio);
		return;
	}
	bp->b_resid = len;
	nfsstats.write_bios++;

	info = kmalloc(sizeof(*info), M_NFSREQ, M_WAITOK);
	info->mrep = NULL;
	info->v3 = NFS_ISV3(vp);
	info->info_writerpc.must_commit = 0;
	if ((bp->b_flags & (B_NEEDCOMMIT | B_NOCACHE | B_CLUSTER)) == 0)
		iomode = NFSV3WRITE_UNSTABLE;
	else
		iomode = NFSV3WRITE_FILESYNC;

	KKASSERT(len <= nmp->nm_wsize);

	nfsstats.rpccnt[NFSPROC_WRITE]++;
	nfsm_reqhead(info, vp, NFSPROC_WRITE,
		     NFSX_FH(info->v3) + 5 * NFSX_UNSIGNED + nfsm_rndup(len));
	ERROROUT(nfsm_fhtom(info, vp));
	if (info->v3) {
		tl = nfsm_build(info, 5 * NFSX_UNSIGNED);
		txdr_hyper(offset, tl);
		tl += 2;
		*tl++ = txdr_unsigned(len);
		*tl++ = txdr_unsigned(iomode);
		*tl = txdr_unsigned(len);
	} else {
		u_int32_t x;

		tl = nfsm_build(info, 4 * NFSX_UNSIGNED);
		/* Set both "begin" and "current" to non-garbage. */
		x = txdr_unsigned((u_int32_t)offset);
		*tl++ = x;	/* "begin offset" */
		*tl++ = x;	/* "current offset" */
		x = txdr_unsigned(len);
		*tl++ = x;	/* total to this offset */
		*tl = x;	/* size of this write */
	}
	ERROROUT(nfsm_biotom(info, bio, bp->b_dirtyoff, len));
	info->bio = bio;
	info->done = nfs_writerpc_bio_done;
	nfsm_request_bio(info, vp, NFSPROC_WRITE, NULL,
			 nfs_vpcred(vp, ND_WRITE));
	return;
nfsmout:
	kfree(info, M_NFSREQ);
	bp->b_error = error;
	bp->b_flags |= B_ERROR;
	biodone(bio);
}

static void
nfs_writerpc_bio_done(nfsm_info_t info)
{
	struct nfsmount *nmp = VFSTONFS(info->vp->v_mount);
	struct nfsnode *np = VTONFS(info->vp);
	struct bio *bio = info->bio;
	struct buf *bp = bio->bio_buf;
	int wccflag = NFSV3_WCCRATTR;
	int iomode = NFSV3WRITE_FILESYNC;
	int commit;
	int rlen;
	int error;
	int len = bp->b_resid;	/* b_resid was set to shortened length */
	u_int32_t *tl;

	lwkt_gettoken(&nmp->nm_token);

	ERROROUT(info->error);
	if (info->v3) {
		/*
		 * The write RPC returns a before and after mtime.  The
		 * nfsm_wcc_data() macro checks the before n_mtime
		 * against the before time and stores the after time
		 * in the nfsnode's cached vattr and n_mtime field.
		 * The NRMODIFIED bit will be set if the before
		 * time did not match the original mtime.
		 */
		wccflag = NFSV3_WCCCHK;
		ERROROUT(nfsm_wcc_data(info, info->vp, &wccflag));
		if (error == 0) {
			NULLOUT(tl = nfsm_dissect(info, 2 * NFSX_UNSIGNED + NFSX_V3WRITEVERF));
			rlen = fxdr_unsigned(int, *tl++);
			if (rlen == 0) {
				error = NFSERR_IO;
				m_freem(info->mrep);
				info->mrep = NULL;
				goto nfsmout;
			} else if (rlen < len) {
#if 0
				/*
				 * XXX what do we do here?
				 */
				backup = len - rlen;
				uiop->uio_iov->iov_base = (char *)uiop->uio_iov->iov_base - backup;
				uiop->uio_iov->iov_len += backup;
				uiop->uio_offset -= backup;
				uiop->uio_resid += backup;
				len = rlen;
#endif
			}
			commit = fxdr_unsigned(int, *tl++);

			/*
			 * Return the lowest committment level
			 * obtained by any of the RPCs.
			 */
			if (iomode == NFSV3WRITE_FILESYNC)
				iomode = commit;
			else if (iomode == NFSV3WRITE_DATASYNC &&
				commit == NFSV3WRITE_UNSTABLE)
				iomode = commit;
			if ((nmp->nm_state & NFSSTA_HASWRITEVERF) == 0){
			    bcopy(tl, (caddr_t)nmp->nm_verf, NFSX_V3WRITEVERF);
			    nmp->nm_state |= NFSSTA_HASWRITEVERF;
			} else if (bcmp(tl, nmp->nm_verf, NFSX_V3WRITEVERF)) {
			    info->info_writerpc.must_commit = 1;
			    bcopy(tl, (caddr_t)nmp->nm_verf, NFSX_V3WRITEVERF);
			}
		}
	} else {
		ERROROUT(nfsm_loadattr(info, info->vp, NULL));
	}
	m_freem(info->mrep);
	info->mrep = NULL;
	len = 0;
nfsmout:
	if (info->vp->v_mount->mnt_flag & MNT_ASYNC)
		iomode = NFSV3WRITE_FILESYNC;
	bp->b_resid = len;

	/*
	 * End of RPC.  Now clean up the bp.
	 *
	 * We no longer enable write clustering for commit operations,
	 * See around line 1157 for a more detailed comment.
	 */
	if (!error && iomode == NFSV3WRITE_UNSTABLE) {
		bp->b_flags |= B_NEEDCOMMIT;
#if 0
		/* XXX do not enable commit clustering */
		if (bp->b_dirtyoff == 0 && bp->b_dirtyend == bp->b_bcount)
			bp->b_flags |= B_CLUSTEROK;
#endif
	} else {
		bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
	}

	/*
	 * For an interrupted write, the buffer is still valid
	 * and the write hasn't been pushed to the server yet,
	 * so we can't set B_ERROR and report the interruption
	 * by setting B_EINTR. For the async case, B_EINTR
	 * is not relevant, so the rpc attempt is essentially
	 * a noop.  For the case of a V3 write rpc not being
	 * committed to stable storage, the block is still
	 * dirty and requires either a commit rpc or another
	 * write rpc with iomode == NFSV3WRITE_FILESYNC before
	 * the block is reused. This is indicated by setting
	 * the B_DELWRI and B_NEEDCOMMIT flags.
	 *
	 * If the buffer is marked B_PAGING, it does not reside on
	 * the vp's paging queues so we cannot call bdirty().  The
	 * bp in this case is not an NFS cache block so we should
	 * be safe. XXX
	 */
	if (error == EINTR || (!error && (bp->b_flags & B_NEEDCOMMIT))) {
		crit_enter();
		bp->b_flags &= ~(B_INVAL|B_NOCACHE);
		if ((bp->b_flags & B_PAGING) == 0)
			bdirty(bp);
		if (error)
			bp->b_flags |= B_EINTR;
		crit_exit();
	} else {
		if (error) {
			bp->b_flags |= B_ERROR;
			bp->b_error = np->n_error = error;
			np->n_flag |= NWRITEERR;
		}
		bp->b_dirtyoff = bp->b_dirtyend = 0;
	}
	if (info->info_writerpc.must_commit)
		nfs_clearcommit(info->vp->v_mount);
	lwkt_reltoken(&nmp->nm_token);

	kfree(info, M_NFSREQ);
	if (error) {
		bp->b_flags |= B_ERROR;
		bp->b_error = error;
	}
	biodone(bio);
}

/*
 * Nfs Version 3 commit rpc - BIO version
 *
 * This function issues the commit rpc and will chain to a write
 * rpc if necessary.
 */
void
nfs_commitrpc_bio(struct vnode *vp, struct bio *bio)
{
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct buf *bp = bio->bio_buf;
	struct nfsm_info *info;
	int error = 0;
	u_int32_t *tl;

	if ((nmp->nm_state & NFSSTA_HASWRITEVERF) == 0) {
		bp->b_dirtyoff = bp->b_dirtyend = 0;
		bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
		bp->b_resid = 0;
		biodone(bio);
		return;
	}

	info = kmalloc(sizeof(*info), M_NFSREQ, M_WAITOK);
	info->mrep = NULL;
	info->v3 = 1;

	nfsstats.rpccnt[NFSPROC_COMMIT]++;
	nfsm_reqhead(info, vp, NFSPROC_COMMIT, NFSX_FH(1));
	ERROROUT(nfsm_fhtom(info, vp));
	tl = nfsm_build(info, 3 * NFSX_UNSIGNED);
	txdr_hyper(bio->bio_offset + bp->b_dirtyoff, tl);
	tl += 2;
	*tl = txdr_unsigned(bp->b_dirtyend - bp->b_dirtyoff);
	info->bio = bio;
	info->done = nfs_commitrpc_bio_done;
	nfsm_request_bio(info, vp, NFSPROC_COMMIT, NULL,
			 nfs_vpcred(vp, ND_WRITE));
	return;
nfsmout:
	/*
	 * Chain to write RPC on (early) error
	 */
	kfree(info, M_NFSREQ);
	nfs_writerpc_bio(vp, bio);
}

static void
nfs_commitrpc_bio_done(nfsm_info_t info)
{
	struct nfsmount *nmp = VFSTONFS(info->vp->v_mount);
	struct bio *bio = info->bio;
	struct buf *bp = bio->bio_buf;
	u_int32_t *tl;
	int wccflag = NFSV3_WCCRATTR;
	int error = 0;

	lwkt_gettoken(&nmp->nm_token);

	ERROROUT(info->error);
	ERROROUT(nfsm_wcc_data(info, info->vp, &wccflag));
	if (error == 0) {
		NULLOUT(tl = nfsm_dissect(info, NFSX_V3WRITEVERF));
		if (bcmp(nmp->nm_verf, tl, NFSX_V3WRITEVERF)) {
			bcopy(tl, nmp->nm_verf, NFSX_V3WRITEVERF);
			error = NFSERR_STALEWRITEVERF;
		}
	}
	m_freem(info->mrep);
	info->mrep = NULL;

	/*
	 * On completion we must chain to a write bio if an
	 * error occurred.
	 */
nfsmout:
	if (error == 0) {
		bp->b_dirtyoff = bp->b_dirtyend = 0;
		bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
		bp->b_resid = 0;
		biodone(bio);
	} else {
		nfs_writerpc_bio(info->vp, bio);
	}
	kfree(info, M_NFSREQ);
	lwkt_reltoken(&nmp->nm_token);
}
