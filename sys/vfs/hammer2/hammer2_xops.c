/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 * by Daniel Flores (GSOC 2013 - mentored by Matthew Dillon, compression) 
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
 * Per-node backend for kernel filesystem interface.
 *
 * This executes a VOP concurrently on multiple nodes, each node via its own
 * thread, and competes to advance the original request.  The original
 * request is retired the moment all requirements are met, even if the
 * operation is still in-progress on some nodes.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/objcache.h>
#include <sys/event.h>
#include <sys/file.h>
#include <vfs/fifofs/fifo.h>

#include "hammer2.h"

void
hammer2_xop_readdir(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_readdir_t *xop = &arg->xop_readdir;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_key_t lkey;
	int cache_index = -1;
	int error = 0;

	lkey = xop->head.lkey;
	if (hammer2_debug & 0x0020)
		kprintf("xop_readdir %p lkey=%016jx\n", xop, lkey);

	/*
	 * The inode's chain is the iterator.  If we cannot acquire it our
	 * contribution ends here.
	 */
	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent == NULL) {
		kprintf("xop_readdir: NULL parent\n");
		goto done;
	}

	/*
	 * Directory scan [re]start and loop.
	 *
	 * We feed the share-locked chain back to the frontend and must be
	 * sure not to unlock it in our iteration.
	 */
	chain = hammer2_chain_lookup(&parent, &key_next, lkey, lkey,
			     &cache_index, HAMMER2_LOOKUP_SHARED);
	if (chain == NULL) {
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lkey, (hammer2_key_t)-1,
					     &cache_index,
					     HAMMER2_LOOKUP_SHARED);
	}
	while (chain && hammer2_xop_active(&xop->head)) {
		error = hammer2_xop_feed(&xop->head, chain, clindex, 0);
		if (error)
			break;
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, (hammer2_key_t)-1,
					   &cache_index,
					   HAMMER2_LOOKUP_SHARED |
					   HAMMER2_LOOKUP_NOUNLOCK);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
}

#if 0

/*
 * hammer2_xop_readlink { vp, uio, cred }
 */
int
hammer2_xop_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp;
	hammer2_inode_t *ip;
	int error;

	vp = ap->a_vp;
	if (vp->v_type != VLNK)
		return (EINVAL);
	ip = VTOI(vp);

	/*error = hammer2_xop_read_file(ip, ap->a_uio, 0);*/
	return (error);
}

