/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 * HAMMER2 IN-MEMORY CACHE OF MEDIA STRUCTURES
 *
 * This header file contains structures used internally by the HAMMER2
 * implementation.  See hammer2_disk.h for on-disk structures.
 *
 * There is an in-memory representation of all on-media data structure.
 * Almost everything is represented by a hammer2_chain structure in-memory.
 * Other higher-level structures typically map to chains.
 *
 * A great deal of data is accessed simply via its buffer cache buffer,
 * which is mapped for the duration of the chain's lock.  Hammer2 must
 * implement its own buffer cache layer on top of the system layer to
 * allow for different threads to lock different sub-block-sized buffers.
 *
 * When modifications are made to a chain a new filesystem block must be
 * allocated.  Multiple modifications do not typically allocate new blocks
 * until the current block has been flushed.  Flushes do not block the
 * front-end unless the front-end operation crosses the current inode being
 * flushed.
 *
 * The in-memory representation may remain cached (for example in order to
 * placemark clustering locks) even after the related data has been
 * detached.
 */

#ifndef _VFS_HAMMER2_HAMMER2_H_
#define _VFS_HAMMER2_HAMMER2_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/tree.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/mountctl.h>
#include <sys/priv.h>
#include <sys/stat.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/lockf.h>
#include <sys/buf.h>
#include <sys/queue.h>
#include <sys/limits.h>
#include <sys/dmsg.h>
#include <sys/mutex.h>
#include <sys/kern_syscall.h>

#include <sys/signal2.h>
#include <sys/buf2.h>
#include <sys/mutex2.h>
#include <sys/thread2.h>

#include "hammer2_disk.h"
#include "hammer2_mount.h"
#include "hammer2_ioctl.h"

struct hammer2_io;
struct hammer2_iocb;
struct hammer2_chain;
struct hammer2_cluster;
struct hammer2_inode;
struct hammer2_dev;
struct hammer2_pfs;
struct hammer2_span;
struct hammer2_state;
struct hammer2_msg;
struct hammer2_thread;
union hammer2_xop;

/*
 * Mutex and lock shims.  Hammer2 requires support for asynchronous and
 * abortable locks, and both exclusive and shared spinlocks.  Normal
 * synchronous non-abortable locks can be substituted for spinlocks.
 */
typedef mtx_t				hammer2_mtx_t;
typedef mtx_link_t			hammer2_mtx_link_t;
typedef mtx_state_t			hammer2_mtx_state_t;

typedef struct spinlock			hammer2_spin_t;

#define hammer2_mtx_ex			mtx_lock_ex_quick
#define hammer2_mtx_sh			mtx_lock_sh_quick
#define hammer2_mtx_unlock		mtx_unlock
#define hammer2_mtx_owned		mtx_owned
#define hammer2_mtx_init		mtx_init
#define hammer2_mtx_temp_release	mtx_lock_temp_release
#define hammer2_mtx_temp_restore	mtx_lock_temp_restore
#define hammer2_mtx_refs		mtx_lockrefs

#define hammer2_spin_init		spin_init
#define hammer2_spin_sh			spin_lock_shared
#define hammer2_spin_ex			spin_lock
#define hammer2_spin_unsh		spin_unlock_shared
#define hammer2_spin_unex		spin_unlock

TAILQ_HEAD(hammer2_xop_list, hammer2_xop_head);

typedef struct hammer2_xop_list	hammer2_xop_list_t;


/*
 * General lock support
 */
static __inline
int
hammer2_mtx_upgrade(hammer2_mtx_t *mtx)
{
	int wasexclusive;

	if (mtx_islocked_ex(mtx)) {
		wasexclusive = 1;
	} else {
		mtx_unlock(mtx);
		mtx_lock_ex_quick(mtx);
		wasexclusive = 0;
	}
	return wasexclusive;
}

/*
 * Downgrade an inode lock from exclusive to shared only if the inode
 * lock was previously shared.  If the inode lock was previously exclusive,
 * this is a NOP.
 */
static __inline
void
hammer2_mtx_downgrade(hammer2_mtx_t *mtx, int wasexclusive)
{
	if (wasexclusive == 0)
		mtx_downgrade(mtx);
}

/*
 * The xid tracks internal transactional updates.
 *
 * XXX fix-me, really needs to be 64-bits
 */
typedef uint32_t hammer2_xid_t;

#define HAMMER2_XID_MIN	0x00000000U
#define HAMMER2_XID_MAX 0x7FFFFFFFU

/*
 * The chain structure tracks a portion of the media topology from the
 * root (volume) down.  Chains represent volumes, inodes, indirect blocks,
 * data blocks, and freemap nodes and leafs.
 *
 * The chain structure utilizes a simple singly-homed topology and the
 * chain's in-memory topology will move around as the chains do, due mainly
 * to renames and indirect block creation.
 *
 * Block Table Updates
 *
 *	Block table updates for insertions and updates are delayed until the
 *	flush.  This allows us to avoid having to modify the parent chain
 *	all the way to the root.
 *
 *	Block table deletions are performed immediately (modifying the parent
 *	in the process) because the flush code uses the chain structure to
 *	track delayed updates and the chain will be (likely) gone or moved to
 *	another location in the topology after a deletion.
 *
 *	A prior iteration of the code tried to keep the relationship intact
 *	on deletes by doing a delete-duplicate operation on the chain, but
 *	it added way too much complexity to the codebase.
 *
 * Flush Synchronization
 *
 *	The flush code must flush modified chains bottom-up.  Because chain
 *	structures can shift around and are NOT topologically stable,
 *	modified chains are independently indexed for the flush.  As the flush
 *	runs it modifies (or further modifies) and updates the parents,
 *	propagating the flush all the way to the volume root.
 *
 *	Modifying front-end operations can occur during a flush but will block
 *	in two cases: (1) when the front-end tries to operate on the inode
 *	currently in the midst of being flushed and (2) if the front-end
 *	crosses an inode currently being flushed (such as during a rename).
 *	So, for example, if you rename directory "x" to "a/b/c/d/e/f/g/x" and
 *	the flusher is currently working on "a/b/c", the rename will block
 *	temporarily in order to ensure that "x" exists in one place or the
 *	other.
 *
 *	Meta-data statistics are updated by the flusher.  The front-end will
 *	make estimates but meta-data must be fully synchronized only during a
 *	flush in order to ensure that it remains correct across a crash.
 *
 *	Multiple flush synchronizations can theoretically be in-flight at the
 *	same time but the implementation is not coded to handle the case and
 *	currently serializes them.
 *
 * Snapshots:
 *
 *	Snapshots currently require the subdirectory tree being snapshotted
 *	to be flushed.  The snapshot then creates a new super-root inode which
 *	copies the flushed blockdata of the directory or file that was
 *	snapshotted.
 *
 * RBTREE NOTES:
 *
 *	- Note that the radix tree runs in powers of 2 only so sub-trees
 *	  cannot straddle edges.
 */
RB_HEAD(hammer2_chain_tree, hammer2_chain);
TAILQ_HEAD(h2_flush_list, hammer2_chain);
TAILQ_HEAD(h2_core_list, hammer2_chain);
TAILQ_HEAD(h2_iocb_list, hammer2_iocb);

#define CHAIN_CORE_DELETE_BMAP_ENTRIES	\
	(HAMMER2_PBUFSIZE / sizeof(hammer2_blockref_t) / sizeof(uint32_t))

/*
 * Core topology for chain (embedded in chain).  Protected by a spinlock.
 */
struct hammer2_chain_core {
	hammer2_spin_t	spin;
	struct hammer2_chain_tree rbtree; /* sub-chains */
	int		live_zero;	/* blockref array opt */
	u_int		live_count;	/* live (not deleted) chains in tree */
	u_int		chain_count;	/* live + deleted chains under core */
	int		generation;	/* generation number (inserts only) */
};

typedef struct hammer2_chain_core hammer2_chain_core_t;

RB_HEAD(hammer2_io_tree, hammer2_io);

/*
 * IOCB - IO callback (into chain, cluster, or manual request)
 */
struct hammer2_iocb {
	TAILQ_ENTRY(hammer2_iocb) entry;
	void (*callback)(struct hammer2_iocb *iocb);
	struct hammer2_io	*dio;
	struct hammer2_cluster	*cluster;
	struct hammer2_chain	*chain;
	void			*ptr;
	off_t			lbase;
	int			lsize;
	uint32_t		flags;
	int			error;
};

typedef struct hammer2_iocb hammer2_iocb_t;

