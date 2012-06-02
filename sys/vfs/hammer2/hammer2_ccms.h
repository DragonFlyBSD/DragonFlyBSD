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
 * CCMS - Cache Coherency Management System.  These structures are used
 * to manage cache coherency and locking for an object.  Cache Coherency is
 * managed at byte granularity with 64 bit offset ranges.
 *
 * Management is broken into two distinct pieces: (1) Local shared/exclusive
 * locks which essentially replace traditional vnode locks and (2) local
 * cache state which interacts with other hosts and follows a MESI-like model.
 *
 * The core to the entire module is the 'CST' (Cache State Tree) structure
 * which stores both pieces of information in a red-black tree styled data
 * structure.  CSTs are non-overlapping offset-ranged entities.  Other
 * higher level structures govern how CSTs in the red-black tree or cut up
 * or merged.
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

/*
 * CCMS uses a red-black tree to sort CSTs.
 */
RB_HEAD(ccms_rb_tree, ccms_cst);
RB_PROTOTYPE3(ccms_rb_tree, ccms_cst, rbnode, ccms_cst_cmp, off_t);

struct ccms_lock;
struct ccms_cst;

/*
 * ccms_state_t - CCMS cache states
 *
 * CCMS uses an extended MESI caching model.  There are two extension
 * states, MASTER and SLAVE, which represents dirty data which has not been
 * synchronized to backing store but which nevertheless is being shared
 * between distinct caches.   These states are designed to allow data
 * to be shared between nodes in a cluster without having to wait for it
 * to be synchronized with its backing store.
 *
 * SLAVE     -	A shared state where the master copy of the data is being
 *		held by a foreign cache rather then by backing store.
 *		This state implies that the backing store may contain stale
 *		data.
 *
 * MASTER    -	A shared state where the master copy of the data is being
 *		held locally.  Zero or more foreign caches may be holding
 *		a copy of our data, so we cannot modify it without
 *		invalidating those caches.  This state implies that the
 *		backing store may contain stale data.
 *
 *		MASTER differs from MODIFIED in that the data is read-only
 *		due to the existance of foreign copies.  However, even though
 *		the data is read-only, it is ALSO DIRTY because the backing
 *		store has not been synchronized
 *
 * NOTE!  The cache state represents the worst case cache state for caching
 * elements such as the buffer cache or VM page cache or the vnode attribute
 * cache (or other things) within the specified range.  It does NOT mean
 * that the local machine actually has all of the requested data in-hand.
 */
typedef enum ccms_state {
    CCMS_STATE_INVALID = 0,
    CCMS_STATE_SHARED,		/* clean, read-only, from backing store */
    CCMS_STATE_SLAVE,		/* clean, read-only, from master */
    CCMS_STATE_MASTER,		/* dirty, read-only, shared master copy */
    CCMS_STATE_EXCLUSIVE,	/* clean, read-only, exclusive */
    CCMS_STATE_MODIFIED		/* clean or dirty, read-write, exclusive */
} ccms_state_t;

/*
 * ccms_ltype_t - local access control lock state
 *
 * Note: A MODIFYING lock is an exclusive lock where the caller intends to
 * make a modification, such as issuing a WRITE.  The difference between the
 * two is in how the cache state is effected by the lock.   The distinction
 * exists because there are many situations where the governing structure
 * on the local machine needs to be locked exclusively, but the underlying
 * data cache does not.
 *
 *	lock type	cache state
 *	---------	---------
 *	SHARED		>= shared
 *	EXCLUSIVE	>= shared
 *	MODIFYING	>= exclusive
 */
typedef enum ccms_ltype {
    CCMS_LTYPE_SHARED = 0,	/* shared lock on the range */
    CCMS_LTYPE_EXCLUSIVE,	/* exclusive lock on the range */
    CCMS_LTYPE_MODIFYING	/* modifying lock on the range */
} ccms_ltype_t;

/*
 * The CCMS ABI information structure.  This structure contains ABI
 * calls to resolve incompatible cache states.
 */
