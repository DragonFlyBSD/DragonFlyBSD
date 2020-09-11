/*
 * Copyright (c) 2011-2018 The DragonFly Project.  All rights reserved.
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
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>

#include "hammer2.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"
#include "hammer2_lz4.h"

#include "zlib/hammer2_zlib.h"

#define REPORT_REFS_ERRORS 1	/* XXX remove me */

MALLOC_DEFINE(M_OBJCACHE, "objcache", "Object Cache");

struct hammer2_sync_info {
	int error;
	int waitfor;
	int pass;
};

TAILQ_HEAD(hammer2_mntlist, hammer2_dev);
static struct hammer2_mntlist hammer2_mntlist;

struct hammer2_pfslist hammer2_pfslist;
struct hammer2_pfslist hammer2_spmplist;
struct lock hammer2_mntlk;

int hammer2_supported_version = HAMMER2_VOL_VERSION_DEFAULT;
int hammer2_debug;
int hammer2_xopgroups;
long hammer2_debug_inode;
int hammer2_cluster_meta_read = 1;	/* physical read-ahead */
int hammer2_cluster_data_read = 4;	/* physical read-ahead */
int hammer2_cluster_write = 0;		/* physical write clustering */
int hammer2_dedup_enable = 1;
int hammer2_always_compress = 0;	/* always try to compress */
int hammer2_flush_pipe = 100;
int hammer2_dio_count;
int hammer2_dio_limit = 256;
int hammer2_bulkfree_tps = 5000;
int hammer2_worker_rmask = 3;
long hammer2_chain_allocs;
long hammer2_limit_dirty_chains;
long hammer2_limit_dirty_inodes;
long hammer2_count_modified_chains;
long hammer2_iod_file_read;
long hammer2_iod_meta_read;
long hammer2_iod_indr_read;
long hammer2_iod_fmap_read;
long hammer2_iod_volu_read;
long hammer2_iod_file_write;
long hammer2_iod_file_wembed;
long hammer2_iod_file_wzero;
long hammer2_iod_file_wdedup;
long hammer2_iod_meta_write;
long hammer2_iod_indr_write;
long hammer2_iod_fmap_write;
long hammer2_iod_volu_write;
static long hammer2_iod_inode_creates;
static long hammer2_iod_inode_deletes;

long hammer2_process_icrc32;
long hammer2_process_xxhash64;

MALLOC_DECLARE(M_HAMMER2_CBUFFER);
MALLOC_DEFINE(M_HAMMER2_CBUFFER, "HAMMER2-compbuffer",
		"Buffer used for compression.");

MALLOC_DECLARE(M_HAMMER2_DEBUFFER);
MALLOC_DEFINE(M_HAMMER2_DEBUFFER, "HAMMER2-decompbuffer",
		"Buffer used for decompression.");

SYSCTL_NODE(_vfs, OID_AUTO, hammer2, CTLFLAG_RW, 0, "HAMMER2 filesystem");

SYSCTL_INT(_vfs_hammer2, OID_AUTO, supported_version, CTLFLAG_RD,
	   &hammer2_supported_version, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, debug, CTLFLAG_RW,
	   &hammer2_debug, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, debug_inode, CTLFLAG_RW,
	   &hammer2_debug_inode, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, cluster_meta_read, CTLFLAG_RW,
	   &hammer2_cluster_meta_read, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, cluster_data_read, CTLFLAG_RW,
	   &hammer2_cluster_data_read, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, cluster_write, CTLFLAG_RW,
	   &hammer2_cluster_write, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, dedup_enable, CTLFLAG_RW,
	   &hammer2_dedup_enable, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, always_compress, CTLFLAG_RW,
	   &hammer2_always_compress, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, flush_pipe, CTLFLAG_RW,
	   &hammer2_flush_pipe, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, worker_rmask, CTLFLAG_RW,
	   &hammer2_worker_rmask, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, bulkfree_tps, CTLFLAG_RW,
	   &hammer2_bulkfree_tps, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, chain_allocs, CTLFLAG_RW,
	   &hammer2_chain_allocs, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, limit_dirty_chains, CTLFLAG_RW,
	   &hammer2_limit_dirty_chains, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, limit_dirty_inodes, CTLFLAG_RW,
	   &hammer2_limit_dirty_inodes, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, count_modified_chains, CTLFLAG_RW,
	   &hammer2_count_modified_chains, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, dio_count, CTLFLAG_RD,
	   &hammer2_dio_count, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, dio_limit, CTLFLAG_RW,
	   &hammer2_dio_limit, 0, "");

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
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_wembed, CTLFLAG_RW,
	   &hammer2_iod_file_wembed, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_wzero, CTLFLAG_RW,
	   &hammer2_iod_file_wzero, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_wdedup, CTLFLAG_RW,
	   &hammer2_iod_file_wdedup, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_meta_write, CTLFLAG_RW,
	   &hammer2_iod_meta_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_indr_write, CTLFLAG_RW,
	   &hammer2_iod_indr_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_fmap_write, CTLFLAG_RW,
	   &hammer2_iod_fmap_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_volu_write, CTLFLAG_RW,
	   &hammer2_iod_volu_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_inode_creates, CTLFLAG_RW,
	   &hammer2_iod_inode_creates, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_inode_deletes, CTLFLAG_RW,
	   &hammer2_iod_inode_deletes, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, process_icrc32, CTLFLAG_RW,
	   &hammer2_process_icrc32, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, process_xxhash64, CTLFLAG_RW,
	   &hammer2_process_xxhash64, 0, "");

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
static int hammer2_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
				struct fid *fhp, struct vnode **vpp);
static int hammer2_vfs_vptofh(struct vnode *vp, struct fid *fhp);
static int hammer2_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
				int *exflagsp, struct ucred **credanonp);
static int hammer2_vfs_modifying(struct mount *mp);

static int hammer2_install_volume_header(hammer2_dev_t *hmp);
#if 0
static int hammer2_sync_scan2(struct mount *mp, struct vnode *vp, void *data);
#endif

static void hammer2_update_pmps(hammer2_dev_t *hmp);

static void hammer2_mount_helper(struct mount *mp, hammer2_pfs_t *pmp);
static void hammer2_unmount_helper(struct mount *mp, hammer2_pfs_t *pmp,
				hammer2_dev_t *hmp);
static int hammer2_fixup_pfses(hammer2_dev_t *hmp);

/*
 * HAMMER2 vfs operations.
 */
static struct vfsops hammer2_vfsops = {
	.vfs_flags	= 0,
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
	.vfs_checkexp	= hammer2_vfs_checkexp,
	.vfs_modifying	= hammer2_vfs_modifying
};

MALLOC_DEFINE(M_HAMMER2, "HAMMER2-mount", "");

VFS_SET(hammer2_vfsops, hammer2, VFCF_MPSAFE);
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
	kmalloc_raise_limit(M_HAMMER2, 0);	/* unlimited */

	/*
	 * hammer2_xopgroups must be even and is most optimal if
	 * 2 x ncpus so strategy functions can be queued to the same
	 * cpu.
	 */
	hammer2_xopgroups = HAMMER2_XOPGROUPS_MIN;
	if (hammer2_xopgroups < ncpus * 2)
		hammer2_xopgroups = ncpus * 2;

	/*
	 * A large DIO cache is needed to retain dedup enablement masks.
	 * The bulkfree code clears related masks as part of the disk block
	 * recycling algorithm, preventing it from being used for a later
	 * dedup.
	 *
	 * NOTE: A large buffer cache can actually interfere with dedup
	 *	 operation because we dedup based on media physical buffers
	 *	 and not logical buffers.  Try to make the DIO case large
	 *	 enough to avoid this problem, but also cap it.
	 */
	hammer2_dio_limit = nbuf * 2;
	if (hammer2_dio_limit > 100000)
		hammer2_dio_limit = 100000;

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
	
	/*
	 * Note thaht for the XOPS cache we want backing store allocations
	 * to use M_ZERO.  This is not allowed in objcache_get() (to avoid
	 * confusion), so use the backing store function that does it.  This
	 * means that initial XOPS objects are zerod but REUSED objects are
	 * not.  So we are responsible for cleaning the object up sufficiently
	 * for our needs before objcache_put()ing it back (typically just the
	 * FIFO indices).
	 */
	cache_buffer_read = objcache_create(margs_read.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL,
				objcache_malloc_alloc,
				objcache_malloc_free,
				&margs_read);
	cache_buffer_write = objcache_create(margs_write.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL,
				objcache_malloc_alloc,
				objcache_malloc_free,
				&margs_write);
	cache_xops = objcache_create(margs_vop.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL,
				objcache_malloc_alloc_zero,
				objcache_malloc_free,
				&margs_vop);


	lockinit(&hammer2_mntlk, "mntlk", 0, 0);
	TAILQ_INIT(&hammer2_mntlist);
	TAILQ_INIT(&hammer2_pfslist);
	TAILQ_INIT(&hammer2_spmplist);

	hammer2_limit_dirty_chains = maxvnodes / 10;
	if (hammer2_limit_dirty_chains > HAMMER2_LIMIT_DIRTY_CHAINS)
		hammer2_limit_dirty_chains = HAMMER2_LIMIT_DIRTY_CHAINS;
	if (hammer2_limit_dirty_chains < 1000)
		hammer2_limit_dirty_chains = 1000;

	hammer2_limit_dirty_inodes = maxvnodes / 25;
	if (hammer2_limit_dirty_inodes < 100)
		hammer2_limit_dirty_inodes = 100;
	if (hammer2_limit_dirty_inodes > HAMMER2_LIMIT_DIRTY_INODES)
		hammer2_limit_dirty_inodes = HAMMER2_LIMIT_DIRTY_INODES;

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
 * Core PFS allocator.  Used to allocate or reference the pmp structure
 * for PFS cluster mounts and the spmp structure for media (hmp) structures.
 * The pmp can be passed in or loaded by this function using the chain and
 * inode data.
 *
 * pmp->modify_tid tracks new modify_tid transaction ids for front-end
 * transactions.  Note that synchronization does not use this field.
 * (typically frontend operations and synchronization cannot run on the
 * same PFS node at the same time).
 *
 * XXX check locking
 */
