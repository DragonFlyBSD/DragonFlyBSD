/*
 * Copyright (c) 1999, Boris Popov
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
 * $FreeBSD: src/sys/nwfs/nwfs_io.c,v 1.6.2.1 2000/10/25 02:11:10 bp Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>	/* defines plimit structure in proc struct */
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_page2.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

#include <netproto/ncp/ncp.h>
#include <netproto/ncp/ncp_conn.h>
#include <netproto/ncp/ncp_subr.h>

#include <sys/thread2.h>

#include <machine/limits.h>

#include "nwfs.h"
#include "nwfs_node.h"
#include "nwfs_subr.h"

static int nwfs_fastlookup = 1;

SYSCTL_DECL(_vfs_nwfs);
SYSCTL_INT(_vfs_nwfs, OID_AUTO, fastlookup, CTLFLAG_RW, &nwfs_fastlookup, 0, "");


extern int nwfs_pbuf_freecnt;

#define	NWFS_RWCACHE

static int
nwfs_readvdir(struct vnode *vp, struct uio *uio, struct ucred *cred)
{
	struct nwmount *nmp = VTONWFS(vp);
	int error, i;
	struct nwnode *np;
	struct nw_entry_info fattr;
	struct vnode *newvp;
	ncpfid fid;
	ino_t d_ino;
	size_t d_namlen;
	const char *d_name;
	uint8_t d_type;

	np = VTONW(vp);
	NCPVNDEBUG("dirname='%s'\n",np->n_name);
	if (uio->uio_offset < 0 || uio->uio_offset > INT_MAX)
		return (EINVAL);
	error = 0;
	i = (int)uio->uio_offset; /* offset in directory */
	if (i == 0) {
		error = ncp_initsearch(vp, uio->uio_td, cred);
		if (error) {
			NCPVNDEBUG("cannot initialize search, error=%d",error);
			return( error );
		}
	}

	for (; !error && uio->uio_resid > 0; i++) {
		switch (i) {
		    case 0:		/* `.' */
			d_ino = np->n_fid.f_id;
			if (d_ino == 0)
				d_ino = NWFS_ROOT_INO;
			d_namlen = 1;
			d_name = ".";
			d_type = DT_DIR;
			break;
		    case 1:		/* `..' */
			d_ino = np->n_parent.f_id;
			if (d_ino == 0)
				d_ino = NWFS_ROOT_INO;
			d_namlen = 2;
			d_name = "..";
			d_type = DT_DIR;
			break;
		    default:
			error = ncp_search_for_file_or_subdir(nmp, &np->n_seq, &fattr, uio->uio_td, cred);
			if (error && error < 0x80)
				goto done;
			d_ino = fattr.dirEntNum;
			d_type = (fattr.attributes & aDIR) ? DT_DIR : DT_REG;
			d_namlen = fattr.nameLen;
			d_name = fattr.entryName;
#if 0
			if (error && eofflag) {
			/*	*eofflag = 1;*/
			        break;
			}
#endif
			break;
		}
		if (nwfs_fastlookup && !error && i > 1) {
			fid.f_id = fattr.dirEntNum;
			fid.f_parent = np->n_fid.f_id;
			error = nwfs_nget(vp->v_mount, fid, &fattr, vp, &newvp);
			if (!error) {
				VTONW(newvp)->n_ctime = VTONW(newvp)->n_vattr.va_ctime.tv_sec;
				vput(newvp);
			} else
				error = 0;
		}
		if (error >= 0x80) {
		    error = 0;
		    break;
		}
		if (vop_write_dirent(&error, uio, d_ino, d_type, d_namlen, d_name))
			break;
	}
done:
	uio->uio_offset = i;
	return (error);
}