#define HAMMER2_IOCB_INTERLOCK	0x00000001
#define HAMMER2_IOCB_ONQ	0x00000002
#define HAMMER2_IOCB_DONE	0x00000004
#define HAMMER2_IOCB_INPROG	0x00000008
#define HAMMER2_IOCB_UNUSED10	0x00000010
#define HAMMER2_IOCB_QUICK	0x00010000
#define HAMMER2_IOCB_ZERO	0x00020000
#define HAMMER2_IOCB_READ	0x00040000
#define HAMMER2_IOCB_WAKEUP	0x00080000

/*
 * DIO - Management structure wrapping system buffer cache.
 *
 *	 Used for multiple purposes including concurrent management
 *	 if small requests by chains into larger DIOs.
 */
struct hammer2_io {
	RB_ENTRY(hammer2_io) rbnode;	/* indexed by device offset */
	struct h2_iocb_list iocbq;
	struct spinlock spin;
	struct hammer2_dev *hmp;
	struct buf	*bp;
	off_t		pbase;
	int		psize;
	int		refs;
	int		act;			/* activity */
};

typedef struct hammer2_io hammer2_io_t;

#define HAMMER2_DIO_INPROG	0x80000000	/* bio in progress */
#define HAMMER2_DIO_GOOD	0x40000000	/* dio->bp is stable */
#define HAMMER2_DIO_WAITING	0x20000000	/* (old) */
#define HAMMER2_DIO_DIRTY	0x10000000	/* flush on last drop */

#define HAMMER2_DIO_MASK	0x0FFFFFFF

/*
 * Primary chain structure keeps track of the topology in-memory.
 */
struct hammer2_chain {
	hammer2_mtx_t		lock;
	hammer2_chain_core_t	core;
	RB_ENTRY(hammer2_chain) rbnode;		/* live chain(s) */
	hammer2_blockref_t	bref;
	struct hammer2_chain	*parent;
	struct hammer2_state	*state;		/* if active cache msg */
	struct hammer2_dev	*hmp;
	struct hammer2_pfs	*pmp;		/* A PFS or super-root (spmp) */

	hammer2_xid_t	flush_xid;		/* flush sequencing */
	hammer2_io_t	*dio;			/* physical data buffer */
	u_int		bytes;			/* physical data size */
	u_int		flags;
	u_int		refs;
	u_int		lockcnt;
	int		error;			/* on-lock data error state */

	hammer2_media_data_t *data;		/* data pointer shortcut */
	TAILQ_ENTRY(hammer2_chain) flush_node;	/* flush list */
};

typedef struct hammer2_chain hammer2_chain_t;

int hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2);
RB_PROTOTYPE(hammer2_chain_tree, hammer2_chain, rbnode, hammer2_chain_cmp);

/*
 * Special notes on flags:
 *
 * INITIAL	- This flag allows a chain to be created and for storage to
 *		  be allocated without having to immediately instantiate the
 *		  related buffer.  The data is assumed to be all-zeros.  It
 *		  is primarily used for indirect blocks.
 *
 * MODIFIED	- The chain's media data has been modified.
 *
 * UPDATE	- Chain might not be modified but parent blocktable needs update
 *
 * FICTITIOUS	- Faked chain as a placeholder for an error condition.  This
 *		  chain is unsuitable for I/O.
 *
 * BMAPPED	- Indicates that the chain is present in the parent blockmap.
 *
 * BMAPUPD	- Indicates that the chain is present but needs to be updated
 *		  in the parent blockmap.
 */
#define HAMMER2_CHAIN_MODIFIED		0x00000001	/* dirty chain data */
#define HAMMER2_CHAIN_ALLOCATED		0x00000002	/* kmalloc'd chain */
#define HAMMER2_CHAIN_DESTROY		0x00000004
#define HAMMER2_CHAIN_UNUSED0008	0x00000008
#define HAMMER2_CHAIN_DELETED		0x00000010	/* deleted chain */
#define HAMMER2_CHAIN_INITIAL		0x00000020	/* initial create */
#define HAMMER2_CHAIN_UPDATE		0x00000040	/* need parent update */
#define HAMMER2_CHAIN_DEFERRED		0x00000080	/* flush depth defer */
#define HAMMER2_CHAIN_IOFLUSH		0x00000100	/* bawrite on put */
#define HAMMER2_CHAIN_ONFLUSH		0x00000200	/* on a flush list */
#define HAMMER2_CHAIN_FICTITIOUS	0x00000400	/* unsuitable for I/O */
#define HAMMER2_CHAIN_VOLUMESYNC	0x00000800	/* needs volume sync */
#define HAMMER2_CHAIN_DELAYED		0x00001000	/* delayed flush */
#define HAMMER2_CHAIN_COUNTEDBREFS	0x00002000	/* block table stats */
#define HAMMER2_CHAIN_ONRBTREE		0x00004000	/* on parent RB tree */
#define HAMMER2_CHAIN_UNUSED00008000	0x00008000
#define HAMMER2_CHAIN_EMBEDDED		0x00010000	/* embedded data */
#define HAMMER2_CHAIN_RELEASE		0x00020000	/* don't keep around */
#define HAMMER2_CHAIN_BMAPPED		0x00040000	/* present in blkmap */
#define HAMMER2_CHAIN_BMAPUPD		0x00080000	/* +needs updating */
#define HAMMER2_CHAIN_IOINPROG		0x00100000	/* I/O interlock */
#define HAMMER2_CHAIN_IOSIGNAL		0x00200000	/* I/O interlock */
#define HAMMER2_CHAIN_PFSBOUNDARY	0x00400000	/* super->pfs inode */

#define HAMMER2_CHAIN_FLUSH_MASK	(HAMMER2_CHAIN_MODIFIED |	\
					 HAMMER2_CHAIN_UPDATE |		\
					 HAMMER2_CHAIN_ONFLUSH)

/*
 * Hammer2 error codes, used by chain->error and cluster->error.  The error
 * code is typically set on-lock unless no I/O was requested, and set on
 * I/O otherwise.  If set for a cluster it generally means that the cluster
 * code could not find a valid copy to present.
 *
 * IO		- An I/O error occurred
 * CHECK	- I/O succeeded but did not match the check code
 * INCOMPLETE	- A cluster is not complete enough to use, or
 *		  a chain cannot be loaded because its parent has an error.
 *
 * NOTE: API allows callers to check zero/non-zero to determine if an error
 *	 condition exists.
 *
 * NOTE: Chain's data field is usually NULL on an IO error but not necessarily
 *	 NULL on other errors.  Check chain->error, not chain->data.
 */
#define HAMMER2_ERROR_NONE		0
#define HAMMER2_ERROR_IO		1	/* device I/O error */
#define HAMMER2_ERROR_CHECK		2	/* check code mismatch */
#define HAMMER2_ERROR_INCOMPLETE	3	/* incomplete cluster */
#define HAMMER2_ERROR_DEPTH		4	/* temporary depth limit */

/*
 * Flags passed to hammer2_chain_lookup() and hammer2_chain_next()
 *
 * NOTES:
 *	NOLOCK	    - Input and output chains are referenced only and not
 *		      locked.  Output chain might be temporarily locked
 *		      internally.
 *
 *	NODATA	    - Asks that the chain->data not be resolved in order
 *		      to avoid I/O.
 *
 *	NODIRECT    - Prevents a lookup of offset 0 in an inode from returning
 *		      the inode itself if the inode is in DIRECTDATA mode
 *		      (i.e. file is <= 512 bytes).  Used by the synchronization
 *		      code to prevent confusion.
 *
 *	SHARED	    - The input chain is expected to be locked shared,
 *		      and the output chain is locked shared.
 *
 *	MATCHIND    - Allows an indirect block / freemap node to be returned
 *		      when the passed key range matches the radix.  Remember
 *		      that key_end is inclusive (e.g. {0x000,0xFFF},
 *		      not {0x000,0x1000}).
 *
 *		      (Cannot be used for remote or cluster ops).
 *
 *	ALLNODES    - Allows NULL focus.
 *
 *	ALWAYS	    - Always resolve the data.  If ALWAYS and NODATA are both
 *		      missing, bulk file data is not resolved but inodes and
 *		      other meta-data will.
 *
 *	NOUNLOCK    - Used by hammer2_chain_next() to leave the lock on
 *		      the input chain intact.  The chain is still dropped.
 *		      This allows the caller to add a reference to the chain
 *		      and retain it in a locked state (used by the
 *		      XOP/feed/collect code).
 */
