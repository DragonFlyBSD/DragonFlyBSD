/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/vfsops.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/objcache.h>

#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>

#include <sys/mutex.h>
#include <sys/mutex2.h>

#include "hammer2.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"
#include "hammer2_lz4.h"

#include "zlib/hammer2_zlib.h"

#define REPORT_REFS_ERRORS 1	/* XXX remove me */

MALLOC_DEFINE(M_OBJCACHE, "objcache", "Object Cache");

struct hammer2_sync_info {
	hammer2_trans_t trans;
	int error;
	int waitfor;
};

TAILQ_HEAD(hammer2_mntlist, hammer2_dev);
TAILQ_HEAD(hammer2_pfslist, hammer2_pfs);
static struct hammer2_mntlist hammer2_mntlist;
static struct hammer2_pfslist hammer2_pfslist;
static struct lock hammer2_mntlk;

int hammer2_debug;
int hammer2_cluster_enable = 1;
int hammer2_hardlink_enable = 1;
int hammer2_flush_pipe = 100;
int hammer2_synchronous_flush = 1;
int hammer2_dio_count;
long hammer2_limit_dirty_chains;
long hammer2_iod_file_read;
long hammer2_iod_meta_read;
long hammer2_iod_indr_read;
long hammer2_iod_fmap_read;
long hammer2_iod_volu_read;
long hammer2_iod_file_write;
long hammer2_iod_meta_write;
long hammer2_iod_indr_write;
long hammer2_iod_fmap_write;
long hammer2_iod_volu_write;
long hammer2_ioa_file_read;
long hammer2_ioa_meta_read;
long hammer2_ioa_indr_read;
long hammer2_ioa_fmap_read;
long hammer2_ioa_volu_read;
long hammer2_ioa_fmap_write;
long hammer2_ioa_file_write;
long hammer2_ioa_meta_write;
long hammer2_ioa_indr_write;
long hammer2_ioa_volu_write;

MALLOC_DECLARE(M_HAMMER2_CBUFFER);
MALLOC_DEFINE(M_HAMMER2_CBUFFER, "HAMMER2-compbuffer",
		"Buffer used for compression.");

MALLOC_DECLARE(M_HAMMER2_DEBUFFER);
MALLOC_DEFINE(M_HAMMER2_DEBUFFER, "HAMMER2-decompbuffer",
		"Buffer used for decompression.");

SYSCTL_NODE(_vfs, OID_AUTO, hammer2, CTLFLAG_RW, 0, "HAMMER2 filesystem");

SYSCTL_INT(_vfs_hammer2, OID_AUTO, debug, CTLFLAG_RW,
	   &hammer2_debug, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, cluster_enable, CTLFLAG_RW,
	   &hammer2_cluster_enable, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, hardlink_enable, CTLFLAG_RW,
	   &hammer2_hardlink_enable, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, flush_pipe, CTLFLAG_RW,
	   &hammer2_flush_pipe, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, synchronous_flush, CTLFLAG_RW,
	   &hammer2_synchronous_flush, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, limit_dirty_chains, CTLFLAG_RW,
	   &hammer2_limit_dirty_chains, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, dio_count, CTLFLAG_RD,
	   &hammer2_dio_count, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_read, CTLFLAG_RW,
	   &hammer2_iod_file_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_meta_read, CTLFLAG_RW,
	   &hammer2_iod_meta_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_indr_read, CTLFLAG_RW,
	   &hammer2_iod_indr_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_fmap_read, CTLFLAG_RW,
	   &hammer2_iod_fmap_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_volu_read, CTLFLAG_RW,
	   &hammer2_iod_volu_read, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_write, CTLFLAG_RW,
	   &hammer2_iod_file_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_meta_write, CTLFLAG_RW,
	   &hammer2_iod_meta_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_indr_write, CTLFLAG_RW,
	   &hammer2_iod_indr_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_fmap_write, CTLFLAG_RW,
	   &hammer2_iod_fmap_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_volu_write, CTLFLAG_RW,
	   &hammer2_iod_volu_write, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_file_read, CTLFLAG_RW,
	   &hammer2_ioa_file_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_meta_read, CTLFLAG_RW,
	   &hammer2_ioa_meta_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_indr_read, CTLFLAG_RW,
	   &hammer2_ioa_indr_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_fmap_read, CTLFLAG_RW,
	   &hammer2_ioa_fmap_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_volu_read, CTLFLAG_RW,
	   &hammer2_ioa_volu_read, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_file_write, CTLFLAG_RW,
	   &hammer2_ioa_file_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_meta_write, CTLFLAG_RW,
	   &hammer2_ioa_meta_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_indr_write, CTLFLAG_RW,
	   &hammer2_ioa_indr_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_fmap_write, CTLFLAG_RW,
	   &hammer2_ioa_fmap_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_volu_write, CTLFLAG_RW,
	   &hammer2_ioa_volu_write, 0, "");

static int hammer2_vfs_init(struct vfsconf *conf);
static int hammer2_vfs_uninit(struct vfsconf *vfsp);
static int hammer2_vfs_mount(struct mount *mp, char *path, caddr_t data,
				struct ucred *cred);
static int hammer2_remount(hammer2_dev_t *, struct mount *, char *,
				struct vnode *, struct ucred *);
static int hammer2_recovery(hammer2_dev_t *hmp);
static int hammer2_vfs_unmount(struct mount *mp, int mntflags);
static int hammer2_vfs_root(struct mount *mp, struct vnode **vpp);
static int hammer2_vfs_statfs(struct mount *mp, struct statfs *sbp,
				struct ucred *cred);
static int hammer2_vfs_statvfs(struct mount *mp, struct statvfs *sbp,
				struct ucred *cred);
static int hammer2_vfs_vget(struct mount *mp, struct vnode *dvp,
				ino_t ino, struct vnode **vpp);
static int hammer2_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
				struct fid *fhp, struct vnode **vpp);
static int hammer2_vfs_vptofh(struct vnode *vp, struct fid *fhp);
static int hammer2_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
				int *exflagsp, struct ucred **credanonp);

static int hammer2_install_volume_header(hammer2_dev_t *hmp);
static int hammer2_sync_scan2(struct mount *mp, struct vnode *vp, void *data);

static void hammer2_update_pmps(hammer2_dev_t *hmp);

static void hammer2_mount_helper(struct mount *mp, hammer2_pfs_t *pmp);
static void hammer2_unmount_helper(struct mount *mp, hammer2_pfs_t *pmp,
				hammer2_dev_t *hmp);

/*
 * HAMMER2 vfs operations.
 */
static struct vfsops hammer2_vfsops = {
	.vfs_init	= hammer2_vfs_init,
	.vfs_uninit	= hammer2_vfs_uninit,
	.vfs_sync	= hammer2_vfs_sync,
	.vfs_mount	= hammer2_vfs_mount,
	.vfs_unmount	= hammer2_vfs_unmount,
	.vfs_root 	= hammer2_vfs_root,
	.vfs_statfs	= hammer2_vfs_statfs,
	.vfs_statvfs	= hammer2_vfs_statvfs,
	.vfs_vget	= hammer2_vfs_vget,
	.vfs_vptofh	= hammer2_vfs_vptofh,
	.vfs_fhtovp	= hammer2_vfs_fhtovp,
	.vfs_checkexp	= hammer2_vfs_checkexp
};

MALLOC_DEFINE(M_HAMMER2, "HAMMER2-mount", "");

VFS_SET(hammer2_vfsops, hammer2, 0);
MODULE_VERSION(hammer2, 1);

static
int
hammer2_vfs_init(struct vfsconf *conf)
{
	static struct objcache_malloc_args margs_read;
	static struct objcache_malloc_args margs_write;
	static struct objcache_malloc_args margs_vop;

	int error;

	error = 0;

	if (HAMMER2_BLOCKREF_BYTES != sizeof(struct hammer2_blockref))
		error = EINVAL;
	if (HAMMER2_INODE_BYTES != sizeof(struct hammer2_inode_data))
		error = EINVAL;
	if (HAMMER2_VOLUME_BYTES != sizeof(struct hammer2_volume_data))
		error = EINVAL;

	if (error)
		kprintf("HAMMER2 structure size mismatch; cannot continue.\n");
	
	margs_read.objsize = 65536;
	margs_read.mtype = M_HAMMER2_DEBUFFER;
	
	margs_write.objsize = 32768;
	margs_write.mtype = M_HAMMER2_CBUFFER;

	margs_vop.objsize = sizeof(hammer2_xop_t);
	margs_vop.mtype = M_HAMMER2;
	
	cache_buffer_read = objcache_create(margs_read.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL, objcache_malloc_alloc,
				objcache_malloc_free, &margs_read);
	cache_buffer_write = objcache_create(margs_write.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL, objcache_malloc_alloc,
				objcache_malloc_free, &margs_write);
	cache_xops = objcache_create(margs_vop.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL, objcache_malloc_alloc,
				objcache_malloc_free, &margs_vop);


	lockinit(&hammer2_mntlk, "mntlk", 0, 0);
	TAILQ_INIT(&hammer2_mntlist);
	TAILQ_INIT(&hammer2_pfslist);

	hammer2_limit_dirty_chains = desiredvnodes / 10;

	return (error);
}

static
int
hammer2_vfs_uninit(struct vfsconf *vfsp __unused)
{
	objcache_destroy(cache_buffer_read);
	objcache_destroy(cache_buffer_write);
	objcache_destroy(cache_xops);
	return 0;
}

/*
 * Core PFS allocator.  Used to allocate the pmp structure for PFS cluster
 * mounts and the spmp structure for media (hmp) structures.
 *
 * pmp->modify_tid tracks new modify_tid transaction ids for front-end
 * transactions.  Note that synchronization does not use this field.
 * (typically frontend operations and synchronization cannot run on the
 * same PFS node at the same time).
 *
 * XXX check locking
 */
