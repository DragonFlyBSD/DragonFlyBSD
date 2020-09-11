/*
 * Copyright (c) 2011-2018 The DragonFly Project.  All rights reserved.
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

#ifdef _KERNEL
#include <sys/param.h>
#endif
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/kernel.h>
#endif
#include <sys/conf.h>
#ifdef _KERNEL
#include <sys/systm.h>
#endif
#include <sys/diskslice.h>
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
#ifdef _KERNEL
#include <sys/kern_syscall.h>
#endif

#ifdef _KERNEL
#include <sys/signal2.h>
#include <sys/buf2.h>
#include <sys/mutex2.h>
#include <sys/spinlock2.h>
#endif

#include "hammer2_xxhash.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"
#include "hammer2_ioctl.h"

struct hammer2_io;
struct hammer2_chain;
struct hammer2_cluster;
struct hammer2_inode;
struct hammer2_depend;
struct hammer2_dev;
struct hammer2_pfs;
struct hammer2_span;
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
#define hammer2_mtx_ex_try		mtx_lock_ex_try
#define hammer2_mtx_sh			mtx_lock_sh_quick
#define hammer2_mtx_sh_again		mtx_lock_sh_again
#define hammer2_mtx_sh_try		mtx_lock_sh_try
#define hammer2_mtx_unlock		mtx_unlock
#define hammer2_mtx_downgrade		mtx_downgrade
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
TAILQ_HEAD(hammer2_chain_list, hammer2_chain);

typedef struct hammer2_xop_list	hammer2_xop_list_t;

#ifdef _KERNEL
/*
 * General lock support
 */
static __inline
int
hammer2_mtx_upgrade_try(hammer2_mtx_t *mtx)
{
	return mtx_upgrade_try(mtx);
}

#endif

/*
 * The xid tracks internal transactional updates.
 *
 * XXX fix-me, really needs to be 64-bits
 */
typedef uint32_t hammer2_xid_t;

#define HAMMER2_XID_MIN			0x00000000U
#define HAMMER2_XID_MAX			0x7FFFFFFFU

/*
 * Cap the dynamic calculation for the maximum number of dirty
 * chains and dirty inodes allowed.
 */
#define HAMMER2_LIMIT_DIRTY_CHAINS	(1024*1024)
#define HAMMER2_LIMIT_DIRTY_INODES	(65536)

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

struct hammer2_reptrack {
	hammer2_spin_t	spin;
	struct hammer2_reptrack *next;
	struct hammer2_chain	*chain;
};

/*
 * Core topology for chain (embedded in chain).  Protected by a spinlock.
 */
struct hammer2_chain_core {
	hammer2_spin_t	spin;
	struct hammer2_reptrack *reptrack;
	struct hammer2_chain_tree rbtree; /* sub-chains */
	int		live_zero;	/* blockref array opt */
	u_int		live_count;	/* live (not deleted) chains in tree */
	u_int		chain_count;	/* live + deleted chains under core */
	int		generation;	/* generation number (inserts only) */
};

typedef struct hammer2_chain_core hammer2_chain_core_t;

RB_HEAD(hammer2_io_tree, hammer2_io);

/*
 * DIO - Management structure wrapping system buffer cache.
 *
 * HAMMER2 uses an I/O abstraction that allows it to cache and manipulate
 * fixed-sized filesystem buffers frontend by variable-sized hammer2_chain
 * structures.
 */
/* #define HAMMER2_IO_DEBUG */

#ifdef HAMMER2_IO_DEBUG
#define HAMMER2_IO_DEBUG_ARGS	, const char *file, int line
#define HAMMER2_IO_DEBUG_CALL	, file, line
#define HAMMER2_IO_DEBUG_COUNT	2048
#define HAMMER2_IO_DEBUG_MASK	(HAMMER2_IO_DEBUG_COUNT - 1)
#else
#define HAMMER2_IO_DEBUG_ARGS
#define HAMMER2_IO_DEBUG_CALL
#endif

struct hammer2_io {
	RB_ENTRY(hammer2_io) rbnode;	/* indexed by device offset */
	struct hammer2_dev *hmp;
	struct buf	*bp;
	off_t		pbase;
	uint64_t	refs;
	int		psize;
	int		act;		/* activity */
	int		btype;		/* approximate BREF_TYPE_* */
	int		ticks;
	int		error;
#ifdef HAMMER2_IO_DEBUG
	int		debug_index;
#else
	int		unused01;
#endif
	uint64_t	dedup_valid;	/* valid for dedup operation */
	uint64_t	dedup_alloc;	/* allocated / de-dupable */
#ifdef HAMMER2_IO_DEBUG
	const char	*debug_file[HAMMER2_IO_DEBUG_COUNT];
	void		*debug_td[HAMMER2_IO_DEBUG_COUNT];
	int		debug_line[HAMMER2_IO_DEBUG_COUNT];
	uint64_t	debug_refs[HAMMER2_IO_DEBUG_COUNT];
#endif
};

typedef struct hammer2_io hammer2_io_t;

#define HAMMER2_DIO_INPROG	0x8000000000000000LLU	/* bio in progress */
#define HAMMER2_DIO_GOOD	0x4000000000000000LLU	/* dio->bp is stable */
#define HAMMER2_DIO_WAITING	0x2000000000000000LLU	/* wait on INPROG */
#define HAMMER2_DIO_DIRTY	0x1000000000000000LLU	/* flush last drop */
#define HAMMER2_DIO_FLUSH	0x0800000000000000LLU	/* immediate flush */

#define HAMMER2_DIO_MASK	0x00FFFFFFFFFFFFFFLLU

/*
 * Primary chain structure keeps track of the topology in-memory.
 */
struct hammer2_chain {
	hammer2_mtx_t		lock;
	hammer2_chain_core_t	core;
	RB_ENTRY(hammer2_chain) rbnode;		/* live chain(s) */
	hammer2_blockref_t	bref;
	struct hammer2_chain	*parent;
	struct hammer2_dev	*hmp;
	struct hammer2_pfs	*pmp;		/* A PFS or super-root (spmp) */

	struct lock	diolk;			/* xop focus interlock */
	hammer2_io_t	*dio;			/* physical data buffer */
	hammer2_media_data_t *data;		/* data pointer shortcut */
	u_int		bytes;			/* physical data size */
	u_int		flags;
	u_int		refs;
	u_int		lockcnt;
	int		error;			/* on-lock data error state */
	int		cache_index;		/* heur speeds up lookup */

	TAILQ_ENTRY(hammer2_chain) lru_node;	/* 0-refs LRU */
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
 * MODIFIED	- The chain's media data has been modified.  Prevents chain
 *		  free on lastdrop if still in the topology.
 *
 * UPDATE	- Chain might not be modified but parent blocktable needs
 *		  an update.  Prevents chain free on lastdrop if still in
 *		  the topology.
 *
 * BMAPPED	- Indicates that the chain is present in the parent blockmap.
 *
 * BMAPUPD	- Indicates that the chain is present but needs to be updated
 *		  in the parent blockmap.
 */
#define HAMMER2_CHAIN_MODIFIED		0x00000001	/* dirty chain data */
#define HAMMER2_CHAIN_ALLOCATED		0x00000002	/* kmalloc'd chain */
#define HAMMER2_CHAIN_DESTROY		0x00000004
#define HAMMER2_CHAIN_DEDUPABLE		0x00000008	/* registered w/dedup */
#define HAMMER2_CHAIN_DELETED		0x00000010	/* deleted chain */
#define HAMMER2_CHAIN_INITIAL		0x00000020	/* initial create */
#define HAMMER2_CHAIN_UPDATE		0x00000040	/* need parent update */
#define HAMMER2_CHAIN_NOTTESTED		0x00000080	/* crc not generated */
#define HAMMER2_CHAIN_TESTEDGOOD	0x00000100	/* crc tested good */
#define HAMMER2_CHAIN_ONFLUSH		0x00000200	/* on a flush list */
#define HAMMER2_CHAIN_UNUSED0400	0x00000400
#define HAMMER2_CHAIN_VOLUMESYNC	0x00000800	/* needs volume sync */
#define HAMMER2_CHAIN_UNUSED1000	0x00001000
#define HAMMER2_CHAIN_COUNTEDBREFS	0x00002000	/* block table stats */
#define HAMMER2_CHAIN_ONRBTREE		0x00004000	/* on parent RB tree */
#define HAMMER2_CHAIN_ONLRU		0x00008000	/* on LRU list */
#define HAMMER2_CHAIN_UNUSED10000	0x00010000
#define HAMMER2_CHAIN_RELEASE		0x00020000	/* don't keep around */
#define HAMMER2_CHAIN_BMAPPED		0x00040000	/* present in blkmap */
#define HAMMER2_CHAIN_BMAPUPD		0x00080000	/* +needs updating */
#define HAMMER2_CHAIN_IOINPROG		0x00100000	/* I/O interlock */
#define HAMMER2_CHAIN_IOSIGNAL		0x00200000	/* I/O interlock */
#define HAMMER2_CHAIN_PFSBOUNDARY	0x00400000	/* super->pfs inode */
#define HAMMER2_CHAIN_HINT_LEAF_COUNT	0x00800000	/* redo leaf count */
#define HAMMER2_CHAIN_LRUHINT		0x01000000	/* was reused */

#define HAMMER2_CHAIN_FLUSH_MASK	(HAMMER2_CHAIN_MODIFIED |	\
					 HAMMER2_CHAIN_UPDATE |		\
					 HAMMER2_CHAIN_ONFLUSH |	\
					 HAMMER2_CHAIN_DESTROY)