int
nwfs_readvnode(struct vnode *vp, struct uio *uiop, struct ucred *cred)
{
	struct nwmount *nmp = VFSTONWFS(vp->v_mount);
	struct nwnode *np = VTONW(vp);
	struct vattr vattr;
	int error;

	if (vp->v_type != VREG && vp->v_type != VDIR) {
		kprintf("%s: vn types other than VREG or VDIR are unsupported !\n",__func__);
		return EIO;
	}
	if (uiop->uio_resid == 0) return 0;
	if (uiop->uio_offset < 0) return EINVAL;
	if (vp->v_type == VDIR) {
		error = nwfs_readvdir(vp, uiop, cred);
		return error;
	}
	if (np->n_flag & NMODIFIED) {
		nwfs_attr_cacheremove(vp);
		error = VOP_GETATTR(vp, &vattr);
		if (error) return (error);
		np->n_mtime = vattr.va_mtime.tv_sec;
	} else {
		error = VOP_GETATTR(vp, &vattr);
		if (error) return (error);
		if (np->n_mtime != vattr.va_mtime.tv_sec) {
			error = nwfs_vinvalbuf(vp, V_SAVE, 1);
			if (error) return (error);
			np->n_mtime = vattr.va_mtime.tv_sec;
		}
	}
	error = ncp_read(NWFSTOCONN(nmp), &np->n_fh, uiop,cred);
	return (error);
}

int
nwfs_writevnode(struct vnode *vp, struct uio *uiop, struct ucred *cred,
		int ioflag)
{
	struct nwmount *nmp = VTONWFS(vp);
	struct nwnode *np = VTONW(vp);
	struct thread *td;
/*	struct vattr vattr;*/
	int error = 0;

	if (vp->v_type != VREG) {
		kprintf("%s: vn types other than VREG unsupported !\n",__func__);
		return EIO;
	}
	NCPVNDEBUG("ofs=%d,resid=%d\n",(int)uiop->uio_offset, uiop->uio_resid);
	if (uiop->uio_offset < 0) return EINVAL;
	td = uiop->uio_td;
	if (ioflag & (IO_APPEND | IO_SYNC)) {
		if (np->n_flag & NMODIFIED) {
			nwfs_attr_cacheremove(vp);
			error = nwfs_vinvalbuf(vp, V_SAVE, 1);
			if (error) return (error);
		}
		if (ioflag & IO_APPEND) {
		/* We can relay only on local information about file size,
		 * because until file is closed NetWare will not return
		 * the correct size. */
#if 0 /* notyet */
			nwfs_attr_cacheremove(vp);
			error = VOP_GETATTR(vp, &vattr);
			if (error) return (error);
#endif
			uiop->uio_offset = np->n_size;
		}
	}
	if (uiop->uio_resid == 0) return 0;
	if (td->td_proc && uiop->uio_offset + uiop->uio_resid > 
	    td->td_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		lwpsignal(td->td_proc, td->td_lwp, SIGXFSZ);
		return (EFBIG);
	}
	error = ncp_write(NWFSTOCONN(nmp), &np->n_fh, uiop, cred);
	NCPVNDEBUG("after: ofs=%d,resid=%d\n",(int)uiop->uio_offset, uiop->uio_resid);
	if (!error) {
		if (uiop->uio_offset > np->n_size) {
			np->n_vattr.va_size = np->n_size = uiop->uio_offset;
			vnode_pager_setsize(vp, np->n_size);
		}
	}
	return (error);
}

/*
 * Do an I/O operation to/from a cache block.
 */