int
hammer2_xop_nresolve(struct vop_nresolve_args *ap)
{
	hammer2_inode_t *ip;
	hammer2_inode_t *dip;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	const hammer2_inode_data_t *ripdata;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error = 0;
	struct vnode *vp;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Note: In DragonFly the kernel handles '.' and '..'.
	 */
	hammer2_inode_lock(dip, HAMMER2_RESOLVE_ALWAYS |
				HAMMER2_RESOLVE_SHARED);
	cparent = hammer2_inode_cluster(dip, HAMMER2_RESOLVE_ALWAYS |
					     HAMMER2_RESOLVE_SHARED);

	cluster = hammer2_cluster_lookup(cparent, &key_next,
					 lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					 HAMMER2_LOOKUP_SHARED);
	while (cluster) {
		if (hammer2_cluster_type(cluster) == HAMMER2_BREF_TYPE_INODE) {
			ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
			if (ripdata->meta.name_len == name_len &&
			    bcmp(ripdata->filename, name, name_len) == 0) {
				break;
			}
		}
		cluster = hammer2_cluster_next(cparent, cluster, &key_next,
					       key_next,
					       lhc + HAMMER2_DIRHASH_LOMASK,
					       HAMMER2_LOOKUP_SHARED);
	}
	hammer2_inode_unlock(dip, cparent);

	/*
	 * Resolve hardlink entries before acquiring the inode.
	 */
	if (cluster) {
		ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
		if (ripdata->meta.type == HAMMER2_OBJTYPE_HARDLINK) {
			hammer2_tid_t inum = ripdata->meta.inum;
			error = hammer2_hardlink_find(dip, NULL, &cluster);
			if (error) {
				kprintf("hammer2: unable to find hardlink "
					"0x%016jx\n", inum);
				LOCKSTOP;

				return error;
			}
		}
	}

	/*
	 * nresolve needs to resolve hardlinks, the original cluster is not
	 * sufficient.
	 */
	if (cluster) {
		ip = hammer2_inode_get(dip->pmp, dip, cluster);
		ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
		if (ripdata->meta.type == HAMMER2_OBJTYPE_HARDLINK) {
			kprintf("nresolve: fixup hardlink\n");
			hammer2_inode_ref(ip);
			hammer2_inode_unlock(ip, NULL);
			hammer2_cluster_unlock(cluster);
			hammer2_cluster_drop(cluster);
			hammer2_inode_lock(ip, HAMMER2_RESOLVE_ALWAYS);
			cluster = hammer2_inode_cluster(ip,
						     HAMMER2_RESOLVE_ALWAYS);
			ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
			hammer2_inode_drop(ip);
			kprintf("nresolve: fixup to type %02x\n",
				ripdata->meta.type);
		}
	} else {
		ip = NULL;
	}

#if 0
	/*
	 * Deconsolidate any hardlink whos nlinks == 1.  Ignore errors.
	 * If an error occurs chain and ip are left alone.
	 *
	 * XXX upgrade shared lock?
	 */
	if (ochain && chain &&
	    chain->data->ipdata.meta.nlinks == 1 && !dip->pmp->ronly) {
		kprintf("hammer2: need to unconsolidate hardlink for %s\n",
			chain->data->ipdata.filename);
		/* XXX retain shared lock on dip? (currently not held) */
		hammer2_trans_init(&trans, dip->pmp, 0);
		hammer2_hardlink_deconsolidate(&trans, dip, &chain, &ochain);
		hammer2_trans_done(&trans);
	}
#endif

	/*
	 * Acquire the related vnode
	 *
	 * NOTE: For error processing, only ENOENT resolves the namecache
	 *	 entry to NULL, otherwise we just return the error and
	 *	 leave the namecache unresolved.
	 *
	 * NOTE: multiple hammer2_inode structures can be aliased to the
	 *	 same chain element, for example for hardlinks.  This
	 *	 use case does not 'reattach' inode associations that
	 *	 might already exist, but always allocates a new one.
	 *
	 * WARNING: inode structure is locked exclusively via inode_get
	 *	    but chain was locked shared.  inode_unlock()
	 *	    will handle it properly.
	 */
	if (cluster) {
		vp = hammer2_igetv(ip, cluster, &error);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
		} else if (error == ENOENT) {
			cache_setvp(ap->a_nch, NULL);
		}
		hammer2_inode_unlock(ip, cluster);

		/*
		 * The vp should not be released until after we've disposed
		 * of our locks, because it might cause vop_inactive() to
		 * be called.
		 */
		if (vp)
			vrele(vp);
	} else {
		error = ENOENT;
		cache_setvp(ap->a_nch, NULL);
	}
	KASSERT(error || ap->a_nch->ncp->nc_vp != NULL,
		("resolve error %d/%p ap %p\n",
		 error, ap->a_nch->ncp->nc_vp, ap));
	LOCKSTOP;
	return error;
}

int
hammer2_xop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cparent;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);

	if ((ip = dip->pip) == NULL) {
		*ap->a_vpp = NULL;
		LOCKSTOP;
		return ENOENT;
	}
	hammer2_inode_lock(ip, HAMMER2_RESOLVE_ALWAYS);
	cparent = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS);
	*ap->a_vpp = hammer2_igetv(ip, cparent, &error);
	hammer2_inode_unlock(ip, cparent);

	LOCKSTOP;
	return error;
}

int
hammer2_xop_nmkdir(struct vop_nmkdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_trans_t trans;
	hammer2_cluster_t *cluster;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	cluster = NULL;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len,
				   &cluster, 0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, cluster, &error);
		hammer2_inode_unlock(nip, cluster);
	}
	hammer2_trans_done(&trans);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	LOCKSTOP;
	return error;
}

/*
 * hammer2_xop_nlink { nch, dvp, vp, cred }
 *
 * Create a hardlink from (vp) to {dvp, nch}.
 */