/*
 * Hammer2 error codes, used by chain->error and cluster->error.  The error
 * code is typically set on-lock unless no I/O was requested, and set on
 * I/O otherwise.  If set for a cluster it generally means that the cluster
 * code could not find a valid copy to present.
 *
 * All H2 error codes are flags and can be accumulated by ORing them
 * together.
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
#define HAMMER2_ERROR_NONE		0	/* no error (must be 0) */
#define HAMMER2_ERROR_EIO		0x00000001	/* device I/O error */
#define HAMMER2_ERROR_CHECK		0x00000002	/* check code error */
#define HAMMER2_ERROR_INCOMPLETE	0x00000004	/* incomplete cluster */
#define HAMMER2_ERROR_DEPTH		0x00000008	/* tmp depth limit */
#define HAMMER2_ERROR_BADBREF		0x00000010	/* illegal bref */
#define HAMMER2_ERROR_ENOSPC		0x00000020	/* allocation failure */
#define HAMMER2_ERROR_ENOENT		0x00000040	/* entry not found */
#define HAMMER2_ERROR_ENOTEMPTY		0x00000080	/* dir not empty */
#define HAMMER2_ERROR_EAGAIN		0x00000100	/* retry */
#define HAMMER2_ERROR_ENOTDIR		0x00000200	/* not directory */
#define HAMMER2_ERROR_EISDIR		0x00000400	/* is directory */
#define HAMMER2_ERROR_EINPROGRESS	0x00000800	/* already running */
#define HAMMER2_ERROR_ABORTED		0x00001000	/* aborted operation */
#define HAMMER2_ERROR_EOF		0x00002000	/* end of scan */
#define HAMMER2_ERROR_EINVAL		0x00004000	/* catch-all */
#define HAMMER2_ERROR_EEXIST		0x00008000	/* entry exists */
#define HAMMER2_ERROR_EDEADLK		0x00010000
#define HAMMER2_ERROR_ESRCH		0x00020000
#define HAMMER2_ERROR_ETIMEDOUT		0x00040000

/*
 * Flags passed to hammer2_chain_lookup() and hammer2_chain_next()
 *
 * NOTES:
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
 *	ALWAYS	    - Always resolve the data.  If ALWAYS and NODATA are both
 *		      missing, bulk file data is not resolved but inodes and
 *		      other meta-data will.
 */
#define HAMMER2_LOOKUP_UNUSED0001	0x00000001
#define HAMMER2_LOOKUP_NODATA		0x00000002	/* data left NULL */
#define HAMMER2_LOOKUP_NODIRECT		0x00000004	/* no offset=0 DD */
#define HAMMER2_LOOKUP_SHARED		0x00000100
#define HAMMER2_LOOKUP_MATCHIND		0x00000200	/* return all chains */
#define HAMMER2_LOOKUP_UNUSED0400	0x00000400
#define HAMMER2_LOOKUP_ALWAYS		0x00000800	/* resolve data */
#define HAMMER2_LOOKUP_UNUSED1000	0x00001000

/*
 * Flags passed to hammer2_chain_modify() and hammer2_chain_resize()
 *
 * NOTE: OPTDATA allows us to avoid instantiating buffers for INDIRECT
 *	 blocks in the INITIAL-create state.
 */
#define HAMMER2_MODIFY_OPTDATA		0x00000002	/* data can be NULL */

/*
 * Flags passed to hammer2_chain_lock()
 *
 * NOTE: RDONLY is set to optimize cluster operations when *no* modifications
 *	 will be made to either the cluster being locked or any underlying
 *	 cluster.  It allows the cluster to lock and access data for a subset
 *	 of available nodes instead of all available nodes.
 *
 * NOTE: NONBLOCK is only used for hammer2_chain_repparent() and getparent(),
 *	 other functions (e.g. hammer2_chain_lookup(), etc) can't handle its
 *	 operation.
 */
#define HAMMER2_RESOLVE_NEVER		1
#define HAMMER2_RESOLVE_MAYBE		2
#define HAMMER2_RESOLVE_ALWAYS		3
#define HAMMER2_RESOLVE_MASK		0x0F

#define HAMMER2_RESOLVE_SHARED		0x10	/* request shared lock */
#define HAMMER2_RESOLVE_LOCKAGAIN	0x20	/* another shared lock */
#define HAMMER2_RESOLVE_UNUSED40	0x40
#define HAMMER2_RESOLVE_NONBLOCK	0x80	/* non-blocking */

/*
 * Flags passed to hammer2_chain_delete()
 */
#define HAMMER2_DELETE_PERMANENT	0x0001

/*
 * Flags passed to hammer2_chain_insert() or hammer2_chain_rename()
 * or hammer2_chain_create().
 */
#define HAMMER2_INSERT_PFSROOT		0x0004
#define HAMMER2_INSERT_SAMEPARENT	0x0008

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
#define HAMMER2_XOPFIFO		16
#define HAMMER2_XOPFIFO_MASK	(HAMMER2_XOPFIFO - 1)
#define HAMMER2_XOPGROUPS_MIN	32

#define HAMMER2_MAXCLUSTER	8
#define HAMMER2_XOPMASK_CLUSTER	((uint64_t)((1LLU << HAMMER2_MAXCLUSTER) - 1))
#define HAMMER2_XOPMASK_VOP	((uint64_t)0x0000000080000000LLU)
#define HAMMER2_XOPMASK_FIFOW	((uint64_t)0x0000000040000000LLU)
#define HAMMER2_XOPMASK_WAIT	((uint64_t)0x0000000020000000LLU)
#define HAMMER2_XOPMASK_FEED	((uint64_t)0x0000000100000000LLU)

#define HAMMER2_XOPMASK_ALLDONE	(HAMMER2_XOPMASK_VOP | HAMMER2_XOPMASK_CLUSTER)