#define HAMMER2_LOOKUP_NOLOCK		0x00000001	/* ref only */
#define HAMMER2_LOOKUP_NODATA		0x00000002	/* data left NULL */
#define HAMMER2_LOOKUP_NODIRECT		0x00000004	/* no offset=0 DD */
#define HAMMER2_LOOKUP_SHARED		0x00000100
#define HAMMER2_LOOKUP_MATCHIND		0x00000200	/* return all chains */
#define HAMMER2_LOOKUP_ALLNODES		0x00000400	/* allow NULL focus */
#define HAMMER2_LOOKUP_ALWAYS		0x00000800	/* resolve data */
#define HAMMER2_LOOKUP_NOUNLOCK		0x00001000	/* leave lock intact */

/*
 * Flags passed to hammer2_chain_modify() and hammer2_chain_resize()
 *
 * NOTE: OPTDATA allows us to avoid instantiating buffers for INDIRECT
 *	 blocks in the INITIAL-create state.
 */
#define HAMMER2_MODIFY_OPTDATA		0x00000002	/* data can be NULL */
#define HAMMER2_MODIFY_NO_MODIFY_TID	0x00000004
#define HAMMER2_MODIFY_UNUSED0008	0x00000008
#define HAMMER2_MODIFY_NOREALLOC	0x00000010

/*
 * Flags passed to hammer2_chain_lock()
 *
 * NOTE: RDONLY is set to optimize cluster operations when *no* modifications
 *	 will be made to either the cluster being locked or any underlying
 *	 cluster.  It allows the cluster to lock and access data for a subset
 *	 of available nodes instead of all available nodes.
 */
#define HAMMER2_RESOLVE_NEVER		1
#define HAMMER2_RESOLVE_MAYBE		2
#define HAMMER2_RESOLVE_ALWAYS		3
#define HAMMER2_RESOLVE_MASK		0x0F

#define HAMMER2_RESOLVE_SHARED		0x10	/* request shared lock */
#define HAMMER2_RESOLVE_UNUSED20	0x20
#define HAMMER2_RESOLVE_RDONLY		0x40	/* higher level op flag */

/*
 * Flags passed to hammer2_chain_delete()
 */
#define HAMMER2_DELETE_PERMANENT	0x0001
#define HAMMER2_DELETE_NOSTATS		0x0002

#define HAMMER2_INSERT_NOSTATS		0x0002
#define HAMMER2_INSERT_PFSROOT		0x0004

/*
 * Flags passed to hammer2_chain_delete_duplicate()
 */
#define HAMMER2_DELDUP_RECORE		0x0001

/*
 * Cluster different types of storage together for allocations
 */
#define HAMMER2_FREECACHE_INODE		0
#define HAMMER2_FREECACHE_INDIR		1
#define HAMMER2_FREECACHE_DATA		2
#define HAMMER2_FREECACHE_UNUSED3	3
#define HAMMER2_FREECACHE_TYPES		4

/*
 * hammer2_freemap_alloc() block preference
 */
#define HAMMER2_OFF_NOPREF		((hammer2_off_t)-1)

/*
 * BMAP read-ahead maximum parameters
 */
#define HAMMER2_BMAP_COUNT		16	/* max bmap read-ahead */
#define HAMMER2_BMAP_BYTES		(HAMMER2_PBUFSIZE * HAMMER2_BMAP_COUNT)

/*
 * hammer2_freemap_adjust()
 */
#define HAMMER2_FREEMAP_DORECOVER	1
#define HAMMER2_FREEMAP_DOMAYFREE	2
#define HAMMER2_FREEMAP_DOREALFREE	3

/*
 * HAMMER2 cluster - A set of chains representing the same entity.
 *
 * hammer2_cluster typically represents a temporary set of representitive
 * chains.  The one exception is that a hammer2_cluster is embedded in
 * hammer2_inode.  This embedded cluster is ONLY used to track the
 * representitive chains and cannot be directly locked.
 *
 * A cluster is usually temporary (and thus per-thread) for locking purposes,
 * allowing us to embed the asynchronous storage required for cluster
 * operations in the cluster itself and adjust the state and status without
 * having to worry too much about SMP issues.
 *
 * The exception is the cluster embedded in the hammer2_inode structure.
 * This is used to cache the cluster state on an inode-by-inode basis.
 * Individual hammer2_chain structures not incorporated into clusters might
 * also stick around to cache miscellanious elements.
 *
 * Because the cluster is a 'working copy' and is usually subject to cluster
 * quorum rules, it is quite possible for us to end up with an insufficient
 * number of live chains to execute an operation.  If an insufficient number
 * of chains remain in a working copy, the operation may have to be
 * downgraded, retried, stall until the requisit number of chains are
 * available, or possibly even error out depending on the mount type.
 *
 * A cluster's focus is set when it is locked.  The focus can only be set
 * to a chain still part of the synchronized set.
 */
#define HAMMER2_MAXCLUSTER	8
#define HAMMER2_XOPFIFO		16
#define HAMMER2_XOPFIFO_MASK	(HAMMER2_XOPFIFO - 1)
#define HAMMER2_XOPGROUPS	16
#define HAMMER2_XOPGROUPS_MASK	(HAMMER2_XOPGROUPS - 1)
#define HAMMER2_XOPMASK_VOP	0x80000000U

struct hammer2_cluster_item {
#if 0
	hammer2_mtx_link_t	async_link;
#endif
	hammer2_chain_t		*chain;
#if 0
	struct hammer2_cluster	*cluster;	/* link back to cluster */
#endif
	int			cache_index;
	uint32_t		flags;
};

typedef struct hammer2_cluster_item hammer2_cluster_item_t;

/*
 * INVALID	- Invalid for focus, i.e. not part of synchronized set.
 *		  Once set, this bit is sticky across operations.
 *
 * FEMOD	- Indicates that front-end modifying operations can
 *		  mess with this entry and MODSYNC will copy also
 *		  effect it.
 */
#define HAMMER2_CITEM_INVALID	0x00000001
#define HAMMER2_CITEM_FEMOD	0x00000002
#define HAMMER2_CITEM_NULL	0x00000004

struct hammer2_cluster {
	int			refs;		/* track for deallocation */
	int			ddflag;
	struct hammer2_pfs	*pmp;
	uint32_t		flags;
	int			nchains;
	int			error;		/* error code valid on lock */
	int			focus_index;
	hammer2_iocb_t		iocb;
	hammer2_chain_t		*focus;		/* current focus (or mod) */
	hammer2_cluster_item_t	array[HAMMER2_MAXCLUSTER];
};

typedef struct hammer2_cluster	hammer2_cluster_t;

/*
 * WRHARD	- Hard mounts can write fully synchronized
 * RDHARD	- Hard mounts can read fully synchronized
 * UNHARD	- Unsynchronized masters present
 * NOHARD	- No masters visible
 * WRSOFT	- Soft mounts can write to at least the SOFT_MASTER
 * RDSOFT	- Soft mounts can read from at least a SOFT_SLAVE
 * UNSOFT	- Unsynchronized slaves present
 * NOSOFT	- No slaves visible
 * RDSLAVE	- slaves are accessible (possibly unsynchronized or remote).
 * MSYNCED	- All masters are fully synchronized
 * SSYNCED	- All known local slaves are fully synchronized to masters
 *
 * All available masters are always incorporated.  All PFSs belonging to a
 * cluster (master, slave, copy, whatever) always try to synchronize the
 * total number of known masters in the PFSs root inode.
 *
 * A cluster might have access to many slaves, copies, or caches, but we
 * have a limited number of cluster slots.  Any such elements which are
 * directly mounted from block device(s) will always be incorporated.   Note
 * that SSYNCED only applies to such elements which are directly mounted,
 * not to any remote slaves, copies, or caches that could be available.  These
 * bits are used to monitor and drive our synchronization threads.
 *
 * When asking the question 'is any data accessible at all', then a simple
 * test against (RDHARD|RDSOFT|RDSLAVE) gives you the answer.  If any of
 * these bits are set the object can be read with certain caveats:
 * RDHARD - no caveats.  RDSOFT - authoritative but might not be synchronized.
 * and RDSLAVE - not authoritative, has some data but it could be old or
 * incomplete.
 *
 * When both soft and hard mounts are available, data will be read and written
 * via the soft mount only.  But all might be in the cluster because
 * background synchronization threads still need to do their work.
 */