hammer2_pfs_t *
hammer2_pfsalloc(hammer2_cluster_t *cluster,
		 const hammer2_inode_data_t *ripdata,
		 hammer2_tid_t modify_tid)
{
	hammer2_chain_t *rchain;
	hammer2_inode_t *iroot;
	hammer2_pfs_t *pmp;
	int count;
	int i;
	int j;

	/*
	 * Locate or create the PFS based on the cluster id.  If ripdata
	 * is NULL this is a spmp which is unique and is always allocated.
	 */
	if (ripdata) {
		TAILQ_FOREACH(pmp, &hammer2_pfslist, mntentry) {
			if (bcmp(&pmp->pfs_clid, &ripdata->meta.pfs_clid,
				 sizeof(pmp->pfs_clid)) == 0) {
					break;
			}
		}
	} else {
		pmp = NULL;
	}

	if (pmp == NULL) {
		pmp = kmalloc(sizeof(*pmp), M_HAMMER2, M_WAITOK | M_ZERO);
		hammer2_trans_manage_init(&pmp->tmanage);
		kmalloc_create(&pmp->minode, "HAMMER2-inodes");
		kmalloc_create(&pmp->mmsg, "HAMMER2-pfsmsg");
		lockinit(&pmp->lock, "pfslk", 0, 0);
		spin_init(&pmp->inum_spin, "hm2pfsalloc_inum");
		RB_INIT(&pmp->inum_tree);
		TAILQ_INIT(&pmp->unlinkq);
		spin_init(&pmp->list_spin, "hm2pfsalloc_list");

		for (j = 0; j < HAMMER2_XOPGROUPS; ++j)
			hammer2_xop_group_init(pmp, &pmp->xop_groups[j]);

		/*
		 * Save the last media transaction id for the flusher.  Set
		 * initial 
		 */
		if (ripdata)
			pmp->pfs_clid = ripdata->meta.pfs_clid;
		hammer2_mtx_init(&pmp->wthread_mtx, "h2wthr");
		bioq_init(&pmp->wthread_bioq);
		TAILQ_INSERT_TAIL(&hammer2_pfslist, pmp, mntentry);

		/*
		 * The synchronization thread may start too early, make
		 * sure it stays frozen until we are ready to let it go.
		 * XXX
		 */
		/*
		pmp->primary_thr.flags = HAMMER2_THREAD_FROZEN |
					 HAMMER2_THREAD_REMASTER;
		*/
	}

	/*
	 * Create the PFS's root inode.
	 */
	if ((iroot = pmp->iroot) == NULL) {
		iroot = hammer2_inode_get(pmp, NULL, NULL);
		pmp->iroot = iroot;
		hammer2_inode_ref(iroot);
		hammer2_inode_unlock(iroot, NULL);
	}

	/*
	 * Stop here if no cluster is passed in.
	 */
	if (cluster == NULL)
		goto done;

	/*
	 * When a cluster is passed in we must add the cluster's chains
	 * to the PFS's root inode, update pmp->pfs_types[], and update
	 * the syncronization threads.
	 *
	 * At the moment empty spots can develop due to removals or failures.
	 * Ultimately we want to re-fill these spots but doing so might
	 * confused running code. XXX
	 */
	hammer2_inode_ref(iroot);
	hammer2_mtx_ex(&iroot->lock);
	j = iroot->cluster.nchains;

	kprintf("add PFS to pmp %p[%d]\n", pmp, j);

	for (i = 0; i < cluster->nchains; ++i) {
		if (j == HAMMER2_MAXCLUSTER)
			break;
		rchain = cluster->array[i].chain;
		KKASSERT(rchain->pmp == NULL);
		rchain->pmp = pmp;
		hammer2_chain_ref(rchain);
		iroot->cluster.array[j].chain = rchain;
		pmp->pfs_types[j] = ripdata->meta.pfs_type;
		pmp->pfs_names[j] = kstrdup(ripdata->filename, M_HAMMER2);

		/*
		 * If the PFS is already mounted we must account
		 * for the mount_count here.
		 */
		if (pmp->mp)
			++rchain->hmp->mount_count;

		/*
		 * May have to fixup dirty chain tracking.  Previous
		 * pmp was NULL so nothing to undo.
		 */
		if (rchain->flags & HAMMER2_CHAIN_MODIFIED)
			hammer2_pfs_memory_inc(pmp);
		++j;
	}
	iroot->cluster.nchains = j;

	if (i != cluster->nchains) {
		kprintf("hammer2_mount: cluster full!\n");
		/* XXX fatal error? */
	}

	/*
	 * Update nmasters from any PFS inode which is part of the cluster.
	 * It is possible that this will result in a value which is too
	 * high.  MASTER PFSs are authoritative for pfs_nmasters and will
	 * override this value later on.
	 *
	 * (This informs us of masters that might not currently be
	 *  discoverable by this mount).
	 */
	if (ripdata && pmp->pfs_nmasters < ripdata->meta.pfs_nmasters) {
		pmp->pfs_nmasters = ripdata->meta.pfs_nmasters;
	}

	/*
	 * Count visible masters.  Masters are usually added with
	 * ripdata->meta.pfs_nmasters set to 1.  This detects when there
	 * are more (XXX and must update the master inodes).
	 */
	count = 0;
	for (i = 0; i < iroot->cluster.nchains; ++i) {
		if (pmp->pfs_types[i] == HAMMER2_PFSTYPE_MASTER)
			++count;
	}
	if (pmp->pfs_nmasters < count)
		pmp->pfs_nmasters = count;

	/*
	 * Create missing synchronization and support threads.
	 *
	 * Single-node masters (including snapshots) have nothing to
	 * synchronize and do not require this thread.
	 *
	 * Multi-node masters or any number of soft masters, slaves, copy,
	 * or other PFS types need the thread.
	 *
	 * Each thread is responsible for its particular cluster index.
	 * We use independent threads so stalls or mismatches related to
	 * any given target do not affect other targets.
	 */
	for (i = 0; i < iroot->cluster.nchains; ++i) {
		/*
		 * Single-node masters (including snapshots) have nothing
		 * to synchronize and will make direct xops support calls,
		 * thus they do not require this thread.
		 *
		 * Note that there can be thousands of snapshots.  We do not
		 * want to create thousands of threads.
		 */
		if (pmp->pfs_nmasters <= 1 &&
		    pmp->pfs_types[i] == HAMMER2_PFSTYPE_MASTER) {
			continue;
		}

		/*
		 * Sync support thread
		 */
		if (pmp->sync_thrs[i].td == NULL) {
			hammer2_thr_create(&pmp->sync_thrs[i], pmp,
					   "h2nod", i, 0,
					   hammer2_primary_sync_thread);
		}

		/*
		 * Xops support threads
		 */
		for (j = 0; j < HAMMER2_XOPGROUPS; ++j) {
			if (pmp->xop_groups[j].thrs[i].td)
				continue;
			hammer2_thr_create(&pmp->xop_groups[j].thrs[i], pmp,
					   "h2xop", i, j,
					   hammer2_primary_xops_thread);
		}
	}

	hammer2_mtx_unlock(&iroot->lock);
	hammer2_inode_drop(iroot);
done:
	return pmp;
}

/*
 * Destroy a PFS, typically only occurs after the last mount on a device
 * has gone away.
 */
static void
hammer2_pfsfree(hammer2_pfs_t *pmp)
{
	hammer2_inode_t *iroot;
	int i;
	int j;

	/*
	 * Cleanup our reference on iroot.  iroot is (should) not be needed
	 * by the flush code.
	 */
	TAILQ_REMOVE(&hammer2_pfslist, pmp, mntentry);

	iroot = pmp->iroot;
	if (iroot) {
		for (i = 0; i < iroot->cluster.nchains; ++i) {
			hammer2_thr_delete(&pmp->sync_thrs[i]);
			for (j = 0; j < HAMMER2_XOPGROUPS; ++j)
				hammer2_thr_delete(&pmp->xop_groups[j].thrs[i]);
		}
#if REPORT_REFS_ERRORS
		if (pmp->iroot->refs != 1)
			kprintf("PMP->IROOT %p REFS WRONG %d\n",
				pmp->iroot, pmp->iroot->refs);
#else
		KKASSERT(pmp->iroot->refs == 1);
#endif
		/* ref for pmp->iroot */
		hammer2_inode_drop(pmp->iroot);
		pmp->iroot = NULL;
	}

	kmalloc_destroy(&pmp->mmsg);
	kmalloc_destroy(&pmp->minode);

	kfree(pmp, M_HAMMER2);
}

/*
 * Remove all references to hmp from the pfs list.  Any PFS which becomes
 * empty is terminated and freed.
 *
 * XXX inefficient.
 */