struct ccms_info {
    int	(*ccms_set_cache)(struct ccms_info *, struct ccms_lock *, ccms_state_t);
    void *data;
    /* XXX */
};

/*
 * A CCMS dataspace, typically stored in a vnode or VM object.   The primary
 * reference is to the ccms_dataspace representing the local machine.  The
 * chain field is used to link ccms_dataspace's representing other machines.
 * These foreign representations typically only contain summary 'worst-case'
 * CSTs.  The chain only needs to be followed if a CST has a cache state
 * that is incompatible with the request.
 */
struct ccms_dataspace {
    struct ccms_rb_tree	tree;
    struct ccms_info	*info;
    struct ccms_dataspace *chain;
    ccms_state_t	defstate;
    struct spinlock	spin;
    struct ccms_cst	*delayed_free;	/* delayed frees */
};

/*
 * The CCMS locking element - represents a high level locking request,
 * such as used by read, write, and truncate operations.  These requests
 * are not organized into any tree but instead are shadowed by items in
 * the actual cache state tree (ccms_cst).  There are no direct links
 * between a ccms_lock and the underlying CST items, only reference count
 * fields in the CST item.
 *
 * When a CCMS lock is established the cache state of the underlying elements
 * is adjusted to meet the requirements of the lock.  The cache state
 * requirements are infered by the lock type:
 *
 * NOTE: Ranges may include negative offsets.  These are typically used to
 * represent meta-data.
 *
 *		  local lock		cache state
 *		  -----------------	--------------------
 * SHARED	- SHARED		must not be invalid
 * EXCLUSIVE	- EXCLUSIVE		must not be invalid
 * MODIFYING	- EXCLUSIVE		must be EXCLUSIVE or MODIFIED
 */
struct ccms_lock {
	struct ccms_dataspace *ds;
	off_t	beg_offset;
	off_t	end_offset;
	ccms_ltype_t ltype;
};

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
 * A CST represents *TWO* different things.  First, it represents local
 * locks held on data ranges.  Second, it represents the best-case cache
 * state for data cached on the local machine for local<->remote host
 * interactions.
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
 * 64 byte area might be represented as (0,63).  The offsets are SIGNED
 * entities.  Negative offsets are often used to represent meta-data
 * such as ownership and permissions.  The file size is typically cached as a
 * side effect of file operations occuring at the file EOF rather then
 * combined with ownership and permissions.
 */
struct ccms_cst {
	RB_ENTRY(ccms_cst) rbnode;	/* stored in a red-black tree */
	struct	ccms_cst *delayed_next;	/* linked list to free */
	off_t	beg_offset;
	off_t	end_offset;
	ccms_state_t state;		/* local cache state */
	int	sharecount;		/* shared or exclusive lock count */
	int	modifycount;		/* number of modifying exclusive lks */
	int	blocked;		/* indicates a blocked lock request */
	int	xrefs;			/* lock overlap references */
	int	lrefs;			/* left edge refs */
	int	rrefs;			/* right edge refs */
};

typedef struct ccms_info *ccms_info_t;
typedef struct ccms_dataspace *ccms_dataspace_t;
typedef struct ccms_lock *ccms_lock_t;
typedef struct ccms_cst *ccms_cst_t;

/*
 * Kernel API
 */
#ifdef _KERNEL

static __inline
void
ccms_lock_init(ccms_lock_t lock, off_t beg_offset, off_t end_offset,
	       ccms_ltype_t ltype)
{
    lock->beg_offset = beg_offset;
    lock->end_offset = end_offset;
    lock->ltype = ltype;
}

void ccms_dataspace_init(ccms_dataspace_t);
void ccms_dataspace_destroy(ccms_dataspace_t);
int ccms_lock_get(ccms_dataspace_t, ccms_lock_t);
int ccms_lock_get_uio(ccms_dataspace_t, ccms_lock_t, struct uio *);
int ccms_lock_put(ccms_dataspace_t, ccms_lock_t);

#endif

#endif