#define HAMMER2_CLUSTER_INODE	0x00000001	/* embedded in inode struct */
#define HAMMER2_CLUSTER_UNUSED2	0x00000002
#define HAMMER2_CLUSTER_LOCKED	0x00000004	/* cluster lks not recursive */
#define HAMMER2_CLUSTER_WRHARD	0x00000100	/* hard-mount can write */
#define HAMMER2_CLUSTER_RDHARD	0x00000200	/* hard-mount can read */
#define HAMMER2_CLUSTER_UNHARD	0x00000400	/* unsynchronized masters */
#define HAMMER2_CLUSTER_NOHARD	0x00000800	/* no masters visible */
#define HAMMER2_CLUSTER_WRSOFT	0x00001000	/* soft-mount can write */
#define HAMMER2_CLUSTER_RDSOFT	0x00002000	/* soft-mount can read */
#define HAMMER2_CLUSTER_UNSOFT	0x00004000	/* unsynchronized slaves */
#define HAMMER2_CLUSTER_NOSOFT	0x00008000	/* no slaves visible */
#define HAMMER2_CLUSTER_MSYNCED	0x00010000	/* all masters synchronized */
#define HAMMER2_CLUSTER_SSYNCED	0x00020000	/* known slaves synchronized */

#define HAMMER2_CLUSTER_ANYDATA	( HAMMER2_CLUSTER_RDHARD |	\
				  HAMMER2_CLUSTER_RDSOFT |	\
				  HAMMER2_CLUSTER_RDSLAVE)

#define HAMMER2_CLUSTER_RDOK	( HAMMER2_CLUSTER_RDHARD |	\
				  HAMMER2_CLUSTER_RDSOFT)

#define HAMMER2_CLUSTER_WROK	( HAMMER2_CLUSTER_WRHARD |	\
				  HAMMER2_CLUSTER_WRSOFT)

#define HAMMER2_CLUSTER_ZFLAGS	( HAMMER2_CLUSTER_WRHARD |	\
				  HAMMER2_CLUSTER_RDHARD |	\
				  HAMMER2_CLUSTER_WRSOFT |	\
				  HAMMER2_CLUSTER_RDSOFT |	\
				  HAMMER2_CLUSTER_MSYNCED |	\
				  HAMMER2_CLUSTER_SSYNCED)

/*
 * Helper functions (cluster must be locked for flags to be valid).
 */
static __inline
int
hammer2_cluster_rdok(hammer2_cluster_t *cluster)
{
	return (cluster->flags & HAMMER2_CLUSTER_RDOK);
}

static __inline
int
hammer2_cluster_wrok(hammer2_cluster_t *cluster)
{
	return (cluster->flags & HAMMER2_CLUSTER_WROK);
}

RB_HEAD(hammer2_inode_tree, hammer2_inode);

/*
 * A hammer2 inode.
 *
 * NOTE: The inode-embedded cluster is never used directly for I/O (since
 *	 it may be shared).  Instead it will be replicated-in and synchronized
 *	 back out if changed.
 */
struct hammer2_inode {
	RB_ENTRY(hammer2_inode) rbnode;		/* inumber lookup (HL) */
	hammer2_mtx_t		lock;		/* inode lock */
	struct hammer2_pfs	*pmp;		/* PFS mount */
	struct hammer2_inode	*pip;		/* parent inode */
	struct vnode		*vp;
	struct spinlock		cluster_spin;	/* update cluster */
	hammer2_cluster_t	cluster;
	struct lockf		advlock;
	u_int			flags;
	u_int			refs;		/* +vpref, +flushref */
	uint8_t			comp_heuristic;
	hammer2_inode_meta_t	meta;		/* copy of meta-data */
	hammer2_blockref_t	bref;		/* copy of bref statistics */
	hammer2_off_t		osize;
};

typedef struct hammer2_inode hammer2_inode_t;

/*
 * MODIFIED	- Inode is in a modified state, ip->meta may have changes.
 * RESIZED	- Inode truncated (any) or inode extended beyond
 *		  EMBEDDED_BYTES.
 */
#define HAMMER2_INODE_MODIFIED		0x0001
#define HAMMER2_INODE_SROOT		0x0002	/* kmalloc special case */
#define HAMMER2_INODE_RENAME_INPROG	0x0004
#define HAMMER2_INODE_ONRBTREE		0x0008
#define HAMMER2_INODE_RESIZED		0x0010	/* requires inode_fsync */
#define HAMMER2_INODE_UNUSED0020	0x0020
#define HAMMER2_INODE_ISUNLINKED	0x0040
#define HAMMER2_INODE_METAGOOD		0x0080	/* inode meta-data good */

int hammer2_inode_cmp(hammer2_inode_t *ip1, hammer2_inode_t *ip2);
RB_PROTOTYPE2(hammer2_inode_tree, hammer2_inode, rbnode, hammer2_inode_cmp,
		hammer2_tid_t);

/*
 * inode-unlink side-structure
 */
struct hammer2_inode_unlink {
	TAILQ_ENTRY(hammer2_inode_unlink) entry;
	hammer2_inode_t	*ip;
};
TAILQ_HEAD(h2_unlk_list, hammer2_inode_unlink);

typedef struct hammer2_inode_unlink hammer2_inode_unlink_t;

/*
 * A hammer2 transaction and flush sequencing structure.
 *
 * This global structure is tied into hammer2_dev and is used
 * to sequence modifying operations and flushes.  These operations
 * run on whole cluster PFSs, not individual nodes (at this level),
 * so we do not record mirror_tid here.
 */
struct hammer2_trans {
	TAILQ_ENTRY(hammer2_trans) entry;
	struct hammer2_pfs	*pmp;
	hammer2_xid_t		sync_xid;	/* transaction sequencer */
	hammer2_tid_t		inode_tid;	/* inode number assignment */
	hammer2_tid_t		modify_tid;	/* modify transaction id */
	thread_t		td;		/* pointer */
	int			flags;
	int			blocked;
	uint8_t			inodes_created;
	uint8_t			dummy[7];
};

typedef struct hammer2_trans hammer2_trans_t;

#define HAMMER2_TRANS_ISFLUSH		0x0001	/* formal flush */
#define HAMMER2_TRANS_CONCURRENT	0x0002	/* concurrent w/flush */
#define HAMMER2_TRANS_BUFCACHE		0x0004	/* from bioq strategy write */
#define HAMMER2_TRANS_NEWINODE		0x0008	/* caller allocating inode */
#define HAMMER2_TRANS_KEEPMODIFY	0x0010	/* do not change bref.modify */
#define HAMMER2_TRANS_PREFLUSH		0x0020	/* preflush state */

#define HAMMER2_FREEMAP_HEUR_NRADIX	4	/* pwr 2 PBUFRADIX-MINIORADIX */
#define HAMMER2_FREEMAP_HEUR_TYPES	8
#define HAMMER2_FREEMAP_HEUR		(HAMMER2_FREEMAP_HEUR_NRADIX * \
					 HAMMER2_FREEMAP_HEUR_TYPES)

/*
 * Transaction Rendezvous
 */
TAILQ_HEAD(hammer2_trans_queue, hammer2_trans);

struct hammer2_trans_manage {
	hammer2_xid_t		flush_xid;	/* last flush transaction */
	hammer2_xid_t		alloc_xid;
	struct lock		translk;	/* lockmgr lock */
	struct hammer2_trans_queue transq;	/* modifying transactions */
	int			flushcnt;	/* track flush trans */
};

typedef struct hammer2_trans_manage hammer2_trans_manage_t;

/*
 * Hammer2 support thread element.
 *
 * Potentially many support threads can hang off of hammer2, primarily
 * off the hammer2_pfs structure.  Typically:
 *
 * td x Nodes		 	A synchronization thread for each node.
 * td x Nodes x workers		Worker threads for frontend operations.
 * td x 1			Bioq thread for logical buffer writes.
 *
 * In addition, the synchronization thread(s) associated with the
 * super-root PFS (spmp) for a node is responsible for automatic bulkfree
 * and dedup scans.
 */
struct hammer2_thread {
	struct hammer2_pfs *pmp;
	thread_t	td;
	uint32_t	flags;
	int		depth;
	int		clindex;	/* cluster element index */
	int		repidx;
	hammer2_trans_t	trans;
	struct lock	lk;		/* thread control lock */
	hammer2_xop_list_t xopq;
};

typedef struct hammer2_thread hammer2_thread_t;