static void
hammer2_pfsfree_scan(hammer2_dev_t *hmp)
{
	hammer2_pfs_t *pmp;
	hammer2_inode_t *iroot;
	hammer2_cluster_t *cluster;
	hammer2_chain_t *rchain;
	int didfreeze;
	int i;
	int j;

again:
	TAILQ_FOREACH(pmp, &hammer2_pfslist, mntentry) {
		if ((iroot = pmp->iroot) == NULL)
			continue;
		if (hmp->spmp == pmp) {
			kprintf("unmount hmp %p remove spmp %p\n",
				hmp, pmp);
			hmp->spmp = NULL;
		}

		/*
		 * Determine if this PFS is affected.  If it is we must
		 * freeze all management threads and lock its iroot.
		 *
		 * Freezing a management thread forces it idle, operations
		 * in-progress will be aborted and it will have to start
		 * over again when unfrozen, or exit if told to exit.
		 */
		cluster = &iroot->cluster;
		for (i = 0; i < cluster->nchains; ++i) {
			rchain = cluster->array[i].chain;
			if (rchain == NULL || rchain->hmp != hmp)
				continue;
			break;
		}
		if (i != cluster->nchains) {
			/*
			 * Make sure all synchronization threads are locked
			 * down.
			 */
			for (i = 0; i < iroot->cluster.nchains; ++i) {
				hammer2_thr_freeze_async(&pmp->sync_thrs[i]);
				for (j = 0; j < HAMMER2_XOPGROUPS; ++j) {
					hammer2_thr_freeze_async(
						&pmp->xop_groups[j].thrs[i]);
				}
			}
			for (i = 0; i < iroot->cluster.nchains; ++i) {
				hammer2_thr_freeze(&pmp->sync_thrs[i]);
				for (j = 0; j < HAMMER2_XOPGROUPS; ++j) {
					hammer2_thr_freeze(
						&pmp->xop_groups[j].thrs[i]);
				}
			}

			/*
			 * Lock the inode and clean out matching chains.
			 * Note that we cannot use hammer2_inode_lock_*()
			 * here because that would attempt to validate the
			 * cluster that we are in the middle of ripping
			 * apart.
			 *
			 * WARNING! We are working directly on the inodes
			 *	    embedded cluster.
			 */
			hammer2_mtx_ex(&iroot->lock);

			/*
			 * Remove the chain from matching elements of the PFS.
			 */
			for (i = 0; i < cluster->nchains; ++i) {
				rchain = cluster->array[i].chain;
				if (rchain == NULL || rchain->hmp != hmp)
					continue;
				hammer2_thr_delete(&pmp->sync_thrs[i]);
				for (j = 0; j < HAMMER2_XOPGROUPS; ++j) {
					hammer2_thr_delete(
						&pmp->xop_groups[j].thrs[i]);
				}
				rchain = cluster->array[i].chain;
				cluster->array[i].chain = NULL;
				pmp->pfs_types[i] = 0;
				if (pmp->pfs_names[i]) {
					kfree(pmp->pfs_names[i], M_HAMMER2);
					pmp->pfs_names[i] = NULL;
				}
				hammer2_chain_drop(rchain);

				/* focus hint */
				if (cluster->focus == rchain)
					cluster->focus = NULL;
			}
			hammer2_mtx_unlock(&iroot->lock);
			didfreeze = 1;	/* remaster, unfreeze down below */
		} else {
			didfreeze = 0;
		}

		/*
		 * Cleanup trailing chains.  Do not reorder chains (for now).
		 * XXX might remove more than we intended.
		 */
		while (i > 0) {
			if (cluster->array[i - 1].chain)
				break;
			--i;
		}
		cluster->nchains = i;

		/*
		 * If the PMP has no elements remaining we can destroy it.
		 * (this will transition management threads from frozen->exit).
		 */
		if (cluster->nchains == 0) {
			kprintf("unmount hmp %p last ref to PMP=%p\n",
				hmp, pmp);
			hammer2_pfsfree(pmp);
			goto again;
		}

		/*
		 * If elements still remain we need to set the REMASTER
		 * flag and unfreeze it.
		 */
		if (didfreeze) {
			for (i = 0; i < iroot->cluster.nchains; ++i) {
				hammer2_thr_remaster(&pmp->sync_thrs[i]);
				hammer2_thr_unfreeze(&pmp->sync_thrs[i]);
				for (j = 0; j < HAMMER2_XOPGROUPS; ++j) {
					hammer2_thr_remaster(
						&pmp->xop_groups[j].thrs[i]);
					hammer2_thr_unfreeze(
						&pmp->xop_groups[j].thrs[i]);
				}
			}
		}
	}
}

/*
 * Mount or remount HAMMER2 fileystem from physical media
 *
 *	mountroot
 *		mp		mount point structure
 *		path		NULL
 *		data		<unused>
 *		cred		<unused>
 *
 *	mount
 *		mp		mount point structure
 *		path		path to mount point
 *		data		pointer to argument structure in user space
 *			volume	volume path (device@LABEL form)
 *			hflags	user mount flags
 *		cred		user credentials
 *
 * RETURNS:	0	Success
 *		!0	error number
 */
static
int
hammer2_vfs_mount(struct mount *mp, char *path, caddr_t data,
		  struct ucred *cred)
{
	struct hammer2_mount_info info;
	hammer2_pfs_t *pmp;
	hammer2_pfs_t *spmp;
	hammer2_dev_t *hmp;
	hammer2_key_t key_next;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	struct vnode *devvp;
	struct nlookupdata nd;
	hammer2_chain_t *parent;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *cparent;
	const hammer2_inode_data_t *ripdata;
	hammer2_blockref_t bref;
	struct file *fp;
	char devstr[MNAMELEN];
	size_t size;
	size_t done;
	char *dev;
	char *label;
	int ronly = 1;
	int error;
	int cache_index;
	int i;

	hmp = NULL;
	pmp = NULL;
	dev = NULL;
	label = NULL;
	devvp = NULL;
	cache_index = -1;

	kprintf("hammer2_mount\n");

	if (path == NULL) {
		/*
		 * Root mount
		 */
		bzero(&info, sizeof(info));
		info.cluster_fd = -1;
		return (EOPNOTSUPP);
	} else {
		/*
		 * Non-root mount or updating a mount
		 */
		error = copyin(data, &info, sizeof(info));
		if (error)
			return (error);

		error = copyinstr(info.volume, devstr, MNAMELEN - 1, &done);
		if (error)
			return (error);

		/* Extract device and label */
		dev = devstr;
		label = strchr(devstr, '@');
		if (label == NULL ||
		    ((label + 1) - dev) > done) {
			return (EINVAL);
		}
		*label = '\0';
		label++;
		if (*label == '\0')
			return (EINVAL);

		if (mp->mnt_flag & MNT_UPDATE) {
			/*
			 * Update mount.  Note that pmp->iroot->cluster is
			 * an inode-embedded cluster and thus cannot be
			 * directly locked.
			 *
			 * XXX HAMMER2 needs to implement NFS export via
			 *     mountctl.
			 */
			pmp = MPTOPMP(mp);
			cluster = &pmp->iroot->cluster;
			for (i = 0; i < cluster->nchains; ++i) {
				if (cluster->array[i].chain == NULL)
					continue;
				hmp = cluster->array[i].chain->hmp;
				devvp = hmp->devvp;
				error = hammer2_remount(hmp, mp, path,
							devvp, cred);
				if (error)
					break;
			}
			/*hammer2_inode_install_hidden(pmp);*/

			return error;
		}
	}