struct hammer2_cluster_item {
	hammer2_chain_t		*chain;
	int			error;
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

RB_HEAD(hammer2_inode_tree, hammer2_inode);	/* ip->rbnode */
TAILQ_HEAD(inoq_head, hammer2_inode);		/* ip->entry */
TAILQ_HEAD(depq_head, hammer2_depend);		/* depend->entry */

struct hammer2_depend {
	TAILQ_ENTRY(hammer2_depend) entry;
	struct inoq_head	sideq;
	long			count;
	int			pass2;
	int			unused01;
};

typedef struct hammer2_depend hammer2_depend_t;

/*
 * A hammer2 inode.
 *
 * NOTE: The inode-embedded cluster is never used directly for I/O (since
 *	 it may be shared).  Instead it will be replicated-in and synchronized
 *	 back out if changed.
 */
struct hammer2_inode {
	RB_ENTRY(hammer2_inode) rbnode;		/* inumber lookup (HL) */
	TAILQ_ENTRY(hammer2_inode) entry;	/* SYNCQ/SIDEQ */
	hammer2_depend_t	*depend;	/* non-NULL if SIDEQ */
	hammer2_depend_t	depend_static;	/* (in-place allocation) */
	hammer2_mtx_t		lock;		/* inode lock */
	hammer2_mtx_t		truncate_lock;	/* prevent truncates */
	struct hammer2_pfs	*pmp;		/* PFS mount */
	struct vnode		*vp;
	hammer2_spin_t		cluster_spin;	/* update cluster */
	hammer2_cluster_t	cluster;
	struct lockf		advlock;
	u_int			flags;
	u_int			refs;		/* +vpref, +flushref */
	uint8_t			comp_heuristic;
	hammer2_inode_meta_t	meta;		/* copy of meta-data */
	hammer2_off_t		osize;
};

typedef struct hammer2_inode hammer2_inode_t;

/*
 * MODIFIED	- Inode is in a modified state, ip->meta may have changes.
 * RESIZED	- Inode truncated (any) or inode extended beyond
 *		  EMBEDDED_BYTES.
 *
 * SYNCQ	- Inode is included in the current filesystem sync.  The
 *		  DELETING and CREATING flags will be acted upon.
 *
 * SIDEQ	- Inode has likely been disconnected from the vnode topology
 *		  and so is not visible to the vnode-based filesystem syncer
 *		  code, but is dirty and must be included in the next
 *		  filesystem sync.  These inodes are moved to the SYNCQ at
 *		  the time the sync occurs.
 *
 *		  Inodes are not placed on this queue simply because they have
 *		  become dirty, if a vnode is attached.
 *
 * DELETING	- Inode is flagged for deletion during the next filesystem
 *		  sync.  That is, the inode's chain is currently connected
 *		  and must be deleting during the current or next fs sync.
 *
 * CREATING	- Inode is flagged for creation during the next filesystem
 *		  sync.  That is, the inode's chain topology exists (so
 *		  kernel buffer flushes can occur), but is currently
 *		  disconnected and must be inserted during the current or
 *		  next fs sync.  If the DELETING flag is also set, the
 *		  topology can be thrown away instead.
 *
 * If an inode that is already part of the current filesystem sync is
 * modified by the frontend, including by buffer flushes, the inode lock
 * code detects the SYNCQ flag and moves the inode to the head of the
 * flush-in-progress, then blocks until the flush has gotten past it.
 */
#define HAMMER2_INODE_MODIFIED		0x0001
#define HAMMER2_INODE_SROOT		0x0002
#define HAMMER2_INODE_UNUSED0004	0x0004
#define HAMMER2_INODE_ONRBTREE		0x0008
#define HAMMER2_INODE_RESIZED		0x0010	/* requires inode_chain_sync */
#define HAMMER2_INODE_UNUSED0020	0x0020
#define HAMMER2_INODE_ISUNLINKED	0x0040
#define HAMMER2_INODE_METAGOOD		0x0080	/* inode meta-data good */
#define HAMMER2_INODE_SIDEQ		0x0100	/* on side processing queue */
#define HAMMER2_INODE_NOSIDEQ		0x0200	/* disable sideq operation */
#define HAMMER2_INODE_DIRTYDATA		0x0400	/* interlocks inode flush */
#define HAMMER2_INODE_SYNCQ		0x0800	/* sync interlock, sequenced */
#define HAMMER2_INODE_DELETING		0x1000	/* sync interlock, chain topo */
#define HAMMER2_INODE_CREATING		0x2000	/* sync interlock, chain topo */
#define HAMMER2_INODE_SYNCQ_WAKEUP	0x4000	/* sync interlock wakeup */
#define HAMMER2_INODE_SYNCQ_PASS2	0x8000	/* force retry delay */

#define HAMMER2_INODE_DIRTY		(HAMMER2_INODE_MODIFIED |	\
					 HAMMER2_INODE_DIRTYDATA |	\
					 HAMMER2_INODE_DELETING |	\
					 HAMMER2_INODE_CREATING)

int hammer2_inode_cmp(hammer2_inode_t *ip1, hammer2_inode_t *ip2);
RB_PROTOTYPE2(hammer2_inode_tree, hammer2_inode, rbnode, hammer2_inode_cmp,
		hammer2_tid_t);

/*
 * Transaction management sub-structure under hammer2_pfs
 */
struct hammer2_trans {
	uint32_t		flags;
	uint32_t		sync_wait;
};

typedef struct hammer2_trans hammer2_trans_t;

#define HAMMER2_TRANS_ISFLUSH		0x80000000	/* flush code */
#define HAMMER2_TRANS_BUFCACHE		0x40000000	/* bio strategy */
#define HAMMER2_TRANS_SIDEQ		0x20000000	/* run sideq */
#define HAMMER2_TRANS_UNUSED10		0x10000000
#define HAMMER2_TRANS_WAITING		0x08000000	/* someone waiting */
#define HAMMER2_TRANS_RESCAN		0x04000000	/* rescan sideq */
#define HAMMER2_TRANS_MASK		0x00FFFFFF	/* count mask */

#define HAMMER2_FREEMAP_HEUR_NRADIX	4	/* pwr 2 PBUFRADIX-LBUFRADIX */
#define HAMMER2_FREEMAP_HEUR_TYPES	8
#define HAMMER2_FREEMAP_HEUR_SIZE	(HAMMER2_FREEMAP_HEUR_NRADIX * \
					 HAMMER2_FREEMAP_HEUR_TYPES)

#define HAMMER2_DEDUP_HEUR_SIZE		(65536 * 4)
#define HAMMER2_DEDUP_HEUR_MASK		(HAMMER2_DEDUP_HEUR_SIZE - 1)

#define HAMMER2_FLUSH_TOP		0x0001
#define HAMMER2_FLUSH_ALL		0x0002
#define HAMMER2_FLUSH_INODE_STOP	0x0004	/* stop at sub-inode */
#define HAMMER2_FLUSH_FSSYNC		0x0008	/* part of filesystem sync */


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
	struct hammer2_dev *hmp;
	hammer2_xop_list_t xopq;
	thread_t	td;
	uint32_t	flags;
	int		depth;
	int		clindex;	/* cluster element index */
	int		repidx;
	char		*scratch;	/* MAXPHYS */
};

typedef struct hammer2_thread hammer2_thread_t;

#define HAMMER2_THREAD_UNMOUNTING	0x0001	/* unmount request */
#define HAMMER2_THREAD_DEV		0x0002	/* related to dev, not pfs */
#define HAMMER2_THREAD_WAITING		0x0004	/* thread in idle tsleep */
#define HAMMER2_THREAD_REMASTER		0x0008	/* remaster request */
#define HAMMER2_THREAD_STOP		0x0010	/* exit request */
#define HAMMER2_THREAD_FREEZE		0x0020	/* force idle */
#define HAMMER2_THREAD_FROZEN		0x0040	/* thread is frozen */
#define HAMMER2_THREAD_XOPQ		0x0080	/* work pending */
#define HAMMER2_THREAD_STOPPED		0x0100	/* thread has stopped */
#define HAMMER2_THREAD_UNFREEZE		0x0200

#define HAMMER2_THREAD_WAKEUP_MASK	(HAMMER2_THREAD_UNMOUNTING |	\
					 HAMMER2_THREAD_REMASTER |	\
					 HAMMER2_THREAD_STOP |		\
					 HAMMER2_THREAD_FREEZE |	\
					 HAMMER2_THREAD_XOPQ)

/*
 * Support structure for dedup heuristic.
 */
struct hammer2_dedup {
	hammer2_off_t	data_off;
	uint64_t	data_crc;
	uint32_t	ticks;
	uint32_t	saved_error;
};

typedef struct hammer2_dedup hammer2_dedup_t;

/*
 * hammer2_xop - container for VOP/XOP operation (allocated, not on stack).
 *
 * This structure is used to distribute a VOP operation across multiple
 * nodes.  It provides a rendezvous for concurrent node execution and
 * can be detached from the frontend operation to allow the frontend to
 * return early.
 *
 * This structure also sequences operations on up to three inodes.
 */
typedef void (*hammer2_xop_func_t)(union hammer2_xop *xop, void *scratch,
				   int clindex);

struct hammer2_xop_desc {
	hammer2_xop_func_t	storage_func;	/* local storage function */
	hammer2_xop_func_t	dmsg_dispatch;	/* dmsg dispatch function */
	hammer2_xop_func_t	dmsg_process;	/* dmsg processing function */
	const char		*id;
};

typedef struct hammer2_xop_desc hammer2_xop_desc_t;

struct hammer2_xop_fifo {
	TAILQ_ENTRY(hammer2_xop_head) entry;
	hammer2_chain_t		*array[HAMMER2_XOPFIFO];
	int			errors[HAMMER2_XOPFIFO];
	int			ri;
	int			wi;
	int			flags;
	hammer2_thread_t	*thr;
};

typedef struct hammer2_xop_fifo hammer2_xop_fifo_t;

#define HAMMER2_XOP_FIFO_RUN	0x0001
#define HAMMER2_XOP_FIFO_STALL	0x0002

struct hammer2_xop_head {
	hammer2_xop_desc_t	*desc;
	hammer2_tid_t		mtid;
	struct hammer2_inode	*ip1;
	struct hammer2_inode	*ip2;
	struct hammer2_inode	*ip3;
	uint64_t		run_mask;
	uint64_t		chk_mask;
	int			flags;
	int			state;
	int			error;
	hammer2_key_t		collect_key;
	char			*name1;
	size_t			name1_len;
	char			*name2;
	size_t			name2_len;
	hammer2_xop_fifo_t	collect[HAMMER2_MAXCLUSTER];
	hammer2_cluster_t	cluster;	/* help collections */
	hammer2_io_t		*focus_dio;
};