int
nwfs_doio(struct vnode *vp, struct bio *bio, struct ucred *cr, struct thread *td)
{
	struct buf *bp = bio->bio_buf;
	struct uio *uiop;
	struct nwnode *np;
	struct nwmount *nmp;
	int error = 0;
	struct uio uio;
	struct iovec io;

	np = VTONW(vp);
	nmp = VFSTONWFS(vp->v_mount);
	uiop = &uio;
	uiop->uio_iov = &io;
	uiop->uio_iovcnt = 1;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_td = td;

	if (bp->b_cmd == BUF_CMD_READ) {
	    io.iov_len = uiop->uio_resid = (size_t)bp->b_bcount;
	    io.iov_base = bp->b_data;
	    uiop->uio_rw = UIO_READ;
	    switch (vp->v_type) {
	      case VREG:
		uiop->uio_offset = bio->bio_offset;
		error = ncp_read(NWFSTOCONN(nmp), &np->n_fh, uiop, cr);
		if (error)
			break;
		if (uiop->uio_resid) {
			size_t left = uiop->uio_resid;
			size_t nread = bp->b_bcount - left;
			if (left > 0)
				bzero((char *)bp->b_data + nread, left);
		}
		break;
/*	    case VDIR:
		nfsstats.readdir_bios++;
		uiop->uio_offset = bio->bio_offset;
		if (nmp->nm_flag & NFSMNT_RDIRPLUS) {
			error = nfs_readdirplusrpc(vp, uiop, cr);
			if (error == NFSERR_NOTSUPP)
				nmp->nm_flag &= ~NFSMNT_RDIRPLUS;
		}
		if ((nmp->nm_flag & NFSMNT_RDIRPLUS) == 0)
			error = nfs_readdirrpc(vp, uiop, cr);
		if (error == 0 && uiop->uio_resid == (size_t)bp->b_bcount)
			bp->b_flags |= B_INVAL;
		break;
*/
	    default:
		kprintf("nwfs_doio:  type %x unexpected\n",vp->v_type);
		break;
	    };
	    if (error) {
		bp->b_flags |= B_ERROR;
		bp->b_error = error;
	    }
	} else { /* write */
	    KKASSERT(bp->b_cmd == BUF_CMD_WRITE);
	    if (bio->bio_offset + bp->b_dirtyend > np->n_size)
		bp->b_dirtyend = np->n_size - bio->bio_offset;

	    if (bp->b_dirtyend > bp->b_dirtyoff) {
		io.iov_len = uiop->uio_resid =
			(size_t)(bp->b_dirtyend - bp->b_dirtyoff);
		uiop->uio_offset = bio->bio_offset + bp->b_dirtyoff;
		io.iov_base = (char *)bp->b_data + bp->b_dirtyoff;
		uiop->uio_rw = UIO_WRITE;
		error = ncp_write(NWFSTOCONN(nmp), &np->n_fh, uiop, cr);

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
		 */
    		if (error == EINTR
		    || (!error && (bp->b_flags & B_NEEDCOMMIT))) {

			crit_enter();
			bp->b_flags &= ~(B_INVAL|B_NOCACHE);
			if ((bp->b_flags & B_PAGING) == 0)
			    bdirty(bp);
			bp->b_flags |= B_EINTR;
			crit_exit();
	    	} else {
			if (error) {
				bp->b_flags |= B_ERROR;
				bp->b_error /*= np->n_error */= error;
/*				np->n_flag |= NWRITEERR;*/
			}
			bp->b_dirtyoff = bp->b_dirtyend = 0;
		}
	    } else {
		bp->b_resid = 0;
		biodone(bio);
		return (0);
	    }
	}
	bp->b_resid = (int)uiop->uio_resid;
	biodone(bio);
	return (error);
}

/*
 * Vnode op for VM getpages.
 * Wish wish .... get rid from multiple IO routines
 *
 * nwfs_getpages(struct vnode *a_vp, vm_page_t *a_m, int a_count,
 *		 int a_reqpage, vm_ooffset_t a_offset)
 */