int
hammer2_xop_nlink(struct vop_nlink_args *ap)
{
	hammer2_inode_t *fdip;	/* target directory to create link in */
	hammer2_inode_t *tdip;	/* target directory to create link in */
	hammer2_inode_t *cdip;	/* common parent directory */
	hammer2_inode_t *ip;	/* inode we are hardlinking to */
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *fdcluster;
	hammer2_cluster_t *tdcluster;
	hammer2_cluster_t *cdcluster;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	tdip = VTOI(ap->a_dvp);
	if (tdip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	/*
	 * ip represents the file being hardlinked.  The file could be a
	 * normal file or a hardlink target if it has already been hardlinked.
	 * If ip is a hardlinked target then ip->pip represents the location
	 * of the hardlinked target, NOT the location of the hardlink pointer.
	 *
	 * Bump nlinks and potentially also create or move the hardlink
	 * target in the parent directory common to (ip) and (tdip).  The
	 * consolidation code can modify ip->cluster and ip->pip.  The
	 * returned cluster is locked.
	 */
	ip = VTOI(ap->a_vp);
	hammer2_pfs_memory_wait(ip->pmp);
	hammer2_trans_init(&trans, ip->pmp, HAMMER2_TRANS_NEWINODE);

	/*
	 * The common parent directory must be locked first to avoid deadlocks.
	 * Also note that fdip and/or tdip might match cdip.
	 */
	fdip = ip->pip;
	cdip = hammer2_inode_common_parent(fdip, tdip);
	hammer2_inode_lock(cdip, HAMMER2_RESOLVE_ALWAYS);
	hammer2_inode_lock(fdip, HAMMER2_RESOLVE_ALWAYS);
	hammer2_inode_lock(tdip, HAMMER2_RESOLVE_ALWAYS);

	cdcluster = hammer2_inode_cluster(cdip, HAMMER2_RESOLVE_ALWAYS);
	fdcluster = hammer2_inode_cluster(fdip, HAMMER2_RESOLVE_ALWAYS);
	tdcluster = hammer2_inode_cluster(tdip, HAMMER2_RESOLVE_ALWAYS);

	hammer2_inode_lock(ip, HAMMER2_RESOLVE_ALWAYS);
	cluster = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS);

	error = hammer2_hardlink_consolidate(&trans, ip, &cluster,
					     cdip, cdcluster, 1);
	if (error)
		goto done;

	/*
	 * Create a directory entry connected to the specified cluster.
	 *
	 * WARNING! chain can get moved by the connect (indirectly due to
	 *	    potential indirect block creation).
	 */
	error = hammer2_inode_connect(&trans,
				      ip, &cluster, 1,
				      tdip, tdcluster,
				      name, name_len, 0);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, ap->a_vp);
	}
done:
	hammer2_inode_unlock(ip, cluster);
	hammer2_inode_unlock(tdip, tdcluster);
	hammer2_inode_unlock(fdip, fdcluster);
	hammer2_inode_unlock(cdip, cdcluster);
	hammer2_inode_drop(cdip);
	hammer2_trans_done(&trans);

	LOCKSTOP;
	return error;
}

/*
 * hammer2_xop_ncreate { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
int
hammer2_xop_ncreate(struct vop_ncreate_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_trans_t trans;
	hammer2_cluster_t *ncluster;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	ncluster = NULL;

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len,
				   &ncluster, 0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, ncluster, &error);
		hammer2_inode_unlock(nip, ncluster);
	}
	hammer2_trans_done(&trans);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	LOCKSTOP;
	return error;
}

/*
 * Make a device node (typically a fifo)
 */
int
hammer2_xop_nmknod(struct vop_nmknod_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_trans_t trans;
	hammer2_cluster_t *ncluster;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	ncluster = NULL;

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len,
				   &ncluster, 0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, ncluster, &error);
		hammer2_inode_unlock(nip, ncluster);
	}
	hammer2_trans_done(&trans);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	LOCKSTOP;
	return error;
}

/*
 * hammer2_xop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
int
hammer2_xop_nsymlink(struct vop_nsymlink_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_cluster_t *ncparent;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;
	
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	ncparent = NULL;

	ap->a_vap->va_type = VLNK;	/* enforce type */

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len,
				   &ncparent, 0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		hammer2_trans_done(&trans);
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, ncparent, &error);

	/*
	 * Build the softlink (~like file data) and finalize the namecache.
	 */
	if (error == 0) {
		size_t bytes;
		struct uio auio;
		struct iovec aiov;
		hammer2_inode_data_t *nipdata;

		nipdata = &hammer2_cluster_wdata(ncparent)->ipdata;
		/* nipdata = &nip->chain->data->ipdata;XXX */
		bytes = strlen(ap->a_target);

		if (bytes <= HAMMER2_EMBEDDED_BYTES) {
			KKASSERT(nipdata->meta.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			bcopy(ap->a_target, nipdata->u.data, bytes);
			nipdata->meta.size = bytes;
			nip->meta.size = bytes;
			hammer2_cluster_modsync(ncparent);
			hammer2_inode_unlock(nip, ncparent);
			/* nipdata = NULL; not needed */
		} else {
			hammer2_inode_unlock(nip, ncparent);
			/* nipdata = NULL; not needed */
			bzero(&auio, sizeof(auio));
			bzero(&aiov, sizeof(aiov));
			auio.uio_iov = &aiov;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_WRITE;
			auio.uio_resid = bytes;
			auio.uio_iovcnt = 1;
			auio.uio_td = curthread;
			aiov.iov_base = ap->a_target;
			aiov.iov_len = bytes;
			/*error = hammer2_xop_write_file(nip, &auio, IO_APPEND, 0);*/
			/* XXX handle error */
			error = 0;
		}
	} else {
		hammer2_inode_unlock(nip, ncparent);
	}
	hammer2_trans_done(&trans);

	/*
	 * Finalize namecache
	 */
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	return error;
}