#define HAMMER2_THREAD_UNMOUNTING	0x0001	/* unmount request */
#define HAMMER2_THREAD_DEV		0x0002	/* related to dev, not pfs */
#define HAMMER2_THREAD_UNUSED04		0x0004
#define HAMMER2_THREAD_REMASTER		0x0008	/* remaster request */
#define HAMMER2_THREAD_STOP		0x0010	/* exit request */
#define HAMMER2_THREAD_FREEZE		0x0020	/* force idle */
#define HAMMER2_THREAD_FROZEN		0x0040	/* restart */


/*
 * hammer2_xop - container for VOP/XOP operation (allocated, not on stack).
 *
 * This structure is used to distribute a VOP operation across multiple
 * nodes.  It provides a rendezvous for concurrent node execution and
 * can be detached from the frontend operation to allow the frontend to
 * return early.
 */
typedef void (*hammer2_xop_func_t)(union hammer2_xop *xop, int clidx);

typedef struct hammer2_xop_fifo {
	TAILQ_ENTRY(hammer2_xop_head) entry;
	hammer2_chain_t		*array[HAMMER2_XOPFIFO];
	int			errors[HAMMER2_XOPFIFO];
	int			ri;
	int			wi;
	int			unused03;
} hammer2_xop_fifo_t;

struct hammer2_xop_head {
	hammer2_xop_func_t	func;
	struct hammer2_inode	*ip;
	struct hammer2_xop_group *xgrp;
	uint32_t		check_counter;
	uint32_t		run_mask;
	uint32_t		chk_mask;
	int			state;
	int			error;
	hammer2_key_t		lkey;
	hammer2_key_t		nkey;
	hammer2_xop_fifo_t	collect[HAMMER2_MAXCLUSTER];
	hammer2_cluster_t	cluster;	/* help collections */
};

typedef struct hammer2_xop_head hammer2_xop_head_t;

struct hammer2_xop_readdir {
	hammer2_xop_head_t	head;
};

typedef struct hammer2_xop_readdir hammer2_xop_readdir_t;

union hammer2_xop {
	hammer2_xop_head_t	head;
	hammer2_xop_readdir_t	xop_readdir;
};

typedef union hammer2_xop hammer2_xop_t;

/*
 * hammer2_xop_group - Manage XOP support threads.
 */
struct hammer2_xop_group {
	hammer2_thread_t	thrs[HAMMER2_MAXCLUSTER];
	hammer2_mtx_t		mtx;
};

typedef struct hammer2_xop_group hammer2_xop_group_t;

/*
 * Global (per partition) management structure, represents a hard block
 * device.  Typically referenced by hammer2_chain structures when applicable.
 * Typically not used for network-managed elements.
 *
 * Note that a single hammer2_dev can be indirectly tied to multiple system
 * mount points.  There is no direct relationship.  System mounts are
 * per-cluster-id, not per-block-device, and a single hard mount might contain
 * many PFSs and those PFSs might combine together in various ways to form
 * the set of available clusters.
 */
struct hammer2_dev {
	struct vnode	*devvp;		/* device vnode */
	int		ronly;		/* read-only mount */
	int		mount_count;	/* number of actively mounted PFSs */
	TAILQ_ENTRY(hammer2_dev) mntentry; /* hammer2_mntlist */

	struct malloc_type *mchain;
	int		nipstacks;
	int		maxipstacks;
	kdmsg_iocom_t	iocom;		/* volume-level dmsg interface */
	struct spinlock	io_spin;	/* iotree access */
	struct hammer2_io_tree iotree;
	int		iofree_count;
	hammer2_chain_t vchain;		/* anchor chain (topology) */
	hammer2_chain_t fchain;		/* anchor chain (freemap) */
	struct spinlock	list_spin;
	struct h2_flush_list	flushq;	/* flush seeds */
	struct hammer2_pfs *spmp;	/* super-root pmp for transactions */
	struct lock	vollk;		/* lockmgr lock */
	hammer2_off_t	heur_freemap[HAMMER2_FREEMAP_HEUR];
	int		volhdrno;	/* last volhdrno written */
	char		devrepname[64];	/* for kprintf */
	hammer2_volume_data_t voldata;
	hammer2_volume_data_t volsync;	/* synchronized voldata */
};

typedef struct hammer2_dev hammer2_dev_t;

/*
 * Helper functions (cluster must be locked for flags to be valid).
 */
static __inline
int
hammer2_chain_rdok(hammer2_chain_t *chain)
{
	return (chain->error == 0);
}

static __inline
int
hammer2_chain_wrok(hammer2_chain_t *chain)
{
	return (chain->error == 0 && chain->hmp->ronly == 0);
}

/*
 * Per-cluster management structure.  This structure will be tied to a
 * system mount point if the system is mounting the PFS, but is also used
 * to manage clusters encountered during the super-root scan or received
 * via LNK_SPANs that might not be mounted.
 *
 * This structure is also used to represent the super-root that hangs off
 * of a hard mount point.  The super-root is not really a cluster element.
 * In this case the spmp_hmp field will be non-NULL.  It's just easier to do
 * this than to special case super-root manipulation in the hammer2_chain*
 * code as being only hammer2_dev-related.
 *
 * pfs_mode and pfs_nmasters are rollup fields which critically describes
 * how elements of the cluster act on the cluster.  pfs_mode is only applicable
 * when a PFS is mounted by the system.  pfs_nmasters is our best guess as to
 * how many masters have been configured for a cluster and is always
 * applicable.  pfs_types[] is an array with 1:1 correspondance to the
 * iroot cluster and describes the PFS types of the nodes making up the
 * cluster.
 *
 * WARNING! Portions of this structure have deferred initialization.  In
 *	    particular, if not mounted there will be no ihidden or wthread.
 *	    umounted network PFSs will also be missing iroot and numerous
 *	    other fields will not be initialized prior to mount.
 *
 *	    Synchronization threads are chain-specific and only applicable
 *	    to local hard PFS entries.  A hammer2_pfs structure may contain
 *	    more than one when multiple hard PFSs are present on the local
 *	    machine which require synchronization monitoring.  Most PFSs
 *	    (such as snapshots) are 1xMASTER PFSs which do not need a
 *	    synchronization thread.
 *
 * WARNING! The chains making up pfs->iroot's cluster are accounted for in
 *	    hammer2_dev->mount_count when the pfs is associated with a mount
 *	    point.
 */
struct hammer2_pfs {
	struct mount		*mp;
	TAILQ_ENTRY(hammer2_pfs) mntentry;	/* hammer2_pfslist */
	uuid_t			pfs_clid;
	hammer2_dev_t		*spmp_hmp;	/* only if super-root pmp */
	hammer2_inode_t		*iroot;		/* PFS root inode */
	hammer2_inode_t		*ihidden;	/* PFS hidden directory */
	uint8_t			pfs_types[HAMMER2_MAXCLUSTER];
	char			*pfs_names[HAMMER2_MAXCLUSTER];
	hammer2_trans_manage_t	tmanage;	/* transaction management */
	struct lock		lock;		/* PFS lock for certain ops */
	struct netexport	export;		/* nfs export */
	int			ronly;		/* read-only mount */
	struct malloc_type	*minode;
	struct malloc_type	*mmsg;
	struct spinlock		inum_spin;	/* inumber lookup */
	struct hammer2_inode_tree inum_tree;	/* (not applicable to spmp) */
	hammer2_tid_t		modify_tid;	/* modify transaction id */
	hammer2_tid_t		inode_tid;	/* inode allocator */
	uint8_t			pfs_nmasters;	/* total masters */
	uint8_t			pfs_mode;	/* operating mode PFSMODE */
	uint8_t			unused01;
	uint8_t			unused02;
	int			xop_iterator;
	long			inmem_inodes;
	uint32_t		inmem_dirty_chains;
	int			count_lwinprog;	/* logical write in prog */
	struct spinlock		list_spin;
	struct h2_unlk_list	unlinkq;	/* last-close unlink */
	hammer2_thread_t	sync_thrs[HAMMER2_MAXCLUSTER];
	thread_t		wthread_td;	/* write thread td */
	struct bio_queue_head	wthread_bioq;	/* logical buffer bioq */
	hammer2_mtx_t 		wthread_mtx;	/* interlock */
	int			wthread_destroy;/* termination sequencing */
	uint32_t		flags;		/* cached cluster flags */
	hammer2_xop_group_t	xop_groups[HAMMER2_XOPGROUPS];
};

typedef struct hammer2_pfs hammer2_pfs_t;

#define HAMMER2_DIRTYCHAIN_WAITING	0x80000000
#define HAMMER2_DIRTYCHAIN_MASK		0x7FFFFFFF