int
nwfs_getpages(struct vop_getpages_args *ap)
{
#ifndef NWFS_RWCACHE
	return vnode_pager_generic_getpages(ap->a_vp, ap->a_m, ap->a_count,
					    ap->a_reqpage, ap->a_seqaccess);
#else
	int i, error, npages;
	size_t nextoff, toff;
	size_t count;
	size_t size;
	struct uio uio;
	struct iovec iov;
	vm_offset_t kva;
	struct buf *bp;
	struct vnode *vp;
	struct thread *td = curthread;	/* XXX */
	struct ucred *cred;
	struct nwmount *nmp;
	struct nwnode *np;
	vm_page_t *pages;

	KKASSERT(td->td_proc);
	cred = td->td_proc->p_ucred;

	vp = ap->a_vp;
	np = VTONW(vp);
	nmp = VFSTONWFS(vp->v_mount);
	pages = ap->a_m;
	count = (size_t)ap->a_count;

	if (vp->v_object == NULL) {
		kprintf("nwfs_getpages: called with non-merged cache vnode??\n");
		return VM_PAGER_ERROR;
	}

	bp = getpbuf_kva(&nwfs_pbuf_freecnt);
	npages = btoc(count);
	kva = (vm_offset_t) bp->b_data;
	pmap_qenter(kva, pages, npages);

	iov.iov_base = (caddr_t) kva;
	iov.iov_len = count;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = IDX_TO_OFF(pages[0]->pindex);
	uio.uio_resid = count;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = td;

	error = ncp_read(NWFSTOCONN(nmp), &np->n_fh, &uio,cred);
	pmap_qremove(kva, npages);

	relpbuf(bp, &nwfs_pbuf_freecnt);

	if (error && (uio.uio_resid == count)) {
		kprintf("nwfs_getpages: error %d\n",error);
		for (i = 0; i < npages; i++) {
			if (ap->a_reqpage != i)
				vnode_pager_freepage(pages[i]);
		}
		return VM_PAGER_ERROR;
	}

	size = count - uio.uio_resid;

	for (i = 0, toff = 0; i < npages; i++, toff = nextoff) {
		vm_page_t m;
		nextoff = toff + PAGE_SIZE;
		m = pages[i];

		m->flags &= ~PG_ZERO;

		/*
		 * NOTE: pmap dirty bit should have already been cleared.
		 *	 We do not clear it here.
		 */
		if (nextoff <= size) {
			m->valid = VM_PAGE_BITS_ALL;
			m->dirty = 0;
		} else {
			int nvalid = ((size + DEV_BSIZE - 1) - toff) &
				      ~(DEV_BSIZE - 1);
			vm_page_set_validclean(m, 0, nvalid);
		}
		
		if (i != ap->a_reqpage) {
			/*
			 * Whether or not to leave the page activated is up in
			 * the air, but we should put the page on a page queue
			 * somewhere (it already is in the object).  Result:
			 * It appears that emperical results show that
			 * deactivating pages is best.
			 */

			/*
			 * Just in case someone was asking for this page we
			 * now tell them that it is ok to use.
			 */
			if (!error) {
				if (m->flags & PG_REFERENCED)
					vm_page_activate(m);
				else
					vm_page_deactivate(m);
				vm_page_wakeup(m);
			} else {
				vnode_pager_freepage(m);
			}
		}
	}
	return 0;
#endif /* NWFS_RWCACHE */
}

/*
 * Vnode op for VM putpages.
 * possible bug: all IO done in sync mode
 * Note that vop_close always invalidate pages before close, so it's
 * not necessary to open vnode.
 *
 * nwfs_putpages(struct vnode *a_vp, vm_page_t *a_m, int a_count,
 *		 int a_sync, int *a_rtvals, vm_ooffset_t a_offset)
 */