	/*
	 * HMP device mount
	 *
	 * Lookup name and verify it refers to a block device.
	 */
	error = nlookup_init(&nd, dev, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	nlookup_done(&nd);

	if (error == 0) {
		if (vn_isdisk(devvp, &error))
			error = vfs_mountedon(devvp);
	}

	/*
	 * Determine if the device has already been mounted.  After this
	 * check hmp will be non-NULL if we are doing the second or more
	 * hammer2 mounts from the same device.
	 */
	lockmgr(&hammer2_mntlk, LK_EXCLUSIVE);
	TAILQ_FOREACH(hmp, &hammer2_mntlist, mntentry) {
		if (hmp->devvp == devvp)
			break;
	}

	/*
	 * Open the device if this isn't a secondary mount and construct
	 * the H2 device mount (hmp).
	 */
	if (hmp == NULL) {
		hammer2_chain_t *schain;
		hammer2_xid_t xid;

		if (error == 0 && vcount(devvp) > 0)
			error = EBUSY;

		/*
		 * Now open the device
		 */
		if (error == 0) {
			ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			error = vinvalbuf(devvp, V_SAVE, 0, 0);
			if (error == 0) {
				error = VOP_OPEN(devvp,
						 ronly ? FREAD : FREAD | FWRITE,
						 FSCRED, NULL);
			}
			vn_unlock(devvp);
		}
		if (error && devvp) {
			vrele(devvp);
			devvp = NULL;
		}
		if (error) {
			lockmgr(&hammer2_mntlk, LK_RELEASE);
			return error;
		}
		hmp = kmalloc(sizeof(*hmp), M_HAMMER2, M_WAITOK | M_ZERO);
		ksnprintf(hmp->devrepname, sizeof(hmp->devrepname), "%s", dev);
		hmp->ronly = ronly;
		hmp->devvp = devvp;
		kmalloc_create(&hmp->mchain, "HAMMER2-chains");
		TAILQ_INSERT_TAIL(&hammer2_mntlist, hmp, mntentry);
		RB_INIT(&hmp->iotree);
		spin_init(&hmp->io_spin, "hm2mount_io");
		spin_init(&hmp->list_spin, "hm2mount_list");
		TAILQ_INIT(&hmp->flushq);

		lockinit(&hmp->vollk, "h2vol", 0, 0);

		/*
		 * vchain setup. vchain.data is embedded.
		 * vchain.refs is initialized and will never drop to 0.
		 *
		 * NOTE! voldata is not yet loaded.
		 */
		hmp->vchain.hmp = hmp;
		hmp->vchain.refs = 1;
		hmp->vchain.data = (void *)&hmp->voldata;
		hmp->vchain.bref.type = HAMMER2_BREF_TYPE_VOLUME;
		hmp->vchain.bref.data_off = 0 | HAMMER2_PBUFRADIX;
		hmp->vchain.bref.mirror_tid = hmp->voldata.mirror_tid;

		hammer2_chain_core_init(&hmp->vchain);
		/* hmp->vchain.u.xxx is left NULL */

		/*
		 * fchain setup.  fchain.data is embedded.
		 * fchain.refs is initialized and will never drop to 0.
		 *
		 * The data is not used but needs to be initialized to
		 * pass assertion muster.  We use this chain primarily
		 * as a placeholder for the freemap's top-level RBTREE
		 * so it does not interfere with the volume's topology
		 * RBTREE.
		 */
		hmp->fchain.hmp = hmp;
		hmp->fchain.refs = 1;
		hmp->fchain.data = (void *)&hmp->voldata.freemap_blockset;
		hmp->fchain.bref.type = HAMMER2_BREF_TYPE_FREEMAP;
		hmp->fchain.bref.data_off = 0 | HAMMER2_PBUFRADIX;
		hmp->fchain.bref.mirror_tid = hmp->voldata.freemap_tid;
		hmp->fchain.bref.methods =
			HAMMER2_ENC_CHECK(HAMMER2_CHECK_FREEMAP) |
			HAMMER2_ENC_COMP(HAMMER2_COMP_NONE);

		hammer2_chain_core_init(&hmp->fchain);
		/* hmp->fchain.u.xxx is left NULL */

		/*
		 * Install the volume header and initialize fields from
		 * voldata.
		 */
		error = hammer2_install_volume_header(hmp);
		if (error) {
			hammer2_unmount_helper(mp, NULL, hmp);
			lockmgr(&hammer2_mntlk, LK_RELEASE);
			hammer2_vfs_unmount(mp, MNT_FORCE);
			return error;
		}

		/*
		 * Really important to get these right or flush will get
		 * confused.
		 */
		hmp->spmp = hammer2_pfsalloc(NULL, NULL, 0);
		kprintf("alloc spmp %p tid %016jx\n",
			hmp->spmp, hmp->voldata.mirror_tid);
		spmp = hmp->spmp;

		/*
		 * Dummy-up vchain and fchain's modify_tid.  mirror_tid
		 * is inherited from the volume header.
		 */
		xid = 0;
		hmp->vchain.bref.mirror_tid = hmp->voldata.mirror_tid;
		hmp->vchain.bref.modify_tid = hmp->vchain.bref.mirror_tid;
		hmp->vchain.pmp = spmp;
		hmp->fchain.bref.mirror_tid = hmp->voldata.freemap_tid;
		hmp->fchain.bref.modify_tid = hmp->fchain.bref.mirror_tid;
		hmp->fchain.pmp = spmp;

		/*
		 * First locate the super-root inode, which is key 0
		 * relative to the volume header's blockset.
		 *
		 * Then locate the root inode by scanning the directory keyspace
		 * represented by the label.
		 */
		parent = hammer2_chain_lookup_init(&hmp->vchain, 0);
		schain = hammer2_chain_lookup(&parent, &key_dummy,
				      HAMMER2_SROOT_KEY, HAMMER2_SROOT_KEY,
				      &cache_index, 0);
		hammer2_chain_lookup_done(parent);
		if (schain == NULL) {
			kprintf("hammer2_mount: invalid super-root\n");
			hammer2_unmount_helper(mp, NULL, hmp);
			lockmgr(&hammer2_mntlk, LK_RELEASE);
			hammer2_vfs_unmount(mp, MNT_FORCE);
			return EINVAL;
		}
		if (schain->error) {
			kprintf("hammer2_mount: error %s reading super-root\n",
				hammer2_error_str(schain->error));
			hammer2_chain_unlock(schain);
			hammer2_chain_drop(schain);
			schain = NULL;
			hammer2_unmount_helper(mp, NULL, hmp);
			lockmgr(&hammer2_mntlk, LK_RELEASE);
			hammer2_vfs_unmount(mp, MNT_FORCE);
			return EINVAL;
		}

		/*
		 * The super-root always uses an inode_tid of 1 when
		 * creating PFSs.
		 */
		spmp->inode_tid = 1;
		spmp->modify_tid = schain->bref.modify_tid;

		/*
		 * Sanity-check schain's pmp and finish initialization.
		 * Any chain belonging to the super-root topology should
		 * have a NULL pmp (not even set to spmp).
		 */
		ripdata = &hammer2_chain_rdata(schain)->ipdata;
		KKASSERT(schain->pmp == NULL);
		spmp->pfs_clid = ripdata->meta.pfs_clid;

		/*
		 * Replace the dummy spmp->iroot with a real one.  It's
		 * easier to just do a wholesale replacement than to try
		 * to update the chain and fixup the iroot fields.
		 *
		 * The returned inode is locked with the supplied cluster.
		 */
		cluster = hammer2_cluster_from_chain(schain);
		hammer2_inode_drop(spmp->iroot);
		spmp->iroot = NULL;
		spmp->iroot = hammer2_inode_get(spmp, NULL, cluster);
		spmp->spmp_hmp = hmp;
		spmp->pfs_types[0] = ripdata->meta.pfs_type;
		hammer2_inode_ref(spmp->iroot);
		hammer2_inode_unlock(spmp->iroot, cluster);
		schain = NULL;
		/* leave spmp->iroot with one ref */

		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			error = hammer2_recovery(hmp);
			/* XXX do something with error */
		}
		hammer2_update_pmps(hmp);
		hammer2_iocom_init(hmp);

		/*
		 * Ref the cluster management messaging descriptor.  The mount
		 * program deals with the other end of the communications pipe.
		 */
		fp = holdfp(curproc->p_fd, info.cluster_fd, -1);
		if (fp) {
			hammer2_cluster_reconnect(hmp, fp);
		} else {
			kprintf("hammer2_mount: bad cluster_fd!\n");
		}
	} else {
		spmp = hmp->spmp;
	}

	/*
	 * Lookup the mount point under the media-localized super-root.
	 * Scanning hammer2_pfslist doesn't help us because it represents
	 * PFS cluster ids which can aggregate several named PFSs together.
	 *
	 * cluster->pmp will incorrectly point to spmp and must be fixed
	 * up later on.
	 */
	hammer2_inode_lock(spmp->iroot, HAMMER2_RESOLVE_ALWAYS);
	cparent = hammer2_inode_cluster(spmp->iroot, HAMMER2_RESOLVE_ALWAYS);
	lhc = hammer2_dirhash(label, strlen(label));
	cluster = hammer2_cluster_lookup(cparent, &key_next,
				      lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				      0);
	while (cluster) {
		if (hammer2_cluster_type(cluster) == HAMMER2_BREF_TYPE_INODE &&
		    strcmp(label,
		       hammer2_cluster_rdata(cluster)->ipdata.filename) == 0) {
			break;
		}
		cluster = hammer2_cluster_next(cparent, cluster, &key_next,
					    key_next,
					    lhc + HAMMER2_DIRHASH_LOMASK, 0);
	}
	hammer2_inode_unlock(spmp->iroot, cparent);

	/*
	 * PFS could not be found?
	 */
	if (cluster == NULL) {
		kprintf("hammer2_mount: PFS label not found\n");
		hammer2_unmount_helper(mp, NULL, hmp);
		lockmgr(&hammer2_mntlk, LK_RELEASE);
		hammer2_vfs_unmount(mp, MNT_FORCE);

		return EINVAL;
	}

	/*
	 * Acquire the pmp structure (it should have already been allocated
	 * via hammer2_update_pmps() so do not pass cluster in to add to
	 * available chains).
	 *
	 * Check if the cluster has already been mounted.  A cluster can
	 * only be mounted once, use null mounts to mount additional copies.
	 */
	ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
	hammer2_cluster_bref(cluster, &bref);
	pmp = hammer2_pfsalloc(NULL, ripdata, bref.modify_tid);
	hammer2_cluster_unlock(cluster);
	hammer2_cluster_drop(cluster);

	if (pmp->mp) {
		kprintf("hammer2_mount: PFS already mounted!\n");
		hammer2_unmount_helper(mp, NULL, hmp);
		lockmgr(&hammer2_mntlk, LK_RELEASE);
		hammer2_vfs_unmount(mp, MNT_FORCE);

		return EBUSY;
	}

	/*
	 * Finish the mount
	 */
        kprintf("hammer2_mount hmp=%p pmp=%p\n", hmp, pmp);

        mp->mnt_flag = MNT_LOCAL;
        mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;   /* all entry pts are SMP */
        mp->mnt_kern_flag |= MNTK_THR_SYNC;     /* new vsyncscan semantics */
 
        /*
         * required mount structure initializations
         */
        mp->mnt_stat.f_iosize = HAMMER2_PBUFSIZE;
        mp->mnt_stat.f_bsize = HAMMER2_PBUFSIZE;
 
        mp->mnt_vstat.f_frsize = HAMMER2_PBUFSIZE;
        mp->mnt_vstat.f_bsize = HAMMER2_PBUFSIZE;
 
        /*
         * Optional fields
         */
        mp->mnt_iosize_max = MAXPHYS;

	/*
	 * Connect up mount pointers.
	 */
	hammer2_mount_helper(mp, pmp);

        lockmgr(&hammer2_mntlk, LK_RELEASE);

	/*
	 * A mounted PFS needs a write thread for logical buffers and
	 * a hidden directory for deletions of open files.  These features
	 * are not used by unmounted PFSs.
	 *
	 * The logical file buffer bio write thread handles things like
	 * physical block assignment and compression.
	 */
	pmp->wthread_destroy = 0;
	lwkt_create(hammer2_write_thread, pmp,
		    &pmp->wthread_td, NULL, 0, -1, "h2pfs-%s", label);

	/*
	 * With the cluster operational install ihidden.
	 * (only applicable to pfs mounts, not applicable to spmp)
	 */
	hammer2_inode_install_hidden(pmp);

	/*
	 * Finish setup
	 */
	vfs_getnewfsid(mp);
	vfs_add_vnodeops(mp, &hammer2_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &hammer2_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &hammer2_fifo_vops, &mp->mnt_vn_fifo_ops);

	copyinstr(info.volume, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	bzero(mp->mnt_stat.f_mntonname, sizeof(mp->mnt_stat.f_mntonname));
	copyinstr(path, mp->mnt_stat.f_mntonname,
		  sizeof(mp->mnt_stat.f_mntonname) - 1,
		  &size);

	/*
	 * Initial statfs to prime mnt_stat.
	 */
	hammer2_vfs_statfs(mp, &mp->mnt_stat, cred);
	
	return 0;
}

/*
 * Scan PFSs under the super-root and create hammer2_pfs structures.
 */
static
void
hammer2_update_pmps(hammer2_dev_t *hmp)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	hammer2_blockref_t bref;
	hammer2_pfs_t *spmp;
	hammer2_pfs_t *pmp;
	hammer2_key_t key_next;

