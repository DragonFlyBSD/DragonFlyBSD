/*
 * Copyright (c) 2006,2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * This module is HAMMER2-independent.
 *
 * CCMS - Cache Coherency Management System.  These structures are used
 * to manage cache coherency and locking for an object.
 *
 *				ccms_inode
 *
 * Cache coherency is tied into a kernel or VFS structure, creating a
 * directory/file topology and a keyspace on an inode-by-inode basis
 * via the (ccms_inode) structure.
 *
 * Each CCMS inode contains a RB-Tree holding ccms_cst (CST) elements
 * for its file range or directory key range, plus two independent embedded
 * ccms_cst structures representing the inode attributes and the entire
 * recursive sub-tree.
 *
 * The CST representing the entire sub-tree is inclusive of that inode's
 * attribute state and data/key range state AND inclusive of the entire
 * filesystem topology under that point, recursively.
 *
 * Two ccms_cst's are embedded in each cached inode via the ccms_inode
 * structure to represent attribute and recursive topological cache state.
 *
 *				 ccms_cst
 *
 * The (ccms_cst) structure, called the CST, represents specific, persistent
 * cache state.  This structure is allocated and freed on the fly as needed
 * (except for the two embedded in the ccms_inode).
 *
 * The persistence ties into network/cluster operations via the 'rstate'
 * field.  When cluster-maintained state is present then certain operations
 * on the CST's local state (including when a vnode is reclaimed) will
 * block while third-party synchronization occurs.
 *
 * The number of dynamically allocated CSTs is strictly limited, forcing
 * a degree of aggregation when the limit is reached.
 *
 *				 ccms_lock
 *
 * The (ccms_lock) structure represents a live local lock for the duration of
 * any given filesystem operation.  A single ccms_lock can cover both
 * attribute state AND a byte-range/key-range.
 *
 * This lock represents the exact lock being requested but the CST structure
 * it points to can be a more general representation which covers the lock.
 * The minimum granularity for the cst pointer in the ccms_lock will be to
 * the ccms_inode's embedded topo_cst.
 *
 * Theoretically a single CST at the root can cover the entire filesystem,
 * but this creates a great deal of SMP interaction.
 *
 *				   Management
 *
 * Because cache state is persistent the CCMS module may desire to limit the
 * total number of CSTs under management.  It does this by aggregating cache
 * state which in turn may require callbacks to invalidate third-party
 * (cluster-related) cache state.
 *
 * CCMS operations related to locks can stall on third-party state
 * transitions.  Because third-party state can also change independently
 * due to foreign interactions (often with a userland program), no filesystem
 * lock can be held while manipulating CST states.  For this reason,
 * HAMMER2 (or any VFS using CCMS) must provide roll-up functions to acquire
 * CCMS lock state up-front prior to locking the VFS inode structure.
 *
 * vnode locks which are under the control of the filesystem can be more
 * problematic and may require additional care.
 */

#ifndef _SYS_CCMS_H_
#define _SYS_CCMS_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_SERIALIZE_H_
#include <sys/serialize.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif

typedef uint64_t	ccms_off_t;
typedef uint8_t		ccms_state_t;

/*
 * CCMS uses a red-black tree to organize CSTs.
 */
RB_HEAD(ccms_rb_tree, ccms_cst);
RB_PROTOTYPE3(ccms_rb_tree, ccms_cst, rbnode, ccms_cst_cmp, ccms_off_t);

struct ccms_inode;
struct ccms_cst;
struct ccms_lock;