int
nwfs_putpages(struct vop_putpages_args *ap)
{
	int error;
	struct thread *td = curthread;	/* XXX */
	struct vnode *vp = ap->a_vp;
	struct ucred *cred;

#ifndef NWFS_RWCACHE
	KKASSERT(td->td_proc);
	cred = td->td_proc->p_ucred;		/* XXX */
	VOP_OPEN(vp, FWRITE, cred, NULL);
	error = vnode_pager_generic_putpages(ap->a_vp, ap->a_m, ap->a_count,
		ap->a_sync, ap->a_rtvals);
	VOP_CLOSE(vp, FWRITE, cred);
	return error;
#else
	struct uio uio;
	struct iovec iov;
	vm_offset_t kva;
	struct buf *bp;
	int i, npages, count;
	int *rtvals;
	struct nwmount *nmp;
	struct nwnode *np;
	vm_page_t *pages;

	KKASSERT(td->td_proc);
	cred = td->td_proc->p_ucred;		/* XXX */

/*	VOP_OPEN(vp, FWRITE, cred, NULL);*/
	np = VTONW(vp);
	nmp = VFSTONWFS(vp->v_mount);
	pages = ap->a_m;
	count = ap->a_count;
	rtvals = ap->a_rtvals;
	npages = btoc(count);

	for (i = 0; i < npages; i++) {
		rtvals[i] = VM_PAGER_AGAIN;
	}

	bp = getpbuf_kva(&nwfs_pbuf_freecnt);
	kva = (vm_offset_t) bp->b_data;
	pmap_qenter(kva, pages, npages);

	iov.iov_base = (caddr_t) kva;
	iov.iov_len = count;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = IDX_TO_OFF(pages[0]->pindex);
	uio.uio_resid = count;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_td = td;
	NCPVNDEBUG("ofs=%d,resid=%d\n",(int)uio.uio_offset, uio.uio_resid);

	error = ncp_write(NWFSTOCONN(nmp), &np->n_fh, &uio, cred);
/*	VOP_CLOSE(vp, FWRITE, cred);*/
	NCPVNDEBUG("paged write done: %d\n", error);

	pmap_qremove(kva, npages);
	relpbuf(bp, &nwfs_pbuf_freecnt);

	if (!error) {
		int nwritten = round_page(count - uio.uio_resid) / PAGE_SIZE;
		for (i = 0; i < nwritten; i++) {
			rtvals[i] = VM_PAGER_OK;
			vm_page_undirty(pages[i]);
		}
	}
	return rtvals[0];
#endif /* NWFS_RWCACHE */
}
/*
 * Flush and invalidate all dirty buffers. If another process is already
 * doing the flush, just wait for completion.
 */
int
nwfs_vinvalbuf(struct vnode *vp, int flags, int intrflg)
{
	struct nwnode *np = VTONW(vp);
/*	struct nwmount *nmp = VTONWFS(vp);*/
	int error = 0, slpflag, slptimeo;

	if (vp->v_flag & VRECLAIMED) {
		return (0);
	}
	if (intrflg) {
		slpflag = PCATCH;
		slptimeo = 2 * hz;
	} else {
		slpflag = 0;
		slptimeo = 0;
	}
	while (np->n_flag & NFLUSHINPROG) {
		np->n_flag |= NFLUSHWANT;
		error = tsleep((caddr_t)&np->n_flag, 0, "nwfsvinv", slptimeo);
		error = ncp_chkintr(NWFSTOCONN(VTONWFS(vp)), curthread);
		if (error == EINTR && intrflg)
			return EINTR;
	}
	np->n_flag |= NFLUSHINPROG;
	error = vinvalbuf(vp, flags, slpflag, 0);
	while (error) {
		if (intrflg && (error == ERESTART || error == EINTR)) {
			np->n_flag &= ~NFLUSHINPROG;
			if (np->n_flag & NFLUSHWANT) {
				np->n_flag &= ~NFLUSHWANT;
				wakeup((caddr_t)&np->n_flag);
			}
			return EINTR;
		}
		error = vinvalbuf(vp, flags, slpflag, 0);
	}
	np->n_flag &= ~(NMODIFIED | NFLUSHINPROG);
	if (np->n_flag & NFLUSHWANT) {
		np->n_flag &= ~NFLUSHWANT;
		wakeup((caddr_t)&np->n_flag);
	}
	return (error);
}