	/*
	 * Lookup mount point under the media-localized super-root.
	 *
	 * cluster->pmp will incorrectly point to spmp and must be fixed
	 * up later on.
	 */
	spmp = hmp->spmp;
	hammer2_inode_lock(spmp->iroot, HAMMER2_RESOLVE_ALWAYS);
	cparent = hammer2_inode_cluster(spmp->iroot, HAMMER2_RESOLVE_ALWAYS);
	cluster = hammer2_cluster_lookup(cparent, &key_next,
					 HAMMER2_KEY_MIN,
					 HAMMER2_KEY_MAX,
					 0);
	while (cluster) {
		if (hammer2_cluster_type(cluster) != HAMMER2_BREF_TYPE_INODE)
			continue;
		ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
		hammer2_cluster_bref(cluster, &bref);
		kprintf("ADD LOCAL PFS: %s\n", ripdata->filename);

		pmp = hammer2_pfsalloc(cluster, ripdata, bref.modify_tid);
		cluster = hammer2_cluster_next(cparent, cluster,
					       &key_next,
					       key_next,
					       HAMMER2_KEY_MAX,
					       0);
	}
	hammer2_inode_unlock(spmp->iroot, cparent);
}

static
int
hammer2_remount(hammer2_dev_t *hmp, struct mount *mp, char *path,
		struct vnode *devvp, struct ucred *cred)
{
	int error;

	if (hmp->ronly && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
		error = hammer2_recovery(hmp);
	} else {
		error = 0;
	}
	return error;
}

static
int
hammer2_vfs_unmount(struct mount *mp, int mntflags)
{
	hammer2_pfs_t *pmp;
	int flags;
	int error = 0;

	pmp = MPTOPMP(mp);

	if (pmp == NULL)
		return(0);

	lockmgr(&hammer2_mntlk, LK_EXCLUSIVE);

	/*
	 * If mount initialization proceeded far enough we must flush
	 * its vnodes and sync the underlying mount points.  Three syncs
	 * are required to fully flush the filesystem (freemap updates lag
	 * by one flush, and one extra for safety).
	 */
	if (mntflags & MNT_FORCE)
		flags = FORCECLOSE;
	else
		flags = 0;
	if (pmp->iroot) {
		error = vflush(mp, 0, flags);
		if (error)
			goto failed;
		hammer2_vfs_sync(mp, MNT_WAIT);
		hammer2_vfs_sync(mp, MNT_WAIT);
		hammer2_vfs_sync(mp, MNT_WAIT);
	}

	if (pmp->wthread_td) {
		hammer2_mtx_ex(&pmp->wthread_mtx);
		pmp->wthread_destroy = 1;
		wakeup(&pmp->wthread_bioq);
		while (pmp->wthread_destroy != -1) {
			mtxsleep(&pmp->wthread_destroy,
				&pmp->wthread_mtx, 0,
				"umount-sleep",	0);
		}
		hammer2_mtx_unlock(&pmp->wthread_mtx);
		pmp->wthread_td = NULL;
	}

	/*
	 * Cleanup our reference on ihidden.
	 */
	if (pmp->ihidden) {
		hammer2_inode_drop(pmp->ihidden);
		pmp->ihidden = NULL;
	}
	if (pmp->mp)
		hammer2_unmount_helper(mp, pmp, NULL);

	error = 0;
failed:
	lockmgr(&hammer2_mntlk, LK_RELEASE);

	return (error);
}

/*
 * Mount helper, hook the system mount into our PFS.
 * The mount lock is held.
 *
 * We must bump the mount_count on related devices for any
 * mounted PFSs.
 */
static
void
hammer2_mount_helper(struct mount *mp, hammer2_pfs_t *pmp)
{
	hammer2_cluster_t *cluster;
	hammer2_chain_t *rchain;
	int i;

        mp->mnt_data = (qaddr_t)pmp;
	pmp->mp = mp;

	/*
	 * After pmp->mp is set we have to adjust hmp->mount_count.
	 */
	cluster = &pmp->iroot->cluster;
	for (i = 0; i < cluster->nchains; ++i) {
		rchain = cluster->array[i].chain;
		if (rchain == NULL)
			continue;
		++rchain->hmp->mount_count;
		kprintf("hammer2_mount hmp=%p ++mount_count=%d\n",
			rchain->hmp, rchain->hmp->mount_count);
	}
}

/*
 * Mount helper, unhook the system mount from our PFS.
 * The mount lock is held.
 *
 * If hmp is supplied a mount responsible for being the first to open
 * the block device failed and the block device and all PFSs using the
 * block device must be cleaned up.
 *
 * If pmp is supplied multiple devices might be backing the PFS and each
 * must be disconnect.  This might not be the last PFS using some of the
 * underlying devices.  Also, we have to adjust our hmp->mount_count
 * accounting for the devices backing the pmp which is now undergoing an
 * unmount.
 */
static
void
hammer2_unmount_helper(struct mount *mp, hammer2_pfs_t *pmp, hammer2_dev_t *hmp)
{
	hammer2_cluster_t *cluster;
	hammer2_chain_t *rchain;
	struct vnode *devvp;
	int dumpcnt;
	int ronly = 0;
	int i;

	/*
	 * If no device supplied this is a high-level unmount and we have to
	 * to disconnect the mount, adjust mount_count, and locate devices
	 * that might now have no mounts.
	 */
	if (pmp) {
		KKASSERT(hmp == NULL);
		KKASSERT((void *)(intptr_t)mp->mnt_data == pmp);
		pmp->mp = NULL;
		mp->mnt_data = NULL;

		/*
		 * After pmp->mp is cleared we have to account for
		 * mount_count.
		 */
		cluster = &pmp->iroot->cluster;
		for (i = 0; i < cluster->nchains; ++i) {
			rchain = cluster->array[i].chain;
			if (rchain == NULL)
				continue;
			--rchain->hmp->mount_count;
			kprintf("hammer2_unmount hmp=%p --mount_count=%d\n",
				rchain->hmp, rchain->hmp->mount_count);
			/* scrapping hmp now may invalidate the pmp */
		}
again:
		TAILQ_FOREACH(hmp, &hammer2_mntlist, mntentry) {
			if (hmp->mount_count == 0) {
				hammer2_unmount_helper(NULL, NULL, hmp);
				goto again;
			}
		}
		return;
	}

	/*
	 * Try to terminate the block device.  We can't terminate it if
	 * there are still PFSs referencing it.
	 */
	kprintf("hammer2_unmount hmp=%p mount_count=%d\n",
		hmp, hmp->mount_count);
	if (hmp->mount_count)
		return;

	hammer2_pfsfree_scan(hmp);
	hammer2_dev_exlock(hmp);	/* XXX order */

	/*
	 * Cycle the volume data lock as a safety (probably not needed any
	 * more).  To ensure everything is out we need to flush at least
	 * three times.  (1) The running of the unlinkq can dirty the
	 * filesystem, (2) A normal flush can dirty the freemap, and
	 * (3) ensure that the freemap is fully synchronized.
	 *
	 * The next mount's recovery scan can clean everything up but we want
	 * to leave the filesystem in a 100% clean state on a normal unmount.
	 */
#if 0
	hammer2_voldata_lock(hmp);
	hammer2_voldata_unlock(hmp);
#endif
	hammer2_iocom_uninit(hmp);

	if ((hmp->vchain.flags | hmp->fchain.flags) &
	    HAMMER2_CHAIN_FLUSH_MASK) {
		kprintf("hammer2_unmount: chains left over "
			"after final sync\n");
		kprintf("    vchain %08x\n", hmp->vchain.flags);
		kprintf("    fchain %08x\n", hmp->fchain.flags);

		if (hammer2_debug & 0x0010)
			Debugger("entered debugger");
	}

	KKASSERT(hmp->spmp == NULL);

	/*
	 * Finish up with the device vnode
	 */
	if ((devvp = hmp->devvp) != NULL) {
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		vinvalbuf(devvp, (ronly ? 0 : V_SAVE), 0, 0);
		hmp->devvp = NULL;
		VOP_CLOSE(devvp, (ronly ? FREAD : FREAD|FWRITE), NULL);
		vn_unlock(devvp);
		vrele(devvp);
		devvp = NULL;
	}

	/*
	 * Clear vchain/fchain flags that might prevent final cleanup
	 * of these chains.
	 */
	if (hmp->vchain.flags & HAMMER2_CHAIN_MODIFIED) {
		atomic_clear_int(&hmp->vchain.flags,
				 HAMMER2_CHAIN_MODIFIED);
		hammer2_pfs_memory_wakeup(hmp->vchain.pmp);
		hammer2_chain_drop(&hmp->vchain);
	}
	if (hmp->vchain.flags & HAMMER2_CHAIN_UPDATE) {
		atomic_clear_int(&hmp->vchain.flags,
				 HAMMER2_CHAIN_UPDATE);
		hammer2_chain_drop(&hmp->vchain);
	}

	if (hmp->fchain.flags & HAMMER2_CHAIN_MODIFIED) {
		atomic_clear_int(&hmp->fchain.flags,
				 HAMMER2_CHAIN_MODIFIED);
		hammer2_pfs_memory_wakeup(hmp->fchain.pmp);
		hammer2_chain_drop(&hmp->fchain);
	}
	if (hmp->fchain.flags & HAMMER2_CHAIN_UPDATE) {
		atomic_clear_int(&hmp->fchain.flags,
				 HAMMER2_CHAIN_UPDATE);
		hammer2_chain_drop(&hmp->fchain);
	}

	/*
	 * Final drop of embedded freemap root chain to
	 * clean up fchain.core (fchain structure is not
	 * flagged ALLOCATED so it is cleaned out and then
	 * left to rot).
	 */
	hammer2_chain_drop(&hmp->fchain);

	/*
	 * Final drop of embedded volume root chain to clean
	 * up vchain.core (vchain structure is not flagged
	 * ALLOCATED so it is cleaned out and then left to
	 * rot).
	 */
	dumpcnt = 50;
	hammer2_dump_chain(&hmp->vchain, 0, &dumpcnt, 'v');
	dumpcnt = 50;
	hammer2_dump_chain(&hmp->fchain, 0, &dumpcnt, 'f');
	hammer2_dev_unlock(hmp);
	hammer2_chain_drop(&hmp->vchain);

	hammer2_io_cleanup(hmp, &hmp->iotree);
	if (hmp->iofree_count) {
		kprintf("io_cleanup: %d I/O's left hanging\n",
			hmp->iofree_count);
	}

	TAILQ_REMOVE(&hammer2_mntlist, hmp, mntentry);
	kmalloc_destroy(&hmp->mchain);
	kfree(hmp, M_HAMMER2);
}