#define HAMMER2_LWINPROG_WAITING	0x80000000
#define HAMMER2_LWINPROG_MASK		0x7FFFFFFF

/*
 * hammer2_cluster_check
 */
#define HAMMER2_CHECK_NULL	0x00000001

/*
 * Bulkscan
 */
#define HAMMER2_BULK_ABORT	0x00000001

/*
 * Misc
 */
#if defined(_KERNEL)

MALLOC_DECLARE(M_HAMMER2);

#define VTOI(vp)	((hammer2_inode_t *)(vp)->v_data)
#define ITOV(ip)	((ip)->vp)

/*
 * Currently locked chains retain the locked buffer cache buffer for
 * indirect blocks, and indirect blocks can be one of two sizes.  The
 * device buffer has to match the case to avoid deadlocking recursive
 * chains that might otherwise try to access different offsets within
 * the same device buffer.
 */
static __inline
int
hammer2_devblkradix(int radix)
{
#if 0
	if (radix <= HAMMER2_LBUFRADIX) {
		return (HAMMER2_LBUFRADIX);
	} else {
		return (HAMMER2_PBUFRADIX);
	}
#endif
	return (HAMMER2_PBUFRADIX);
}

/*
 * XXX almost time to remove this.  DIO uses PBUFSIZE exclusively now.
 */
static __inline
size_t
hammer2_devblksize(size_t bytes)
{
#if 0
	if (bytes <= HAMMER2_LBUFSIZE) {
		return(HAMMER2_LBUFSIZE);
	} else {
		KKASSERT(bytes <= HAMMER2_PBUFSIZE &&
			 (bytes ^ (bytes - 1)) == ((bytes << 1) - 1));
		return (HAMMER2_PBUFSIZE);
	}
#endif
	return (HAMMER2_PBUFSIZE);
}


static __inline
hammer2_pfs_t *
MPTOPMP(struct mount *mp)
{
	return ((hammer2_pfs_t *)mp->mnt_data);
}

#define LOCKSTART	int __nlocks = curthread->td_locks
#define LOCKENTER	(++curthread->td_locks)
#define LOCKEXIT	(--curthread->td_locks)
#define LOCKSTOP	KKASSERT(curthread->td_locks == __nlocks)

extern struct vop_ops hammer2_vnode_vops;
extern struct vop_ops hammer2_spec_vops;
extern struct vop_ops hammer2_fifo_vops;

extern int hammer2_debug;
extern int hammer2_cluster_enable;
extern int hammer2_hardlink_enable;
extern int hammer2_flush_pipe;
extern int hammer2_synchronous_flush;
extern int hammer2_dio_count;
extern long hammer2_limit_dirty_chains;
extern long hammer2_iod_file_read;
extern long hammer2_iod_meta_read;
extern long hammer2_iod_indr_read;
extern long hammer2_iod_fmap_read;
extern long hammer2_iod_volu_read;
extern long hammer2_iod_file_write;
extern long hammer2_iod_meta_write;
extern long hammer2_iod_indr_write;
extern long hammer2_iod_fmap_write;
extern long hammer2_iod_volu_write;
extern long hammer2_ioa_file_read;
extern long hammer2_ioa_meta_read;
extern long hammer2_ioa_indr_read;
extern long hammer2_ioa_fmap_read;
extern long hammer2_ioa_volu_read;
extern long hammer2_ioa_file_write;
extern long hammer2_ioa_meta_write;
extern long hammer2_ioa_indr_write;
extern long hammer2_ioa_fmap_write;
extern long hammer2_ioa_volu_write;

extern struct objcache *cache_buffer_read;
extern struct objcache *cache_buffer_write;
extern struct objcache *cache_xops;

extern int destroy;
extern int write_thread_wakeup;

/*
 * hammer2_subr.c
 */
#define hammer2_icrc32(buf, size)	iscsi_crc32((buf), (size))
#define hammer2_icrc32c(buf, size, crc)	iscsi_crc32_ext((buf), (size), (crc))

int hammer2_signal_check(time_t *timep);
const char *hammer2_error_str(int error);

void hammer2_inode_lock(hammer2_inode_t *ip, int how);
void hammer2_inode_unlock(hammer2_inode_t *ip, hammer2_cluster_t *cluster);
hammer2_cluster_t *hammer2_inode_cluster(hammer2_inode_t *ip, int how);
hammer2_chain_t *hammer2_inode_chain(hammer2_inode_t *ip, int clindex, int how);
hammer2_mtx_state_t hammer2_inode_lock_temp_release(hammer2_inode_t *ip);
void hammer2_inode_lock_temp_restore(hammer2_inode_t *ip,
			hammer2_mtx_state_t ostate);
int hammer2_inode_lock_upgrade(hammer2_inode_t *ip);
void hammer2_inode_lock_downgrade(hammer2_inode_t *ip, int);

void hammer2_dev_exlock(hammer2_dev_t *hmp);
void hammer2_dev_shlock(hammer2_dev_t *hmp);
void hammer2_dev_unlock(hammer2_dev_t *hmp);

int hammer2_get_dtype(const hammer2_inode_data_t *ipdata);
int hammer2_get_vtype(uint8_t type);
u_int8_t hammer2_get_obj_type(enum vtype vtype);
void hammer2_time_to_timespec(u_int64_t xtime, struct timespec *ts);
u_int64_t hammer2_timespec_to_time(const struct timespec *ts);
u_int32_t hammer2_to_unix_xid(const uuid_t *uuid);
void hammer2_guid_to_uuid(uuid_t *uuid, u_int32_t guid);
hammer2_xid_t hammer2_trans_newxid(hammer2_pfs_t *pmp);
void hammer2_trans_manage_init(hammer2_trans_manage_t *tman);

hammer2_key_t hammer2_dirhash(const unsigned char *name, size_t len);
int hammer2_getradix(size_t bytes);

int hammer2_calc_logical(hammer2_inode_t *ip, hammer2_off_t uoff,
			hammer2_key_t *lbasep, hammer2_key_t *leofp);
int hammer2_calc_physical(hammer2_inode_t *ip, hammer2_key_t lbase);
void hammer2_update_time(uint64_t *timep);
void hammer2_adjreadcounter(hammer2_blockref_t *bref, size_t bytes);

/*
 * hammer2_inode.c
 */
struct vnode *hammer2_igetv(hammer2_inode_t *ip, hammer2_cluster_t *cparent,
			int *errorp);
hammer2_inode_t *hammer2_inode_lookup(hammer2_pfs_t *pmp,
			hammer2_tid_t inum);
hammer2_inode_t *hammer2_inode_get(hammer2_pfs_t *pmp,
			hammer2_inode_t *dip, hammer2_cluster_t *cluster);
void hammer2_inode_free(hammer2_inode_t *ip);
void hammer2_inode_ref(hammer2_inode_t *ip);
void hammer2_inode_drop(hammer2_inode_t *ip);
void hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_inode_t *pip,
			hammer2_cluster_t *cluster);
void hammer2_inode_repoint_one(hammer2_inode_t *ip, hammer2_cluster_t *cluster,
			int idx);
void hammer2_inode_modify(hammer2_trans_t *trans, hammer2_inode_t *ip);
void hammer2_run_unlinkq(hammer2_trans_t *trans, hammer2_pfs_t *pmp);

hammer2_inode_t *hammer2_inode_create(hammer2_trans_t *trans,
			hammer2_inode_t *dip,
			struct vattr *vap, struct ucred *cred,
			const uint8_t *name, size_t name_len,
			hammer2_cluster_t **clusterp,
			int flags, int *errorp);
int hammer2_inode_connect(hammer2_trans_t *trans,
			hammer2_inode_t *ip, hammer2_cluster_t **clusterp,
			int hlink,
			hammer2_inode_t *dip, hammer2_cluster_t *dcluster,
			const uint8_t *name, size_t name_len,
			hammer2_key_t key);
hammer2_inode_t *hammer2_inode_common_parent(hammer2_inode_t *fdip,
			hammer2_inode_t *tdip);
void hammer2_inode_fsync(hammer2_trans_t *trans, hammer2_inode_t *ip,
			hammer2_cluster_t *cparent);
int hammer2_unlink_file(hammer2_trans_t *trans,
			hammer2_inode_t *dip, hammer2_inode_t *ip,
			const uint8_t *name, size_t name_len, int isdir,
			int *hlinkp, struct nchandle *nch, int nlinks);