/*
 * hammer2_xop_nremove { nch, dvp, cred }
 */
int
hammer2_xop_nremove(struct vop_nremove_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return(EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);
	error = hammer2_unlink_file(&trans, dip, NULL, name, name_len,
				    0, NULL, ap->a_nch, -1);
	hammer2_run_unlinkq(&trans, dip->pmp);
	hammer2_trans_done(&trans);
	if (error == 0)
		cache_unlink(ap->a_nch);
	LOCKSTOP;
	return (error);
}

/*
 * hammer2_xop_nrmdir { nch, dvp, cred }
 */
int
hammer2_xop_nrmdir(struct vop_nrmdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return(EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);
	hammer2_run_unlinkq(&trans, dip->pmp);
	error = hammer2_unlink_file(&trans, dip, NULL, name, name_len,
				    1, NULL, ap->a_nch, -1);
	hammer2_trans_done(&trans);
	if (error == 0)
		cache_unlink(ap->a_nch);
	LOCKSTOP;
	return (error);
}

/*
 * hammer2_xop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
int
hammer2_xop_nrename(struct vop_nrename_args *ap)
{
	struct namecache *fncp;
	struct namecache *tncp;
	hammer2_inode_t *cdip;
	hammer2_inode_t *fdip;
	hammer2_inode_t *tdip;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *fdcluster;
	hammer2_cluster_t *tdcluster;
	hammer2_cluster_t *cdcluster;
	hammer2_trans_t trans;
	const uint8_t *fname;
	size_t fname_len;
	const uint8_t *tname;
	size_t tname_len;
	int error;
	int tnch_error;
	int hlink;

	if (ap->a_fdvp->v_mount != ap->a_tdvp->v_mount)
		return(EXDEV);
	if (ap->a_fdvp->v_mount != ap->a_fnch->ncp->nc_vp->v_mount)
		return(EXDEV);

	fdip = VTOI(ap->a_fdvp);	/* source directory */
	tdip = VTOI(ap->a_tdvp);	/* target directory */

	if (fdip->pmp->ronly)
		return(EROFS);

	LOCKSTART;
	fncp = ap->a_fnch->ncp;		/* entry name in source */
	fname = fncp->nc_name;
	fname_len = fncp->nc_nlen;

	tncp = ap->a_tnch->ncp;		/* entry name in target */
	tname = tncp->nc_name;
	tname_len = tncp->nc_nlen;

	hammer2_pfs_memory_wait(tdip->pmp);
	hammer2_trans_init(&trans, tdip->pmp, 0);

	/*
	 * ip is the inode being renamed.  If this is a hardlink then
	 * ip represents the actual file and not the hardlink marker.
	 */
	ip = VTOI(fncp->nc_vp);
	cluster = NULL;


	/*
	 * The common parent directory must be locked first to avoid deadlocks.
	 * Also note that fdip and/or tdip might match cdip.
	 *
	 * WARNING! fdip may not match ip->pip.  That is, if the source file
	 *	    is already a hardlink then what we are renaming is the
	 *	    hardlink pointer, not the hardlink itself.  The hardlink
	 *	    directory (ip->pip) will already be at a common parent
	 *	    of fdrip.
	 *
	 *	    Be sure to use ip->pip when finding the common parent
	 *	    against tdip or we might accidently move the hardlink
	 *	    target into a subdirectory that makes it inaccessible to
	 *	    other pointers.
	 */
	cdip = hammer2_inode_common_parent(ip->pip, tdip);
	hammer2_inode_lock(cdip, HAMMER2_RESOLVE_ALWAYS);
	hammer2_inode_lock(fdip, HAMMER2_RESOLVE_ALWAYS);
	hammer2_inode_lock(tdip, HAMMER2_RESOLVE_ALWAYS);

	cdcluster = hammer2_inode_cluster(cdip, HAMMER2_RESOLVE_ALWAYS);
	fdcluster = hammer2_inode_cluster(fdip, HAMMER2_RESOLVE_ALWAYS);
	tdcluster = hammer2_inode_cluster(tdip, HAMMER2_RESOLVE_ALWAYS);

	/*
	 * Keep a tight grip on the inode so the temporary unlinking from
	 * the source location prior to linking to the target location
	 * does not cause the cluster to be destroyed.
	 *
	 * NOTE: To avoid deadlocks we cannot lock (ip) while we are
	 *	 unlinking elements from their directories.  Locking
	 *	 the nlinks field does not lock the whole inode.
	 */
	hammer2_inode_ref(ip);

	/*
	 * Remove target if it exists.
	 */
	error = hammer2_unlink_file(&trans, tdip, NULL, tname, tname_len,
				    -1, NULL, ap->a_tnch, -1);
	tnch_error = error;
	if (error && error != ENOENT)
		goto done2;

	/*
	 * When renaming a hardlinked file we may have to re-consolidate
	 * the location of the hardlink target.
	 *
	 * If ip represents a regular file the consolidation code essentially
	 * does nothing other than return the same locked cluster that was
	 * passed in.
	 *
	 * The returned cluster will be locked.
	 *
	 * WARNING!  We do not currently have a local copy of ipdata but
	 *	     we do use one later remember that it must be reloaded
	 *	     on any modification to the inode, including connects.
	 */
	hammer2_inode_lock(ip, HAMMER2_RESOLVE_ALWAYS);
	cluster = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS);

	error = hammer2_hardlink_consolidate(&trans, ip, &cluster,
					     cdip, cdcluster, 0);
	if (error)
		goto done1;

	/*
	 * Disconnect (fdip, fname) from the source directory.  This will
	 * disconnect (ip) if it represents a direct file.  If (ip) represents
	 * a hardlink the HARDLINK pointer object will be removed but the
	 * hardlink will stay intact.
	 *
	 * Always pass nch as NULL because we intend to reconnect the inode,
	 * so we don't want hammer2_unlink_file() to rename it to the hidden
	 * open-but-unlinked directory.
	 *
	 * The target cluster may be marked DELETED but will not be destroyed
	 * since we retain our hold on ip and cluster.
	 *
	 * NOTE: We pass nlinks as 0 (not -1) in order to retain the file's
	 *	 link count.
	 */
	error = hammer2_unlink_file(&trans, fdip, ip, fname, fname_len,
				    -1, &hlink, NULL, 0);
	KKASSERT(error != EAGAIN);
	if (error)
		goto done1;

	/*
	 * Reconnect ip to target directory using cluster.  Chains cannot
	 * actually be moved, so this will duplicate the cluster in the new
	 * spot and assign it to the ip, replacing the old cluster.
	 *
	 * WARNING: Because recursive locks are allowed and we unlinked the
	 *	    file that we have a cluster-in-hand for just above, the
	 *	    cluster might have been delete-duplicated.  We must
	 *	    refactor the cluster.
	 *
	 * WARNING: Chain locks can lock buffer cache buffers, to avoid
	 *	    deadlocks we want to unlock before issuing a cache_*()
	 *	    op (that might have to lock a vnode).
	 *
	 * NOTE:    Pass nlinks as 0 because we retained the link count from
	 *	    the unlink, so we do not have to modify it.
	 */
	error = hammer2_inode_connect(&trans,
				      ip, &cluster, hlink,
				      tdip, tdcluster,
				      tname, tname_len, 0);
	if (error == 0) {
		KKASSERT(cluster != NULL);
		hammer2_inode_repoint(ip, (hlink ? ip->pip : tdip), cluster);
	}
done1:
	hammer2_inode_unlock(ip, cluster);
done2:
	hammer2_inode_unlock(tdip, tdcluster);
	hammer2_inode_unlock(fdip, fdcluster);
	hammer2_inode_unlock(cdip, cdcluster);
	hammer2_inode_drop(ip);
	hammer2_inode_drop(cdip);
	hammer2_run_unlinkq(&trans, fdip->pmp);
	hammer2_trans_done(&trans);

	/*
	 * Issue the namecache update after unlocking all the internal
	 * hammer structures, otherwise we might deadlock.
	 */
	if (tnch_error == 0) {
		cache_unlink(ap->a_tnch);
		cache_setunresolved(ap->a_tnch);
	}
	if (error == 0)
		cache_rename(ap->a_fnch, ap->a_tnch);

	LOCKSTOP;
	return (error);
}

#endif