static
int
hammer2_vfs_vget(struct mount *mp, struct vnode *dvp,
	     ino_t ino, struct vnode **vpp)
{
	kprintf("hammer2_vget\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vfs_root(struct mount *mp, struct vnode **vpp)
{
	hammer2_pfs_t *pmp;
	hammer2_cluster_t *cparent;
	int error;
	struct vnode *vp;

	pmp = MPTOPMP(mp);
	if (pmp->iroot == NULL) {
		*vpp = NULL;
		error = EINVAL;
	} else {
		hammer2_inode_lock(pmp->iroot, HAMMER2_RESOLVE_ALWAYS |
					       HAMMER2_RESOLVE_SHARED);
		cparent = hammer2_inode_cluster(pmp->iroot,
						HAMMER2_RESOLVE_ALWAYS |
					        HAMMER2_RESOLVE_SHARED);

		/*
		 * Initialize pmp->inode_tid and pmp->modify_tid on first access
		 * to the root of mount that resolves good.
		 * XXX probably not the best place for this.
		 */
		if (pmp->inode_tid == 0 &&
		    cparent->error == 0 && cparent->focus) {
			const hammer2_inode_data_t *ripdata;
			hammer2_blockref_t bref;

			ripdata = &hammer2_cluster_rdata(cparent)->ipdata;
			hammer2_cluster_bref(cparent, &bref);
			pmp->inode_tid = ripdata->meta.pfs_inum + 1;
			pmp->modify_tid = bref.modify_tid;
			pmp->iroot->meta = ripdata->meta;
			hammer2_cluster_bref(cparent, &pmp->iroot->bref);
			kprintf("PMP focus good set nextino=%ld mod=%016jx\n",
				pmp->inode_tid, pmp->modify_tid);
		}

		vp = hammer2_igetv(pmp->iroot, cparent, &error);
		hammer2_inode_unlock(pmp->iroot, cparent);
		*vpp = vp;
		if (vp == NULL)
			kprintf("vnodefail\n");
	}

	return (error);
}

/*
 * Filesystem status
 *
 * XXX incorporate ipdata->meta.inode_quota and data_quota
 */
static
int
hammer2_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	hammer2_pfs_t *pmp;
	hammer2_dev_t *hmp;
	hammer2_blockref_t bref;

	pmp = MPTOPMP(mp);
	KKASSERT(pmp->iroot->cluster.nchains >= 1);
	hmp = pmp->iroot->cluster.focus->hmp;	/* iroot retains focus */
	bref = pmp->iroot->cluster.focus->bref;	/* no lock */

	mp->mnt_stat.f_files = bref.inode_count;
	mp->mnt_stat.f_ffree = 0;
	mp->mnt_stat.f_blocks = (bref.data_count +
				 hmp->voldata.allocator_free) /
				mp->mnt_vstat.f_bsize;
	mp->mnt_stat.f_bfree =  hmp->voldata.allocator_free /
				mp->mnt_vstat.f_bsize;
	mp->mnt_stat.f_bavail = mp->mnt_stat.f_bfree;

	*sbp = mp->mnt_stat;
	return (0);
}

static
int
hammer2_vfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	hammer2_pfs_t *pmp;
	hammer2_dev_t *hmp;
	hammer2_blockref_t bref;

	pmp = MPTOPMP(mp);
	KKASSERT(pmp->iroot->cluster.nchains >= 1);
	hmp = pmp->iroot->cluster.focus->hmp;	/* iroot retains focus */
	bref = pmp->iroot->cluster.focus->bref;	/* no lock */

	mp->mnt_vstat.f_bsize = HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_files = bref.inode_count;
	mp->mnt_vstat.f_ffree = 0;
	mp->mnt_vstat.f_blocks = (bref.data_count +
				 hmp->voldata.allocator_free) /
				mp->mnt_vstat.f_bsize;
	mp->mnt_vstat.f_bfree = hmp->voldata.allocator_free /
				mp->mnt_vstat.f_bsize;
	mp->mnt_vstat.f_bavail = mp->mnt_vstat.f_bfree;

	*sbp = mp->mnt_vstat;
	return (0);
}

/*
 * Mount-time recovery (RW mounts)
 *
 * Updates to the free block table are allowed to lag flushes by one
 * transaction.  In case of a crash, then on a fresh mount we must do an
 * incremental scan of the last committed transaction id and make sure that
 * all related blocks have been marked allocated.
 *
 * The super-root topology and each PFS has its own transaction id domain,
 * so we must track PFS boundary transitions.
 */
struct hammer2_recovery_elm {
	TAILQ_ENTRY(hammer2_recovery_elm) entry;
	hammer2_chain_t *chain;
	hammer2_tid_t sync_tid;
};

TAILQ_HEAD(hammer2_recovery_list, hammer2_recovery_elm);

struct hammer2_recovery_info {
	struct hammer2_recovery_list list;
	int	depth;
};

static int hammer2_recovery_scan(hammer2_trans_t *trans, hammer2_dev_t *hmp,
			hammer2_chain_t *parent,
			struct hammer2_recovery_info *info,
			hammer2_tid_t sync_tid);

#define HAMMER2_RECOVERY_MAXDEPTH	10

static
int
hammer2_recovery(hammer2_dev_t *hmp)
{
	hammer2_trans_t trans;
	struct hammer2_recovery_info info;
	struct hammer2_recovery_elm *elm;
	hammer2_chain_t *parent;
	hammer2_tid_t sync_tid;
	hammer2_tid_t mirror_tid;
	int error;
	int cumulative_error = 0;

	hammer2_trans_init(&trans, hmp->spmp, 0);

	sync_tid = hmp->voldata.freemap_tid;
	mirror_tid = hmp->voldata.mirror_tid;

	kprintf("hammer2 mount \"%s\": ", hmp->devrepname);
	if (sync_tid >= mirror_tid) {
		kprintf(" no recovery needed\n");
	} else {
		kprintf(" freemap recovery %016jx-%016jx\n",
			sync_tid + 1, mirror_tid);
	}

	TAILQ_INIT(&info.list);
	info.depth = 0;
	parent = hammer2_chain_lookup_init(&hmp->vchain, 0);
	cumulative_error = hammer2_recovery_scan(&trans, hmp, parent,
						 &info, sync_tid);
	hammer2_chain_lookup_done(parent);

	while ((elm = TAILQ_FIRST(&info.list)) != NULL) {
		TAILQ_REMOVE(&info.list, elm, entry);
		parent = elm->chain;
		sync_tid = elm->sync_tid;
		kfree(elm, M_HAMMER2);

		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
		error = hammer2_recovery_scan(&trans, hmp, parent,
					      &info,
					      hmp->voldata.freemap_tid);
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);	/* drop elm->chain ref */
		if (error)
			cumulative_error = error;
	}
	hammer2_trans_done(&trans);

	return cumulative_error;
}

static
int
hammer2_recovery_scan(hammer2_trans_t *trans, hammer2_dev_t *hmp,
		      hammer2_chain_t *parent,
		      struct hammer2_recovery_info *info,
		      hammer2_tid_t sync_tid)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_chain_t *chain;
	int cache_index;
	int cumulative_error = 0;
	int error;

	/*
	 * Adjust freemap to ensure that the block(s) are marked allocated.
	 */
	if (parent->bref.type != HAMMER2_BREF_TYPE_VOLUME) {
		hammer2_freemap_adjust(trans, hmp, &parent->bref,
				       HAMMER2_FREEMAP_DORECOVER);
	}

	/*
	 * Check type for recursive scan
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		/* data already instantiated */
		break;
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Must instantiate data for DIRECTDATA test and also
		 * for recursion.
		 */
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
		ripdata = &hammer2_chain_rdata(parent)->ipdata;
		if (ripdata->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
			/* not applicable to recovery scan */
			hammer2_chain_unlock(parent);
			return 0;
		}
		hammer2_chain_unlock(parent);
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		/*
		 * Must instantiate data for recursion
		 */
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_unlock(parent);
		break;
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		/* not applicable to recovery scan */
		return 0;
		break;
	default:
		return EDOM;
	}

	/*
	 * Defer operation if depth limit reached or if we are crossing a
	 * PFS boundary.
	 */
	if (info->depth >= HAMMER2_RECOVERY_MAXDEPTH) {
		struct hammer2_recovery_elm *elm;

		elm = kmalloc(sizeof(*elm), M_HAMMER2, M_ZERO | M_WAITOK);
		elm->chain = parent;
		elm->sync_tid = sync_tid;
		hammer2_chain_ref(parent);
		TAILQ_INSERT_TAIL(&info->list, elm, entry);
		/* unlocked by caller */

		return(0);
	}


	/*
	 * Recursive scan of the last flushed transaction only.  We are
	 * doing this without pmp assignments so don't leave the chains
	 * hanging around after we are done with them.
	 */
	cache_index = 0;
	chain = hammer2_chain_scan(parent, NULL, &cache_index,
				   HAMMER2_LOOKUP_NODATA);
	while (chain) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_RELEASE);
		if (chain->bref.mirror_tid > sync_tid) {
			++info->depth;
			error = hammer2_recovery_scan(trans, hmp, chain,
						      info, sync_tid);
			--info->depth;
			if (error)
				cumulative_error = error;
		}

		/*
		 * Flush the recovery at the PFS boundary to stage it for
		 * the final flush of the super-root topology.
		 */
		if ((chain->bref.flags & HAMMER2_BREF_FLAG_PFSROOT) &&
		    (chain->flags & HAMMER2_CHAIN_ONFLUSH)) {
			hammer2_flush(trans, chain, 1);
		}
		chain = hammer2_chain_scan(parent, chain, &cache_index,
					   HAMMER2_LOOKUP_NODATA);
	}

	return cumulative_error;
}