/*
 * CCMS cache states
 *
 * CCMS uses an extended MESI caching model.  There are two extension states,
 * MASTER and SLAVE, which represents dirty data which has not been
 * synchronized to backing store but which nevertheless is being shared
 * between distinct caches.   These states are designed to allow data
 * to be shared between nodes in a cluster without having to wait for it
 * to be synchronized with its backing store.
 *
 * Each CST has lstate and rstate.  lstate is the local cache state and rstate
 * is the remotely-granted state.  Changes to the lstate require a compatible
 * rstate.  If the rstate is not compatible a third-party transaction is
 * required to obtain the proper rstate.
 *
 * INVALID   -	Cache state is unknown and must be acquired.
 *
 * ALLOWED   -  (topo_cst.rstate only).  This is a granted state which
 *		allows cache state transactions underneath the current
 *		node (data, attribute, and recursively), but is not a proper
 *		grant for topo_cst itself.  Someone specifically trying to
 *		acquire topo_cst still needs to do a third party transaction
 *		to get the cache into the proper state.
 *
 * SHARED    -  Indicates that the information is clean, shared, read-only.
 *
 * SLAVE     -  Indicates that the information is clean, shared, read-only.
 *		Indicates that local backing store is out of date but the
 *		in-memory cache is valid, meaning that we can only obtain
 *		the data from the MASTER (somewhere in the cluster), and
 *		that we may not be allowed to sync it to local backing
 *		store yet e.g. due to the quorum protocol not having
 *		completed.
 *
 * MASTER    -  Indicates that the information is dirty, but readonly
 *		because other nodes in the cluster are in a SLAVE state.
 *		This state is typically transitional and occurs while
 *		a quorum operation is in progress, allowing slaves to
 *		access the data without stalling.
 *
 * EXCLUSIVE -	Indicates that the information is clean, read-only, and
 *		that nobody else can access the data while we are in this
 *		state.  A local node can upgrade both rstate and lstate
 *		from EXCLUSIVE to MODIFIED without having to perform a
 *		third-party transaction.
 *
 * MODIFIED  -  Indicates that the information is dirty, read-write, and
 *		that nobody else can access the data while we are in this
 *		state.
 *
 * It is important to note that remote cache-state grants can be more
 * general than what was requested, plus they can be persistent.  So,
 * for example, a remote can grant EXCLUSIVE access even if you just
 * requested SHARED, which saves you from having to do another network
 * transaction if you later need EXCLUSIVE.
 */

#define CCMS_STATE_INVALID	0	/* unknown cache state */
#define CCMS_STATE_ALLOWED	1	/* allow subsystem (topo only) */
#define CCMS_STATE_SHARED	2	/* clean, shared, read-only */
#define CCMS_STATE_SLAVE	3	/* live only, shared, read-only */
#define CCMS_STATE_MASTER	4	/* dirty, shared, read-only */
#define CCMS_STATE_EXCLUSIVE	5	/* clean, exclusive, read-only */
#define CCMS_STATE_MODIFIED	6	/* dirty, exclusive, read-write */

/*
 * A CCMS locking element - represents a high level locking request,
 * such as used by read, write, and attribute operations.  Initialize
 * the ccms_lock structure and call ccms_lock_get().
 *
 * When a CCMS lock is established the cache state of the underlying elements
 * is adjusted to meet the requirements of the lock.  The cache state
 * requirements are infered by the lock type.  CCMS locks can block on
 * third party interactions if the underlying remote cache state is not
 * compatible.
 *
 * CCMS data locks imply a shared CCMS inode lock.  A CCMS topology lock does
 * not imply a data or inode lock but topology locks can have far-reaching
 * effects and block on numerous CST state.
 */
struct ccms_lock {
	ccms_state_t	tstate;
	ccms_state_t	astate;
	ccms_state_t	dstate;
	ccms_off_t	beg_offset;	/* applies to dstate */
	ccms_off_t	end_offset;	/* applies to dstate */
	struct ccms_cst *icst;		/* points to topo_cst or attr_cst */
	struct ccms_cst *dcst;		/* points to left edge in rbtree */
#ifdef CCMS_DEBUG
	TAILQ_ENTRY(ccms_lock) entry;
#endif
};

#ifdef CCMS_DEBUG

TAILQ_HEAD(ccms_lock_head, ccms_lock);

#endif

/*
 * CCMS cache state tree element (CST) - represents the actual cache
 * management state for a data space.  The cache state tree is a
 * non-overlaping red-black tree containing ranged ccms_cst structures
 * which reflect the resolved state for all current high level locking
 * requests.  For example, two overlapping ccms_lock requests for shared
 * access would typically be represented by three non-overlapping ccms_cst
 * items in the CST.  The CST item representing the overlapped portion of
 * the ccms_lock requests would have ref count of 2 while the other CST
 * items would have a ref count of 1.
 *
 *	[lock request #01]
 *	         [lock request #02]
 *	[--cst--][--cst--][--cst--]
 *
 * CSTs are partitioned so their edges line up to all current and pending
 * ccms_lock requests.  CSTs are re-merged whenever possible.  A freshly
 * initialized database typically has a single CST representing the default
 * cache state for the host.
 *
 * A CST keeps track of local cache state (lstate) AND remote cache state
 * (rstate).
 *
 * Any arbitrary data range within a dataspace can be locked shared or
 * exclusive.  Obtaining a lock has the side effect of potentially modifying
 * the cache state.  A positive sharecount in a CST indicates that a
 * shared access lock is being held.  A negative sharecount indicates an
 * exclusive access lock is being held on the range.  A MODIFYING lock
 * type is just an exclusive lock but one which effects the cache state
 * differently.
 *
 * The end offset is byte-inclusive, allowing the entire 64 bit data space
 * to be represented without overflowing the edge case.  For example, a
 * 64 byte area might be represented as (0,63).  The offsets are UNSIGNED
 * entities.
 */
