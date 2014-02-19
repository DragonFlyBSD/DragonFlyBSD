/*
 * Copyright (c) 2006,2014 The DragonFly Project.  All rights reserved.
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
 * CCMS - Cache Coherency Management System.
 *
 * This subsystem can be tied into a VFS in order to supply persistent
 * cache management state for cluster or for remote cache-coherent operations.
 *
 * Local and cluster/remote cache state is maintained in a cache-coherent
 * fashion as well as integrated into the VFS's inode locking subsystem
 * (as a means of avoiding deadlocks).
 *
 * To operate properly the VFS must maintain a complete directory topology
 * leading to any given vnode/inode either open or cached by the system.
 * The vnode/namecache subsystem does not have to implement this but the
 * VFS (aka HAMMER2) does.
 *
 * The filesystem embeds CCMS_CST structures in its internal inode
 * representatino as needed and implements callback to allow CCMS to
 * do topological recursions.
 *
 * --
 *
 * The CCMS_CST structures represent granted cache and local locking states.
 * Grants can be recursively inherited, minimizing protocol overhead in
 * situations where there are no conflicts of interest.
 *
 * --
 *
 * CCMS supports active front-end 'locks' on data objects utilizing the
 * ccms_inode, key, and desired cache state.  It can grant the lock based
 * on inherited CST state and prevents downgrading of the CST by other
 * parties or threads while the lock is held.  The CST's arranged
 * lock within the embedded CCMS_INODE and ref-counts the related CST.
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

typedef uint64_t	ccms_key_t;
typedef uint64_t	ccms_tid_t;
typedef uint8_t		ccms_state_t;
typedef uint8_t		ccms_type_t;

struct ccms_cst;
struct ccms_lock;

/*
 * CCMS_STATE_T - CCMS cache states.
 *
 * INVALID   -	Cache state is unknown and must be acquired.
 *
 * ALLOWED   -  Cache state allows any recursive state to be acquired.
 *
 * SHARED    -	Cache state allows shared access.  If this is a topo_cst
 *		only INVALID or SHARED recursive states are allowed.
 *
 * EXCLUSIVE -  Cache state allows exclusive access.  If this is a
 *		topo_cst then INVALID, SHARED, or EXCLUSIVE recursive
 *		state is allowed.
 *
 * CCMS Implements an extended MESI model.  The extensions are implemented
 * as CCMS_TYPE_T flags.
 */
#define CCMS_STATE_INVALID	0	/* unknown cache state */
#define CCMS_STATE_ALLOWED	1	/* allow subsystem (topo only) */
#define CCMS_STATE_SHARED	2	/* clean, shared, read-only */
#define CCMS_STATE_EXCLUSIVE	3	/* clean, exclusive, read-only */

/*
 * CCMS_TYPE_T FLAGS
 *
 * INHERITED -  Indicates the state field was inherited and was not directly
 *		granted by the cluster controller.
 *
 * MODIFIED  -  This is a type-field flag associated with an EXCLUSIVE cache
 *		state
 *
 * MASTER    -  This is a type-field flag associated with an EXCLUSIVE+MODIFIED
 *		cache state which indicates that slaves might be present
 *		which are caching our unsynchronized state.
 *
 * SLAVE     -  This is a type-field flag associated with the SHARED cache
 *		state which indicates that the data present in our memory
 *		caches is being mastered elsewhere and has not been
 *		synchronized (meaning no quorum protocol has been run to
 *		sync the data yet).  Thus only the version of the data in
 *		our memory and its originator is valid.
 *
 * QSLAVE    -  This indicates that the slaved data is also present in the
 *		memory caches of a quorum of master nodes.
 */
#define CCMS_TYPE_INHERITED	0x01
#define CCMS_TYPE_MODIFIED	0x02
#define CCMS_TYPE_MASTER	0x04
#define CCMS_TYPE_SLAVE		0x08
#define CCMS_TYPE_QSALVE	0x10
#define CCMS_TYPE_RECURSIVE	0x80