/*
 * Sync a mount point; this is called on a per-mount basis from the
 * filesystem syncer process periodically and whenever a user issues
 * a sync.
 */
int
hammer2_vfs_sync(struct mount *mp, int waitfor)
{
	struct hammer2_sync_info info;
	hammer2_inode_t *iroot;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_pfs_t *pmp;
	hammer2_dev_t *hmp;
	int flags;
	int error;
	int total_error;
	int i;
	int j;

	pmp = MPTOPMP(mp);
	iroot = pmp->iroot;
	KKASSERT(iroot);
	KKASSERT(iroot->pmp == pmp);

	/*
	 * We can't acquire locks on existing vnodes while in a transaction
	 * without risking a deadlock.  This assumes that vfsync() can be
	 * called without the vnode locked (which it can in DragonFly).
	 * Otherwise we'd have to implement a multi-pass or flag the lock
	 * failures and retry.
	 *
	 * The reclamation code interlocks with the sync list's token
	 * (by removing the vnode from the scan list) before unlocking
	 * the inode, giving us time to ref the inode.
	 */
	/*flags = VMSC_GETVP;*/
	flags = 0;
	if (waitfor & MNT_LAZY)
		flags |= VMSC_ONEPASS;

#if 0
	/*
	 * Preflush the vnodes using a normal transaction before interlocking
	 * with a flush transaction.
	 */
	hammer2_trans_init(&info.trans, pmp, 0);
	info.error = 0;
	info.waitfor = MNT_NOWAIT;
	vsyncscan(mp, flags | VMSC_NOWAIT, hammer2_sync_scan2, &info);
	hammer2_trans_done(&info.trans);
#endif

	/*
	 * Start our flush transaction.  This does not return until all
	 * concurrent transactions have completed and will prevent any
	 * new transactions from running concurrently, except for the
	 * buffer cache transactions.
	 *
	 * For efficiency do an async pass before making sure with a
	 * synchronous pass on all related buffer cache buffers.  It
	 * should theoretically not be possible for any new file buffers
	 * to be instantiated during this sequence.
	 */
	hammer2_trans_init(&info.trans, pmp, HAMMER2_TRANS_ISFLUSH |
					     HAMMER2_TRANS_PREFLUSH);
	hammer2_run_unlinkq(&info.trans, pmp);

	info.error = 0;
	info.waitfor = MNT_NOWAIT;
	vsyncscan(mp, flags | VMSC_NOWAIT, hammer2_sync_scan2, &info);
	info.waitfor = MNT_WAIT;
	vsyncscan(mp, flags, hammer2_sync_scan2, &info);

	/*
	 * Clear PREFLUSH.  This prevents (or asserts on) any new logical
	 * buffer cache flushes which occur during the flush.  Device buffers
	 * are not affected.
	 */
	hammer2_bioq_sync(info.trans.pmp);
	atomic_clear_int(&info.trans.flags, HAMMER2_TRANS_PREFLUSH);

	total_error = 0;

	/*
	 * Flush all nodes to synchronize the PFSROOT subtopology to the media.
	 *
	 * Note that this flush will not be visible on crash recovery until
	 * we flush the super-root topology in the next loop.
	 */
	for (i = 0; iroot && i < iroot->cluster.nchains; ++i) {
		chain = iroot->cluster.array[i].chain;
		if (chain == NULL)
			continue;

		hammer2_chain_ref(chain);
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
		if (chain->flags & HAMMER2_CHAIN_FLUSH_MASK) {
			hammer2_flush(&info.trans, chain, 1);
			parent = chain->parent;
			KKASSERT(chain->pmp != parent->pmp);
			hammer2_chain_setflush(&info.trans, parent);
		}
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	hammer2_trans_done(&info.trans);

	/*
	 * Flush all volume roots to synchronize PFS flushes with the
	 * storage media volume header.  This will flush the freemap and
	 * the superroot topology but stops when it reaches a PFSROOT
	 * (which we already flushed above).
	 *
	 * This is the last step which connects the volume root to the
	 * PFSROOT dirs flushed above.
	 *
	 * Each spmp (representing the hmp's super-root) requires its own
	 * transaction.
	 */
	for (i = 0; iroot && i < iroot->cluster.nchains; ++i) {
		hammer2_chain_t *tmp;

		chain = iroot->cluster.array[i].chain;
		if (chain == NULL)
			continue;

		hmp = chain->hmp;

		/*
		 * We only have to flush each hmp once
		 */
		for (j = i - 1; j >= 0; --j) {
			if ((tmp = iroot->cluster.array[j].chain) != NULL) {
				if (tmp->hmp == hmp)
					break;
			}
		}
		if (j >= 0)
			continue;

		/*
		 * spmp transaction.  The super-root is never directly
		 * mounted so there shouldn't be any vnodes, let alone any
		 * dirty vnodes associated with it.
		 */
		hammer2_trans_init(&info.trans, hmp->spmp,
				   HAMMER2_TRANS_ISFLUSH);

		/*
		 * Media mounts have two 'roots', vchain for the topology
		 * and fchain for the free block table.  Flush both.
		 *
		 * Note that the topology and free block table are handled
		 * independently, so the free block table can wind up being
		 * ahead of the topology.  We depend on the bulk free scan
		 * code to deal with any loose ends.
		 */
		hammer2_chain_ref(&hmp->vchain);
		hammer2_chain_lock(&hmp->vchain, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_ref(&hmp->fchain);
		hammer2_chain_lock(&hmp->fchain, HAMMER2_RESOLVE_ALWAYS);
		if (hmp->fchain.flags & HAMMER2_CHAIN_FLUSH_MASK) {
			/*
			 * This will also modify vchain as a side effect,
			 * mark vchain as modified now.
			 */
			hammer2_voldata_modify(hmp);
			chain = &hmp->fchain;
			hammer2_flush(&info.trans, chain, 1);
			KKASSERT(chain == &hmp->fchain);
		}
		hammer2_chain_unlock(&hmp->fchain);
		hammer2_chain_unlock(&hmp->vchain);
		hammer2_chain_drop(&hmp->fchain);
		/* vchain dropped down below */

		hammer2_chain_lock(&hmp->vchain, HAMMER2_RESOLVE_ALWAYS);
		if (hmp->vchain.flags & HAMMER2_CHAIN_FLUSH_MASK) {
			chain = &hmp->vchain;
			hammer2_flush(&info.trans, chain, 1);
			KKASSERT(chain == &hmp->vchain);
		}
		hammer2_chain_unlock(&hmp->vchain);
		hammer2_chain_drop(&hmp->vchain);

		error = 0;

		/*
		 * We can't safely flush the volume header until we have
		 * flushed any device buffers which have built up.
		 *
		 * XXX this isn't being incremental
		 */
		vn_lock(hmp->devvp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_FSYNC(hmp->devvp, MNT_WAIT, 0);
		vn_unlock(hmp->devvp);

		/*
		 * The flush code sets CHAIN_VOLUMESYNC to indicate that the
		 * volume header needs synchronization via hmp->volsync.
		 *
		 * XXX synchronize the flag & data with only this flush XXX
		 */
		if (error == 0 &&
		    (hmp->vchain.flags & HAMMER2_CHAIN_VOLUMESYNC)) {
			struct buf *bp;

			/*
			 * Synchronize the disk before flushing the volume
			 * header.
			 */
			bp = getpbuf(NULL);
			bp->b_bio1.bio_offset = 0;
			bp->b_bufsize = 0;
			bp->b_bcount = 0;
			bp->b_cmd = BUF_CMD_FLUSH;
			bp->b_bio1.bio_done = biodone_sync;
			bp->b_bio1.bio_flags |= BIO_SYNC;
			vn_strategy(hmp->devvp, &bp->b_bio1);
			biowait(&bp->b_bio1, "h2vol");
			relpbuf(bp, NULL);

			/*
			 * Then we can safely flush the version of the
			 * volume header synchronized by the flush code.
			 */
			i = hmp->volhdrno + 1;
			if (i >= HAMMER2_NUM_VOLHDRS)
				i = 0;
			if (i * HAMMER2_ZONE_BYTES64 + HAMMER2_SEGSIZE >
			    hmp->volsync.volu_size) {
				i = 0;
			}
			kprintf("sync volhdr %d %jd\n",
				i, (intmax_t)hmp->volsync.volu_size);
			bp = getblk(hmp->devvp, i * HAMMER2_ZONE_BYTES64,
				    HAMMER2_PBUFSIZE, 0, 0);
			atomic_clear_int(&hmp->vchain.flags,
					 HAMMER2_CHAIN_VOLUMESYNC);
			bcopy(&hmp->volsync, bp->b_data, HAMMER2_PBUFSIZE);
			bawrite(bp);
			hmp->volhdrno = i;
		}
		if (error)
			total_error = error;

		hammer2_trans_done(&info.trans);	/* spmp trans */
	}
	return (total_error);
}

/*
 * Sync passes.
 */
static int
hammer2_sync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	struct hammer2_sync_info *info = data;
	hammer2_inode_t *ip;
	int error;

	/*
	 * Degenerate cases.  Note that ip == NULL typically means the
	 * syncer vnode itself and we don't want to vclrisdirty() in that
	 * situation.
	 */
	ip = VTOI(vp);
	if (ip == NULL) {
		return(0);
	}
	if (vp->v_type == VNON || vp->v_type == VBAD) {
		vclrisdirty(vp);
		return(0);
	}

	/*
	 * VOP_FSYNC will start a new transaction so replicate some code
	 * here to do it inline (see hammer2_vop_fsync()).
	 *
	 * WARNING: The vfsync interacts with the buffer cache and might
	 *          block, we can't hold the inode lock at that time.
	 *	    However, we MUST ref ip before blocking to ensure that
	 *	    it isn't ripped out from under us (since we do not
	 *	    hold a lock on the vnode).
	 */
	hammer2_inode_ref(ip);
	if ((ip->flags & HAMMER2_INODE_MODIFIED) ||
	    !RB_EMPTY(&vp->v_rbdirty_tree)) {
		vfsync(vp, info->waitfor, 1, NULL, NULL);
		hammer2_inode_fsync(&info->trans, ip, NULL);
	}
	if ((ip->flags & HAMMER2_INODE_MODIFIED) == 0 &&
	    RB_EMPTY(&vp->v_rbdirty_tree)) {
		vclrisdirty(vp);
	}

	hammer2_inode_drop(ip);
#if 1
	error = 0;
	if (error)
		info->error = error;
#endif
	return(0);
}

static
int
hammer2_vfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	return (0);
}

static
int
hammer2_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
	       struct fid *fhp, struct vnode **vpp)
{
	return (0);
}