typedef struct hammer2_xop_head hammer2_xop_head_t;

struct hammer2_xop_ipcluster {
	hammer2_xop_head_t	head;
};

struct hammer2_xop_strategy {
	hammer2_xop_head_t	head;
	hammer2_key_t		lbase;
	int			finished;
	hammer2_mtx_t		lock;
	struct bio		*bio;
};

struct hammer2_xop_readdir {
	hammer2_xop_head_t	head;
	hammer2_key_t		lkey;
};

struct hammer2_xop_nresolve {
	hammer2_xop_head_t	head;
	hammer2_key_t		lhc;	/* if name is NULL used lhc */
};

struct hammer2_xop_unlink {
	hammer2_xop_head_t	head;
	int			isdir;
	int			dopermanent;
};

#define H2DOPERM_PERMANENT	0x01
#define H2DOPERM_FORCE		0x02
#define H2DOPERM_IGNINO		0x04

struct hammer2_xop_nrename {
	hammer2_xop_head_t	head;
	hammer2_tid_t		lhc;
	int			ip_key;
};

struct hammer2_xop_scanlhc {
	hammer2_xop_head_t	head;
	hammer2_key_t		lhc;
};

struct hammer2_xop_scanall {
	hammer2_xop_head_t	head;
	hammer2_key_t		key_beg;	/* inclusive */
	hammer2_key_t		key_end;	/* inclusive */
	int			resolve_flags;
	int			lookup_flags;
};

struct hammer2_xop_lookup {
	hammer2_xop_head_t	head;
	hammer2_key_t		lhc;
};

struct hammer2_xop_mkdirent {
	hammer2_xop_head_t	head;
	hammer2_dirent_head_t	dirent;
	hammer2_key_t		lhc;
};

struct hammer2_xop_create {
	hammer2_xop_head_t	head;
	hammer2_inode_meta_t	meta;		/* initial metadata */
	hammer2_key_t		lhc;
	int			flags;
};

struct hammer2_xop_destroy {
	hammer2_xop_head_t	head;
};

struct hammer2_xop_fsync {
	hammer2_xop_head_t	head;
	hammer2_inode_meta_t	meta;
	hammer2_off_t		osize;
	u_int			ipflags;
	int			clear_directdata;
};

struct hammer2_xop_unlinkall {
	hammer2_xop_head_t	head;
	hammer2_key_t		key_beg;
	hammer2_key_t		key_end;
};

struct hammer2_xop_connect {
	hammer2_xop_head_t	head;
	hammer2_key_t		lhc;
};

struct hammer2_xop_flush {
	hammer2_xop_head_t	head;
};

typedef struct hammer2_xop_readdir hammer2_xop_readdir_t;
typedef struct hammer2_xop_nresolve hammer2_xop_nresolve_t;
typedef struct hammer2_xop_unlink hammer2_xop_unlink_t;
typedef struct hammer2_xop_nrename hammer2_xop_nrename_t;
typedef struct hammer2_xop_ipcluster hammer2_xop_ipcluster_t;
typedef struct hammer2_xop_strategy hammer2_xop_strategy_t;
typedef struct hammer2_xop_mkdirent hammer2_xop_mkdirent_t;
typedef struct hammer2_xop_create hammer2_xop_create_t;
typedef struct hammer2_xop_destroy hammer2_xop_destroy_t;
typedef struct hammer2_xop_fsync hammer2_xop_fsync_t;
typedef struct hammer2_xop_unlinkall hammer2_xop_unlinkall_t;
typedef struct hammer2_xop_scanlhc hammer2_xop_scanlhc_t;
typedef struct hammer2_xop_scanall hammer2_xop_scanall_t;
typedef struct hammer2_xop_lookup hammer2_xop_lookup_t;
typedef struct hammer2_xop_connect hammer2_xop_connect_t;
typedef struct hammer2_xop_flush hammer2_xop_flush_t;

union hammer2_xop {
	hammer2_xop_head_t	head;
	hammer2_xop_ipcluster_t	xop_ipcluster;
	hammer2_xop_readdir_t	xop_readdir;
	hammer2_xop_nresolve_t	xop_nresolve;
	hammer2_xop_unlink_t	xop_unlink;
	hammer2_xop_nrename_t	xop_nrename;
	hammer2_xop_strategy_t	xop_strategy;
	hammer2_xop_mkdirent_t	xop_mkdirent;
	hammer2_xop_create_t	xop_create;
	hammer2_xop_destroy_t	xop_destroy;
	hammer2_xop_fsync_t	xop_fsync;
	hammer2_xop_unlinkall_t	xop_unlinkall;
	hammer2_xop_scanlhc_t	xop_scanlhc;
	hammer2_xop_scanall_t	xop_scanall;
	hammer2_xop_lookup_t	xop_lookup;
	hammer2_xop_flush_t	xop_flush;
	hammer2_xop_connect_t	xop_connect;
};

typedef union hammer2_xop hammer2_xop_t;

/*
 * hammer2_xop_group - Manage XOP support threads.
 */
struct hammer2_xop_group {
	hammer2_thread_t	thrs[HAMMER2_MAXCLUSTER];
};

typedef struct hammer2_xop_group hammer2_xop_group_t;

/*
 * flags to hammer2_xop_collect()
 */
#define HAMMER2_XOP_COLLECT_NOWAIT	0x00000001
#define HAMMER2_XOP_COLLECT_WAITALL	0x00000002

/*
 * flags to hammer2_xop_alloc()
 *
 * MODIFYING	- This is a modifying transaction, allocate a mtid.
 * RECURSE	- Recurse top-level inode (for root flushes)
 */
#define HAMMER2_XOP_MODIFYING		0x00000001
#define HAMMER2_XOP_STRATEGY		0x00000002
#define HAMMER2_XOP_INODE_STOP		0x00000004
#define HAMMER2_XOP_VOLHDR		0x00000008
#define HAMMER2_XOP_FSSYNC		0x00000010
#define HAMMER2_XOP_IROOT		0x00000020

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
	hammer2_spin_t	io_spin;	/* iotree, iolruq access */
	struct hammer2_io_tree iotree;
	int		iofree_count;
	int		freemap_relaxed;
	hammer2_chain_t vchain;		/* anchor chain (topology) */
	hammer2_chain_t fchain;		/* anchor chain (freemap) */
	hammer2_spin_t	list_spin;
	struct hammer2_pfs *spmp;	/* super-root pmp for transactions */
	struct lock	vollk;		/* lockmgr lock */
	struct lock	bulklk;		/* bulkfree operation lock */
	struct lock	bflock;		/* bulk-free manual function lock */
	hammer2_off_t	heur_freemap[HAMMER2_FREEMAP_HEUR_SIZE];
	hammer2_dedup_t heur_dedup[HAMMER2_DEDUP_HEUR_SIZE];
	int		volhdrno;	/* last volhdrno written */
	uint32_t	hflags;		/* HMNT2 flags applicable to device */
	hammer2_off_t	free_reserved;	/* nominal free reserved */
	hammer2_thread_t bfthr;		/* bulk-free thread */
	char		devrepname[64];	/* for kprintf */
	hammer2_ioc_bulkfree_t bflast;	/* stats for last bulkfree run */
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
 *	    particular, if not mounted there will be no wthread.
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
	hammer2_dev_t		*force_local;	/* only if 'local' mount */
	hammer2_inode_t		*iroot;		/* PFS root inode */
	uint8_t			pfs_types[HAMMER2_MAXCLUSTER];
	char			*pfs_names[HAMMER2_MAXCLUSTER];
	hammer2_dev_t		*pfs_hmps[HAMMER2_MAXCLUSTER];
	hammer2_blockset_t	pfs_iroot_blocksets[HAMMER2_MAXCLUSTER];
	hammer2_trans_t		trans;
	struct lock		lock;		/* PFS lock for certain ops */
	struct lock		lock_nlink;	/* rename and nlink lock */
	struct netexport	export;		/* nfs export */
	int			unused00;
	int			ronly;		/* read-only mount */
	int			hflags;		/* pfs-specific mount flags */
	struct malloc_type	*minode;
	struct malloc_type	*mmsg;
	hammer2_spin_t		inum_spin;	/* inumber lookup */
	struct hammer2_inode_tree inum_tree;	/* (not applicable to spmp) */
	long			inum_count;	/* #of inodes in inum_tree */
	hammer2_spin_t		lru_spin;	/* inumber lookup */
	struct hammer2_chain_list lru_list;	/* basis for LRU tests */
	int			lru_count;	/* #of chains on LRU */
	int			flags;
	hammer2_tid_t		modify_tid;	/* modify transaction id */
	hammer2_tid_t		inode_tid;	/* inode allocator */
	uint8_t			pfs_nmasters;	/* total masters */
	uint8_t			pfs_mode;	/* operating mode PFSMODE */
	uint8_t			unused01;
	uint8_t			unused02;
	int			free_ticks;	/* free_* calculations */
	long			inmem_inodes;
	hammer2_off_t		free_reserved;
	hammer2_off_t		free_nominal;
	uint32_t		inmem_dirty_chains;
	int			count_lwinprog;	/* logical write in prog */
	hammer2_spin_t		list_spin;
	struct inoq_head	syncq;		/* SYNCQ flagged inodes */
	struct depq_head	depq;		/* SIDEQ flagged inodes */
	long			sideq_count;	/* total inodes on depq */
	hammer2_thread_t	sync_thrs[HAMMER2_MAXCLUSTER];
	uint32_t		cluster_flags;	/* cached cluster flags */
	int			has_xop_threads;
	hammer2_spin_t		xop_spin;	/* xop sequencer */
	hammer2_xop_group_t	*xop_groups;
};