int hammer2_hardlink_consolidate(hammer2_trans_t *trans,
			hammer2_inode_t *ip, hammer2_cluster_t **clusterp,
			hammer2_inode_t *cdip, hammer2_cluster_t *cdcluster,
			int nlinks);
int hammer2_hardlink_deconsolidate(hammer2_trans_t *trans, hammer2_inode_t *dip,
			hammer2_chain_t **chainp, hammer2_chain_t **ochainp);
int hammer2_hardlink_find(hammer2_inode_t *dip, hammer2_cluster_t **cparentp,
			hammer2_cluster_t **clusterp);
int hammer2_parent_find(hammer2_cluster_t **cparentp,
			hammer2_cluster_t *cluster);
void hammer2_inode_install_hidden(hammer2_pfs_t *pmp);

/*
 * hammer2_chain.c
 */
void hammer2_voldata_lock(hammer2_dev_t *hmp);
void hammer2_voldata_unlock(hammer2_dev_t *hmp);
void hammer2_voldata_modify(hammer2_dev_t *hmp);
hammer2_chain_t *hammer2_chain_alloc(hammer2_dev_t *hmp,
				hammer2_pfs_t *pmp,
				hammer2_trans_t *trans,
				hammer2_blockref_t *bref);
void hammer2_chain_core_init(hammer2_chain_t *chain);
void hammer2_chain_ref(hammer2_chain_t *chain);
void hammer2_chain_drop(hammer2_chain_t *chain);
void hammer2_chain_lock(hammer2_chain_t *chain, int how);
void hammer2_chain_load_data(hammer2_chain_t *chain);
const hammer2_media_data_t *hammer2_chain_rdata(hammer2_chain_t *chain);
hammer2_media_data_t *hammer2_chain_wdata(hammer2_chain_t *chain);

/*
 * hammer2_cluster.c
 */
void hammer2_cluster_load_async(hammer2_cluster_t *cluster,
				void (*callback)(hammer2_iocb_t *iocb),
				void *ptr);
void hammer2_chain_moved(hammer2_chain_t *chain);
void hammer2_chain_modify(hammer2_trans_t *trans,
				hammer2_chain_t *chain, int flags);
void hammer2_chain_resize(hammer2_trans_t *trans, hammer2_inode_t *ip,
				hammer2_chain_t *parent,
				hammer2_chain_t *chain,
				int nradix, int flags);
void hammer2_chain_unlock(hammer2_chain_t *chain);
void hammer2_chain_wait(hammer2_chain_t *chain);
hammer2_chain_t *hammer2_chain_get(hammer2_chain_t *parent, int generation,
				hammer2_blockref_t *bref);
hammer2_chain_t *hammer2_chain_lookup_init(hammer2_chain_t *parent, int flags);
void hammer2_chain_lookup_done(hammer2_chain_t *parent);
hammer2_chain_t *hammer2_chain_lookup(hammer2_chain_t **parentp,
				hammer2_key_t *key_nextp,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int *cache_indexp, int flags);
hammer2_chain_t *hammer2_chain_next(hammer2_chain_t **parentp,
				hammer2_chain_t *chain,
				hammer2_key_t *key_nextp,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int *cache_indexp, int flags);
hammer2_chain_t *hammer2_chain_scan(hammer2_chain_t *parent,
				hammer2_chain_t *chain,
				int *cache_indexp, int flags);

int hammer2_chain_create(hammer2_trans_t *trans, hammer2_chain_t **parentp,
				hammer2_chain_t **chainp,
				hammer2_pfs_t *pmp,
				hammer2_key_t key, int keybits,
				int type, size_t bytes, int flags);
void hammer2_chain_rename(hammer2_trans_t *trans, hammer2_blockref_t *bref,
				hammer2_chain_t **parentp,
				hammer2_chain_t *chain, int flags);
int hammer2_chain_snapshot(hammer2_trans_t *trans, hammer2_chain_t **chainp,
				hammer2_ioc_pfs_t *pmp);
void hammer2_chain_delete(hammer2_trans_t *trans, hammer2_chain_t *parent,
				hammer2_chain_t *chain, int flags);
void hammer2_chain_delete_duplicate(hammer2_trans_t *trans,
				hammer2_chain_t **chainp, int flags);
void hammer2_flush(hammer2_trans_t *trans, hammer2_chain_t *chain, int istop);
void hammer2_delayed_flush(hammer2_trans_t *trans, hammer2_chain_t *chain);
void hammer2_chain_commit(hammer2_trans_t *trans, hammer2_chain_t *chain);
void hammer2_chain_setflush(hammer2_trans_t *trans, hammer2_chain_t *chain);
void hammer2_chain_countbrefs(hammer2_chain_t *chain,
				hammer2_blockref_t *base, int count);

void hammer2_chain_setcheck(hammer2_chain_t *chain, void *bdata);
int hammer2_chain_testcheck(hammer2_chain_t *chain, void *bdata);


void hammer2_pfs_memory_wait(hammer2_pfs_t *pmp);
void hammer2_pfs_memory_inc(hammer2_pfs_t *pmp);
void hammer2_pfs_memory_wakeup(hammer2_pfs_t *pmp);

void hammer2_base_delete(hammer2_trans_t *trans, hammer2_chain_t *chain,
				hammer2_blockref_t *base, int count,
				int *cache_indexp, hammer2_chain_t *child);
void hammer2_base_insert(hammer2_trans_t *trans, hammer2_chain_t *chain,
				hammer2_blockref_t *base, int count,
				int *cache_indexp, hammer2_chain_t *child);

/*
 * hammer2_trans.c
 */
void hammer2_trans_init(hammer2_trans_t *trans, hammer2_pfs_t *pmp,
				int flags);
void hammer2_trans_done(hammer2_trans_t *trans);
void hammer2_trans_assert_strategy(hammer2_pfs_t *pmp);

/*
 * hammer2_ioctl.c
 */
int hammer2_ioctl(hammer2_inode_t *ip, u_long com, void *data,
				int fflag, struct ucred *cred);

/*
 * hammer2_io.c
 */
void hammer2_io_putblk(hammer2_io_t **diop);
void hammer2_io_cleanup(hammer2_dev_t *hmp, struct hammer2_io_tree *tree);
char *hammer2_io_data(hammer2_io_t *dio, off_t lbase);
void hammer2_io_getblk(hammer2_dev_t *hmp, off_t lbase, int lsize,
				hammer2_iocb_t *iocb);
void hammer2_io_complete(hammer2_iocb_t *iocb);
void hammer2_io_callback(struct bio *bio);
void hammer2_iocb_wait(hammer2_iocb_t *iocb);
int hammer2_io_new(hammer2_dev_t *hmp, off_t lbase, int lsize,
				hammer2_io_t **diop);
int hammer2_io_newnz(hammer2_dev_t *hmp, off_t lbase, int lsize,
				hammer2_io_t **diop);
int hammer2_io_newq(hammer2_dev_t *hmp, off_t lbase, int lsize,
				hammer2_io_t **diop);
int hammer2_io_bread(hammer2_dev_t *hmp, off_t lbase, int lsize,
				hammer2_io_t **diop);
void hammer2_io_bawrite(hammer2_io_t **diop);
void hammer2_io_bdwrite(hammer2_io_t **diop);
int hammer2_io_bwrite(hammer2_io_t **diop);
int hammer2_io_isdirty(hammer2_io_t *dio);
void hammer2_io_setdirty(hammer2_io_t *dio);
void hammer2_io_setinval(hammer2_io_t *dio, u_int bytes);
void hammer2_io_brelse(hammer2_io_t **diop);
void hammer2_io_bqrelse(hammer2_io_t **diop);

/*
 * hammer2_xops.c
 */
void hammer2_xop_group_init(hammer2_pfs_t *pmp, hammer2_xop_group_t *xgrp);
hammer2_xop_t *hammer2_xop_alloc(hammer2_inode_t *ip, hammer2_xop_func_t func);
void hammer2_xop_helper_create(hammer2_pfs_t *pmp);
void hammer2_xop_helper_cleanup(hammer2_pfs_t *pmp);
void hammer2_xop_start(hammer2_xop_head_t *xop);
int hammer2_xop_collect(hammer2_xop_head_t *xop);
void hammer2_xop_retire(hammer2_xop_head_t *xop, uint32_t mask);
int hammer2_xop_active(hammer2_xop_head_t *xop);
int hammer2_xop_feed(hammer2_xop_head_t *xop, hammer2_chain_t *chain,
				int clindex, int error);