static
int
hammer2_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
		 int *exflagsp, struct ucred **credanonp)
{
	return (0);
}

/*
 * Support code for hammer2_vfs_mount().  Read, verify, and install the volume
 * header into the HMP
 *
 * XXX read four volhdrs and use the one with the highest TID whos CRC
 *     matches.
 *
 * XXX check iCRCs.
 *
 * XXX For filesystems w/ less than 4 volhdrs, make sure to not write to
 *     nonexistant locations.
 *
 * XXX Record selected volhdr and ring updates to each of 4 volhdrs
 */
static
int
hammer2_install_volume_header(hammer2_dev_t *hmp)
{
	hammer2_volume_data_t *vd;
	struct buf *bp;
	hammer2_crc32_t crc0, crc, bcrc0, bcrc;
	int error_reported;
	int error;
	int valid;
	int i;

	error_reported = 0;
	error = 0;
	valid = 0;
	bp = NULL;

	/*
	 * There are up to 4 copies of the volume header (syncs iterate
	 * between them so there is no single master).  We don't trust the
	 * volu_size field so we don't know precisely how large the filesystem
	 * is, so depend on the OS to return an error if we go beyond the
	 * block device's EOF.
	 */
	for (i = 0; i < HAMMER2_NUM_VOLHDRS; i++) {
		error = bread(hmp->devvp, i * HAMMER2_ZONE_BYTES64,
			      HAMMER2_VOLUME_BYTES, &bp);
		if (error) {
			brelse(bp);
			bp = NULL;
			continue;
		}

		vd = (struct hammer2_volume_data *) bp->b_data;
		if ((vd->magic != HAMMER2_VOLUME_ID_HBO) &&
		    (vd->magic != HAMMER2_VOLUME_ID_ABO)) {
			brelse(bp);
			bp = NULL;
			continue;
		}

		if (vd->magic == HAMMER2_VOLUME_ID_ABO) {
			/* XXX: Reversed-endianness filesystem */
			kprintf("hammer2: reverse-endian filesystem detected");
			brelse(bp);
			bp = NULL;
			continue;
		}

		crc = vd->icrc_sects[HAMMER2_VOL_ICRC_SECT0];
		crc0 = hammer2_icrc32(bp->b_data + HAMMER2_VOLUME_ICRC0_OFF,
				      HAMMER2_VOLUME_ICRC0_SIZE);
		bcrc = vd->icrc_sects[HAMMER2_VOL_ICRC_SECT1];
		bcrc0 = hammer2_icrc32(bp->b_data + HAMMER2_VOLUME_ICRC1_OFF,
				       HAMMER2_VOLUME_ICRC1_SIZE);
		if ((crc0 != crc) || (bcrc0 != bcrc)) {
			kprintf("hammer2 volume header crc "
				"mismatch copy #%d %08x/%08x\n",
				i, crc0, crc);
			error_reported = 1;
			brelse(bp);
			bp = NULL;
			continue;
		}
		if (valid == 0 || hmp->voldata.mirror_tid < vd->mirror_tid) {
			valid = 1;
			hmp->voldata = *vd;
			hmp->volhdrno = i;
		}
		brelse(bp);
		bp = NULL;
	}
	if (valid) {
		hmp->volsync = hmp->voldata;
		error = 0;
		if (error_reported || bootverbose || 1) { /* 1/DEBUG */
			kprintf("hammer2: using volume header #%d\n",
				hmp->volhdrno);
		}
	} else {
		error = EINVAL;
		kprintf("hammer2: no valid volume headers found!\n");
	}
	return (error);
}

/*
 * This handles hysteresis on regular file flushes.  Because the BIOs are
 * routed to a thread it is possible for an excessive number to build up
 * and cause long front-end stalls long before the runningbuffspace limit
 * is hit, so we implement hammer2_flush_pipe to control the
 * hysteresis.
 *
 * This is a particular problem when compression is used.
 */
void
hammer2_lwinprog_ref(hammer2_pfs_t *pmp)
{
	atomic_add_int(&pmp->count_lwinprog, 1);
}

void
hammer2_lwinprog_drop(hammer2_pfs_t *pmp)
{
	int lwinprog;

	lwinprog = atomic_fetchadd_int(&pmp->count_lwinprog, -1);
	if ((lwinprog & HAMMER2_LWINPROG_WAITING) &&
	    (lwinprog & HAMMER2_LWINPROG_MASK) <= hammer2_flush_pipe * 2 / 3) {
		atomic_clear_int(&pmp->count_lwinprog,
				 HAMMER2_LWINPROG_WAITING);
		wakeup(&pmp->count_lwinprog);
	}
}

void
hammer2_lwinprog_wait(hammer2_pfs_t *pmp)
{
	int lwinprog;

	for (;;) {
		lwinprog = pmp->count_lwinprog;
		cpu_ccfence();
		if ((lwinprog & HAMMER2_LWINPROG_MASK) < hammer2_flush_pipe)
			break;
		tsleep_interlock(&pmp->count_lwinprog, 0);
		atomic_set_int(&pmp->count_lwinprog, HAMMER2_LWINPROG_WAITING);
		lwinprog = pmp->count_lwinprog;
		if ((lwinprog & HAMMER2_LWINPROG_MASK) < hammer2_flush_pipe)
			break;
		tsleep(&pmp->count_lwinprog, PINTERLOCKED, "h2wpipe", hz);
	}
}

/*
 * Manage excessive memory resource use for chain and related
 * structures.
 */
void
hammer2_pfs_memory_wait(hammer2_pfs_t *pmp)
{
	uint32_t waiting;
	uint32_t count;
	uint32_t limit;
#if 0
	static int zzticks;
#endif

	/*
	 * Atomic check condition and wait.  Also do an early speedup of
	 * the syncer to try to avoid hitting the wait.
	 */
	for (;;) {
		waiting = pmp->inmem_dirty_chains;
		cpu_ccfence();
		count = waiting & HAMMER2_DIRTYCHAIN_MASK;

		limit = pmp->mp->mnt_nvnodelistsize / 10;
		if (limit < hammer2_limit_dirty_chains)
			limit = hammer2_limit_dirty_chains;
		if (limit < 1000)
			limit = 1000;

#if 0
		if ((int)(ticks - zzticks) > hz) {
			zzticks = ticks;
			kprintf("count %ld %ld\n", count, limit);
		}
#endif

		/*
		 * Block if there are too many dirty chains present, wait
		 * for the flush to clean some out.
		 */
		if (count > limit) {
			tsleep_interlock(&pmp->inmem_dirty_chains, 0);
			if (atomic_cmpset_int(&pmp->inmem_dirty_chains,
					       waiting,
				       waiting | HAMMER2_DIRTYCHAIN_WAITING)) {
				speedup_syncer(pmp->mp);
				tsleep(&pmp->inmem_dirty_chains, PINTERLOCKED,
				       "chnmem", hz);
			}
			continue;	/* loop on success or fail */
		}

		/*
		 * Try to start an early flush before we are forced to block.
		 */
		if (count > limit * 7 / 10)
			speedup_syncer(pmp->mp);
		break;
	}
}

void
hammer2_pfs_memory_inc(hammer2_pfs_t *pmp)
{
	if (pmp) {
		atomic_add_int(&pmp->inmem_dirty_chains, 1);
	}
}

void
hammer2_pfs_memory_wakeup(hammer2_pfs_t *pmp)
{
	uint32_t waiting;

	if (pmp == NULL)
		return;

	for (;;) {
		waiting = pmp->inmem_dirty_chains;
		cpu_ccfence();
		if (atomic_cmpset_int(&pmp->inmem_dirty_chains,
				       waiting,
				       (waiting - 1) &
					~HAMMER2_DIRTYCHAIN_WAITING)) {
			break;
		}
	}

	if (waiting & HAMMER2_DIRTYCHAIN_WAITING)
		wakeup(&pmp->inmem_dirty_chains);
}

/*
 * Debugging
 */
void
hammer2_dump_chain(hammer2_chain_t *chain, int tab, int *countp, char pfx)
{
	hammer2_chain_t *scan;
	hammer2_chain_t *parent;

	--*countp;
	if (*countp == 0) {
		kprintf("%*.*s...\n", tab, tab, "");
		return;
	}
	if (*countp < 0)
		return;
	kprintf("%*.*s%c-chain %p.%d %016jx/%d mir=%016jx\n",
		tab, tab, "", pfx,
		chain, chain->bref.type,
		chain->bref.key, chain->bref.keybits,
		chain->bref.mirror_tid);

	kprintf("%*.*s      [%08x] (%s) refs=%d",
		tab, tab, "",
		chain->flags,
		((chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		chain->data) ?  (char *)chain->data->ipdata.filename : "?"),
		chain->refs);

	parent = chain->parent;
	if (parent)
		kprintf("\n%*.*s      p=%p [pflags %08x prefs %d",
			tab, tab, "",
			parent, parent->flags, parent->refs);
	if (RB_EMPTY(&chain->core.rbtree)) {
		kprintf("\n");
	} else {
		kprintf(" {\n");
		RB_FOREACH(scan, hammer2_chain_tree, &chain->core.rbtree)
			hammer2_dump_chain(scan, tab + 4, countp, 'a');
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE && chain->data)
			kprintf("%*.*s}(%s)\n", tab, tab, "",
				chain->data->ipdata.filename);
		else
			kprintf("%*.*s}\n", tab, tab, "");
	}
}