typedef struct hammer2_pfs hammer2_pfs_t;

TAILQ_HEAD(hammer2_pfslist, hammer2_pfs);

/*
 * pmp->flags
 */
#define HAMMER2_PMPF_SPMP	0x00000001
#define HAMMER2_PMPF_EMERG	0x00000002	/* Emergency delete mode */

/*
 * NOTE: The LRU list contains at least all the chains with refs == 0
 *	 that can be recycled, and may contain additional chains which
 *	 cannot.
 */
#define HAMMER2_LRU_LIMIT		4096

#define HAMMER2_DIRTYCHAIN_WAITING	0x80000000
#define HAMMER2_DIRTYCHAIN_MASK		0x7FFFFFFF

#define HAMMER2_LWINPROG_WAITING	0x80000000
#define HAMMER2_LWINPROG_WAITING0	0x40000000
#define HAMMER2_LWINPROG_MASK		0x3FFFFFFF

/*
 * hammer2_cluster_check
 */
#define HAMMER2_CHECK_NULL	0x00000001

/*
 * Misc
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define VTOI(vp)	((hammer2_inode_t *)(vp)->v_data)
#endif

#if defined(_KERNEL)

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_HAMMER2);
#endif

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

#define HAMMER2_DEDUP_FRAG      (HAMMER2_PBUFSIZE / 64)
#define HAMMER2_DEDUP_FRAGRADIX (HAMMER2_PBUFRADIX - 6)

static __inline
uint64_t
hammer2_dedup_mask(hammer2_io_t *dio, hammer2_off_t data_off, u_int bytes)
{
	int bbeg;
	int bits;
	uint64_t mask;

	bbeg = (int)((data_off & ~HAMMER2_OFF_MASK_RADIX) - dio->pbase) >>
	       HAMMER2_DEDUP_FRAGRADIX;
	bits = (int)((bytes + (HAMMER2_DEDUP_FRAG - 1)) >>
	       HAMMER2_DEDUP_FRAGRADIX);
	mask = ((uint64_t)1 << bbeg) - 1;
	if (bbeg + bits == 64)
		mask = (uint64_t)-1;
	else
		mask = ((uint64_t)1 << (bbeg + bits)) - 1;

	mask &= ~(((uint64_t)1 << bbeg) - 1);

	return mask;
}

static __inline
int
hammer2_error_to_errno(int error)
{
	if (error) {
		if (error & HAMMER2_ERROR_EIO)
			error = EIO;
		else if (error & HAMMER2_ERROR_CHECK)
			error = EDOM;
		else if (error & HAMMER2_ERROR_ABORTED)
			error = EINTR;
		else if (error & HAMMER2_ERROR_BADBREF)
			error = EIO;
		else if (error & HAMMER2_ERROR_ENOSPC)
			error = ENOSPC;
		else if (error & HAMMER2_ERROR_ENOENT)
			error = ENOENT;
		else if (error & HAMMER2_ERROR_ENOTEMPTY)
			error = ENOTEMPTY;
		else if (error & HAMMER2_ERROR_EAGAIN)
			error = EAGAIN;
		else if (error & HAMMER2_ERROR_ENOTDIR)
			error = ENOTDIR;
		else if (error & HAMMER2_ERROR_EISDIR)
			error = EISDIR;
		else if (error & HAMMER2_ERROR_EINPROGRESS)
			error = EINPROGRESS;
		else if (error & HAMMER2_ERROR_EEXIST)
			error = EEXIST;
		else if (error & HAMMER2_ERROR_EINVAL)
			error = EINVAL;
		else if (error & HAMMER2_ERROR_EDEADLK)
			error = EDEADLK;
		else if (error & HAMMER2_ERROR_ESRCH)
			error = ESRCH;
		else if (error & HAMMER2_ERROR_ETIMEDOUT)
			error = ETIMEDOUT;
		else
			error = EDOM;
	}
	return error;
}

static __inline
int
hammer2_errno_to_error(int error)
{
	switch(error) {
	case 0:
		return 0;
	case EIO:
		return HAMMER2_ERROR_EIO;
	case EDOM:
		return HAMMER2_ERROR_CHECK;
	case EINTR:
		return HAMMER2_ERROR_ABORTED;
	//case EIO:
	//	return HAMMER2_ERROR_BADBREF;
	case ENOSPC:
		return HAMMER2_ERROR_ENOSPC;
	case ENOENT:
		return HAMMER2_ERROR_ENOENT;
	case ENOTEMPTY:
		return HAMMER2_ERROR_ENOTEMPTY;
	case EAGAIN:
		return HAMMER2_ERROR_EAGAIN;
	case ENOTDIR:
		return HAMMER2_ERROR_ENOTDIR;
	case EISDIR:
		return HAMMER2_ERROR_EISDIR;
	case EINPROGRESS:
		return HAMMER2_ERROR_EINPROGRESS;
	case EEXIST:
		return HAMMER2_ERROR_EEXIST;
	case EINVAL:
		return HAMMER2_ERROR_EINVAL;
	case EDEADLK:
		return HAMMER2_ERROR_EDEADLK;
	case ESRCH:
		return HAMMER2_ERROR_ESRCH;
	case ETIMEDOUT:
		return HAMMER2_ERROR_ETIMEDOUT;
	default:
		return HAMMER2_ERROR_EINVAL;
	}
}


extern struct vop_ops hammer2_vnode_vops;
extern struct vop_ops hammer2_spec_vops;
extern struct vop_ops hammer2_fifo_vops;
extern struct hammer2_pfslist hammer2_pfslist;
extern struct lock hammer2_mntlk;


extern int hammer2_debug;
extern int hammer2_xopgroups;
extern long hammer2_debug_inode;
extern int hammer2_cluster_meta_read;
extern int hammer2_cluster_data_read;
extern int hammer2_cluster_write;
extern int hammer2_dedup_enable;
extern int hammer2_always_compress;
extern int hammer2_flush_pipe;
extern int hammer2_dio_count;
extern int hammer2_dio_limit;
extern int hammer2_bulkfree_tps;
extern int hammer2_worker_rmask;
extern long hammer2_chain_allocs;
extern long hammer2_limit_dirty_chains;
extern long hammer2_limit_dirty_inodes;
extern long hammer2_count_modified_chains;
extern long hammer2_iod_file_read;
extern long hammer2_iod_meta_read;
extern long hammer2_iod_indr_read;
extern long hammer2_iod_fmap_read;
extern long hammer2_iod_volu_read;
extern long hammer2_iod_file_write;
extern long hammer2_iod_file_wembed;
extern long hammer2_iod_file_wzero;
extern long hammer2_iod_file_wdedup;
extern long hammer2_iod_meta_write;
extern long hammer2_iod_indr_write;
extern long hammer2_iod_fmap_write;
extern long hammer2_iod_volu_write;

extern long hammer2_process_icrc32;
extern long hammer2_process_xxhash64;

extern struct objcache *cache_buffer_read;
extern struct objcache *cache_buffer_write;
extern struct objcache *cache_xops;

/*
 * hammer2_subr.c
 */
#define hammer2_icrc32(buf, size)	iscsi_crc32((buf), (size))
#define hammer2_icrc32c(buf, size, crc)	iscsi_crc32_ext((buf), (size), (crc))

int hammer2_signal_check(time_t *timep);
const char *hammer2_error_str(int error);
const char *hammer2_bref_type_str(int btype);