/*
 * CCMS_LOCK - High level active lock
 *
 * This represents a high level locking request, such as used by
 * read, write, and attribute operations.  Initialize the ccms_lock
 * structure and call ccms_lock_get().
 *
 * When a CCMS lock is established the cache state of the underlying elements
 * is adjusted to meet the requirements of the lock.  The cache state
 * requirements are infered by the lock type.  CCMS locks can block on
 * third party interactions if the underlying remote cache state is not
 * compatible.
 *
 * CCMS data locks imply a shared CCMS inode lock.  A CCMS topology lock does
 * not imply a data or inode lock but topology locks can have far-reaching
 * effects such as block ccms_locks on multiple inodes.
 */
struct ccms_lock {
	TAILQ_ENTRY(ccms_lock) entry;
	ccms_state_t	req_t;
	ccms_state_t	req_a;
	ccms_state_t	req_d;
	uint8_t		flags;
	struct ccms_cst	*topo_cst;
	struct ccms_cst	*attr_cst;
	struct ccms_cst	*data_cst;
	ccms_key_t	key_beg;	/* applies to dstate */
	ccms_key_t	key_end;	/* applies to dstate */
};

#define CCMS_LOCK_FAILED	0x01

/*
 * CCMS_CST - Low level locking state, persistent cache state
 *
 * Offset ranges are byte-inclusive, allowing the entire 64 bit data space
 * to be represented without overflowing the edge case.  For example, a
 * 64 byte area might be represented as (0,63).  The offsets are UNSIGNED
 * entities.
 *
 * High level CST locks must be obtained top-down.
 *
 * count - Negative value indicates active exclusive lock, positive value
 *	   indicates active shared lock.
 *
 * spin  - Structural spinlock, typically just one is held at a time.
 *	   However, to complement the top-down nature of the higher level
 *	   lock we allow the spin lock to be held recursively in a bottom-up
 *	   fashion for race-to-root flags updates and lastdrop iterations.
 */
struct ccms_cst {
	struct spinlock spin;		/* thread spinlock */
	void		*handle;	/* opaque VFS handle */
	ccms_state_t	state;		/* granted or inherited state */
	ccms_type_t	type;		/* CST type and flags */
	uint8_t		unused02;
	uint8_t		unused03;

	ccms_tid_t	path_id;	/* rendezvous inode id */
	ccms_tid_t	tid;		/* [meta]data versioning id */
	ccms_key_t	key_beg;	/* key range (inclusive) */
	ccms_key_t	key_end;	/* key range (inclusive) */

	int32_t		upgrade;	/* upgrades pending */
	int32_t		count;		/* active shared/exclusive count */
	int32_t		blocked;	/* wakeup blocked on release */
	thread_t	td;		/* if excl lock (count < 0) */
};

/*
 * Domain management, contains a pseudo-root for the CCMS topology.
 */
struct ccms_domain {
	int			cst_count;	/* dynamic cst count */
	int			cst_limit;	/* dynamic cst limit */
};

typedef struct ccms_lock	ccms_lock_t;
typedef struct ccms_cst		ccms_cst_t;
typedef struct ccms_domain	ccms_domain_t;

/*
 * Kernel API
 */
#ifdef _KERNEL

void ccms_domain_init(ccms_domain_t *dom);
void ccms_domain_uninit(ccms_domain_t *dom);
void ccms_cst_init(ccms_cst_t *cst, void *handle);
void ccms_cst_uninit(ccms_cst_t *cst);

void ccms_thread_lock(ccms_cst_t *cst, ccms_state_t state);
int ccms_thread_lock_nonblock(ccms_cst_t *cst, ccms_state_t state);
ccms_state_t ccms_thread_lock_temp_release(ccms_cst_t *cst);
void ccms_thread_lock_temp_restore(ccms_cst_t *cst, ccms_state_t ostate);
ccms_state_t ccms_thread_lock_upgrade(ccms_cst_t *cst);
void ccms_thread_lock_downgrade(ccms_cst_t *cst, ccms_state_t ostate);
void ccms_thread_unlock(ccms_cst_t *cst);
void ccms_thread_unlock_upgraded(ccms_cst_t *cst, ccms_state_t ostate);
/*int ccms_thread_unlock_zero(ccms_cst_t *cst);*/
int ccms_thread_lock_owned(ccms_cst_t *cst);
void ccms_thread_lock_setown(ccms_cst_t *cst);

void ccms_lock_get(ccms_lock_t *lock);
void ccms_lock_put(ccms_lock_t *lock);

#endif

#endif