struct ccms_cst {
	RB_ENTRY(ccms_cst) rbnode;	/* stored in a red-black tree */
	struct ccms_cst *free_next;	/* free cache linked list */
	struct ccms_inode *cino;	/* related ccms_inode */
	ccms_off_t beg_offset;		/* range (inclusive) */
	ccms_off_t end_offset;		/* range (inclusive) */
	ccms_state_t lstate;		/* local cache state */
	ccms_state_t rstate;		/* cache state granted by protocol */

	int32_t flags;
	int32_t	count;			/* shared/exclusive count */
	int32_t	blocked;		/* indicates a blocked lock request */
	int32_t	xrefs;			/* lock overlap references */
	int32_t	lrefs;			/* left edge refs */
	int32_t	rrefs;			/* right edge refs */
#ifdef CCMS_DEBUG
	struct ccms_lock_head list;
#endif
};

#define CCMS_CST_DYNAMIC	0x00000001
#define CCMS_CST_DELETING	0x00000002
#define CCMS_CST_INSERTED	0x00000004
#define CCMS_CST_INHERITED	0x00000008	/* rstate inherited from par */

/*
 * A CCMS inode is typically embedded in a VFS file or directory object.
 *
 * The subdirectory topology is accessible downward by indexing topo_cst's
 * from the children in the parent's cst_tree.
 *
 * attr_cst is independent of data-range CSTs.  However, adjustments to
 * the topo_cst can have far-reaching effects to attr_cst, the CSTs in
 * the tree, recursively both downward and upward.
 */
struct ccms_inode {
	struct spinlock		spin;
	struct ccms_inode	*parent;
	struct ccms_rb_tree	tree;
	struct ccms_cst		attr_cst;
	struct ccms_cst		topo_cst;
	struct ccms_cst		*free_cache;	/* cst free cache */
	struct ccms_domain	*domain;
	void			*handle;	/* VFS opaque */
	int32_t			flags;
};

#define CCMS_INODE_INSERTED	0x0001
#define CCMS_INODE_DELETING	0x0002

/*
 * Domain management, contains a pseudo-root for the CCMS topology.
 */
struct ccms_domain {
	struct malloc_type	*mcst;		/* malloc space for cst's */
	struct ccms_inode	root;		/* dummy protocol root */
	int			cst_count;	/* dynamic cst count */
	int			cst_limit;	/* dynamic cst limit */
};

typedef struct ccms_lock	ccms_lock_t;
typedef struct ccms_cst		ccms_cst_t;
typedef struct ccms_inode	ccms_inode_t;
typedef struct ccms_domain	ccms_domain_t;

/*
 * Kernel API
 */
#ifdef _KERNEL

/*
 * Helper inline to initialize primarily a dstate lock which shortcuts
 * the more common locking operations.  A dstate is specified and an
 * astate is implied.  tstate locks cannot be acquired with this inline.
 */
static __inline
void
ccms_lock_init(ccms_lock_t *lock, ccms_state_t dstate,
	       ccms_off_t beg_offset, ccms_off_t end_offset)
{
	lock->beg_offset = beg_offset;
	lock->end_offset = end_offset;
	lock->tstate = 0;
	lock->astate = 0;
	lock->dstate = dstate;
}

void ccms_domain_init(ccms_domain_t *dom);
void ccms_inode_init(ccms_domain_t *dom, ccms_inode_t *cino, void *handle);
void ccms_inode_insert(ccms_inode_t *cpar, ccms_inode_t *cino);
void ccms_inode_delete(ccms_inode_t *cino);
void ccms_inode_uninit(ccms_inode_t *cino);

int ccms_lock_get(ccms_inode_t *cino, ccms_lock_t *lock);
int ccms_lock_get_uio(ccms_inode_t *cino, ccms_lock_t *lock, struct uio *uio);
int ccms_lock_get_attr(ccms_inode_t *cino, ccms_lock_t *lock, ccms_state_t st);
int ccms_lock_put(ccms_inode_t *cino, ccms_lock_t *lock);

#endif

#endif