void hammer2_dev_exlock(hammer2_dev_t *hmp);
void hammer2_dev_shlock(hammer2_dev_t *hmp);
void hammer2_dev_unlock(hammer2_dev_t *hmp);

int hammer2_get_dtype(uint8_t type);
int hammer2_get_vtype(uint8_t type);
uint8_t hammer2_get_obj_type(enum vtype vtype);
void hammer2_time_to_timespec(uint64_t xtime, struct timespec *ts);
uint64_t hammer2_timespec_to_time(const struct timespec *ts);
uint32_t hammer2_to_unix_xid(const uuid_t *uuid);
void hammer2_guid_to_uuid(uuid_t *uuid, uint32_t guid);

hammer2_key_t hammer2_dirhash(const unsigned char *name, size_t len);
int hammer2_getradix(size_t bytes);

int hammer2_calc_logical(hammer2_inode_t *ip, hammer2_off_t uoff,
			hammer2_key_t *lbasep, hammer2_key_t *leofp);
int hammer2_calc_physical(hammer2_inode_t *ip, hammer2_key_t lbase);
void hammer2_update_time(uint64_t *timep);
void hammer2_adjreadcounter(int btype, size_t bytes);
void hammer2_adjwritecounter(int btype, size_t bytes);

/*
 * hammer2_inode.c
 */
struct vnode *hammer2_igetv(hammer2_inode_t *ip, int *errorp);
hammer2_inode_t *hammer2_inode_lookup(hammer2_pfs_t *pmp,
			hammer2_tid_t inum);
hammer2_inode_t *hammer2_inode_get(hammer2_pfs_t *pmp,
			hammer2_xop_head_t *xop, hammer2_tid_t inum, int idx);
void hammer2_inode_ref(hammer2_inode_t *ip);
void hammer2_inode_drop(hammer2_inode_t *ip);
void hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_inode_t *pip,
			hammer2_cluster_t *cluster);
void hammer2_inode_repoint_one(hammer2_inode_t *ip, hammer2_cluster_t *cluster,
			int idx);
void hammer2_inode_modify(hammer2_inode_t *ip);
void hammer2_inode_delayed_sideq(hammer2_inode_t *ip);
void hammer2_inode_lock(hammer2_inode_t *ip, int how);
void hammer2_inode_lock4(hammer2_inode_t *ip1, hammer2_inode_t *ip2,
			hammer2_inode_t *ip3, hammer2_inode_t *ip4);
void hammer2_inode_unlock(hammer2_inode_t *ip);
void hammer2_inode_depend(hammer2_inode_t *ip1, hammer2_inode_t *ip2);
hammer2_chain_t *hammer2_inode_chain(hammer2_inode_t *ip, int clindex, int how);
hammer2_chain_t *hammer2_inode_chain_and_parent(hammer2_inode_t *ip,
			int clindex, hammer2_chain_t **parentp, int how);
hammer2_mtx_state_t hammer2_inode_lock_temp_release(hammer2_inode_t *ip);
void hammer2_inode_lock_temp_restore(hammer2_inode_t *ip,
			hammer2_mtx_state_t ostate);
int hammer2_inode_lock_upgrade(hammer2_inode_t *ip);
void hammer2_inode_lock_downgrade(hammer2_inode_t *ip, int);

hammer2_inode_t *hammer2_inode_create_normal(hammer2_inode_t *pip,
			struct vattr *vap, struct ucred *cred,
			hammer2_key_t inum, int *errorp);
hammer2_inode_t *hammer2_inode_create_pfs(hammer2_pfs_t *spmp,
			const uint8_t *name, size_t name_len,
			int *errorp);
int hammer2_inode_chain_ins(hammer2_inode_t *ip);
int hammer2_inode_chain_des(hammer2_inode_t *ip);
int hammer2_inode_chain_sync(hammer2_inode_t *ip);
int hammer2_inode_chain_flush(hammer2_inode_t *ip, int flags);
int hammer2_inode_unlink_finisher(hammer2_inode_t *ip, int isopen);
int hammer2_dirent_create(hammer2_inode_t *dip, const char *name,
			size_t name_len, hammer2_key_t inum, uint8_t type);

/*
 * hammer2_chain.c
 */
void hammer2_voldata_lock(hammer2_dev_t *hmp);
void hammer2_voldata_unlock(hammer2_dev_t *hmp);
void hammer2_voldata_modify(hammer2_dev_t *hmp);
hammer2_chain_t *hammer2_chain_alloc(hammer2_dev_t *hmp,
				hammer2_pfs_t *pmp,
				hammer2_blockref_t *bref);
void hammer2_chain_core_init(hammer2_chain_t *chain);
void hammer2_chain_ref(hammer2_chain_t *chain);
void hammer2_chain_ref_hold(hammer2_chain_t *chain);
void hammer2_chain_drop(hammer2_chain_t *chain);
void hammer2_chain_drop_unhold(hammer2_chain_t *chain);
void hammer2_chain_unhold(hammer2_chain_t *chain);
void hammer2_chain_rehold(hammer2_chain_t *chain);
int hammer2_chain_lock(hammer2_chain_t *chain, int how);
void hammer2_chain_lock_unhold(hammer2_chain_t *chain, int how);
void hammer2_chain_load_data(hammer2_chain_t *chain);
const hammer2_media_data_t *hammer2_chain_rdata(hammer2_chain_t *chain);
hammer2_media_data_t *hammer2_chain_wdata(hammer2_chain_t *chain);

int hammer2_chain_inode_find(hammer2_pfs_t *pmp, hammer2_key_t inum,
				int clindex, int flags,
				hammer2_chain_t **parentp,
				hammer2_chain_t **chainp);
int hammer2_chain_modify(hammer2_chain_t *chain, hammer2_tid_t mtid,
				hammer2_off_t dedup_off, int flags);
int hammer2_chain_modify_ip(hammer2_inode_t *ip, hammer2_chain_t *chain,
				hammer2_tid_t mtid, int flags);
int hammer2_chain_resize(hammer2_chain_t *chain,
				hammer2_tid_t mtid, hammer2_off_t dedup_off,
				int nradix, int flags);
void hammer2_chain_unlock(hammer2_chain_t *chain);
void hammer2_chain_unlock_hold(hammer2_chain_t *chain);
void hammer2_chain_wait(hammer2_chain_t *chain);
hammer2_chain_t *hammer2_chain_get(hammer2_chain_t *parent, int generation,
				hammer2_blockref_t *bref, int how);
hammer2_chain_t *hammer2_chain_lookup_init(hammer2_chain_t *parent, int flags);
void hammer2_chain_lookup_done(hammer2_chain_t *parent);
hammer2_chain_t *hammer2_chain_getparent(hammer2_chain_t *chain, int flags);
hammer2_chain_t *hammer2_chain_repparent(hammer2_chain_t **chainp, int flags);
hammer2_chain_t *hammer2_chain_lookup(hammer2_chain_t **parentp,
				hammer2_key_t *key_nextp,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int *errorp, int flags);
hammer2_chain_t *hammer2_chain_next(hammer2_chain_t **parentp,
				hammer2_chain_t *chain,
				hammer2_key_t *key_nextp,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int *errorp, int flags);
int hammer2_chain_scan(hammer2_chain_t *parent,
				hammer2_chain_t **chainp,
				hammer2_blockref_t *bref,
				int *firstp, int flags);

int hammer2_chain_create(hammer2_chain_t **parentp, hammer2_chain_t **chainp,
				hammer2_dev_t *hmp, hammer2_pfs_t *pmp,
				int methods, hammer2_key_t key, int keybits,
				int type, size_t bytes, hammer2_tid_t mtid,
				hammer2_off_t dedup_off, int flags);
void hammer2_chain_rename(hammer2_chain_t **parentp,
				hammer2_chain_t *chain,
				hammer2_tid_t mtid, int flags);
int hammer2_chain_delete(hammer2_chain_t *parent, hammer2_chain_t *chain,
				hammer2_tid_t mtid, int flags);
int hammer2_chain_indirect_maintenance(hammer2_chain_t *parent,
				hammer2_chain_t *chain);
void hammer2_chain_setflush(hammer2_chain_t *chain);
void hammer2_chain_countbrefs(hammer2_chain_t *chain,
				hammer2_blockref_t *base, int count);
hammer2_chain_t *hammer2_chain_bulksnap(hammer2_dev_t *hmp);
void hammer2_chain_bulkdrop(hammer2_chain_t *copy);

void hammer2_chain_setcheck(hammer2_chain_t *chain, void *bdata);
int hammer2_chain_testcheck(hammer2_chain_t *chain, void *bdata);
int hammer2_chain_dirent_test(hammer2_chain_t *chain, const char *name,
				size_t name_len);