hammer2_pfs_t *
hammer2_pfsalloc(hammer2_chain_t *chain,
		 const hammer2_inode_data_t *ripdata,
		 hammer2_tid_t modify_tid, hammer2_dev_t *force_local)
{
	hammer2_pfs_t *pmp;
	hammer2_inode_t *iroot;
	int count;
	int i;
	int j;

	pmp = NULL;

	/*
	 * Locate or create the PFS based on the cluster id.  If ripdata
	 * is NULL this is a spmp which is unique and is always allocated.
	 *
	 * If the device is mounted in local mode all PFSs are considered
	 * independent and not part of any cluster (for debugging only).
	 */
	if (ripdata) {
		TAILQ_FOREACH(pmp, &hammer2_pfslist, mntentry) {
			if (force_local != pmp->force_local)
				continue;
			if (force_local == NULL &&
			    bcmp(&pmp->pfs_clid, &ripdata->meta.pfs_clid,
				 sizeof(pmp->pfs_clid)) == 0) {
					break;
			} else if (force_local && pmp->pfs_names[0] &&
			    strcmp(pmp->pfs_names[0], ripdata->filename) == 0) {
					break;
			}
		}
	}

	if (pmp == NULL) {
		pmp = kmalloc(sizeof(*pmp), M_HAMMER2, M_WAITOK | M_ZERO);
		pmp->force_local = force_local;
		hammer2_trans_manage_init(pmp);
		kmalloc_create(&pmp->minode, "HAMMER2-inodes");
		kmalloc_create(&pmp->mmsg, "HAMMER2-pfsmsg");
		lockinit(&pmp->lock, "pfslk", 0, 0);
		lockinit(&pmp->lock_nlink, "h2nlink", 0, 0);
		spin_init(&pmp->inum_spin, "hm2pfsalloc_inum");
		spin_init(&pmp->xop_spin, "h2xop");
		spin_init(&pmp->lru_spin, "h2lru");
		RB_INIT(&pmp->inum_tree);
		TAILQ_INIT(&pmp->syncq);
		TAILQ_INIT(&pmp->depq);
		TAILQ_INIT(&pmp->lru_list);
		spin_init(&pmp->list_spin, "h2pfsalloc_list");

		/*
		 * Save the last media transaction id for the flusher.  Set
		 * initial 
		 */
		if (ripdata) {
			pmp->pfs_clid = ripdata->meta.pfs_clid;
			TAILQ_INSERT_TAIL(&hammer2_pfslist, pmp, mntentry);
		} else {
			pmp->flags |= HAMMER2_PMPF_SPMP;
			TAILQ_INSERT_TAIL(&hammer2_spmplist, pmp, mntentry);
		}

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
	 * Create the PFS's root inode and any missing XOP helper threads.
	 */
	if ((iroot = pmp->iroot) == NULL) {
		iroot = hammer2_inode_get(pmp, NULL, 1, -1);
		if (ripdata)
			iroot->meta = ripdata->meta;
		pmp->iroot = iroot;
		hammer2_inode_ref(iroot);
		hammer2_inode_unlock(iroot);
	}

	/*
	 * Stop here if no chain is passed in.
	 */
	if (chain == NULL)
		goto done;

	/*
	 * When a chain is passed in we must add it to the PFS's root
	 * inode, update pmp->pfs_types[], and update the syncronization
	 * threads.
	 *
	 * When forcing local mode, mark the PFS as a MASTER regardless.
	 *
	 * At the moment empty spots can develop due to removals or failures.
	 * Ultimately we want to re-fill these spots but doing so might
	 * confused running code. XXX
	 */
	hammer2_inode_ref(iroot);
	hammer2_mtx_ex(&iroot->lock);
	j = iroot->cluster.nchains;

	if (j == HAMMER2_MAXCLUSTER) {
		kprintf("hammer2_pfsalloc: cluster full!\n");
		/* XXX fatal error? */
	} else {
		KKASSERT(chain->pmp == NULL);
		chain->pmp = pmp;
		hammer2_chain_ref(chain);
		iroot->cluster.array[j].chain = chain;
		if (force_local)
			pmp->pfs_types[j] = HAMMER2_PFSTYPE_MASTER;
		else
			pmp->pfs_types[j] = ripdata->meta.pfs_type;
		pmp->pfs_names[j] = kstrdup(ripdata->filename, M_HAMMER2);
		pmp->pfs_hmps[j] = chain->hmp;
		hammer2_spin_ex(&pmp->inum_spin);
		pmp->pfs_iroot_blocksets[j] = chain->data->ipdata.u.blockset;
		hammer2_spin_unex(&pmp->inum_spin);

		/*
		 * If the PFS is already mounted we must account
		 * for the mount_count here.
		 */
		if (pmp->mp)
			++chain->hmp->mount_count;

		/*
		 * May have to fixup dirty chain tracking.  Previous
		 * pmp was NULL so nothing to undo.
		 */
		if (chain->flags & HAMMER2_CHAIN_MODIFIED)
			hammer2_pfs_memory_inc(pmp);
		++j;
	}
	iroot->cluster.nchains = j;

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
			hammer2_thr_create(&pmp->sync_thrs[i], pmp, NULL,
					   "h2nod", i, -1,
					   hammer2_primary_sync_thread);
		}
	}

	/*
	 * Create missing Xop threads
	 *
	 * NOTE: We create helper threads for all mounted PFSs or any
	 *	 PFSs with 2+ nodes (so the sync thread can update them,
	 *	 even if not mounted).
	 */
	if (pmp->mp || iroot->cluster.nchains >= 2)
		hammer2_xop_helper_create(pmp);

	hammer2_mtx_unlock(&iroot->lock);
	hammer2_inode_drop(iroot);
done:
	return pmp;
}

/*
 * Deallocate an element of a probed PFS.  If destroying and this is a
 * MASTER, adjust nmasters.
 *
 * This function does not physically destroy the PFS element in its device
 * under the super-root  (see hammer2_ioctl_pfs_delete()).
 */
void
hammer2_pfsdealloc(hammer2_pfs_t *pmp, int clindex, int destroying)
{
	hammer2_inode_t *iroot;
	hammer2_chain_t *chain;
	int j;

	/*
	 * Cleanup our reference on iroot.  iroot is (should) not be needed
	 * by the flush code.
	 */
	iroot = pmp->iroot;
	if (iroot) {
		/*
		 * Stop synchronizing
		 *
		 * XXX flush after acquiring the iroot lock.
		 * XXX clean out the cluster index from all inode structures.
		 */
		hammer2_thr_delete(&pmp->sync_thrs[clindex]);

		/*
		 * Remove the cluster index from the group.  If destroying
		 * the PFS and this is a master, adjust pfs_nmasters.
		 */
		hammer2_mtx_ex(&iroot->lock);
		chain = iroot->cluster.array[clindex].chain;
		iroot->cluster.array[clindex].chain = NULL;

		switch(pmp->pfs_types[clindex]) {
		case HAMMER2_PFSTYPE_MASTER:
			if (destroying && pmp->pfs_nmasters > 0)
				--pmp->pfs_nmasters;
			/* XXX adjust ripdata->meta.pfs_nmasters */
			break;
		default:
			break;
		}
		pmp->pfs_types[clindex] = HAMMER2_PFSTYPE_NONE;

		hammer2_mtx_unlock(&iroot->lock);

		/*
		 * Release the chain.
		 */
		if (chain) {
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_RELEASE);
			hammer2_chain_drop(chain);
		}

		/*
		 * Terminate all XOP threads for the cluster index.
		 */
		if (pmp->xop_groups) {
			for (j = 0; j < hammer2_xopgroups; ++j) {
				hammer2_thr_delete(
					&pmp->xop_groups[j].thrs[clindex]);
			}
		}
	}
}

/*
 * Destroy a PFS, typically only occurs after the last mount on a device
 * has gone away.
 */
static void
hammer2_pfsfree(hammer2_pfs_t *pmp)
{
	hammer2_inode_t *iroot;
	hammer2_chain_t *chain;
	int chains_still_present = 0;
	int i;
	int j;

	/*
	 * Cleanup our reference on iroot.  iroot is (should) not be needed
	 * by the flush code.
	 */
	if (pmp->flags & HAMMER2_PMPF_SPMP)
		TAILQ_REMOVE(&hammer2_spmplist, pmp, mntentry);
	else
		TAILQ_REMOVE(&hammer2_pfslist, pmp, mntentry);

	/*
	 * Cleanup chains remaining on LRU list.
	 */
	hammer2_spin_ex(&pmp->lru_spin);
	while ((chain = TAILQ_FIRST(&pmp->lru_list)) != NULL) {
		KKASSERT(chain->flags & HAMMER2_CHAIN_ONLRU);
		atomic_add_int(&pmp->lru_count, -1);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONLRU);
		TAILQ_REMOVE(&pmp->lru_list, chain, lru_node);
		hammer2_chain_ref(chain);
		hammer2_spin_unex(&pmp->lru_spin);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_RELEASE);
		hammer2_chain_drop(chain);
		hammer2_spin_ex(&pmp->lru_spin);
	}
	hammer2_spin_unex(&pmp->lru_spin);

	/*
	 * Clean up iroot
	 */
	iroot = pmp->iroot;
	if (iroot) {
		for (i = 0; i < iroot->cluster.nchains; ++i) {
			hammer2_thr_delete(&pmp->sync_thrs[i]);
			if (pmp->xop_groups) {
				for (j = 0; j < hammer2_xopgroups; ++j)
					hammer2_thr_delete(
						&pmp->xop_groups[j].thrs[i]);
			}
			chain = iroot->cluster.array[i].chain;
			if (chain && !RB_EMPTY(&chain->core.rbtree)) {
				kprintf("hammer2: Warning pmp %p still "
					"has active chains\n", pmp);
				chains_still_present = 1;
			}
		}
#if REPORT_REFS_ERRORS
		if (iroot->refs != 1)
			kprintf("PMP->IROOT %p REFS WRONG %d\n",
				iroot, iroot->refs);
#else
		KKASSERT(iroot->refs == 1);
#endif
		/* ref for iroot */
		hammer2_inode_drop(iroot);
		pmp->iroot = NULL;
	}

	/*
	 * Free remaining pmp resources
	 */
	if (chains_still_present) {
		kprintf("hammer2: cannot free pmp %p, still in use\n", pmp);
	} else {
		kmalloc_destroy(&pmp->mmsg);
		kmalloc_destroy(&pmp->minode);
		kfree(pmp, M_HAMMER2);
	}
}