void hammer2_xop_readdir(hammer2_xop_t *xop, int clidx);
int hammer2_xop_readlink(struct vop_readlink_args *ap);
int hammer2_xop_nresolve(struct vop_nresolve_args *ap);
int hammer2_xop_nlookupdotdot(struct vop_nlookupdotdot_args *ap);
int hammer2_xop_nmkdir(struct vop_nmkdir_args *ap);
int hammer2_xop_advlock(struct vop_advlock_args *ap);
int hammer2_xop_nlink(struct vop_nlink_args *ap);
int hammer2_xop_ncreate(struct vop_ncreate_args *ap);
int hammer2_xop_nmknod(struct vop_nmknod_args *ap);
int hammer2_xop_nsymlink(struct vop_nsymlink_args *ap);
int hammer2_xop_nremove(struct vop_nremove_args *ap);
int hammer2_xop_nrmdir(struct vop_nrmdir_args *ap);
int hammer2_xop_nrename(struct vop_nrename_args *ap);

/*
 * hammer2_msgops.c
 */
int hammer2_msg_dbg_rcvmsg(kdmsg_msg_t *msg);
int hammer2_msg_adhoc_input(kdmsg_msg_t *msg);

/*
 * hammer2_vfsops.c
 */
void hammer2_clusterctl_wakeup(kdmsg_iocom_t *iocom);
void hammer2_volconf_update(hammer2_dev_t *hmp, int index);
void hammer2_dump_chain(hammer2_chain_t *chain, int tab, int *countp, char pfx);
int hammer2_vfs_sync(struct mount *mp, int waitflags);
hammer2_pfs_t *hammer2_pfsalloc(hammer2_cluster_t *cluster,
				const hammer2_inode_data_t *ripdata,
				hammer2_tid_t modify_tid);

void hammer2_lwinprog_ref(hammer2_pfs_t *pmp);
void hammer2_lwinprog_drop(hammer2_pfs_t *pmp);
void hammer2_lwinprog_wait(hammer2_pfs_t *pmp);

/*
 * hammer2_freemap.c
 */
int hammer2_freemap_alloc(hammer2_trans_t *trans, hammer2_chain_t *chain,
				size_t bytes);
void hammer2_freemap_adjust(hammer2_trans_t *trans, hammer2_dev_t *hmp,
				hammer2_blockref_t *bref, int how);

/*
 * hammer2_cluster.c
 */
int hammer2_cluster_need_resize(hammer2_cluster_t *cluster, int bytes);
uint8_t hammer2_cluster_type(hammer2_cluster_t *cluster);
const hammer2_media_data_t *hammer2_cluster_rdata(hammer2_cluster_t *cluster);
const hammer2_media_data_t *hammer2_cluster_rdata_bytes(
				hammer2_cluster_t *cluster, size_t *bytesp);
hammer2_media_data_t *hammer2_cluster_wdata(hammer2_cluster_t *cluster);
hammer2_cluster_t *hammer2_cluster_from_chain(hammer2_chain_t *chain);
int hammer2_cluster_modified(hammer2_cluster_t *cluster);
int hammer2_cluster_duplicated(hammer2_cluster_t *cluster);
void hammer2_cluster_bref(hammer2_cluster_t *cluster, hammer2_blockref_t *bref);
void hammer2_cluster_setflush(hammer2_trans_t *trans,
			hammer2_cluster_t *cluster);
void hammer2_cluster_setmethod_check(hammer2_trans_t *trans,
			hammer2_cluster_t *cluster, int check_algo);
hammer2_cluster_t *hammer2_cluster_alloc(hammer2_pfs_t *pmp,
			hammer2_trans_t *trans,
			hammer2_blockref_t *bref);
void hammer2_cluster_ref(hammer2_cluster_t *cluster);
void hammer2_cluster_drop(hammer2_cluster_t *cluster);
void hammer2_cluster_wait(hammer2_cluster_t *cluster);
void hammer2_cluster_lock(hammer2_cluster_t *cluster, int how);
void hammer2_cluster_lock_except(hammer2_cluster_t *cluster, int idx, int how);
int hammer2_cluster_check(hammer2_cluster_t *cluster, hammer2_key_t lokey,
			int flags);
void hammer2_cluster_resolve(hammer2_cluster_t *cluster);
void hammer2_cluster_forcegood(hammer2_cluster_t *cluster);
hammer2_cluster_t *hammer2_cluster_copy(hammer2_cluster_t *ocluster);
void hammer2_cluster_unlock(hammer2_cluster_t *cluster);
void hammer2_cluster_unlock_except(hammer2_cluster_t *cluster, int idx);
void hammer2_cluster_resize(hammer2_trans_t *trans, hammer2_inode_t *ip,
			hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
			int nradix, int flags);
void hammer2_cluster_modify(hammer2_trans_t *trans, hammer2_cluster_t *cluster,
			int flags);
hammer2_inode_data_t *hammer2_cluster_modify_ip(hammer2_trans_t *trans,
			hammer2_inode_t *ip, hammer2_cluster_t *cluster,
			int flags);
void hammer2_cluster_modsync(hammer2_cluster_t *cluster);
hammer2_cluster_t *hammer2_cluster_lookup_init(hammer2_cluster_t *cparent,
			int flags);
void hammer2_cluster_lookup_done(hammer2_cluster_t *cparent);
hammer2_cluster_t *hammer2_cluster_lookup(hammer2_cluster_t *cparent,
			hammer2_key_t *key_nextp,
			hammer2_key_t key_beg, hammer2_key_t key_end,
			int flags);
hammer2_cluster_t *hammer2_cluster_next(hammer2_cluster_t *cparent,
			hammer2_cluster_t *cluster,
			hammer2_key_t *key_nextp,
			hammer2_key_t key_beg, hammer2_key_t key_end,
			int flags);
void hammer2_cluster_next_single_chain(hammer2_cluster_t *cparent,
			hammer2_cluster_t *cluster,
			hammer2_key_t *key_nextp,
			hammer2_key_t key_beg,
			hammer2_key_t key_end,
			int i, int flags);
hammer2_cluster_t *hammer2_cluster_scan(hammer2_cluster_t *cparent,
			hammer2_cluster_t *cluster, int flags);
int hammer2_cluster_create(hammer2_trans_t *trans, hammer2_cluster_t *cparent,
			hammer2_cluster_t **clusterp,
			hammer2_key_t key, int keybits,
			int type, size_t bytes, int flags);
void hammer2_cluster_rename(hammer2_trans_t *trans, hammer2_blockref_t *bref,
			hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
			int flags);
void hammer2_cluster_delete(hammer2_trans_t *trans, hammer2_cluster_t *pcluster,
			hammer2_cluster_t *cluster, int flags);
int hammer2_cluster_snapshot(hammer2_trans_t *trans,
			hammer2_cluster_t *ocluster, hammer2_ioc_pfs_t *pmp);
hammer2_cluster_t *hammer2_cluster_parent(hammer2_cluster_t *cluster);

int hammer2_bulk_scan(hammer2_trans_t *trans, hammer2_chain_t *parent,
			int (*func)(hammer2_chain_t *chain, void *info),
			void *info);
int hammer2_bulkfree_pass(hammer2_dev_t *hmp,
			struct hammer2_ioc_bulkfree *bfi);

/*
 * hammer2_iocom.c
 */
void hammer2_iocom_init(hammer2_dev_t *hmp);
void hammer2_iocom_uninit(hammer2_dev_t *hmp);
void hammer2_cluster_reconnect(hammer2_dev_t *hmp, struct file *fp);

/*
 * hammer2_thread.c
 */
void hammer2_thr_create(hammer2_thread_t *thr, hammer2_pfs_t *pmp,
			const char *id, int clindex, int repidx,
			void (*func)(void *arg));
void hammer2_thr_delete(hammer2_thread_t *thr);
void hammer2_thr_remaster(hammer2_thread_t *thr);
void hammer2_thr_freeze_async(hammer2_thread_t *thr);
void hammer2_thr_freeze(hammer2_thread_t *thr);
void hammer2_thr_unfreeze(hammer2_thread_t *thr);
void hammer2_primary_sync_thread(void *arg);
void hammer2_primary_xops_thread(void *arg);

/*
 * hammer2_strategy.c
 */
int hammer2_vop_strategy(struct vop_strategy_args *ap);
int hammer2_vop_bmap(struct vop_bmap_args *ap);
void hammer2_write_thread(void *arg);
void hammer2_bioq_sync(hammer2_pfs_t *pmp);

#endif /* !_KERNEL */
#endif /* !_VFS_HAMMER2_HAMMER2_H_ */