void hammer2_base_delete(hammer2_chain_t *parent,
				hammer2_blockref_t *base, int count,
				hammer2_chain_t *chain,
				hammer2_blockref_t *obref);
void hammer2_base_insert(hammer2_chain_t *parent,
				hammer2_blockref_t *base, int count,
				hammer2_chain_t *chain,
				hammer2_blockref_t *elm);

/*
 * hammer2_flush.c
 */
void hammer2_trans_manage_init(hammer2_pfs_t *pmp);
int hammer2_flush(hammer2_chain_t *chain, int istop);
void hammer2_trans_init(hammer2_pfs_t *pmp, uint32_t flags);
void hammer2_trans_setflags(hammer2_pfs_t *pmp, uint32_t flags);
void hammer2_trans_clearflags(hammer2_pfs_t *pmp, uint32_t flags);
hammer2_tid_t hammer2_trans_sub(hammer2_pfs_t *pmp);
void hammer2_trans_done(hammer2_pfs_t *pmp, uint32_t flags);
hammer2_tid_t hammer2_trans_newinum(hammer2_pfs_t *pmp);
void hammer2_trans_assert_strategy(hammer2_pfs_t *pmp);

/*
 * hammer2_ioctl.c
 */
int hammer2_ioctl(hammer2_inode_t *ip, u_long com, void *data,
				int fflag, struct ucred *cred);

/*
 * hammer2_io.c
 */
void hammer2_io_inval(hammer2_io_t *dio, hammer2_off_t data_off, u_int bytes);
void hammer2_io_cleanup(hammer2_dev_t *hmp, struct hammer2_io_tree *tree);
char *hammer2_io_data(hammer2_io_t *dio, off_t lbase);
void hammer2_io_bkvasync(hammer2_io_t *dio);
void hammer2_io_dedup_set(hammer2_dev_t *hmp, hammer2_blockref_t *bref);
void hammer2_io_dedup_delete(hammer2_dev_t *hmp, uint8_t btype,
				hammer2_off_t data_off, u_int bytes);
void hammer2_io_dedup_assert(hammer2_dev_t *hmp, hammer2_off_t data_off,
				u_int bytes);
int hammer2_io_new(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
				hammer2_io_t **diop);
int hammer2_io_newnz(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
				hammer2_io_t **diop);
int _hammer2_io_bread(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
				hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS);
void hammer2_io_setdirty(hammer2_io_t *dio);

hammer2_io_t *_hammer2_io_getblk(hammer2_dev_t *hmp, int btype, off_t lbase,
				int lsize, int op HAMMER2_IO_DEBUG_ARGS);
hammer2_io_t *_hammer2_io_getquick(hammer2_dev_t *hmp, off_t lbase,
				int lsize HAMMER2_IO_DEBUG_ARGS);
void _hammer2_io_putblk(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS);
int _hammer2_io_bwrite(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS);
void _hammer2_io_bawrite(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS);
void _hammer2_io_bdwrite(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS);
void _hammer2_io_brelse(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS);
void _hammer2_io_bqrelse(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS);
void _hammer2_io_ref(hammer2_io_t *dio HAMMER2_IO_DEBUG_ARGS);

#ifndef HAMMER2_IO_DEBUG

#define hammer2_io_getblk(hmp, btype, lbase, lsize, op)			\
	_hammer2_io_getblk((hmp), (btype), (lbase), (lsize), (op))
#define hammer2_io_getquick(hmp, lbase, lsize)				\
	_hammer2_io_getquick((hmp), (lbase), (lsize))
#define hammer2_io_putblk(diop)						\
	_hammer2_io_putblk(diop)
#define hammer2_io_bwrite(diop)						\
	_hammer2_io_bwrite((diop))
#define hammer2_io_bawrite(diop)					\
	_hammer2_io_bawrite((diop))
#define hammer2_io_bdwrite(diop)					\
	_hammer2_io_bdwrite((diop))
#define hammer2_io_brelse(diop)						\
	_hammer2_io_brelse((diop))
#define hammer2_io_bqrelse(diop)					\
	_hammer2_io_bqrelse((diop))
#define hammer2_io_ref(dio)						\
	_hammer2_io_ref((dio))

#define hammer2_io_bread(hmp, btype, lbase, lsize, diop)		\
	_hammer2_io_bread((hmp), (btype), (lbase), (lsize), (diop))

#else

#define hammer2_io_getblk(hmp, btype, lbase, lsize, op)			\
	_hammer2_io_getblk((hmp), (btype), (lbase), (lsize), (op),	\
	__FILE__, __LINE__)

#define hammer2_io_getquick(hmp, lbase, lsize)				\
	_hammer2_io_getquick((hmp), (lbase), (lsize), __FILE__, __LINE__)

#define hammer2_io_putblk(diop)						\
	_hammer2_io_putblk(diop, __FILE__, __LINE__)

#define hammer2_io_bwrite(diop)						\
	_hammer2_io_bwrite((diop), __FILE__, __LINE__)
#define hammer2_io_bawrite(diop)					\
	_hammer2_io_bawrite((diop), __FILE__, __LINE__)
#define hammer2_io_bdwrite(diop)					\
	_hammer2_io_bdwrite((diop), __FILE__, __LINE__)
#define hammer2_io_brelse(diop)						\
	_hammer2_io_brelse((diop), __FILE__, __LINE__)
#define hammer2_io_bqrelse(diop)					\
	_hammer2_io_bqrelse((diop), __FILE__, __LINE__)
#define hammer2_io_ref(dio)						\
	_hammer2_io_ref((dio), __FILE__, __LINE__)

#define hammer2_io_bread(hmp, btype, lbase, lsize, diop)		\
	_hammer2_io_bread((hmp), (btype), (lbase), (lsize), (diop),	\
			  __FILE__, __LINE__)

#endif

/*
 * hammer2_admin.c
 */
void hammer2_thr_signal(hammer2_thread_t *thr, uint32_t flags);
void hammer2_thr_signal2(hammer2_thread_t *thr,
			uint32_t pflags, uint32_t nflags);
void hammer2_thr_wait(hammer2_thread_t *thr, uint32_t flags);
void hammer2_thr_wait_neg(hammer2_thread_t *thr, uint32_t flags);
int hammer2_thr_wait_any(hammer2_thread_t *thr, uint32_t flags, int timo);
void hammer2_thr_create(hammer2_thread_t *thr,
			hammer2_pfs_t *pmp, hammer2_dev_t *hmp,
			const char *id, int clindex, int repidx,
			void (*func)(void *arg));
void hammer2_thr_delete(hammer2_thread_t *thr);
void hammer2_thr_remaster(hammer2_thread_t *thr);
void hammer2_thr_freeze_async(hammer2_thread_t *thr);
void hammer2_thr_freeze(hammer2_thread_t *thr);
void hammer2_thr_unfreeze(hammer2_thread_t *thr);
int hammer2_thr_break(hammer2_thread_t *thr);
void hammer2_primary_xops_thread(void *arg);

/*
 * hammer2_thread.c (XOP API)
 */
void *hammer2_xop_alloc(hammer2_inode_t *ip, int flags);
void hammer2_xop_setname(hammer2_xop_head_t *xop,
				const char *name, size_t name_len);
void hammer2_xop_setname2(hammer2_xop_head_t *xop,
				const char *name, size_t name_len);
size_t hammer2_xop_setname_inum(hammer2_xop_head_t *xop, hammer2_key_t inum);
void hammer2_xop_setip2(hammer2_xop_head_t *xop, hammer2_inode_t *ip2);
void hammer2_xop_setip3(hammer2_xop_head_t *xop, hammer2_inode_t *ip3);
void hammer2_xop_reinit(hammer2_xop_head_t *xop);
void hammer2_xop_helper_create(hammer2_pfs_t *pmp);
void hammer2_xop_helper_cleanup(hammer2_pfs_t *pmp);
void hammer2_xop_start(hammer2_xop_head_t *xop, hammer2_xop_desc_t *desc);
void hammer2_xop_start_except(hammer2_xop_head_t *xop, hammer2_xop_desc_t *desc,
				int notidx);
int hammer2_xop_collect(hammer2_xop_head_t *xop, int flags);
void hammer2_xop_retire(hammer2_xop_head_t *xop, uint64_t mask);
int hammer2_xop_active(hammer2_xop_head_t *xop);
int hammer2_xop_feed(hammer2_xop_head_t *xop, hammer2_chain_t *chain,
				int clindex, int error);

/*
 * hammer2_synchro.c
 */
void hammer2_primary_sync_thread(void *arg);