/*
 * Remove all references to hmp from the pfs list.  Any PFS which becomes
 * empty is terminated and freed.
 *
 * XXX inefficient.
 */
static void
hammer2_pfsfree_scan(hammer2_dev_t *hmp, int which)
{
	hammer2_pfs_t *pmp;
	hammer2_inode_t *iroot;
	hammer2_chain_t *rchain;
	int i;
	int j;
	struct hammer2_pfslist *wlist;

	if (which == 0)
		wlist = &hammer2_pfslist;
	else
		wlist = &hammer2_spmplist;
again:
	TAILQ_FOREACH(pmp, wlist, mntentry) {
		if ((iroot = pmp->iroot) == NULL)
			continue;

		/*
		 * Determine if this PFS is affected.  If it is we must
		 * freeze all management threads and lock its iroot.
		 *
		 * Freezing a management thread forces it idle, operations
		 * in-progress will be aborted and it will have to start
		 * over again when unfrozen, or exit if told to exit.
		 */
		for (i = 0; i < HAMMER2_MAXCLUSTER; ++i) {
			if (pmp->pfs_hmps[i] == hmp)
				break;
		}
		if (i == HAMMER2_MAXCLUSTER)
			continue;

		hammer2_vfs_sync_pmp(pmp, MNT_WAIT);

		/*
		 * Make sure all synchronization threads are locked
		 * down.
		 */
		for (i = 0; i < HAMMER2_MAXCLUSTER; ++i) {
			if (pmp->pfs_hmps[i] == NULL)
				continue;
			hammer2_thr_freeze_async(&pmp->sync_thrs[i]);
			if (pmp->xop_groups) {
				for (j = 0; j < hammer2_xopgroups; ++j) {
					hammer2_thr_freeze_async(
						&pmp->xop_groups[j].thrs[i]);
				}
			}
		}
		for (i = 0; i < HAMMER2_MAXCLUSTER; ++i) {
			if (pmp->pfs_hmps[i] == NULL)
				continue;
			hammer2_thr_freeze(&pmp->sync_thrs[i]);
			if (pmp->xop_groups) {
				for (j = 0; j < hammer2_xopgroups; ++j) {
					hammer2_thr_freeze(
						&pmp->xop_groups[j].thrs[i]);
				}
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
		for (i = 0; i < HAMMER2_MAXCLUSTER; ++i) {
			if (pmp->pfs_hmps[i] != hmp)
				continue;
			hammer2_thr_delete(&pmp->sync_thrs[i]);
			if (pmp->xop_groups) {
				for (j = 0; j < hammer2_xopgroups; ++j) {
					hammer2_thr_delete(
						&pmp->xop_groups[j].thrs[i]);
				}
			}
			rchain = iroot->cluster.array[i].chain;
			iroot->cluster.array[i].chain = NULL;
			pmp->pfs_types[i] = 0;
			if (pmp->pfs_names[i]) {
				kfree(pmp->pfs_names[i], M_HAMMER2);
				pmp->pfs_names[i] = NULL;
			}
			if (rchain) {
				hammer2_chain_drop(rchain);
				/* focus hint */
				if (iroot->cluster.focus == rchain)
					iroot->cluster.focus = NULL;
			}
			pmp->pfs_hmps[i] = NULL;
		}
		hammer2_mtx_unlock(&iroot->lock);

		/*
		 * Cleanup trailing chains.  Gaps may remain.
		 */
		for (i = HAMMER2_MAXCLUSTER - 1; i >= 0; --i) {
			if (pmp->pfs_hmps[i])
				break;
		}
		iroot->cluster.nchains = i + 1;

		/*
		 * If the PMP has no elements remaining we can destroy it.
		 * (this will transition management threads from frozen->exit).
		 */
		if (iroot->cluster.nchains == 0) {
			/*
			 * If this was the hmp's spmp, we need to clean
			 * a little more stuff out.
			 */
			if (hmp->spmp == pmp) {
				hmp->spmp = NULL;
				hmp->vchain.pmp = NULL;
				hmp->fchain.pmp = NULL;
			}

			/*
			 * Free the pmp and restart the loop
			 */
			KKASSERT(TAILQ_EMPTY(&pmp->syncq));
			KKASSERT(TAILQ_EMPTY(&pmp->depq));
			hammer2_pfsfree(pmp);
			goto again;
		}

		/*
		 * If elements still remain we need to set the REMASTER
		 * flag and unfreeze it.
		 */
		for (i = 0; i < HAMMER2_MAXCLUSTER; ++i) {
			if (pmp->pfs_hmps[i] == NULL)
				continue;
			hammer2_thr_remaster(&pmp->sync_thrs[i]);
			hammer2_thr_unfreeze(&pmp->sync_thrs[i]);
			if (pmp->xop_groups) {
				for (j = 0; j < hammer2_xopgroups; ++j) {
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
	hammer2_dev_t *force_local;
	hammer2_key_t key_next;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	struct vnode *devvp;
	struct nlookupdata nd;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
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
	int i;

	hmp = NULL;
	pmp = NULL;
	dev = NULL;
	label = NULL;
	devvp = NULL;

	if (path == NULL) {
		/*
		 * Root mount
		 */
		bzero(&info, sizeof(info));
		info.cluster_fd = -1;
		ksnprintf(devstr, sizeof(devstr), "%s",
			  mp->mnt_stat.f_mntfromname);
		kprintf("hammer2_mount: root '%s'\n", devstr);
		done = strlen(devstr) + 1;
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
		kprintf("hammer2_mount: '%s'\n", devstr);
	}

	/*
	 * Extract device and label, automatically mount @BOOT, @ROOT, or @DATA
	 * if no label specified, based on the partition id.  Error out if no
	 * label or device (with partition id) is specified.  This is strictly
	 * a convenience to match the default label created by newfs_hammer2,
	 * our preference is that a label always be specified.
	 *
	 * NOTE: We allow 'mount @LABEL <blah>'... that is, a mount command
	 *	 that does not specify a device, as long as some H2 label
	 *	 has already been mounted from that device.  This makes
	 *	 mounting snapshots a lot easier.
	 */
	dev = devstr;
	label = strchr(devstr, '@');
	if (label && ((label + 1) - dev) > done) {
		kprintf("hammer2: mount: bad label %s/%zd\n",
			devstr, done);
		return (EINVAL);
	}
	if (label == NULL || label[1] == 0) {
		char slice;

		if (label == NULL)
			label = devstr + strlen(devstr);
		else
			*label = '\0';		/* clean up trailing @ */

		slice = label[-1];
		switch(slice) {
		case 'a':
			label = "BOOT";
			break;
		case 'd':
			label = "ROOT";
			break;
		default:
			label = "DATA";
			break;
		}
	} else {
		*label = '\0';
		label++;
	}

	kprintf("hammer2_mount: dev=\"%s\" label=\"%s\" rdonly=%d\n",
		dev, label, (mp->mnt_flag & MNT_RDONLY));

	if (mp->mnt_flag & MNT_UPDATE) {
		/*
		 * Update mount.  Note that pmp->iroot->cluster is
		 * an inode-embedded cluster and thus cannot be
		 * directly locked.
		 *
		 * XXX HAMMER2 needs to implement NFS export via
		 *     mountctl.
		 */
		hammer2_cluster_t *cluster;

		pmp = MPTOPMP(mp);
		pmp->hflags = info.hflags;
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

		return error;
	}

	/*
	 * HMP device mount
	 *
	 * If a path is specified and dev is not an empty string, lookup the
	 * name and verify that it referes to a block device.
	 *
	 * If a path is specified and dev is an empty string we fall through
	 * and locate the label in the hmp search.
	 */
	if (path && *dev != 0) {
		error = nlookup_init(&nd, dev, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error == 0)
			error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
		nlookup_done(&nd);
	} else if (path == NULL) {
		/* root mount */
		cdev_t cdev = kgetdiskbyname(dev);
		error = bdevvp(cdev, &devvp);
		if (error)
			kprintf("hammer2: cannot find '%s'\n", dev);
	} else {
		/*
		 * We will locate the hmp using the label in the hmp loop.
		 */
		error = 0;
	}

	/*
	 * Make sure its a block device.  Do not check to see if it is
	 * already mounted until we determine that its a fresh H2 device.
	 */
	if (error == 0 && devvp) {
		vn_isdisk(devvp, &error);
	}

	/*
	 * Determine if the device has already been mounted.  After this
	 * check hmp will be non-NULL if we are doing the second or more
	 * hammer2 mounts from the same device.
	 */
	lockmgr(&hammer2_mntlk, LK_EXCLUSIVE);
	if (devvp) {
		/*
		 * Match the device.  Due to the way devfs works,
		 * we may not be able to directly match the vnode pointer,
		 * so also check to see if the underlying device matches.
		 */
		TAILQ_FOREACH(hmp, &hammer2_mntlist, mntentry) {
			if (hmp->devvp == devvp)
				break;
			if (devvp->v_rdev &&
			    hmp->devvp->v_rdev == devvp->v_rdev) {
				break;
			}
		}

		/*
		 * If no match this may be a fresh H2 mount, make sure
		 * the device is not mounted on anything else.
		 */
		if (hmp == NULL)
			error = vfs_mountedon(devvp);
	} else if (error == 0) {
		/*
		 * Match the label to a pmp already probed.
		 */
		TAILQ_FOREACH(pmp, &hammer2_pfslist, mntentry) {
			for (i = 0; i < HAMMER2_MAXCLUSTER; ++i) {
				if (pmp->pfs_names[i] &&
				    strcmp(pmp->pfs_names[i], label) == 0) {
					hmp = pmp->pfs_hmps[i];
					break;
				}
			}
			if (hmp)
				break;
		}
		if (hmp == NULL)
			error = ENOENT;
	}

	/*
	 * Open the device if this isn't a secondary mount and construct
	 * the H2 device mount (hmp).
	 */
	if (hmp == NULL) {
		hammer2_chain_t *schain;
		hammer2_xid_t xid;
		hammer2_xop_head_t xop;

		if (error == 0 && vcount(devvp) > 0) {
			kprintf("Primary device already has references\n");
			error = EBUSY;
		}

		/*
		 * Now open the device
		 */
		if (error == 0) {
			ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			error = vinvalbuf(devvp, V_SAVE, 0, 0);
			if (error == 0) {
				error = VOP_OPEN(devvp,
					     (ronly ? FREAD : FREAD | FWRITE),
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
		hmp->hflags = info.hflags & HMNT2_DEVFLAGS;
		kmalloc_create(&hmp->mchain, "HAMMER2-chains");
		TAILQ_INSERT_TAIL(&hammer2_mntlist, hmp, mntentry);
		RB_INIT(&hmp->iotree);
		spin_init(&hmp->io_spin, "h2mount_io");
		spin_init(&hmp->list_spin, "h2mount_list");

		lockinit(&hmp->vollk, "h2vol", 0, 0);
		lockinit(&hmp->bulklk, "h2bulk", 0, 0);
		lockinit(&hmp->bflock, "h2bflk", 0, 0);

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
		 * Really important to get these right or the flush and
		 * teardown code will get confused.
		 */
		hmp->spmp = hammer2_pfsalloc(NULL, NULL, 0, NULL);
		spmp = hmp->spmp;
		spmp->pfs_hmps[0] = hmp;

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
				      &error, 0);
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
		spmp->modify_tid = schain->bref.modify_tid + 1;

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
		hammer2_dummy_xop_from_chain(&xop, schain);
		hammer2_inode_drop(spmp->iroot);
		spmp->iroot = NULL;
		spmp->iroot = hammer2_inode_get(spmp, &xop, -1, -1);
		spmp->spmp_hmp = hmp;
		spmp->pfs_types[0] = ripdata->meta.pfs_type;
		spmp->pfs_hmps[0] = hmp;
		hammer2_inode_ref(spmp->iroot);
		hammer2_inode_unlock(spmp->iroot);
		hammer2_cluster_unlock(&xop.cluster);
		hammer2_chain_drop(schain);
		/* do not call hammer2_cluster_drop() on an embedded cluster */
		schain = NULL;	/* now invalid */
		/* leave spmp->iroot with one ref */

		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			error = hammer2_recovery(hmp);
			if (error == 0)
				error |= hammer2_fixup_pfses(hmp);
			/* XXX do something with error */
		}
		hammer2_update_pmps(hmp);
		hammer2_iocom_init(hmp);
		hammer2_bulkfree_init(hmp);

		/*
		 * Ref the cluster management messaging descriptor.  The mount
		 * program deals with the other end of the communications pipe.
		 *
		 * Root mounts typically do not supply one.
		 */
		if (info.cluster_fd >= 0) {
			fp = holdfp(curthread, info.cluster_fd, -1);
			if (fp) {
				hammer2_cluster_reconnect(hmp, fp);
			} else {
				kprintf("hammer2_mount: bad cluster_fd!\n");
			}
		}
	} else {
		spmp = hmp->spmp;
		if (info.hflags & HMNT2_DEVFLAGS) {
			kprintf("hammer2: Warning: mount flags pertaining "
				"to the whole device may only be specified "
				"on the first mount of the device: %08x\n",
				info.hflags & HMNT2_DEVFLAGS);
		}
	}

	/*
	 * Force local mount (disassociate all PFSs from their clusters).
	 * Used primarily for debugging.
	 */
	force_local = (hmp->hflags & HMNT2_LOCAL) ? hmp : NULL;

	/*
	 * Lookup the mount point under the media-localized super-root.
	 * Scanning hammer2_pfslist doesn't help us because it represents
	 * PFS cluster ids which can aggregate several named PFSs together.
	 *
	 * cluster->pmp will incorrectly point to spmp and must be fixed
	 * up later on.
	 */
	hammer2_inode_lock(spmp->iroot, 0);
	parent = hammer2_inode_chain(spmp->iroot, 0, HAMMER2_RESOLVE_ALWAYS);
	lhc = hammer2_dirhash(label, strlen(label));
	chain = hammer2_chain_lookup(&parent, &key_next,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     &error, 0);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    strcmp(label, chain->data->ipdata.filename) == 0) {
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					    key_next,
					    lhc + HAMMER2_DIRHASH_LOMASK,
					    &error, 0);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_inode_unlock(spmp->iroot);

	/*
	 * PFS could not be found?
	 */
	if (chain == NULL) {
		if (error)
			kprintf("hammer2_mount: PFS label I/O error\n");
		else
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
	if (chain->error) {
		kprintf("hammer2_mount: PFS label I/O error\n");
	} else {
		ripdata = &chain->data->ipdata;
		bref = chain->bref;
		pmp = hammer2_pfsalloc(NULL, ripdata,
				       bref.modify_tid, force_local);
	}
	hammer2_chain_unlock(chain);
	hammer2_chain_drop(chain);

	/*
	 * Finish the mount
	 */
        kprintf("hammer2_mount hmp=%p pmp=%p\n", hmp, pmp);

	if (pmp->mp) {
		kprintf("hammer2_mount: PFS already mounted!\n");
		hammer2_unmount_helper(mp, NULL, hmp);
		lockmgr(&hammer2_mntlk, LK_RELEASE);
		hammer2_vfs_unmount(mp, MNT_FORCE);

		return EBUSY;
	}

	pmp->hflags = info.hflags;
        mp->mnt_flag |= MNT_LOCAL;
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
	 * Finish setup
	 */
	vfs_getnewfsid(mp);
	vfs_add_vnodeops(mp, &hammer2_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &hammer2_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &hammer2_fifo_vops, &mp->mnt_vn_fifo_ops);

	if (path) {
		copyinstr(info.volume, mp->mnt_stat.f_mntfromname,
			  MNAMELEN - 1, &size);
		bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	} /* else root mount, already in there */

	bzero(mp->mnt_stat.f_mntonname, sizeof(mp->mnt_stat.f_mntonname));
	if (path) {
		copyinstr(path, mp->mnt_stat.f_mntonname,
			  sizeof(mp->mnt_stat.f_mntonname) - 1,
			  &size);
	} else {
		/* root mount */
		mp->mnt_stat.f_mntonname[0] = '/';
	}

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
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_blockref_t bref;
	hammer2_dev_t *force_local;
	hammer2_pfs_t *spmp;
	hammer2_pfs_t *pmp;
	hammer2_key_t key_next;
	int error;

	/*
	 * Force local mount (disassociate all PFSs from their clusters).
	 * Used primarily for debugging.
	 */
	force_local = (hmp->hflags & HMNT2_LOCAL) ? hmp : NULL;

	/*
	 * Lookup mount point under the media-localized super-root.
	 *
	 * cluster->pmp will incorrectly point to spmp and must be fixed
	 * up later on.
	 */
	spmp = hmp->spmp;
	hammer2_inode_lock(spmp->iroot, 0);
	parent = hammer2_inode_chain(spmp->iroot, 0, HAMMER2_RESOLVE_ALWAYS);
	chain = hammer2_chain_lookup(&parent, &key_next,
					 HAMMER2_KEY_MIN, HAMMER2_KEY_MAX,
					 &error, 0);
	while (chain) {
		if (chain->bref.type != HAMMER2_BREF_TYPE_INODE)
			continue;
		if (chain->error) {
			kprintf("I/O error scanning PFS labels\n");
		} else {
			ripdata = &chain->data->ipdata;
			bref = chain->bref;

			pmp = hammer2_pfsalloc(chain, ripdata,
					       bref.modify_tid, force_local);
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, HAMMER2_KEY_MAX,
					   &error, 0);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_inode_unlock(spmp->iroot);
}

static
int
hammer2_remount(hammer2_dev_t *hmp, struct mount *mp, char *path __unused,
		struct vnode *devvp, struct ucred *cred)
{
	int error;

	if (hmp->ronly && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		VOP_OPEN(devvp, FREAD | FWRITE, FSCRED, NULL);
		vn_unlock(devvp);
		error = hammer2_recovery(hmp);
		if (error == 0)
			error |= hammer2_fixup_pfses(hmp);
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		if (error == 0) {
			VOP_CLOSE(devvp, FREAD, NULL);
			hmp->ronly = 0;
		} else {
			VOP_CLOSE(devvp, FREAD | FWRITE, NULL);
		}
		vn_unlock(devvp);
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

	/*
	 * Cleanup the frontend support XOPS threads
	 */
	hammer2_xop_helper_cleanup(pmp);

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
	}

	/*
	 * Create missing Xop threads
	 */
	hammer2_xop_helper_create(pmp);
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
 * must be disconnected.  This might not be the last PFS using some of the
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
	int ronly;
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
	if (hmp->mount_count)
		return;

	/*
	 * Decomission the network before we start messing with the
	 * device and PFS.
	 */
	hammer2_iocom_uninit(hmp);

	hammer2_bulkfree_uninit(hmp);
	hammer2_pfsfree_scan(hmp, 0);
#if 0
	hammer2_dev_exlock(hmp);	/* XXX order */
#endif

	/*
	 * Cycle the volume data lock as a safety (probably not needed any
	 * more).  To ensure everything is out we need to flush at least
	 * three times.  (1) The running of the sideq can dirty the
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

	/*
	 * Flush whatever is left.  Unmounted but modified PFS's might still
	 * have some dirty chains on them.
	 */
	hammer2_chain_lock(&hmp->vchain, HAMMER2_RESOLVE_ALWAYS);
	hammer2_chain_lock(&hmp->fchain, HAMMER2_RESOLVE_ALWAYS);

	if (hmp->fchain.flags & HAMMER2_CHAIN_FLUSH_MASK) {
		hammer2_voldata_modify(hmp);
		hammer2_flush(&hmp->fchain, HAMMER2_FLUSH_TOP |
					    HAMMER2_FLUSH_ALL);
	}
	hammer2_chain_unlock(&hmp->fchain);

	if (hmp->vchain.flags & HAMMER2_CHAIN_FLUSH_MASK) {
		hammer2_flush(&hmp->vchain, HAMMER2_FLUSH_TOP |
					    HAMMER2_FLUSH_ALL);
	}
	hammer2_chain_unlock(&hmp->vchain);

	if ((hmp->vchain.flags | hmp->fchain.flags) &
	    HAMMER2_CHAIN_FLUSH_MASK) {
		kprintf("hammer2_unmount: chains left over "
			"after final sync\n");
		kprintf("    vchain %08x\n", hmp->vchain.flags);
		kprintf("    fchain %08x\n", hmp->fchain.flags);

		if (hammer2_debug & 0x0010)
			Debugger("entered debugger");
	}

	hammer2_pfsfree_scan(hmp, 1);

	KKASSERT(hmp->spmp == NULL);

	/*
	 * Finish up with the device vnode
	 */
	if ((devvp = hmp->devvp) != NULL) {
		ronly = hmp->ronly;
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		kprintf("hammer2_unmount(A): devvp %s rbdirty %p ronly=%d\n",
			hmp->devrepname, RB_ROOT(&devvp->v_rbdirty_tree),
			ronly);
		vinvalbuf(devvp, (ronly ? 0 : V_SAVE), 0, 0);
		kprintf("hammer2_unmount(B): devvp %s rbdirty %p\n",
			hmp->devrepname, RB_ROOT(&devvp->v_rbdirty_tree));
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
		atomic_add_long(&hammer2_count_modified_chains, -1);
		atomic_clear_int(&hmp->vchain.flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_pfs_memory_wakeup(hmp->vchain.pmp, -1);
	}
	if (hmp->vchain.flags & HAMMER2_CHAIN_UPDATE) {
		atomic_clear_int(&hmp->vchain.flags, HAMMER2_CHAIN_UPDATE);
	}

	if (hmp->fchain.flags & HAMMER2_CHAIN_MODIFIED) {
		atomic_add_long(&hammer2_count_modified_chains, -1);
		atomic_clear_int(&hmp->fchain.flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_pfs_memory_wakeup(hmp->fchain.pmp, -1);
	}
	if (hmp->fchain.flags & HAMMER2_CHAIN_UPDATE) {
		atomic_clear_int(&hmp->fchain.flags, HAMMER2_CHAIN_UPDATE);
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
	hammer2_dump_chain(&hmp->vchain, 0, &dumpcnt, 'v', (u_int)-1);
	dumpcnt = 50;
	hammer2_dump_chain(&hmp->fchain, 0, &dumpcnt, 'f', (u_int)-1);
#if 0
	hammer2_dev_unlock(hmp);
#endif
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

int
hammer2_vfs_vget(struct mount *mp, struct vnode *dvp,
		 ino_t ino, struct vnode **vpp)
{
	hammer2_xop_lookup_t *xop;
	hammer2_pfs_t *pmp;
	hammer2_inode_t *ip;
	hammer2_tid_t inum;
	int error;

	inum = (hammer2_tid_t)ino & HAMMER2_DIRHASH_USERMSK;

	error = 0;
	pmp = MPTOPMP(mp);

	/*
	 * Easy if we already have it cached
	 */
	ip = hammer2_inode_lookup(pmp, inum);
	if (ip) {
		hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
		*vpp = hammer2_igetv(ip, &error);
		hammer2_inode_unlock(ip);
		hammer2_inode_drop(ip);		/* from lookup */

		return error;
	}

	/*
	 * Otherwise we have to find the inode
	 */
	xop = hammer2_xop_alloc(pmp->iroot, 0);
	xop->lhc = inum;
	hammer2_xop_start(&xop->head, &hammer2_lookup_desc);
	error = hammer2_xop_collect(&xop->head, 0);

	if (error == 0)
		ip = hammer2_inode_get(pmp, &xop->head, -1, -1);
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);

	if (ip) {
		*vpp = hammer2_igetv(ip, &error);
		hammer2_inode_unlock(ip);
	} else {
		*vpp = NULL;
		error = ENOENT;
	}
	return (error);
}

static
int
hammer2_vfs_root(struct mount *mp, struct vnode **vpp)
{
	hammer2_pfs_t *pmp;
	struct vnode *vp;
	int error;

	pmp = MPTOPMP(mp);
	if (pmp->iroot == NULL) {
		kprintf("hammer2 (%s): no root inode\n",
			mp->mnt_stat.f_mntfromname);
		*vpp = NULL;
		return EINVAL;
	}

	error = 0;
	hammer2_inode_lock(pmp->iroot, HAMMER2_RESOLVE_SHARED);

	while (pmp->inode_tid == 0) {
		hammer2_xop_ipcluster_t *xop;
		const hammer2_inode_meta_t *meta;

		xop = hammer2_xop_alloc(pmp->iroot, HAMMER2_XOP_MODIFYING);
		hammer2_xop_start(&xop->head, &hammer2_ipcluster_desc);
		error = hammer2_xop_collect(&xop->head, 0);

		if (error == 0) {
			meta = &hammer2_xop_gdata(&xop->head)->ipdata.meta;
			pmp->iroot->meta = *meta;
			pmp->inode_tid = meta->pfs_inum + 1;
			hammer2_xop_pdata(&xop->head);
			/* meta invalid */

			if (pmp->inode_tid < HAMMER2_INODE_START)
				pmp->inode_tid = HAMMER2_INODE_START;
			pmp->modify_tid =
				xop->head.cluster.focus->bref.modify_tid + 1;
#if 0
			kprintf("PFS: Starting inode %jd\n",
				(intmax_t)pmp->inode_tid);
			kprintf("PMP focus good set nextino=%ld mod=%016jx\n",
				pmp->inode_tid, pmp->modify_tid);
#endif
			wakeup(&pmp->iroot);

			hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);

			/*
			 * Prime the mount info.
			 */
			hammer2_vfs_statfs(mp, &mp->mnt_stat, NULL);
			break;
		}

		/*
		 * Loop, try again
		 */
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		hammer2_inode_unlock(pmp->iroot);
		error = tsleep(&pmp->iroot, PCATCH, "h2root", hz);
		hammer2_inode_lock(pmp->iroot, HAMMER2_RESOLVE_SHARED);
		if (error == EINTR)
			break;
	}

	if (error) {
		hammer2_inode_unlock(pmp->iroot);
		*vpp = NULL;
	} else {
		vp = hammer2_igetv(pmp->iroot, &error);
		hammer2_inode_unlock(pmp->iroot);
		*vpp = vp;
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
	struct statfs tmp;
	int i;

	/*
	 * NOTE: iroot might not have validated the cluster yet.
	 */
	pmp = MPTOPMP(mp);

	bzero(&tmp, sizeof(tmp));

	for (i = 0; i < pmp->iroot->cluster.nchains; ++i) {
		hmp = pmp->pfs_hmps[i];
		if (hmp == NULL)
			continue;
		if (pmp->iroot->cluster.array[i].chain)
			bref = pmp->iroot->cluster.array[i].chain->bref;
		else
			bzero(&bref, sizeof(bref));

		tmp.f_files = bref.embed.stats.inode_count;
		tmp.f_ffree = 0;
		tmp.f_blocks = hmp->voldata.allocator_size /
			       mp->mnt_vstat.f_bsize;
		tmp.f_bfree = hmp->voldata.allocator_free /
			      mp->mnt_vstat.f_bsize;
		tmp.f_bavail = tmp.f_bfree;

		if (cred && cred->cr_uid != 0) {
			uint64_t adj;

			/* 5% */
			adj = hmp->free_reserved / mp->mnt_vstat.f_bsize;
			tmp.f_blocks -= adj;
			tmp.f_bfree -= adj;
			tmp.f_bavail -= adj;
		}

		mp->mnt_stat.f_blocks = tmp.f_blocks;
		mp->mnt_stat.f_bfree = tmp.f_bfree;
		mp->mnt_stat.f_bavail = tmp.f_bavail;
		mp->mnt_stat.f_files = tmp.f_files;
		mp->mnt_stat.f_ffree = tmp.f_ffree;

		*sbp = mp->mnt_stat;
	}
	return (0);
}

static
int
hammer2_vfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	hammer2_pfs_t *pmp;
	hammer2_dev_t *hmp;
	hammer2_blockref_t bref;
	struct statvfs tmp;
	int i;

	/*
	 * NOTE: iroot might not have validated the cluster yet.
	 */
	pmp = MPTOPMP(mp);
	bzero(&tmp, sizeof(tmp));

	for (i = 0; i < pmp->iroot->cluster.nchains; ++i) {
		hmp = pmp->pfs_hmps[i];
		if (hmp == NULL)
			continue;
		if (pmp->iroot->cluster.array[i].chain)
			bref = pmp->iroot->cluster.array[i].chain->bref;
		else
			bzero(&bref, sizeof(bref));

		tmp.f_files = bref.embed.stats.inode_count;
		tmp.f_ffree = 0;
		tmp.f_blocks = hmp->voldata.allocator_size /
			       mp->mnt_vstat.f_bsize;
		tmp.f_bfree = hmp->voldata.allocator_free /
			      mp->mnt_vstat.f_bsize;
		tmp.f_bavail = tmp.f_bfree;

		if (cred && cred->cr_uid != 0) {
			uint64_t adj;

			/* 5% */
			adj = hmp->free_reserved / mp->mnt_vstat.f_bsize;
			tmp.f_blocks -= adj;
			tmp.f_bfree -= adj;
			tmp.f_bavail -= adj;
		}

		mp->mnt_vstat.f_blocks = tmp.f_blocks;
		mp->mnt_vstat.f_bfree = tmp.f_bfree;
		mp->mnt_vstat.f_bavail = tmp.f_bavail;
		mp->mnt_vstat.f_files = tmp.f_files;
		mp->mnt_vstat.f_ffree = tmp.f_ffree;

		*sbp = mp->mnt_vstat;
	}
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
	hammer2_tid_t	mtid;
	int	depth;
};

static int hammer2_recovery_scan(hammer2_dev_t *hmp,
			hammer2_chain_t *parent,
			struct hammer2_recovery_info *info,
			hammer2_tid_t sync_tid);

#define HAMMER2_RECOVERY_MAXDEPTH	10

static
int
hammer2_recovery(hammer2_dev_t *hmp)
{
	struct hammer2_recovery_info info;
	struct hammer2_recovery_elm *elm;
	hammer2_chain_t *parent;
	hammer2_tid_t sync_tid;
	hammer2_tid_t mirror_tid;
	int error;

	hammer2_trans_init(hmp->spmp, 0);

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
	error = hammer2_recovery_scan(hmp, parent, &info, sync_tid);
	hammer2_chain_lookup_done(parent);

	while ((elm = TAILQ_FIRST(&info.list)) != NULL) {
		TAILQ_REMOVE(&info.list, elm, entry);
		parent = elm->chain;
		sync_tid = elm->sync_tid;
		kfree(elm, M_HAMMER2);

		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
		error |= hammer2_recovery_scan(hmp, parent, &info,
					      hmp->voldata.freemap_tid);
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);	/* drop elm->chain ref */
	}

	hammer2_trans_done(hmp->spmp, 0);

	return error;
}

static
int
hammer2_recovery_scan(hammer2_dev_t *hmp, hammer2_chain_t *parent,
		      struct hammer2_recovery_info *info,
		      hammer2_tid_t sync_tid)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_chain_t *chain;
	hammer2_blockref_t bref;
	int tmp_error;
	int rup_error;
	int error;
	int first;

	/*
	 * Adjust freemap to ensure that the block(s) are marked allocated.
	 */
	if (parent->bref.type != HAMMER2_BREF_TYPE_VOLUME) {
		hammer2_freemap_adjust(hmp, &parent->bref,
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
	case HAMMER2_BREF_TYPE_DIRENT:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		/* not applicable to recovery scan */
		return 0;
		break;
	default:
		return HAMMER2_ERROR_BADBREF;
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
	 *
	 * error	Cumulative error this level only
	 * rup_error	Cumulative error for recursion
	 * tmp_error	Specific non-cumulative recursion error
	 */
	chain = NULL;
	first = 1;
	rup_error = 0;
	error = 0;

	for (;;) {
		error |= hammer2_chain_scan(parent, &chain, &bref,
					    &first,
					    HAMMER2_LOOKUP_NODATA);

		/*
		 * Problem during scan or EOF
		 */
		if (error)
			break;

		/*
		 * If this is a leaf
		 */
		if (chain == NULL) {
			if (bref.mirror_tid > sync_tid) {
				hammer2_freemap_adjust(hmp, &bref,
						     HAMMER2_FREEMAP_DORECOVER);
			}
			continue;
		}

		/*
		 * This may or may not be a recursive node.
		 */
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_RELEASE);
		if (bref.mirror_tid > sync_tid) {
			++info->depth;
			tmp_error = hammer2_recovery_scan(hmp, chain,
							   info, sync_tid);
			--info->depth;
		} else {
			tmp_error = 0;
		}

		/*
		 * Flush the recovery at the PFS boundary to stage it for
		 * the final flush of the super-root topology.
		 */
		if (tmp_error == 0 &&
		    (bref.flags & HAMMER2_BREF_FLAG_PFSROOT) &&
		    (chain->flags & HAMMER2_CHAIN_ONFLUSH)) {
			hammer2_flush(chain, HAMMER2_FLUSH_TOP |
					     HAMMER2_FLUSH_ALL);
		}
		rup_error |= tmp_error;
	}
	return ((error | rup_error) & ~HAMMER2_ERROR_EOF);
}

/*
 * This fixes up an error introduced in earlier H2 implementations where
 * moving a PFS inode into an indirect block wound up causing the
 * HAMMER2_BREF_FLAG_PFSROOT flag in the bref to get cleared.
 */
static
int
hammer2_fixup_pfses(hammer2_dev_t *hmp)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	hammer2_pfs_t *spmp;
	int error;

	error = 0;

	/*
	 * Lookup mount point under the media-localized super-root.
	 *
	 * cluster->pmp will incorrectly point to spmp and must be fixed
	 * up later on.
	 */
	spmp = hmp->spmp;
	hammer2_inode_lock(spmp->iroot, 0);
	parent = hammer2_inode_chain(spmp->iroot, 0, HAMMER2_RESOLVE_ALWAYS);
	chain = hammer2_chain_lookup(&parent, &key_next,
					 HAMMER2_KEY_MIN, HAMMER2_KEY_MAX,
					 &error, 0);
	while (chain) {
		if (chain->bref.type != HAMMER2_BREF_TYPE_INODE)
			continue;
		if (chain->error) {
			kprintf("I/O error scanning PFS labels\n");
			error |= chain->error;
		} else if ((chain->bref.flags &
			    HAMMER2_BREF_FLAG_PFSROOT) == 0) {
			int error2;

			ripdata = &chain->data->ipdata;
			hammer2_trans_init(hmp->spmp, 0);
			error2 = hammer2_chain_modify(chain,
						      chain->bref.modify_tid,
						      0, 0);
			if (error2 == 0) {
				kprintf("hammer2: Correct mis-flagged PFS %s\n",
					ripdata->filename);
				chain->bref.flags |= HAMMER2_BREF_FLAG_PFSROOT;
			} else {
				error |= error2;
			}
			hammer2_flush(chain, HAMMER2_FLUSH_TOP |
					     HAMMER2_FLUSH_ALL);
			hammer2_trans_done(hmp->spmp, 0);
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, HAMMER2_KEY_MAX,
					   &error, 0);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	hammer2_inode_unlock(spmp->iroot);

	return error;
}

/*
 * Sync a mount point; this is called periodically on a per-mount basis from
 * the filesystem syncer, and whenever a user issues a sync.
 */
int
hammer2_vfs_sync(struct mount *mp, int waitfor)
{
	int error;

	error = hammer2_vfs_sync_pmp(MPTOPMP(mp), waitfor);

	return error;
}

/*
 * Because frontend operations lock vnodes before we get a chance to
 * lock the related inode, we can't just acquire a vnode lock without
 * risking a deadlock.  The frontend may be holding a vnode lock while
 * also blocked on our SYNCQ flag while trying to get the inode lock.
 *
 * To deal with this situation we can check the vnode lock situation
 * after locking the inode and perform a work-around.
 */
int
hammer2_vfs_sync_pmp(hammer2_pfs_t *pmp, int waitfor)
{
	struct mount *mp;
	/*hammer2_xop_flush_t *xop;*/
	/*struct hammer2_sync_info info;*/
	hammer2_inode_t *ip;
	hammer2_depend_t *depend;
	hammer2_depend_t *depend_next;
	struct vnode *vp;
	uint32_t pass2;
	int error;
	int wakecount;
	int dorestart;

	mp = pmp->mp;

	/*
	 * Move all inodes on sideq to syncq.  This will clear sideq.
	 * This should represent all flushable inodes.  These inodes
	 * will already have refs due to being on syncq or sideq.  We
	 * must do this all at once with the spinlock held to ensure that
	 * all inode dependencies are part of the same flush.
	 *
	 * We should be able to do this asynchronously from frontend
	 * operations because we will be locking the inodes later on
	 * to actually flush them, and that will partition any frontend
	 * op using the same inode.  Either it has already locked the
	 * inode and we will block, or it has not yet locked the inode
	 * and it will block until we are finished flushing that inode.
	 *
	 * When restarting, only move the inodes flagged as PASS2 from
	 * SIDEQ to SYNCQ.  PASS2 propagation by inode_lock4() and
	 * inode_depend() are atomic with the spin-lock.
	 */
	hammer2_trans_init(pmp, HAMMER2_TRANS_ISFLUSH);
#ifdef HAMMER2_DEBUG_SYNC
	kprintf("FILESYSTEM SYNC BOUNDARY\n");
#endif
	dorestart = 0;

	/*
	 * Move inodes from depq to syncq, releasing the related
	 * depend structures.
	 */
restart:
#ifdef HAMMER2_DEBUG_SYNC
	kprintf("FILESYSTEM SYNC RESTART (%d)\n", dorestart);
#endif
	hammer2_trans_setflags(pmp, 0/*HAMMER2_TRANS_COPYQ*/);
	hammer2_trans_clearflags(pmp, HAMMER2_TRANS_RESCAN);

	/*
	 * Move inodes from depq to syncq.  When restarting, only depq's
	 * marked pass2 are moved.
	 */
	hammer2_spin_ex(&pmp->list_spin);
	depend_next = TAILQ_FIRST(&pmp->depq);
	wakecount = 0;

	while ((depend = depend_next) != NULL) {
		depend_next = TAILQ_NEXT(depend, entry);
		if (dorestart && depend->pass2 == 0)
			continue;
		TAILQ_FOREACH(ip, &depend->sideq, entry) {
			KKASSERT(ip->flags & HAMMER2_INODE_SIDEQ);
			atomic_set_int(&ip->flags, HAMMER2_INODE_SYNCQ);
			atomic_clear_int(&ip->flags, HAMMER2_INODE_SIDEQ);
			ip->depend = NULL;
		}

		/*
		 * NOTE: pmp->sideq_count includes both sideq and syncq
		 */
		TAILQ_CONCAT(&pmp->syncq, &depend->sideq, entry);

		depend->count = 0;
		depend->pass2 = 0;
		TAILQ_REMOVE(&pmp->depq, depend, entry);
	}

	hammer2_spin_unex(&pmp->list_spin);
	hammer2_trans_clearflags(pmp, /*HAMMER2_TRANS_COPYQ |*/
				      HAMMER2_TRANS_WAITING);
	dorestart = 0;

	/*
	 * sideq_count may have dropped enough to allow us to unstall
	 * the frontend.
	 */
	hammer2_pfs_memory_wakeup(pmp, 0);

	/*
	 * Now run through all inodes on syncq.
	 *
	 * Flush transactions only interlock with other flush transactions.
	 * Any conflicting frontend operations will block on the inode, but
	 * may hold a vnode lock while doing so.
	 */
	hammer2_spin_ex(&pmp->list_spin);
	while ((ip = TAILQ_FIRST(&pmp->syncq)) != NULL) {
		/*
		 * Remove the inode from the SYNCQ, transfer the syncq ref
		 * to us.  We must clear SYNCQ to allow any potential
		 * front-end deadlock to proceed.  We must set PASS2 so
		 * the dependency code knows what to do.
		 */
		pass2 = ip->flags;
		cpu_ccfence();
		if (atomic_cmpset_int(&ip->flags,
			      pass2,
			      (pass2 & ~(HAMMER2_INODE_SYNCQ |
					 HAMMER2_INODE_SYNCQ_WAKEUP)) |
			      HAMMER2_INODE_SYNCQ_PASS2) == 0) {
			continue;
		}
		TAILQ_REMOVE(&pmp->syncq, ip, entry);
		--pmp->sideq_count;
		hammer2_spin_unex(&pmp->list_spin);

		/*
		 * Tickle anyone waiting on ip->flags or the hysteresis
		 * on the dirty inode count.
		 */
		if (pass2 & HAMMER2_INODE_SYNCQ_WAKEUP)
			wakeup(&ip->flags);
		if (++wakecount >= hammer2_limit_dirty_inodes / 20 + 1) {
			wakecount = 0;
			hammer2_pfs_memory_wakeup(pmp, 0);
		}

		/*
		 * Relock the inode, and we inherit a ref from the above.
		 * We will check for a race after we acquire the vnode.
		 */
		hammer2_mtx_ex(&ip->lock);

		/*
		 * We need the vp in order to vfsync() dirty buffers, so if
		 * one isn't attached we can skip it.
		 *
		 * Ordering the inode lock and then the vnode lock has the
		 * potential to deadlock.  If we had left SYNCQ set that could
		 * also deadlock us against the frontend even if we don't hold
		 * any locks, but the latter is not a problem now since we
		 * cleared it.  igetv will temporarily release the inode lock
		 * in a safe manner to work-around the deadlock.
		 *
		 * Unfortunately it is still possible to deadlock when the
		 * frontend obtains multiple inode locks, because all the
		 * related vnodes are already locked (nor can the vnode locks
		 * be released and reacquired without messing up RECLAIM and
		 * INACTIVE sequencing).
		 *
		 * The solution for now is to move the vp back onto SIDEQ
		 * and set dorestart, which will restart the flush after we
		 * exhaust the current SYNCQ.  Note that additional
		 * dependencies may build up, so we definitely need to move
		 * the whole SIDEQ back to SYNCQ when we restart.
		 */
		vp = ip->vp;
		if (vp) {
			if (vget(vp, LK_EXCLUSIVE|LK_NOWAIT)) {
				/*
				 * Failed to get the vnode, requeue the inode
				 * (PASS2 is already set so it will be found
				 * again on the restart).
				 *
				 * Then unlock, possibly sleep, and retry
				 * later.  We sleep if PASS2 was *previously*
				 * set, before we set it again above.
				 */
				vp = NULL;
				dorestart = 1;
#ifdef HAMMER2_DEBUG_SYNC
				kprintf("inum %ld (sync delayed by vnode)\n",
					(long)ip->meta.inum);
#endif
				hammer2_inode_delayed_sideq(ip);

				hammer2_mtx_unlock(&ip->lock);
				hammer2_inode_drop(ip);

				if (pass2 & HAMMER2_INODE_SYNCQ_PASS2) {
					tsleep(&dorestart, 0, "h2syndel", 2);
				}
				hammer2_spin_ex(&pmp->list_spin);
				continue;
			}
		} else {
			vp = NULL;
		}

		/*
		 * If the inode wound up on a SIDEQ again it will already be
		 * prepped for another PASS2.  In this situation if we flush
		 * it now we will just wind up flushing it again in the same
		 * syncer run, so we might as well not flush it now.
		 */
		if (ip->flags & HAMMER2_INODE_SIDEQ) {
			hammer2_mtx_unlock(&ip->lock);
			hammer2_inode_drop(ip);
			if (vp)
				vput(vp);
			dorestart = 1;
			hammer2_spin_ex(&pmp->list_spin);
			continue;
		}

		/*
		 * Ok we have the inode exclusively locked and if vp is
		 * not NULL that will also be exclusively locked.  Do the
		 * meat of the flush.
		 *
		 * vp token needed for v_rbdirty_tree check / vclrisdirty
		 * sequencing.  Though we hold the vnode exclusively so
		 * we shouldn't need to hold the token also in this case.
		 */
		if (vp) {
			vfsync(vp, MNT_WAIT, 1, NULL, NULL);
			bio_track_wait(&vp->v_track_write, 0, 0); /* XXX */
		}

		/*
		 * If the inode has not yet been inserted into the tree
		 * we must do so.  Then sync and flush it.  The flush should
		 * update the parent.
		 */
		if (ip->flags & HAMMER2_INODE_DELETING) {
#ifdef HAMMER2_DEBUG_SYNC
			kprintf("inum %ld destroy\n", (long)ip->meta.inum);
#endif
			hammer2_inode_chain_des(ip);
			atomic_add_long(&hammer2_iod_inode_deletes, 1);
		} else if (ip->flags & HAMMER2_INODE_CREATING) {
#ifdef HAMMER2_DEBUG_SYNC
			kprintf("inum %ld insert\n", (long)ip->meta.inum);
#endif
			hammer2_inode_chain_ins(ip);
			atomic_add_long(&hammer2_iod_inode_creates, 1);
		}
#ifdef HAMMER2_DEBUG_SYNC
		kprintf("inum %ld chain-sync\n", (long)ip->meta.inum);
#endif

		/*
		 * Because I kinda messed up the design and index the inodes
		 * under the root inode, along side the directory entries,
		 * we can't flush the inode index under the iroot until the
		 * end.  If we do it now we might miss effects created by
		 * other inodes on the SYNCQ.
		 *
		 * Do a normal (non-FSSYNC) flush instead, which allows the
		 * vnode code to work the same.  We don't want to force iroot
		 * back onto the SIDEQ, and we also don't want the flush code
		 * to update pfs_iroot_blocksets until the final flush later.
		 *
		 * XXX at the moment this will likely result in a double-flush
		 * of the iroot chain.
		 */
		hammer2_inode_chain_sync(ip);
		if (ip == pmp->iroot) {
			hammer2_inode_chain_flush(ip, HAMMER2_XOP_INODE_STOP);
		} else {
			hammer2_inode_chain_flush(ip, HAMMER2_XOP_INODE_STOP |
						      HAMMER2_XOP_FSSYNC);
		}
		if (vp) {
			lwkt_gettoken(&vp->v_token);
			if ((ip->flags & (HAMMER2_INODE_MODIFIED |
					  HAMMER2_INODE_RESIZED |
					  HAMMER2_INODE_DIRTYDATA)) == 0 &&
			    RB_EMPTY(&vp->v_rbdirty_tree) &&
			    !bio_track_active(&vp->v_track_write)) {
				vclrisdirty(vp);
			} else {
				hammer2_inode_delayed_sideq(ip);
			}
			lwkt_reltoken(&vp->v_token);
			vput(vp);
			vp = NULL;	/* safety */
		}
		atomic_clear_int(&ip->flags, HAMMER2_INODE_SYNCQ_PASS2);
		hammer2_inode_unlock(ip);	/* unlock+drop */
		/* ip pointer invalid */

		/*
		 * If the inode got dirted after we dropped our locks,
		 * it will have already been moved back to the SIDEQ.
		 */
		hammer2_spin_ex(&pmp->list_spin);
	}
	hammer2_spin_unex(&pmp->list_spin);
	hammer2_pfs_memory_wakeup(pmp, 0);

	if (dorestart || (pmp->trans.flags & HAMMER2_TRANS_RESCAN)) {
#ifdef HAMMER2_DEBUG_SYNC
		kprintf("FILESYSTEM SYNC STAGE 1 RESTART\n");
		/*tsleep(&dorestart, 0, "h2STG1-R", hz*20);*/
#endif
		dorestart = 1;
		goto restart;
	}
#ifdef HAMMER2_DEBUG_SYNC
	kprintf("FILESYSTEM SYNC STAGE 2 BEGIN\n");
	/*tsleep(&dorestart, 0, "h2STG2", hz*20);*/
#endif

	/*
	 * We have to flush the PFS root last, even if it does not appear to
	 * be dirty, because all the inodes in the PFS are indexed under it.
	 * The normal flushing of iroot above would only occur if directory
	 * entries under the root were changed.
	 *
	 * Specifying VOLHDR will cause an additionl flush of hmp->spmp
	 * for the media making up the cluster.
	 */
	if ((ip = pmp->iroot) != NULL) {
		hammer2_inode_ref(ip);
		hammer2_mtx_ex(&ip->lock);
		hammer2_inode_chain_sync(ip);
		hammer2_inode_chain_flush(ip, HAMMER2_XOP_INODE_STOP |
					      HAMMER2_XOP_FSSYNC |
					      HAMMER2_XOP_VOLHDR);
		hammer2_inode_unlock(ip);	/* unlock+drop */
	}
#ifdef HAMMER2_DEBUG_SYNC
	kprintf("FILESYSTEM SYNC STAGE 2 DONE\n");
#endif

	/*
	 * device bioq sync
	 */
	hammer2_bioq_sync(pmp);

#if 0
	info.pass = 1;
	info.waitfor = MNT_WAIT;
	vsyncscan(mp, flags, hammer2_sync_scan2, &info);

	info.pass = 2;
	info.waitfor = MNT_WAIT;
	vsyncscan(mp, flags, hammer2_sync_scan2, &info);
#endif
#if 0
	/*
	 * Generally speaking we now want to flush the media topology from
	 * the iroot through to the inodes.  The flush stops at any inode
	 * boundary, which allows the frontend to continue running concurrent
	 * modifying operations on inodes (including kernel flushes of
	 * buffers) without interfering with the main sync.
	 *
	 * Use the XOP interface to concurrently flush all nodes to
	 * synchronize the PFSROOT subtopology to the media.  A standard
	 * end-of-scan ENOENT error indicates cluster sufficiency.
	 *
	 * Note that this flush will not be visible on crash recovery until
	 * we flush the super-root topology in the next loop.
	 *
	 * XXX For now wait for all flushes to complete.
	 */
	if (mp && (ip = pmp->iroot) != NULL) {
		/*
		 * If unmounting try to flush everything including any
		 * sub-trees under inodes, just in case there is dangling
		 * modified data, as a safety.  Otherwise just flush up to
		 * the inodes in this stage.
		 */
		kprintf("MP & IROOT\n");
#ifdef HAMMER2_DEBUG_SYNC
		kprintf("FILESYSTEM SYNC STAGE 3 IROOT BEGIN\n");
#endif
		if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
			xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING |
						    HAMMER2_XOP_VOLHDR |
						    HAMMER2_XOP_FSSYNC |
						    HAMMER2_XOP_INODE_STOP);
		} else {
			xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING |
						    HAMMER2_XOP_INODE_STOP |
						    HAMMER2_XOP_VOLHDR |
						    HAMMER2_XOP_FSSYNC |
						    HAMMER2_XOP_INODE_STOP);
		}
		hammer2_xop_start(&xop->head, &hammer2_inode_flush_desc);
		error = hammer2_xop_collect(&xop->head,
					    HAMMER2_XOP_COLLECT_WAITALL);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
#ifdef HAMMER2_DEBUG_SYNC
		kprintf("FILESYSTEM SYNC STAGE 3 IROOT END\n");
#endif
		if (error == HAMMER2_ERROR_ENOENT)
			error = 0;
		else
			error = hammer2_error_to_errno(error);
	} else {
		error = 0;
	}
#endif
	error = 0;	/* XXX */
	hammer2_trans_done(pmp, HAMMER2_TRANS_ISFLUSH);

	return (error);
}

static
int
hammer2_vfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	hammer2_inode_t *ip;

	KKASSERT(MAXFIDSZ >= 16);
	ip = VTOI(vp);
	fhp->fid_len = offsetof(struct fid, fid_data[16]);
	fhp->fid_ext = 0;
	((hammer2_tid_t *)fhp->fid_data)[0] = ip->meta.inum;
	((hammer2_tid_t *)fhp->fid_data)[1] = 0;

	return 0;
}

static
int
hammer2_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
	       struct fid *fhp, struct vnode **vpp)
{
	hammer2_pfs_t *pmp;
	hammer2_tid_t inum;
	int error;

	pmp = MPTOPMP(mp);
	inum = ((hammer2_tid_t *)fhp->fid_data)[0] & HAMMER2_DIRHASH_USERMSK;
	if (vpp) {
		if (inum == 1)
			error = hammer2_vfs_root(mp, vpp);
		else
			error = hammer2_vfs_vget(mp, NULL, inum, vpp);
	} else {
		error = 0;
	}
	if (error)
		kprintf("fhtovp: %016jx -> %p, %d\n", inum, *vpp, error);
	return error;
}

static
int
hammer2_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
		 int *exflagsp, struct ucred **credanonp)
{
	hammer2_pfs_t *pmp;
	struct netcred *np;
	int error;

	pmp = MPTOPMP(mp);
	np = vfs_export_lookup(mp, &pmp->export, nam);
	if (np) {
		*exflagsp = np->netc_exflags;
		*credanonp = &np->netc_anon;
		error = 0;
	} else {
		error = EACCES;
	}
	return error;
}

/*
 * Support code for hammer2_vfs_mount().  Read, verify, and install the volume
 * header into the HMP
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
		hmp->free_reserved = hmp->voldata.allocator_size / 20;
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
	if ((lwinprog & HAMMER2_LWINPROG_WAITING0) &&
	    (lwinprog & HAMMER2_LWINPROG_MASK) <= 0) {
		atomic_clear_int(&pmp->count_lwinprog,
				 HAMMER2_LWINPROG_WAITING0);
		wakeup(&pmp->count_lwinprog);
	}
}

void
hammer2_lwinprog_wait(hammer2_pfs_t *pmp, int flush_pipe)
{
	int lwinprog;
	int lwflag = (flush_pipe) ? HAMMER2_LWINPROG_WAITING :
				    HAMMER2_LWINPROG_WAITING0;

	for (;;) {
		lwinprog = pmp->count_lwinprog;
		cpu_ccfence();
		if ((lwinprog & HAMMER2_LWINPROG_MASK) <= flush_pipe)
			break;
		tsleep_interlock(&pmp->count_lwinprog, 0);
		atomic_set_int(&pmp->count_lwinprog, lwflag);
		lwinprog = pmp->count_lwinprog;
		if ((lwinprog & HAMMER2_LWINPROG_MASK) <= flush_pipe)
			break;
		tsleep(&pmp->count_lwinprog, PINTERLOCKED, "h2wpipe", hz);
	}
}

/*
 * It is possible for an excessive number of dirty chains or dirty inodes
 * to build up.  When this occurs we start an asynchronous filesystem sync.
 * If the level continues to build up, we stall, waiting for it to drop,
 * with some hysteresis.
 *
 * This relies on the kernel calling hammer2_vfs_modifying() prior to
 * obtaining any vnode locks before making a modifying VOP call.
 */
static int
hammer2_vfs_modifying(struct mount *mp)
{
	if (mp->mnt_flag & MNT_RDONLY)
		return EROFS;
	hammer2_pfs_memory_wait(MPTOPMP(mp));

	return 0;
}

/*
 * Initiate an asynchronous filesystem sync and, with hysteresis,
 * stall if the internal data structure count becomes too bloated.
 */
void
hammer2_pfs_memory_wait(hammer2_pfs_t *pmp)
{
	uint32_t waiting;
	int pcatch;
	int error;

	if (pmp == NULL || pmp->mp == NULL)
		return;

	for (;;) {
		waiting = pmp->inmem_dirty_chains & HAMMER2_DIRTYCHAIN_MASK;
		cpu_ccfence();

		/*
		 * Start the syncer running at 1/2 the limit
		 */
		if (waiting > hammer2_limit_dirty_chains / 2 ||
		    pmp->sideq_count > hammer2_limit_dirty_inodes / 2) {
			trigger_syncer(pmp->mp);
		}

		/*
		 * Stall at the limit waiting for the counts to drop.
		 * This code will typically be woken up once the count
		 * drops below 3/4 the limit, or in one second.
		 */
		if (waiting < hammer2_limit_dirty_chains &&
		    pmp->sideq_count < hammer2_limit_dirty_inodes) {
			break;
		}

		pcatch = curthread->td_proc ? PCATCH : 0;

		tsleep_interlock(&pmp->inmem_dirty_chains, pcatch);
		atomic_set_int(&pmp->inmem_dirty_chains,
			       HAMMER2_DIRTYCHAIN_WAITING);
		if (waiting < hammer2_limit_dirty_chains &&
		    pmp->sideq_count < hammer2_limit_dirty_inodes) {
			break;
		}
		trigger_syncer(pmp->mp);
		error = tsleep(&pmp->inmem_dirty_chains, PINTERLOCKED | pcatch,
			       "h2memw", hz);
		if (error == ERESTART)
			break;
	}
}

/*
 * Wake up any stalled frontend ops waiting, with hysteresis, using
 * 2/3 of the limit.
 */
void
hammer2_pfs_memory_wakeup(hammer2_pfs_t *pmp, int count)
{
	uint32_t waiting;

	if (pmp) {
		waiting = atomic_fetchadd_int(&pmp->inmem_dirty_chains, count);
		/* don't need --waiting to test flag */

		if ((waiting & HAMMER2_DIRTYCHAIN_WAITING) &&
		    (pmp->inmem_dirty_chains & HAMMER2_DIRTYCHAIN_MASK) <=
		    hammer2_limit_dirty_chains * 2 / 3 &&
		    pmp->sideq_count <= hammer2_limit_dirty_inodes * 2 / 3) {
			atomic_clear_int(&pmp->inmem_dirty_chains,
					 HAMMER2_DIRTYCHAIN_WAITING);
			wakeup(&pmp->inmem_dirty_chains);
		}
	}
}

void
hammer2_pfs_memory_inc(hammer2_pfs_t *pmp)
{
	if (pmp) {
		atomic_add_int(&pmp->inmem_dirty_chains, 1);
	}
}

/*
 * Returns 0 if the filesystem has tons of free space
 * Returns 1 if the filesystem has less than 10% remaining
 * Returns 2 if the filesystem has less than 2%/5% (user/root) remaining.
 */
int
hammer2_vfs_enospace(hammer2_inode_t *ip, off_t bytes, struct ucred *cred)
{
	hammer2_pfs_t *pmp;
	hammer2_dev_t *hmp;
	hammer2_off_t free_reserved;
	hammer2_off_t free_nominal;
	int i;

	pmp = ip->pmp;

	if (pmp->free_ticks == 0 || pmp->free_ticks != ticks) {
		free_reserved = HAMMER2_SEGSIZE;
		free_nominal = 0x7FFFFFFFFFFFFFFFLLU;
		for (i = 0; i < pmp->iroot->cluster.nchains; ++i) {
			hmp = pmp->pfs_hmps[i];
			if (hmp == NULL)
				continue;
			if (pmp->pfs_types[i] != HAMMER2_PFSTYPE_MASTER &&
			    pmp->pfs_types[i] != HAMMER2_PFSTYPE_SOFT_MASTER)
				continue;

			if (free_nominal > hmp->voldata.allocator_free)
				free_nominal = hmp->voldata.allocator_free;
			if (free_reserved < hmp->free_reserved)
				free_reserved = hmp->free_reserved;
		}

		/*
		 * SMP races ok
		 */
		pmp->free_reserved = free_reserved;
		pmp->free_nominal = free_nominal;
		pmp->free_ticks = ticks;
	} else {
		free_reserved = pmp->free_reserved;
		free_nominal = pmp->free_nominal;
	}
	if (cred && cred->cr_uid != 0) {
		if ((int64_t)(free_nominal - bytes) <
		    (int64_t)free_reserved) {
			return 2;
		}
	} else {
		if ((int64_t)(free_nominal - bytes) <
		    (int64_t)free_reserved / 2) {
			return 2;
		}
	}
	if ((int64_t)(free_nominal - bytes) < (int64_t)free_reserved * 2)
		return 1;
	return 0;
}

/*
 * Debugging
 */
void
hammer2_dump_chain(hammer2_chain_t *chain, int tab, int *countp, char pfx,
		   u_int flags)
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
		RB_FOREACH(scan, hammer2_chain_tree, &chain->core.rbtree) {
			if ((scan->flags & flags) || flags == (u_int)-1) {
				hammer2_dump_chain(scan, tab + 4, countp, 'a',
						   flags);
			}
		}
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE && chain->data)
			kprintf("%*.*s}(%s)\n", tab, tab, "",
				chain->data->ipdata.filename);
		else
			kprintf("%*.*s}\n", tab, tab, "");
	}
}