/*
 * XOP backends in hammer2_xops.c, primarily for VNOPS.  Other XOP backends
 * may be integrated into other source files.
 */
void hammer2_xop_ipcluster(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_readdir(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_nresolve(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_unlink(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_nrename(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_scanlhc(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_scanall(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_lookup(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_delete(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_inode_mkdirent(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_inode_create(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_inode_create_det(hammer2_xop_t *xop,
				void *scratch, int clindex);
void hammer2_xop_inode_create_ins(hammer2_xop_t *xop,
				void *scratch, int clindex);
void hammer2_xop_inode_destroy(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_inode_chain_sync(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_xop_inode_unlinkall(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_xop_inode_connect(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_inode_flush(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_strategy_read(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_xop_strategy_write(hammer2_xop_t *xop, void *scratch, int clindex);

void hammer2_dmsg_ipcluster(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_readdir(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_nresolve(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_unlink(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_nrename(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_scanlhc(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_scanall(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_lookup(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_inode_mkdirent(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_dmsg_inode_create(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_inode_destroy(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_inode_chain_sync(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_dmsg_inode_unlinkall(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_dmsg_inode_connect(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_inode_flush(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_strategy_read(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_dmsg_strategy_write(hammer2_xop_t *xop, void *scratch,
				int clindex);

void hammer2_rmsg_ipcluster(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_readdir(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_nresolve(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_unlink(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_nrename(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_scanlhc(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_scanall(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_lookup(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_inode_mkdirent(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_rmsg_inode_create(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_inode_destroy(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_inode_chain_sync(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_rmsg_inode_unlinkall(hammer2_xop_t *xop, void *scratch,
				int clindex);
void hammer2_rmsg_inode_connect(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_inode_flush(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_strategy_read(hammer2_xop_t *xop, void *scratch, int clindex);
void hammer2_rmsg_strategy_write(hammer2_xop_t *xop, void *scratch,
				int clindex);

extern hammer2_xop_desc_t hammer2_ipcluster_desc;
extern hammer2_xop_desc_t hammer2_readdir_desc;
extern hammer2_xop_desc_t hammer2_nresolve_desc;
extern hammer2_xop_desc_t hammer2_unlink_desc;
extern hammer2_xop_desc_t hammer2_nrename_desc;
extern hammer2_xop_desc_t hammer2_scanlhc_desc;
extern hammer2_xop_desc_t hammer2_scanall_desc;
extern hammer2_xop_desc_t hammer2_lookup_desc;
extern hammer2_xop_desc_t hammer2_delete_desc;
extern hammer2_xop_desc_t hammer2_inode_mkdirent_desc;
extern hammer2_xop_desc_t hammer2_inode_create_desc;
extern hammer2_xop_desc_t hammer2_inode_create_det_desc;
extern hammer2_xop_desc_t hammer2_inode_create_ins_desc;
extern hammer2_xop_desc_t hammer2_inode_destroy_desc;
extern hammer2_xop_desc_t hammer2_inode_chain_sync_desc;
extern hammer2_xop_desc_t hammer2_inode_unlinkall_desc;
extern hammer2_xop_desc_t hammer2_inode_connect_desc;
extern hammer2_xop_desc_t hammer2_inode_flush_desc;
extern hammer2_xop_desc_t hammer2_strategy_read_desc;
extern hammer2_xop_desc_t hammer2_strategy_write_desc;

/*
 * hammer2_msgops.c
 */
int hammer2_msg_dbg_rcvmsg(kdmsg_msg_t *msg);
int hammer2_msg_adhoc_input(kdmsg_msg_t *msg);

/*
 * hammer2_vfsops.c
 */
void hammer2_dump_chain(hammer2_chain_t *chain, int tab, int *countp, char pfx,
				u_int flags);
int hammer2_vfs_sync(struct mount *mp, int waitflags);
int hammer2_vfs_sync_pmp(hammer2_pfs_t *pmp, int waitfor);
int hammer2_vfs_enospace(hammer2_inode_t *ip, off_t bytes, struct ucred *cred);

hammer2_pfs_t *hammer2_pfsalloc(hammer2_chain_t *chain,
				const hammer2_inode_data_t *ripdata,
				hammer2_tid_t modify_tid,
				hammer2_dev_t *force_local);
void hammer2_pfsdealloc(hammer2_pfs_t *pmp, int clindex, int destroying);
int hammer2_vfs_vget(struct mount *mp, struct vnode *dvp,
				ino_t ino, struct vnode **vpp);

void hammer2_lwinprog_ref(hammer2_pfs_t *pmp);
void hammer2_lwinprog_drop(hammer2_pfs_t *pmp);
void hammer2_lwinprog_wait(hammer2_pfs_t *pmp, int pipe);

void hammer2_pfs_memory_wait(hammer2_pfs_t *pmp);
void hammer2_pfs_memory_inc(hammer2_pfs_t *pmp);
void hammer2_pfs_memory_wakeup(hammer2_pfs_t *pmp, int count);

/*
 * hammer2_freemap.c
 */
int hammer2_freemap_alloc(hammer2_chain_t *chain, size_t bytes);
void hammer2_freemap_adjust(hammer2_dev_t *hmp,
				hammer2_blockref_t *bref, int how);

/*
 * hammer2_cluster.c
 */
uint8_t hammer2_cluster_type(hammer2_cluster_t *cluster);
void hammer2_cluster_bref(hammer2_cluster_t *cluster, hammer2_blockref_t *bref);
void hammer2_cluster_ref(hammer2_cluster_t *cluster);
void hammer2_cluster_drop(hammer2_cluster_t *cluster);
void hammer2_cluster_unhold(hammer2_cluster_t *cluster);
void hammer2_cluster_rehold(hammer2_cluster_t *cluster);
void hammer2_cluster_lock(hammer2_cluster_t *cluster, int how);
int hammer2_cluster_check(hammer2_cluster_t *cluster, hammer2_key_t lokey,
			int flags);
void hammer2_cluster_resolve(hammer2_cluster_t *cluster);
void hammer2_cluster_forcegood(hammer2_cluster_t *cluster);
void hammer2_cluster_unlock(hammer2_cluster_t *cluster);

void hammer2_bulkfree_init(hammer2_dev_t *hmp);
void hammer2_bulkfree_uninit(hammer2_dev_t *hmp);
int hammer2_bulkfree_pass(hammer2_dev_t *hmp, hammer2_chain_t *vchain,
			struct hammer2_ioc_bulkfree *bfi);
void hammer2_dummy_xop_from_chain(hammer2_xop_head_t *xop,
			hammer2_chain_t *chain);

/*
 * hammer2_iocom.c
 */
void hammer2_iocom_init(hammer2_dev_t *hmp);
void hammer2_iocom_uninit(hammer2_dev_t *hmp);
void hammer2_cluster_reconnect(hammer2_dev_t *hmp, struct file *fp);
void hammer2_volconf_update(hammer2_dev_t *hmp, int index);

/*
 * hammer2_strategy.c
 */
int hammer2_vop_strategy(struct vop_strategy_args *ap);
int hammer2_vop_bmap(struct vop_bmap_args *ap);
void hammer2_bioq_sync(hammer2_pfs_t *pmp);
void hammer2_dedup_record(hammer2_chain_t *chain, hammer2_io_t *dio,
				const char *data);
void hammer2_dedup_clear(hammer2_dev_t *hmp);

/*
 * More complex inlines
 */

#define hammer2_xop_gdata(xop)	_hammer2_xop_gdata((xop), __FILE__, __LINE__)

static __inline
const hammer2_media_data_t *
_hammer2_xop_gdata(hammer2_xop_head_t *xop, const char *file, int line)
{
	hammer2_chain_t *focus;
	const void *data;

	focus = xop->cluster.focus;
	if (focus->dio) {
		lockmgr(&focus->diolk, LK_SHARED);
		if ((xop->focus_dio = focus->dio) != NULL) {
			_hammer2_io_ref(xop->focus_dio HAMMER2_IO_DEBUG_CALL);
			hammer2_io_bkvasync(xop->focus_dio);
		}
		data = focus->data;
		lockmgr(&focus->diolk, LK_RELEASE);
	} else {
		data = focus->data;
	}

	return data;
}

#define hammer2_xop_pdata(xop)	_hammer2_xop_pdata((xop), __FILE__, __LINE__)

static __inline
void
_hammer2_xop_pdata(hammer2_xop_head_t *xop, const char *file, int line)
{
	if (xop->focus_dio)
		_hammer2_io_putblk(&xop->focus_dio HAMMER2_IO_DEBUG_CALL);
}

#endif /* !_KERNEL */
#endif /* !_VFS_HAMMER2_HAMMER2_H_ */
