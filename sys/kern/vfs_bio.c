/*
 * Copyright (c) 1994,1997 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Absolutely no warranty of function or purpose is made by the author
 *		John S. Dyson.
 *
 * $FreeBSD: src/sys/kern/vfs_bio.c,v 1.242.2.20 2003/05/28 18:38:10 alc Exp $
 */

/*
 * this file contains a new buffer I/O scheme implementing a coherent
 * VM object and buffer cache scheme.  Pains have been taken to make
 * sure that the performance degradation associated with schemes such
 * as this is not realized.
 *
 * Author:  John S. Dyson
 * Significant help during the development and debugging phases
 * had been provided by David Greenman, also of the FreeBSD core team.
 *
 * see man buf(9) for more info.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/resourcevar.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/dsched.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>

#include <sys/buf2.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>
#include <vm/vm_page2.h>

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#endif

/*
 * Buffer queues.
 */
enum bufq_type {
	BQUEUE_NONE,    	/* not on any queue */
	BQUEUE_LOCKED,  	/* locked buffers */
	BQUEUE_CLEAN,   	/* non-B_DELWRI buffers */
	BQUEUE_DIRTY,   	/* B_DELWRI buffers */
	BQUEUE_DIRTY_HW,   	/* B_DELWRI buffers - heavy weight */
	BQUEUE_EMPTYKVA, 	/* empty buffer headers with KVA assignment */
	BQUEUE_EMPTY,    	/* empty buffer headers */

	BUFFER_QUEUES		/* number of buffer queues */
};

typedef enum bufq_type bufq_type_t;

#define BD_WAKE_SIZE	16384
#define BD_WAKE_MASK	(BD_WAKE_SIZE - 1)

TAILQ_HEAD(bqueues, buf);

struct bufpcpu {
	struct spinlock spin;
	struct bqueues bufqueues[BUFFER_QUEUES];
} __cachealign;

struct bufpcpu bufpcpu[MAXCPU];

static MALLOC_DEFINE(M_BIOBUF, "BIO buffer", "BIO buffer");

struct buf *buf;		/* buffer header pool */

static void vfs_clean_pages(struct buf *bp);
static void vfs_clean_one_page(struct buf *bp, int pageno, vm_page_t m);
#if 0
static void vfs_dirty_one_page(struct buf *bp, int pageno, vm_page_t m);
#endif
static void vfs_vmio_release(struct buf *bp);
static int flushbufqueues(struct buf *marker, bufq_type_t q);
static vm_page_t bio_page_alloc(struct buf *bp, vm_object_t obj,
				vm_pindex_t pg, int deficit);

static void bd_signal(long totalspace);
static void buf_daemon(void);
static void buf_daemon_hw(void);

/*
 * bogus page -- for I/O to/from partially complete buffers
 * this is a temporary solution to the problem, but it is not
 * really that bad.  it would be better to split the buffer
 * for input in the case of buffers partially already in memory,
 * but the code is intricate enough already.
 */
vm_page_t bogus_page;

/*
 * These are all static, but make the ones we export globals so we do
 * not need to use compiler magic.
 */
long bufspace;			/* locked by buffer_map */
long maxbufspace;
static long bufmallocspace;	/* atomic ops */
long maxbufmallocspace, lobufspace, hibufspace;
static long bufreusecnt, bufdefragcnt, buffreekvacnt;
static long lorunningspace;
static long hirunningspace;
static long dirtykvaspace;		/* atomic */
static long dirtybufspace;		/* atomic */
static long dirtybufcount;		/* atomic */
static long dirtybufspacehw;		/* atomic */
static long dirtybufcounthw;		/* atomic */
static long runningbufspace;		/* atomic */
static long runningbufcount;		/* atomic */
long lodirtybufspace;
long hidirtybufspace;
static int getnewbufcalls;
static int getnewbufrestarts;
static int recoverbufcalls;
static int needsbuffer;			/* atomic */
static int runningbufreq;		/* atomic */
static int bd_request;			/* atomic */
static int bd_request_hw;		/* atomic */
static u_int bd_wake_ary[BD_WAKE_SIZE];
static u_int bd_wake_index;
static u_int vm_cycle_point = 40; /* 23-36 will migrate more act->inact */
static int debug_commit;

static struct thread *bufdaemon_td;
static struct thread *bufdaemonhw_td;
static u_int lowmempgallocs;
static u_int lowmempgfails;

/*
 * Sysctls for operational control of the buffer cache.
 */
SYSCTL_LONG(_vfs, OID_AUTO, lodirtybufspace, CTLFLAG_RW, &lodirtybufspace, 0,
	"Number of dirty buffers to flush before bufdaemon becomes inactive");
SYSCTL_LONG(_vfs, OID_AUTO, hidirtybufspace, CTLFLAG_RW, &hidirtybufspace, 0,
	"High watermark used to trigger explicit flushing of dirty buffers");
SYSCTL_LONG(_vfs, OID_AUTO, lorunningspace, CTLFLAG_RW, &lorunningspace, 0,
	"Minimum amount of buffer space required for active I/O");
SYSCTL_LONG(_vfs, OID_AUTO, hirunningspace, CTLFLAG_RW, &hirunningspace, 0,
	"Maximum amount of buffer space to usable for active I/O");
SYSCTL_UINT(_vfs, OID_AUTO, lowmempgallocs, CTLFLAG_RW, &lowmempgallocs, 0,
	"Page allocations done during periods of very low free memory");
SYSCTL_UINT(_vfs, OID_AUTO, lowmempgfails, CTLFLAG_RW, &lowmempgfails, 0,
	"Page allocations which failed during periods of very low free memory");
SYSCTL_UINT(_vfs, OID_AUTO, vm_cycle_point, CTLFLAG_RW, &vm_cycle_point, 0,
	"Recycle pages to active or inactive queue transition pt 0-64");
/*
 * Sysctls determining current state of the buffer cache.
 */
SYSCTL_LONG(_vfs, OID_AUTO, nbuf, CTLFLAG_RD, &nbuf, 0,
	"Total number of buffers in buffer cache");
SYSCTL_LONG(_vfs, OID_AUTO, dirtykvaspace, CTLFLAG_RD, &dirtykvaspace, 0,
	"KVA reserved by dirty buffers (all)");
SYSCTL_LONG(_vfs, OID_AUTO, dirtybufspace, CTLFLAG_RD, &dirtybufspace, 0,
	"Pending bytes of dirty buffers (all)");
SYSCTL_LONG(_vfs, OID_AUTO, dirtybufspacehw, CTLFLAG_RD, &dirtybufspacehw, 0,
	"Pending bytes of dirty buffers (heavy weight)");
SYSCTL_LONG(_vfs, OID_AUTO, dirtybufcount, CTLFLAG_RD, &dirtybufcount, 0,
	"Pending number of dirty buffers");
SYSCTL_LONG(_vfs, OID_AUTO, dirtybufcounthw, CTLFLAG_RD, &dirtybufcounthw, 0,
	"Pending number of dirty buffers (heavy weight)");
SYSCTL_LONG(_vfs, OID_AUTO, runningbufspace, CTLFLAG_RD, &runningbufspace, 0,
	"I/O bytes currently in progress due to asynchronous writes");
SYSCTL_LONG(_vfs, OID_AUTO, runningbufcount, CTLFLAG_RD, &runningbufcount, 0,
	"I/O buffers currently in progress due to asynchronous writes");
SYSCTL_LONG(_vfs, OID_AUTO, maxbufspace, CTLFLAG_RD, &maxbufspace, 0,
	"Hard limit on maximum amount of memory usable for buffer space");
SYSCTL_LONG(_vfs, OID_AUTO, hibufspace, CTLFLAG_RD, &hibufspace, 0,
	"Soft limit on maximum amount of memory usable for buffer space");
SYSCTL_LONG(_vfs, OID_AUTO, lobufspace, CTLFLAG_RD, &lobufspace, 0,
	"Minimum amount of memory to reserve for system buffer space");
SYSCTL_LONG(_vfs, OID_AUTO, bufspace, CTLFLAG_RD, &bufspace, 0,
	"Amount of memory available for buffers");
SYSCTL_LONG(_vfs, OID_AUTO, maxmallocbufspace, CTLFLAG_RD, &maxbufmallocspace,
	0, "Maximum amount of memory reserved for buffers using malloc");
SYSCTL_LONG(_vfs, OID_AUTO, bufmallocspace, CTLFLAG_RD, &bufmallocspace, 0,
	"Amount of memory left for buffers using malloc-scheme");
SYSCTL_INT(_vfs, OID_AUTO, getnewbufcalls, CTLFLAG_RD, &getnewbufcalls, 0,
	"New buffer header acquisition requests");
SYSCTL_INT(_vfs, OID_AUTO, getnewbufrestarts, CTLFLAG_RD, &getnewbufrestarts,
	0, "New buffer header acquisition restarts");
SYSCTL_INT(_vfs, OID_AUTO, recoverbufcalls, CTLFLAG_RD, &recoverbufcalls, 0,
	"Recover VM space in an emergency");
SYSCTL_INT(_vfs, OID_AUTO, bufdefragcnt, CTLFLAG_RD, &bufdefragcnt, 0,
	"Buffer acquisition restarts due to fragmented buffer map");
SYSCTL_INT(_vfs, OID_AUTO, buffreekvacnt, CTLFLAG_RD, &buffreekvacnt, 0,
	"Amount of time KVA space was deallocated in an arbitrary buffer");
SYSCTL_INT(_vfs, OID_AUTO, bufreusecnt, CTLFLAG_RD, &bufreusecnt, 0,
	"Amount of time buffer re-use operations were successful");
SYSCTL_INT(_vfs, OID_AUTO, debug_commit, CTLFLAG_RW, &debug_commit, 0, "");
SYSCTL_INT(_debug_sizeof, OID_AUTO, buf, CTLFLAG_RD, 0, sizeof(struct buf),
	"sizeof(struct buf)");

char *buf_wmesg = BUF_WMESG;

#define VFS_BIO_NEED_ANY	0x01	/* any freeable buffer */
#define VFS_BIO_NEED_UNUSED02	0x02
#define VFS_BIO_NEED_UNUSED04	0x04
#define VFS_BIO_NEED_BUFSPACE	0x08	/* wait for buf space, lo hysteresis */

/*
 * bufspacewakeup:
 *
 *	Called when buffer space is potentially available for recovery.
 *	getnewbuf() will block on this flag when it is unable to free 
 *	sufficient buffer space.  Buffer space becomes recoverable when 
 *	bp's get placed back in the queues.
 */
static __inline void
bufspacewakeup(void)
{
	/*
	 * If someone is waiting for BUF space, wake them up.  Even
	 * though we haven't freed the kva space yet, the waiting
	 * process will be able to now.
	 */
	for (;;) {
		int flags = needsbuffer;
		cpu_ccfence();
		if ((flags & VFS_BIO_NEED_BUFSPACE) == 0)
			break;
		if (atomic_cmpset_int(&needsbuffer, flags,
				      flags & ~VFS_BIO_NEED_BUFSPACE)) {
			wakeup(&needsbuffer);
			break;
		}
		/* retry */
	}
}

/*
 * runningbufwakeup:
 *
 *	Accounting for I/O in progress.
 *
 */
static __inline void
runningbufwakeup(struct buf *bp)
{
	long totalspace;
	long limit;
	long flags;

	if ((totalspace = bp->b_runningbufspace) != 0) {
		atomic_add_long(&runningbufspace, -totalspace);
		atomic_add_long(&runningbufcount, -1);
		bp->b_runningbufspace = 0;

		/*
		 * see waitrunningbufspace() for limit test.
		 */
		limit = hirunningspace * 3 / 6;
		for (;;) {
			flags = runningbufreq;
			cpu_ccfence();
			if (flags == 0)
				break;
			if (atomic_cmpset_int(&runningbufreq, flags, 0)) {
				wakeup(&runningbufreq);
				break;
			}
			/* retry */
		}
		bd_signal(totalspace);
	}
}

/*
 * bufcountwakeup:
 *
 *	Called when a buffer has been added to one of the free queues to
 *	account for the buffer and to wakeup anyone waiting for free buffers.
 *	This typically occurs when large amounts of metadata are being handled
 *	by the buffer cache ( else buffer space runs out first, usually ).
 */
static __inline void
bufcountwakeup(void) 
{
	long flags;

	for (;;) {
		flags = needsbuffer;
		if (flags == 0)
			break;
		if (atomic_cmpset_int(&needsbuffer, flags,
				      (flags & ~VFS_BIO_NEED_ANY))) {
			wakeup(&needsbuffer);
			break;
		}
		/* retry */
	}
}

/*
 * waitrunningbufspace()
 *
 * If runningbufspace exceeds 4/6 hirunningspace we block until
 * runningbufspace drops to 3/6 hirunningspace.  We also block if another
 * thread blocked here in order to be fair, even if runningbufspace
 * is now lower than the limit.
 *
 * The caller may be using this function to block in a tight loop, we
 * must block while runningbufspace is greater than at least
 * hirunningspace * 3 / 6.
 */
void
waitrunningbufspace(void)
{
	long limit = hirunningspace * 4 / 6;
	long flags;

	while (runningbufspace > limit || runningbufreq) {
		tsleep_interlock(&runningbufreq, 0);
		flags = atomic_fetchadd_int(&runningbufreq, 0);
		if (runningbufspace > limit || flags)
			tsleep(&runningbufreq, PINTERLOCKED, "wdrn1", 0);
	}
}

/*
 * buf_dirty_count_severe:
 *
 *	Return true if we have too many dirty buffers.
 */
int
buf_dirty_count_severe(void)
{
	return (runningbufspace + dirtykvaspace >= hidirtybufspace ||
	        dirtybufcount >= nbuf / 2);
}

/*
 * Return true if the amount of running I/O is severe and BIOQ should
 * start bursting.
 */
int
buf_runningbufspace_severe(void)
{
	return (runningbufspace >= hirunningspace * 4 / 6);
}

/*
 * vfs_buf_test_cache:
 *
 * Called when a buffer is extended.  This function clears the B_CACHE
 * bit if the newly extended portion of the buffer does not contain
 * valid data.
 *
 * NOTE! Dirty VM pages are not processed into dirty (B_DELWRI) buffer
 * cache buffers.  The VM pages remain dirty, as someone had mmap()'d
 * them while a clean buffer was present.
 */
static __inline__
void
vfs_buf_test_cache(struct buf *bp,
		  vm_ooffset_t foff, vm_offset_t off, vm_offset_t size,
		  vm_page_t m)
{
	if (bp->b_flags & B_CACHE) {
		int base = (foff + off) & PAGE_MASK;
		if (vm_page_is_valid(m, base, size) == 0)
			bp->b_flags &= ~B_CACHE;
	}
}

/*
 * bd_speedup()
 *
 * Spank the buf_daemon[_hw] if the total dirty buffer space exceeds the
 * low water mark.
 */
static __inline__
void
bd_speedup(void)
{
	if (dirtykvaspace < lodirtybufspace && dirtybufcount < nbuf / 2)
		return;

	if (bd_request == 0 &&
	    (dirtykvaspace > lodirtybufspace / 2 ||
	     dirtybufcount - dirtybufcounthw >= nbuf / 2)) {
		if (atomic_fetchadd_int(&bd_request, 1) == 0)
			wakeup(&bd_request);
	}
	if (bd_request_hw == 0 &&
	    (dirtykvaspace > lodirtybufspace / 2 ||
	     dirtybufcounthw >= nbuf / 2)) {
		if (atomic_fetchadd_int(&bd_request_hw, 1) == 0)
			wakeup(&bd_request_hw);
	}
}

/*
 * bd_heatup()
 *
 *	Get the buf_daemon heated up when the number of running and dirty
 *	buffers exceeds the mid-point.
 *
 *	Return the total number of dirty bytes past the second mid point
 *	as a measure of how much excess dirty data there is in the system.
 */
long
bd_heatup(void)
{
	long mid1;
	long mid2;
	long totalspace;

	mid1 = lodirtybufspace + (hidirtybufspace - lodirtybufspace) / 2;

	totalspace = runningbufspace + dirtykvaspace;
	if (totalspace >= mid1 || dirtybufcount >= nbuf / 2) {
		bd_speedup();
		mid2 = mid1 + (hidirtybufspace - mid1) / 2;
		if (totalspace >= mid2)
			return(totalspace - mid2);
	}
	return(0);
}

/*
 * bd_wait()
 *
 *	Wait for the buffer cache to flush (totalspace) bytes worth of
 *	buffers, then return.
 *
 *	Regardless this function blocks while the number of dirty buffers
 *	exceeds hidirtybufspace.
 */
void
bd_wait(long totalspace)
{
	u_int i;
	u_int j;
	u_int mi;
	int count;

	if (curthread == bufdaemonhw_td || curthread == bufdaemon_td)
		return;

	while (totalspace > 0) {
		bd_heatup();

		/*
		 * Order is important.  Suppliers adjust bd_wake_index after
		 * updating runningbufspace/dirtykvaspace.  We want to fetch
		 * bd_wake_index before accessing.  Any error should thus
		 * be in our favor.
		 */
		i = atomic_fetchadd_int(&bd_wake_index, 0);
		if (totalspace > runningbufspace + dirtykvaspace)
			totalspace = runningbufspace + dirtykvaspace;
		count = totalspace / BKVASIZE;
		if (count >= BD_WAKE_SIZE / 2)
			count = BD_WAKE_SIZE / 2;
		i = i + count;
		mi = i & BD_WAKE_MASK;

		/*
		 * This is not a strict interlock, so we play a bit loose
		 * with locking access to dirtybufspace*.  We have to re-check
		 * bd_wake_index to ensure that it hasn't passed us.
		 */
		tsleep_interlock(&bd_wake_ary[mi], 0);
		atomic_add_int(&bd_wake_ary[mi], 1);
		j = atomic_fetchadd_int(&bd_wake_index, 0);
		if ((int)(i - j) >= 0)
			tsleep(&bd_wake_ary[mi], PINTERLOCKED, "flstik", hz);

		totalspace = runningbufspace + dirtykvaspace - hidirtybufspace;
	}
}

/*
 * bd_signal()
 * 
 *	This function is called whenever runningbufspace or dirtykvaspace
 *	is reduced.  Track threads waiting for run+dirty buffer I/O
 *	complete.
 */
static void
bd_signal(long totalspace)
{
	u_int i;

	if (totalspace > 0) {
		if (totalspace > BKVASIZE * BD_WAKE_SIZE)
			totalspace = BKVASIZE * BD_WAKE_SIZE;
		while (totalspace > 0) {
			i = atomic_fetchadd_int(&bd_wake_index, 1);
			i &= BD_WAKE_MASK;
			if (atomic_readandclear_int(&bd_wake_ary[i]))
				wakeup(&bd_wake_ary[i]);
			totalspace -= BKVASIZE;
		}
	}
}

/*
 * BIO tracking support routines.
 *
 * Release a ref on a bio_track.  Wakeup requests are atomically released
 * along with the last reference so bk_active will never wind up set to
 * only 0x80000000.
 */
static
void
bio_track_rel(struct bio_track *track)
{
	int	active;
	int	desired;

	/*
	 * Shortcut
	 */
	active = track->bk_active;
	if (active == 1 && atomic_cmpset_int(&track->bk_active, 1, 0))
		return;

	/*
	 * Full-on.  Note that the wait flag is only atomically released on
	 * the 1->0 count transition.
	 *
	 * We check for a negative count transition using bit 30 since bit 31
	 * has a different meaning.
	 */
	for (;;) {
		desired = (active & 0x7FFFFFFF) - 1;
		if (desired)
			desired |= active & 0x80000000;
		if (atomic_cmpset_int(&track->bk_active, active, desired)) {
			if (desired & 0x40000000)
				panic("bio_track_rel: bad count: %p", track);
			if (active & 0x80000000)
				wakeup(track);
			break;
		}
		active = track->bk_active;
	}
}

/*
 * Wait for the tracking count to reach 0.
 *
 * Use atomic ops such that the wait flag is only set atomically when
 * bk_active is non-zero.
 */
int
bio_track_wait(struct bio_track *track, int slp_flags, int slp_timo)
{
	int	active;
	int	desired;
	int	error;

	/*
	 * Shortcut
	 */
	if (track->bk_active == 0)
		return(0);

	/*
	 * Full-on.  Note that the wait flag may only be atomically set if
	 * the active count is non-zero.
	 *
	 * NOTE: We cannot optimize active == desired since a wakeup could
	 *	 clear active prior to our tsleep_interlock().
	 */
	error = 0;
	while ((active = track->bk_active) != 0) {
		cpu_ccfence();
		desired = active | 0x80000000;
		tsleep_interlock(track, slp_flags);
		if (atomic_cmpset_int(&track->bk_active, active, desired)) {
			error = tsleep(track, slp_flags | PINTERLOCKED,
				       "trwait", slp_timo);
			if (error)
				break;
		}
	}
	return (error);
}

/*
 * bufinit:
 *
 *	Load time initialisation of the buffer cache, called from machine
 *	dependant initialization code. 
 */
void
bufinit(void)
{
	struct bufpcpu *pcpu;
	struct buf *bp;
	vm_offset_t bogus_offset;
	int i;
	int j;
	long n;

	/* next, make a null set of free lists */
	for (i = 0; i < ncpus; ++i) {
		pcpu = &bufpcpu[i];
		spin_init(&pcpu->spin);
		for (j = 0; j < BUFFER_QUEUES; j++)
			TAILQ_INIT(&pcpu->bufqueues[j]);
	}

	/* finally, initialize each buffer header and stick on empty q */
	i = 0;
	pcpu = &bufpcpu[i];

	for (n = 0; n < nbuf; n++) {
		bp = &buf[n];
		bzero(bp, sizeof *bp);
		bp->b_flags = B_INVAL;	/* we're just an empty header */
		bp->b_cmd = BUF_CMD_DONE;
		bp->b_qindex = BQUEUE_EMPTY;
		bp->b_qcpu = i;
		initbufbio(bp);
		xio_init(&bp->b_xio);
		buf_dep_init(bp);
		TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
				  bp, b_freelist);

		i = (i + 1) % ncpus;
		pcpu = &bufpcpu[i];
	}

	/*
	 * maxbufspace is the absolute maximum amount of buffer space we are 
	 * allowed to reserve in KVM and in real terms.  The absolute maximum
	 * is nominally used by buf_daemon.  hibufspace is the nominal maximum
	 * used by most other processes.  The differential is required to 
	 * ensure that buf_daemon is able to run when other processes might 
	 * be blocked waiting for buffer space.
	 *
	 * maxbufspace is based on BKVASIZE.  Allocating buffers larger then
	 * this may result in KVM fragmentation which is not handled optimally
	 * by the system.
	 */
	maxbufspace = nbuf * BKVASIZE;
	hibufspace = lmax(3 * maxbufspace / 4, maxbufspace - MAXBSIZE * 10);
	lobufspace = hibufspace - MAXBSIZE;

	lorunningspace = 512 * 1024;
	/* hirunningspace -- see below */

	/*
	 * Limit the amount of malloc memory since it is wired permanently
	 * into the kernel space.  Even though this is accounted for in
	 * the buffer allocation, we don't want the malloced region to grow
	 * uncontrolled.  The malloc scheme improves memory utilization
	 * significantly on average (small) directories.
	 */
	maxbufmallocspace = hibufspace / 20;

	/*
	 * Reduce the chance of a deadlock occuring by limiting the number
	 * of delayed-write dirty buffers we allow to stack up.
	 *
	 * We don't want too much actually queued to the device at once
	 * (XXX this needs to be per-mount!), because the buffers will
	 * wind up locked for a very long period of time while the I/O
	 * drains.
	 */
	hidirtybufspace = hibufspace / 2;	/* dirty + running */
	hirunningspace = hibufspace / 16;	/* locked & queued to device */
	if (hirunningspace < 1024 * 1024)
		hirunningspace = 1024 * 1024;

	dirtykvaspace = 0;
	dirtybufspace = 0;
	dirtybufspacehw = 0;

	lodirtybufspace = hidirtybufspace / 2;

	/*
	 * Maximum number of async ops initiated per buf_daemon loop.  This is
	 * somewhat of a hack at the moment, we really need to limit ourselves
	 * based on the number of bytes of I/O in-transit that were initiated
	 * from buf_daemon.
	 */

	bogus_offset = kmem_alloc_pageable(&kernel_map, PAGE_SIZE);
	vm_object_hold(&kernel_object);
	bogus_page = vm_page_alloc(&kernel_object,
				   (bogus_offset >> PAGE_SHIFT),
				   VM_ALLOC_NORMAL);
	vm_object_drop(&kernel_object);
	vmstats.v_wire_count++;

}

/*
 * Initialize the embedded bio structures, typically used by
 * deprecated code which tries to allocate its own struct bufs.
 */
void
initbufbio(struct buf *bp)
{
	bp->b_bio1.bio_buf = bp;
	bp->b_bio1.bio_prev = NULL;
	bp->b_bio1.bio_offset = NOOFFSET;
	bp->b_bio1.bio_next = &bp->b_bio2;
	bp->b_bio1.bio_done = NULL;
	bp->b_bio1.bio_flags = 0;

	bp->b_bio2.bio_buf = bp;
	bp->b_bio2.bio_prev = &bp->b_bio1;
	bp->b_bio2.bio_offset = NOOFFSET;
	bp->b_bio2.bio_next = NULL;
	bp->b_bio2.bio_done = NULL;
	bp->b_bio2.bio_flags = 0;

	BUF_LOCKINIT(bp);
}

/*
 * Reinitialize the embedded bio structures as well as any additional
 * translation cache layers.
 */
void
reinitbufbio(struct buf *bp)
{
	struct bio *bio;

	for (bio = &bp->b_bio1; bio; bio = bio->bio_next) {
		bio->bio_done = NULL;
		bio->bio_offset = NOOFFSET;
	}
}

/*
 * Undo the effects of an initbufbio().
 */
void
uninitbufbio(struct buf *bp)
{
	dsched_exit_buf(bp);
	BUF_LOCKFREE(bp);
}

/*
 * Push another BIO layer onto an existing BIO and return it.  The new
 * BIO layer may already exist, holding cached translation data.
 */
struct bio *
push_bio(struct bio *bio)
{
	struct bio *nbio;

	if ((nbio = bio->bio_next) == NULL) {
		int index = bio - &bio->bio_buf->b_bio_array[0];
		if (index >= NBUF_BIO - 1) {
			panic("push_bio: too many layers bp %p",
				bio->bio_buf);
		}
		nbio = &bio->bio_buf->b_bio_array[index + 1];
		bio->bio_next = nbio;
		nbio->bio_prev = bio;
		nbio->bio_buf = bio->bio_buf;
		nbio->bio_offset = NOOFFSET;
		nbio->bio_done = NULL;
		nbio->bio_next = NULL;
	}
	KKASSERT(nbio->bio_done == NULL);
	return(nbio);
}

/*
 * Pop a BIO translation layer, returning the previous layer.  The
 * must have been previously pushed.
 */
struct bio *
pop_bio(struct bio *bio)
{
	return(bio->bio_prev);
}

void
clearbiocache(struct bio *bio)
{
	while (bio) {
		bio->bio_offset = NOOFFSET;
		bio = bio->bio_next;
	}
}

/*
 * bfreekva:
 *
 *	Free the KVA allocation for buffer 'bp'.
 *
 *	Must be called from a critical section as this is the only locking for
 *	buffer_map.
 *
 *	Since this call frees up buffer space, we call bufspacewakeup().
 */
static void
bfreekva(struct buf *bp)
{
	int count;

	if (bp->b_kvasize) {
		++buffreekvacnt;
		count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
		vm_map_lock(&buffer_map);
		bufspace -= bp->b_kvasize;
		vm_map_delete(&buffer_map,
		    (vm_offset_t) bp->b_kvabase,
		    (vm_offset_t) bp->b_kvabase + bp->b_kvasize,
		    &count
		);
		vm_map_unlock(&buffer_map);
		vm_map_entry_release(count);
		bp->b_kvasize = 0;
		bp->b_kvabase = NULL;
		bufspacewakeup();
	}
}

/*
 * Remove the buffer from the appropriate free list.
 * (caller must be locked)
 */
static __inline void
_bremfree(struct buf *bp)
{
	struct bufpcpu *pcpu = &bufpcpu[bp->b_qcpu];

	if (bp->b_qindex != BQUEUE_NONE) {
		KASSERT(BUF_REFCNTNB(bp) == 1, 
			("bremfree: bp %p not locked",bp));
		TAILQ_REMOVE(&pcpu->bufqueues[bp->b_qindex], bp, b_freelist);
		bp->b_qindex = BQUEUE_NONE;
	} else {
		if (BUF_REFCNTNB(bp) <= 1)
			panic("bremfree: removing a buffer not on a queue");
	}
}

/*
 * bremfree() - must be called with a locked buffer
 */
void
bremfree(struct buf *bp)
{
	struct bufpcpu *pcpu = &bufpcpu[bp->b_qcpu];

	spin_lock(&pcpu->spin);
	_bremfree(bp);
	spin_unlock(&pcpu->spin);
}

/*
 * bremfree_locked - must be called with pcpu->spin locked
 */
static void
bremfree_locked(struct buf *bp)
{
	_bremfree(bp);
}

/*
 * This version of bread issues any required I/O asyncnronously and
 * makes a callback on completion.
 *
 * The callback must check whether BIO_DONE is set in the bio and issue
 * the bpdone(bp, 0) if it isn't.  The callback is responsible for clearing
 * BIO_DONE and disposing of the I/O (bqrelse()ing it).
 */
void
breadcb(struct vnode *vp, off_t loffset, int size,
	void (*func)(struct bio *), void *arg)
{
	struct buf *bp;

	bp = getblk(vp, loffset, size, 0, 0);

	/* if not found in cache, do some I/O */
	if ((bp->b_flags & B_CACHE) == 0) {
		bp->b_flags &= ~(B_ERROR | B_EINTR | B_INVAL);
		bp->b_cmd = BUF_CMD_READ;
		bp->b_bio1.bio_done = func;
		bp->b_bio1.bio_caller_info1.ptr = arg;
		vfs_busy_pages(vp, bp);
		BUF_KERNPROC(bp);
		vn_strategy(vp, &bp->b_bio1);
	} else if (func) {
		/*
		 * Since we are issuing the callback synchronously it cannot
		 * race the BIO_DONE, so no need for atomic ops here.
		 */
		/*bp->b_bio1.bio_done = func;*/
		bp->b_bio1.bio_caller_info1.ptr = arg;
		bp->b_bio1.bio_flags |= BIO_DONE;
		func(&bp->b_bio1);
	} else {
		bqrelse(bp);
	}
}

/*
 * breadnx() - Terminal function for bread() and breadn().
 *
 * This function will start asynchronous I/O on read-ahead blocks as well
 * as satisfy the primary request.
 *
 * We must clear B_ERROR and B_INVAL prior to initiating I/O.  If B_CACHE is
 * set, the buffer is valid and we do not have to do anything.
 */
int
breadnx(struct vnode *vp, off_t loffset, int size, off_t *raoffset,
	int *rabsize, int cnt, struct buf **bpp)
{
	struct buf *bp, *rabp;
	int i;
	int rv = 0, readwait = 0;

	if (*bpp)
		bp = *bpp;
	else
		*bpp = bp = getblk(vp, loffset, size, 0, 0);

	/* if not found in cache, do some I/O */
	if ((bp->b_flags & B_CACHE) == 0) {
		bp->b_flags &= ~(B_ERROR | B_EINTR | B_INVAL);
		bp->b_cmd = BUF_CMD_READ;
		bp->b_bio1.bio_done = biodone_sync;
		bp->b_bio1.bio_flags |= BIO_SYNC;
		vfs_busy_pages(vp, bp);
		vn_strategy(vp, &bp->b_bio1);
		++readwait;
	}

	for (i = 0; i < cnt; i++, raoffset++, rabsize++) {
		if (inmem(vp, *raoffset))
			continue;
		rabp = getblk(vp, *raoffset, *rabsize, 0, 0);

		if ((rabp->b_flags & B_CACHE) == 0) {
			rabp->b_flags &= ~(B_ERROR | B_EINTR | B_INVAL);
			rabp->b_cmd = BUF_CMD_READ;
			vfs_busy_pages(vp, rabp);
			BUF_KERNPROC(rabp);
			vn_strategy(vp, &rabp->b_bio1);
		} else {
			brelse(rabp);
		}
	}
	if (readwait)
		rv = biowait(&bp->b_bio1, "biord");
	return (rv);
}

/*
 * bwrite:
 *
 *	Synchronous write, waits for completion.
 *
 *	Write, release buffer on completion.  (Done by iodone
 *	if async).  Do not bother writing anything if the buffer
 *	is invalid.
 *
 *	Note that we set B_CACHE here, indicating that buffer is
 *	fully valid and thus cacheable.  This is true even of NFS
 *	now so we set it generally.  This could be set either here 
 *	or in biodone() since the I/O is synchronous.  We put it
 *	here.
 */
int
bwrite(struct buf *bp)
{
	int error;

	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return (0);
	}
	if (BUF_REFCNTNB(bp) == 0)
		panic("bwrite: buffer is not busy???");

	/* Mark the buffer clean */
	bundirty(bp);

	bp->b_flags &= ~(B_ERROR | B_EINTR);
	bp->b_flags |= B_CACHE;
	bp->b_cmd = BUF_CMD_WRITE;
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	vfs_busy_pages(bp->b_vp, bp);

	/*
	 * Normal bwrites pipeline writes.  NOTE: b_bufsize is only
	 * valid for vnode-backed buffers.
	 */
	bsetrunningbufspace(bp, bp->b_bufsize);
	vn_strategy(bp->b_vp, &bp->b_bio1);
	error = biowait(&bp->b_bio1, "biows");
	brelse(bp);

	return (error);
}

/*
 * bawrite:
 *
 *	Asynchronous write.  Start output on a buffer, but do not wait for
 *	it to complete.  The buffer is released when the output completes.
 *
 *	bwrite() ( or the VOP routine anyway ) is responsible for handling
 *	B_INVAL buffers.  Not us.
 */
void
bawrite(struct buf *bp)
{
	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return;
	}
	if (BUF_REFCNTNB(bp) == 0)
		panic("bwrite: buffer is not busy???");

	/* Mark the buffer clean */
	bundirty(bp);

	bp->b_flags &= ~(B_ERROR | B_EINTR);
	bp->b_flags |= B_CACHE;
	bp->b_cmd = BUF_CMD_WRITE;
	KKASSERT(bp->b_bio1.bio_done == NULL);
	vfs_busy_pages(bp->b_vp, bp);

	/*
	 * Normal bwrites pipeline writes.  NOTE: b_bufsize is only
	 * valid for vnode-backed buffers.
	 */
	bsetrunningbufspace(bp, bp->b_bufsize);
	BUF_KERNPROC(bp);
	vn_strategy(bp->b_vp, &bp->b_bio1);
}

/*
 * bowrite:
 *
 *	Ordered write.  Start output on a buffer, and flag it so that the
 *	device will write it in the order it was queued.  The buffer is
 *	released when the output completes.  bwrite() ( or the VOP routine
 *	anyway ) is responsible for handling B_INVAL buffers.
 */
int
bowrite(struct buf *bp)
{
	bp->b_flags |= B_ORDERED;
	bawrite(bp);
	return (0);
}

/*
 * bdwrite:
 *
 *	Delayed write. (Buffer is marked dirty).  Do not bother writing
 *	anything if the buffer is marked invalid.
 *
 *	Note that since the buffer must be completely valid, we can safely
 *	set B_CACHE.  In fact, we have to set B_CACHE here rather then in
 *	biodone() in order to prevent getblk from writing the buffer
 *	out synchronously.
 */
void
bdwrite(struct buf *bp)
{
	if (BUF_REFCNTNB(bp) == 0)
		panic("bdwrite: buffer is not busy");

	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return;
	}
	bdirty(bp);

	if (dsched_is_clear_buf_priv(bp))
		dsched_new_buf(bp);

	/*
	 * Set B_CACHE, indicating that the buffer is fully valid.  This is
	 * true even of NFS now.
	 */
	bp->b_flags |= B_CACHE;

	/*
	 * This bmap keeps the system from needing to do the bmap later,
	 * perhaps when the system is attempting to do a sync.  Since it
	 * is likely that the indirect block -- or whatever other datastructure
	 * that the filesystem needs is still in memory now, it is a good
	 * thing to do this.  Note also, that if the pageout daemon is
	 * requesting a sync -- there might not be enough memory to do
	 * the bmap then...  So, this is important to do.
	 */
	if (bp->b_bio2.bio_offset == NOOFFSET) {
		VOP_BMAP(bp->b_vp, bp->b_loffset, &bp->b_bio2.bio_offset,
			 NULL, NULL, BUF_CMD_WRITE);
	}

	/*
	 * Because the underlying pages may still be mapped and
	 * writable trying to set the dirty buffer (b_dirtyoff/end)
	 * range here will be inaccurate.
	 *
	 * However, we must still clean the pages to satisfy the
	 * vnode_pager and pageout daemon, so theythink the pages
	 * have been "cleaned".  What has really occured is that
	 * they've been earmarked for later writing by the buffer
	 * cache.
	 *
	 * So we get the b_dirtyoff/end update but will not actually
	 * depend on it (NFS that is) until the pages are busied for
	 * writing later on.
	 */
	vfs_clean_pages(bp);
	bqrelse(bp);

	/*
	 * note: we cannot initiate I/O from a bdwrite even if we wanted to,
	 * due to the softdep code.
	 */
}

/*
 * Fake write - return pages to VM system as dirty, leave the buffer clean.
 * This is used by tmpfs.
 *
 * It is important for any VFS using this routine to NOT use it for
 * IO_SYNC or IO_ASYNC operations which occur when the system really
 * wants to flush VM pages to backing store.
 */
void
buwrite(struct buf *bp)
{
	vm_page_t m;
	int i;

	/*
	 * Only works for VMIO buffers.  If the buffer is already
	 * marked for delayed-write we can't avoid the bdwrite().
	 */
	if ((bp->b_flags & B_VMIO) == 0 || (bp->b_flags & B_DELWRI)) {
		bdwrite(bp);
		return;
	}

	/*
	 * Mark as needing a commit.
	 */
	for (i = 0; i < bp->b_xio.xio_npages; i++) {
		m = bp->b_xio.xio_pages[i];
		vm_page_need_commit(m);
	}
	bqrelse(bp);
}

/*
 * bdirty:
 *
 *	Turn buffer into delayed write request by marking it B_DELWRI.
 *	B_RELBUF and B_NOCACHE must be cleared.
 *
 *	We reassign the buffer to itself to properly update it in the
 *	dirty/clean lists. 
 *
 *	Must be called from a critical section.
 *	The buffer must be on BQUEUE_NONE.
 */
void
bdirty(struct buf *bp)
{
	KASSERT(bp->b_qindex == BQUEUE_NONE,
		("bdirty: buffer %p still on queue %d", bp, bp->b_qindex));
	if (bp->b_flags & B_NOCACHE) {
		kprintf("bdirty: clearing B_NOCACHE on buf %p\n", bp);
		bp->b_flags &= ~B_NOCACHE;
	}
	if (bp->b_flags & B_INVAL) {
		kprintf("bdirty: warning, dirtying invalid buffer %p\n", bp);
	}
	bp->b_flags &= ~B_RELBUF;

	if ((bp->b_flags & B_DELWRI) == 0) {
		lwkt_gettoken(&bp->b_vp->v_token);
		bp->b_flags |= B_DELWRI;
		reassignbuf(bp);
		lwkt_reltoken(&bp->b_vp->v_token);

		atomic_add_long(&dirtybufcount, 1);
		atomic_add_long(&dirtykvaspace, bp->b_kvasize);
		atomic_add_long(&dirtybufspace, bp->b_bufsize);
		if (bp->b_flags & B_HEAVY) {
			atomic_add_long(&dirtybufcounthw, 1);
			atomic_add_long(&dirtybufspacehw, bp->b_bufsize);
		}
		bd_heatup();
	}
}

/*
 * Set B_HEAVY, indicating that this is a heavy-weight buffer that
 * needs to be flushed with a different buf_daemon thread to avoid
 * deadlocks.  B_HEAVY also imposes restrictions in getnewbuf().
 */
void
bheavy(struct buf *bp)
{
	if ((bp->b_flags & B_HEAVY) == 0) {
		bp->b_flags |= B_HEAVY;
		if (bp->b_flags & B_DELWRI) {
			atomic_add_long(&dirtybufcounthw, 1);
			atomic_add_long(&dirtybufspacehw, bp->b_bufsize);
		}
	}
}

/*
 * bundirty:
 *
 *	Clear B_DELWRI for buffer.
 *
 *	Must be called from a critical section.
 *
 *	The buffer is typically on BQUEUE_NONE but there is one case in 
 *	brelse() that calls this function after placing the buffer on
 *	a different queue.
 */
void
bundirty(struct buf *bp)
{
	if (bp->b_flags & B_DELWRI) {
		lwkt_gettoken(&bp->b_vp->v_token);
		bp->b_flags &= ~B_DELWRI;
		reassignbuf(bp);
		lwkt_reltoken(&bp->b_vp->v_token);

		atomic_add_long(&dirtybufcount, -1);
		atomic_add_long(&dirtykvaspace, -bp->b_kvasize);
		atomic_add_long(&dirtybufspace, -bp->b_bufsize);
		if (bp->b_flags & B_HEAVY) {
			atomic_add_long(&dirtybufcounthw, -1);
			atomic_add_long(&dirtybufspacehw, -bp->b_bufsize);
		}
		bd_signal(bp->b_bufsize);
	}
	/*
	 * Since it is now being written, we can clear its deferred write flag.
	 */
	bp->b_flags &= ~B_DEFERRED;
}

/*
 * Set the b_runningbufspace field, used to track how much I/O is
 * in progress at any given moment.
 */
void
bsetrunningbufspace(struct buf *bp, int bytes)
{
	bp->b_runningbufspace = bytes;
	if (bytes) {
		atomic_add_long(&runningbufspace, bytes);
		atomic_add_long(&runningbufcount, 1);
	}
}

/*
 * brelse:
 *
 *	Release a busy buffer and, if requested, free its resources.  The
 *	buffer will be stashed in the appropriate bufqueue[] allowing it
 *	to be accessed later as a cache entity or reused for other purposes.
 */
void
brelse(struct buf *bp)
{
	struct bufpcpu *pcpu;
#ifdef INVARIANTS
	int saved_flags = bp->b_flags;
#endif

	KASSERT(!(bp->b_flags & (B_CLUSTER|B_PAGING)),
		("brelse: inappropriate B_PAGING or B_CLUSTER bp %p", bp));

	/*
	 * If B_NOCACHE is set we are being asked to destroy the buffer and
	 * its backing store.  Clear B_DELWRI.
	 *
	 * B_NOCACHE is set in two cases: (1) when the caller really wants
	 * to destroy the buffer and backing store and (2) when the caller
	 * wants to destroy the buffer and backing store after a write 
	 * completes.
	 */
	if ((bp->b_flags & (B_NOCACHE|B_DELWRI)) == (B_NOCACHE|B_DELWRI)) {
		bundirty(bp);
	}

	if ((bp->b_flags & (B_INVAL | B_DELWRI)) == B_DELWRI) {
		/*
		 * A re-dirtied buffer is only subject to destruction
		 * by B_INVAL.  B_ERROR and B_NOCACHE are ignored.
		 */
		/* leave buffer intact */
	} else if ((bp->b_flags & (B_NOCACHE | B_INVAL | B_ERROR)) ||
		   (bp->b_bufsize <= 0)) {
		/*
		 * Either a failed read or we were asked to free or not
		 * cache the buffer.  This path is reached with B_DELWRI
		 * set only if B_INVAL is already set.  B_NOCACHE governs
		 * backing store destruction.
		 *
		 * NOTE: HAMMER will set B_LOCKED in buf_deallocate if the
		 * buffer cannot be immediately freed.
		 */
		bp->b_flags |= B_INVAL;
		if (LIST_FIRST(&bp->b_dep) != NULL)
			buf_deallocate(bp);
		if (bp->b_flags & B_DELWRI) {
			atomic_add_long(&dirtybufcount, -1);
			atomic_add_long(&dirtykvaspace, -bp->b_kvasize);
			atomic_add_long(&dirtybufspace, -bp->b_bufsize);
			if (bp->b_flags & B_HEAVY) {
				atomic_add_long(&dirtybufcounthw, -1);
				atomic_add_long(&dirtybufspacehw,
						-bp->b_bufsize);
			}
			bd_signal(bp->b_bufsize);
		}
		bp->b_flags &= ~(B_DELWRI | B_CACHE);
	}

	/*
	 * We must clear B_RELBUF if B_DELWRI or B_LOCKED is set,
	 * or if b_refs is non-zero.
	 *
	 * If vfs_vmio_release() is called with either bit set, the
	 * underlying pages may wind up getting freed causing a previous
	 * write (bdwrite()) to get 'lost' because pages associated with
	 * a B_DELWRI bp are marked clean.  Pages associated with a
	 * B_LOCKED buffer may be mapped by the filesystem.
	 *
	 * If we want to release the buffer ourselves (rather then the
	 * originator asking us to release it), give the originator a
	 * chance to countermand the release by setting B_LOCKED.
	 * 
	 * We still allow the B_INVAL case to call vfs_vmio_release(), even
	 * if B_DELWRI is set.
	 *
	 * If B_DELWRI is not set we may have to set B_RELBUF if we are low
	 * on pages to return pages to the VM page queues.
	 */
	if ((bp->b_flags & (B_DELWRI | B_LOCKED)) || bp->b_refs) {
		bp->b_flags &= ~B_RELBUF;
	} else if (vm_page_count_min(0)) {
		if (LIST_FIRST(&bp->b_dep) != NULL)
			buf_deallocate(bp);		/* can set B_LOCKED */
		if (bp->b_flags & (B_DELWRI | B_LOCKED))
			bp->b_flags &= ~B_RELBUF;
		else
			bp->b_flags |= B_RELBUF;
	}

	/*
	 * Make sure b_cmd is clear.  It may have already been cleared by
	 * biodone().
	 *
	 * At this point destroying the buffer is governed by the B_INVAL 
	 * or B_RELBUF flags.
	 */
	bp->b_cmd = BUF_CMD_DONE;
	dsched_exit_buf(bp);

	/*
	 * VMIO buffer rundown.  Make sure the VM page array is restored
	 * after an I/O may have replaces some of the pages with bogus pages
	 * in order to not destroy dirty pages in a fill-in read.
	 *
	 * Note that due to the code above, if a buffer is marked B_DELWRI
	 * then the B_RELBUF and B_NOCACHE bits will always be clear.
	 * B_INVAL may still be set, however.
	 *
	 * For clean buffers, B_INVAL or B_RELBUF will destroy the buffer
	 * but not the backing store.   B_NOCACHE will destroy the backing
	 * store.
	 *
	 * Note that dirty NFS buffers contain byte-granular write ranges
	 * and should not be destroyed w/ B_INVAL even if the backing store
	 * is left intact.
	 */
	if (bp->b_flags & B_VMIO) {
		/*
		 * Rundown for VMIO buffers which are not dirty NFS buffers.
		 */
		int i, j, resid;
		vm_page_t m;
		off_t foff;
		vm_pindex_t poff;
		vm_object_t obj;
		struct vnode *vp;

		vp = bp->b_vp;

		/*
		 * Get the base offset and length of the buffer.  Note that 
		 * in the VMIO case if the buffer block size is not
		 * page-aligned then b_data pointer may not be page-aligned.
		 * But our b_xio.xio_pages array *IS* page aligned.
		 *
		 * block sizes less then DEV_BSIZE (usually 512) are not 
		 * supported due to the page granularity bits (m->valid,
		 * m->dirty, etc...). 
		 *
		 * See man buf(9) for more information
		 */

		resid = bp->b_bufsize;
		foff = bp->b_loffset;

		for (i = 0; i < bp->b_xio.xio_npages; i++) {
			m = bp->b_xio.xio_pages[i];
			vm_page_flag_clear(m, PG_ZERO);
			/*
			 * If we hit a bogus page, fixup *all* of them
			 * now.  Note that we left these pages wired
			 * when we removed them so they had better exist,
			 * and they cannot be ripped out from under us so
			 * no critical section protection is necessary.
			 */
			if (m == bogus_page) {
				obj = vp->v_object;
				poff = OFF_TO_IDX(bp->b_loffset);

				vm_object_hold(obj);
				for (j = i; j < bp->b_xio.xio_npages; j++) {
					vm_page_t mtmp;

					mtmp = bp->b_xio.xio_pages[j];
					if (mtmp == bogus_page) {
						mtmp = vm_page_lookup(obj, poff + j);
						if (!mtmp) {
							panic("brelse: page missing");
						}
						bp->b_xio.xio_pages[j] = mtmp;
					}
				}
				bp->b_flags &= ~B_HASBOGUS;
				vm_object_drop(obj);

				if ((bp->b_flags & B_INVAL) == 0) {
					pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
						bp->b_xio.xio_pages, bp->b_xio.xio_npages);
				}
				m = bp->b_xio.xio_pages[i];
			}

			/*
			 * Invalidate the backing store if B_NOCACHE is set
			 * (e.g. used with vinvalbuf()).  If this is NFS
			 * we impose a requirement that the block size be
			 * a multiple of PAGE_SIZE and create a temporary
			 * hack to basically invalidate the whole page.  The
			 * problem is that NFS uses really odd buffer sizes
			 * especially when tracking piecemeal writes and
			 * it also vinvalbuf()'s a lot, which would result
			 * in only partial page validation and invalidation
			 * here.  If the file page is mmap()'d, however,
			 * all the valid bits get set so after we invalidate
			 * here we would end up with weird m->valid values
			 * like 0xfc.  nfs_getpages() can't handle this so
			 * we clear all the valid bits for the NFS case
			 * instead of just some of them.
			 *
			 * The real bug is the VM system having to set m->valid
			 * to VM_PAGE_BITS_ALL for faulted-in pages, which
			 * itself is an artifact of the whole 512-byte
			 * granular mess that exists to support odd block 
			 * sizes and UFS meta-data block sizes (e.g. 6144).
			 * A complete rewrite is required.
			 *
			 * XXX
			 */
			if (bp->b_flags & (B_NOCACHE|B_ERROR)) {
				int poffset = foff & PAGE_MASK;
				int presid;

				presid = PAGE_SIZE - poffset;
				if (bp->b_vp->v_tag == VT_NFS &&
				    bp->b_vp->v_type == VREG) {
					; /* entire page */
				} else if (presid > resid) {
					presid = resid;
				}
				KASSERT(presid >= 0, ("brelse: extra page"));
				vm_page_set_invalid(m, poffset, presid);

				/*
				 * Also make sure any swap cache is removed
				 * as it is now stale (HAMMER in particular
				 * uses B_NOCACHE to deal with buffer
				 * aliasing).
				 */
				swap_pager_unswapped(m);
			}
			resid -= PAGE_SIZE - (foff & PAGE_MASK);
			foff = (foff + PAGE_SIZE) & ~(off_t)PAGE_MASK;
		}
		if (bp->b_flags & (B_INVAL | B_RELBUF))
			vfs_vmio_release(bp);
	} else {
		/*
		 * Rundown for non-VMIO buffers.
		 */
		if (bp->b_flags & (B_INVAL | B_RELBUF)) {
			if (bp->b_bufsize)
				allocbuf(bp, 0);
			KKASSERT (LIST_FIRST(&bp->b_dep) == NULL);
			if (bp->b_vp)
				brelvp(bp);
		}
	}
			
	if (bp->b_qindex != BQUEUE_NONE)
		panic("brelse: free buffer onto another queue???");
	if (BUF_REFCNTNB(bp) > 1) {
		/* Temporary panic to verify exclusive locking */
		/* This panic goes away when we allow shared refs */
		panic("brelse: multiple refs");
		/* NOT REACHED */
		return;
	}

	/*
	 * Figure out the correct queue to place the cleaned up buffer on.
	 * Buffers placed in the EMPTY or EMPTYKVA had better already be
	 * disassociated from their vnode.
	 *
	 * Return the buffer to its original pcpu area
	 */
	pcpu = &bufpcpu[bp->b_qcpu];
	spin_lock(&pcpu->spin);

	if (bp->b_flags & B_LOCKED) {
		/*
		 * Buffers that are locked are placed in the locked queue
		 * immediately, regardless of their state.
		 */
		bp->b_qindex = BQUEUE_LOCKED;
		TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
				  bp, b_freelist);
	} else if (bp->b_bufsize == 0) {
		/*
		 * Buffers with no memory.  Due to conditionals near the top
		 * of brelse() such buffers should probably already be
		 * marked B_INVAL and disassociated from their vnode.
		 */
		bp->b_flags |= B_INVAL;
		KASSERT(bp->b_vp == NULL,
			("bp1 %p flags %08x/%08x vnode %p "
			 "unexpectededly still associated!",
			bp, saved_flags, bp->b_flags, bp->b_vp));
		KKASSERT((bp->b_flags & B_HASHED) == 0);
		if (bp->b_kvasize) {
			bp->b_qindex = BQUEUE_EMPTYKVA;
		} else {
			bp->b_qindex = BQUEUE_EMPTY;
		}
		TAILQ_INSERT_HEAD(&pcpu->bufqueues[bp->b_qindex],
				  bp, b_freelist);
	} else if (bp->b_flags & (B_INVAL | B_NOCACHE | B_RELBUF)) {
		/*
		 * Buffers with junk contents.   Again these buffers had better
		 * already be disassociated from their vnode.
		 */
		KASSERT(bp->b_vp == NULL,
			("bp2 %p flags %08x/%08x vnode %p unexpectededly "
			 "still associated!",
			bp, saved_flags, bp->b_flags, bp->b_vp));
		KKASSERT((bp->b_flags & B_HASHED) == 0);
		bp->b_flags |= B_INVAL;
		bp->b_qindex = BQUEUE_CLEAN;
		TAILQ_INSERT_HEAD(&pcpu->bufqueues[bp->b_qindex],
				  bp, b_freelist);
	} else {
		/*
		 * Remaining buffers.  These buffers are still associated with
		 * their vnode.
		 */
		switch(bp->b_flags & (B_DELWRI|B_HEAVY)) {
		case B_DELWRI:
			bp->b_qindex = BQUEUE_DIRTY;
			TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
					  bp, b_freelist);
			break;
		case B_DELWRI | B_HEAVY:
			bp->b_qindex = BQUEUE_DIRTY_HW;
			TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
					  bp, b_freelist);
			break;
		default:
			/*
			 * NOTE: Buffers are always placed at the end of the
			 * queue.  If B_AGE is not set the buffer will cycle
			 * through the queue twice.
			 */
			bp->b_qindex = BQUEUE_CLEAN;
			TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
					  bp, b_freelist);
			break;
		}
	}
	spin_unlock(&pcpu->spin);

	/*
	 * If B_INVAL, clear B_DELWRI.  We've already placed the buffer
	 * on the correct queue but we have not yet unlocked it.
	 */
	if ((bp->b_flags & (B_INVAL|B_DELWRI)) == (B_INVAL|B_DELWRI))
		bundirty(bp);

	/*
	 * The bp is on an appropriate queue unless locked.  If it is not
	 * locked or dirty we can wakeup threads waiting for buffer space.
	 *
	 * We've already handled the B_INVAL case ( B_DELWRI will be clear
	 * if B_INVAL is set ).
	 */
	if ((bp->b_flags & (B_LOCKED|B_DELWRI)) == 0)
		bufcountwakeup();

	/*
	 * Something we can maybe free or reuse
	 */
	if (bp->b_bufsize || bp->b_kvasize)
		bufspacewakeup();

	/*
	 * Clean up temporary flags and unlock the buffer.
	 */
	bp->b_flags &= ~(B_ORDERED | B_NOCACHE | B_RELBUF | B_DIRECT);
	BUF_UNLOCK(bp);
}

/*
 * bqrelse:
 *
 *	Release a buffer back to the appropriate queue but do not try to free
 *	it.  The buffer is expected to be used again soon.
 *
 *	bqrelse() is used by bdwrite() to requeue a delayed write, and used by
 *	biodone() to requeue an async I/O on completion.  It is also used when
 *	known good buffers need to be requeued but we think we may need the data
 *	again soon.
 *
 *	XXX we should be able to leave the B_RELBUF hint set on completion.
 */
void
bqrelse(struct buf *bp)
{
	struct bufpcpu *pcpu;

	KASSERT(!(bp->b_flags & (B_CLUSTER|B_PAGING)),
		("bqrelse: inappropriate B_PAGING or B_CLUSTER bp %p", bp));

	if (bp->b_qindex != BQUEUE_NONE)
		panic("bqrelse: free buffer onto another queue???");
	if (BUF_REFCNTNB(bp) > 1) {
		/* do not release to free list */
		panic("bqrelse: multiple refs");
		return;
	}

	buf_act_advance(bp);

	pcpu = &bufpcpu[bp->b_qcpu];
	spin_lock(&pcpu->spin);

	if (bp->b_flags & B_LOCKED) {
		/*
		 * Locked buffers are released to the locked queue.  However,
		 * if the buffer is dirty it will first go into the dirty
		 * queue and later on after the I/O completes successfully it
		 * will be released to the locked queue.
		 */
		bp->b_qindex = BQUEUE_LOCKED;
		TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
				  bp, b_freelist);
	} else if (bp->b_flags & B_DELWRI) {
		bp->b_qindex = (bp->b_flags & B_HEAVY) ?
			       BQUEUE_DIRTY_HW : BQUEUE_DIRTY;
		TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
				  bp, b_freelist);
	} else if (vm_page_count_min(0)) {
		/*
		 * We are too low on memory, we have to try to free the
		 * buffer (most importantly: the wired pages making up its
		 * backing store) *now*.
		 */
		spin_unlock(&pcpu->spin);
		brelse(bp);
		return;
	} else {
		bp->b_qindex = BQUEUE_CLEAN;
		TAILQ_INSERT_TAIL(&pcpu->bufqueues[bp->b_qindex],
				  bp, b_freelist);
	}
	spin_unlock(&pcpu->spin);

	/*
	 * We have now placed the buffer on the proper queue, but have yet
	 * to unlock it.
	 */
	if ((bp->b_flags & B_LOCKED) == 0 &&
	    ((bp->b_flags & B_INVAL) || (bp->b_flags & B_DELWRI) == 0)) {
		bufcountwakeup();
	}

	/*
	 * Something we can maybe free or reuse.
	 */
	if (bp->b_bufsize && !(bp->b_flags & B_DELWRI))
		bufspacewakeup();

	/*
	 * Final cleanup and unlock.  Clear bits that are only used while a
	 * buffer is actively locked.
	 */
	bp->b_flags &= ~(B_ORDERED | B_NOCACHE | B_RELBUF);
	dsched_exit_buf(bp);
	BUF_UNLOCK(bp);
}

/*
 * Hold a buffer, preventing it from being reused.  This will prevent
 * normal B_RELBUF operations on the buffer but will not prevent B_INVAL
 * operations.  If a B_INVAL operation occurs the buffer will remain held
 * but the underlying pages may get ripped out.
 *
 * These functions are typically used in VOP_READ/VOP_WRITE functions
 * to hold a buffer during a copyin or copyout, preventing deadlocks
 * or recursive lock panics when read()/write() is used over mmap()'d
 * space.
 *
 * NOTE: bqhold() requires that the buffer be locked at the time of the
 *	 hold.  bqdrop() has no requirements other than the buffer having
 *	 previously been held.
 */
void
bqhold(struct buf *bp)
{
	atomic_add_int(&bp->b_refs, 1);
}

void
bqdrop(struct buf *bp)
{
	KKASSERT(bp->b_refs > 0);
	atomic_add_int(&bp->b_refs, -1);
}

/*
 * Return backing pages held by the buffer 'bp' back to the VM system.
 * This routine is called when the bp is invalidated, released, or
 * reused.
 *
 * The KVA mapping (b_data) for the underlying pages is removed by
 * this function.
 *
 * WARNING! This routine is integral to the low memory critical path
 *          when a buffer is B_RELBUF'd.  If the system has a severe page
 *          deficit we need to get the page(s) onto the PQ_FREE or PQ_CACHE
 *          queues so they can be reused in the current pageout daemon
 *          pass.
 */
static void
vfs_vmio_release(struct buf *bp)
{
	int i;
	vm_page_t m;

	for (i = 0; i < bp->b_xio.xio_npages; i++) {
		m = bp->b_xio.xio_pages[i];
		bp->b_xio.xio_pages[i] = NULL;

		/*
		 * We need to own the page in order to safely unwire it.
		 */
		vm_page_busy_wait(m, FALSE, "vmiopg");

		/*
		 * The VFS is telling us this is not a meta-data buffer
		 * even if it is backed by a block device.
		 */
		if (bp->b_flags & B_NOTMETA)
			vm_page_flag_set(m, PG_NOTMETA);

		/*
		 * This is a very important bit of code.  We try to track
		 * VM page use whether the pages are wired into the buffer
		 * cache or not.  While wired into the buffer cache the
		 * bp tracks the act_count.
		 *
		 * We can choose to place unwired pages on the inactive
		 * queue (0) or active queue (1).  If we place too many
		 * on the active queue the queue will cycle the act_count
		 * on pages we'd like to keep, just from single-use pages
		 * (such as when doing a tar-up or file scan).
		 */
		if (bp->b_act_count < vm_cycle_point)
			vm_page_unwire(m, 0);
		else
			vm_page_unwire(m, 1);

		/*
		 * If the wire_count has dropped to 0 we may need to take
		 * further action before unbusying the page.
		 *
		 * WARNING: vm_page_try_*() also checks PG_NEED_COMMIT for us.
		 */
		if (m->wire_count == 0) {
			vm_page_flag_clear(m, PG_ZERO);

			if (bp->b_flags & B_DIRECT) {
				/*
				 * Attempt to free the page if B_DIRECT is
				 * set, the caller does not desire the page
				 * to be cached.
				 */
				vm_page_wakeup(m);
				vm_page_try_to_free(m);
			} else if ((bp->b_flags & B_NOTMETA) ||
				   vm_page_count_min(0)) {
				/*
				 * Attempt to move the page to PQ_CACHE
				 * if B_NOTMETA is set.  This flag is set
				 * by HAMMER to remove one of the two pages
				 * present when double buffering is enabled.
				 *
				 * Attempt to move the page to PQ_CACHE
				 * If we have a severe page deficit.  This
				 * will cause buffer cache operations related
				 * to pageouts to recycle the related pages
				 * in order to avoid a low memory deadlock.
				 */
				m->act_count = bp->b_act_count;
				vm_page_wakeup(m);
				vm_page_try_to_cache(m);
			} else {
				/*
				 * Nominal case, leave the page on the
				 * queue the original unwiring placed it on
				 * (active or inactive).
				 */
				m->act_count = bp->b_act_count;
				vm_page_wakeup(m);
			}
		} else {
			vm_page_wakeup(m);
		}
	}

	pmap_qremove(trunc_page((vm_offset_t) bp->b_data),
		     bp->b_xio.xio_npages);
	if (bp->b_bufsize) {
		bufspacewakeup();
		bp->b_bufsize = 0;
	}
	bp->b_xio.xio_npages = 0;
	bp->b_flags &= ~B_VMIO;
	KKASSERT (LIST_FIRST(&bp->b_dep) == NULL);
	if (bp->b_vp)
		brelvp(bp);
}

/*
 * Find and initialize a new buffer header, freeing up existing buffers
 * in the bufqueues as necessary.  The new buffer is returned locked.
 *
 * Important:  B_INVAL is not set.  If the caller wishes to throw the
 * buffer away, the caller must set B_INVAL prior to calling brelse().
 *
 * We block if:
 *	We have insufficient buffer headers
 *	We have insufficient buffer space
 *	buffer_map is too fragmented ( space reservation fails )
 *	If we have to flush dirty buffers ( but we try to avoid this )
 *
 * To avoid VFS layer recursion we do not flush dirty buffers ourselves.
 * Instead we ask the buf daemon to do it for us.  We attempt to
 * avoid piecemeal wakeups of the pageout daemon.
 */
struct buf *
getnewbuf(int blkflags, int slptimeo, int size, int maxsize)
{
	struct bufpcpu *pcpu;
	struct buf *bp;
	struct buf *nbp;
	int defrag = 0;
	int nqindex;
	int nqcpu;
	int slpflags = (blkflags & GETBLK_PCATCH) ? PCATCH : 0;
	static int flushingbufs;

	/*
	 * We can't afford to block since we might be holding a vnode lock,
	 * which may prevent system daemons from running.  We deal with
	 * low-memory situations by proactively returning memory and running
	 * async I/O rather then sync I/O.
	 */
	
	++getnewbufcalls;
	--getnewbufrestarts;
	nqcpu = mycpu->gd_cpuid;
restart:
	++getnewbufrestarts;

	/*
	 * Setup for scan.  If we do not have enough free buffers,
	 * we setup a degenerate case that immediately fails.  Note
	 * that if we are specially marked process, we are allowed to
	 * dip into our reserves.
	 *
	 * The scanning sequence is nominally:  EMPTY->EMPTYKVA->CLEAN
	 *
	 * We start with EMPTYKVA.  If the list is empty we backup to EMPTY.
	 * However, there are a number of cases (defragging, reusing, ...)
	 * where we cannot backup.
	 */
	pcpu = &bufpcpu[nqcpu];
	nqindex = BQUEUE_EMPTYKVA;
	spin_lock(&pcpu->spin);

	nbp = TAILQ_FIRST(&pcpu->bufqueues[BQUEUE_EMPTYKVA]);

	if (nbp == NULL) {
		/*
		 * If no EMPTYKVA buffers and we are either
		 * defragging or reusing, locate a CLEAN buffer
		 * to free or reuse.  If bufspace useage is low
		 * skip this step so we can allocate a new buffer.
		 */
		if (defrag || bufspace >= lobufspace) {
			nqindex = BQUEUE_CLEAN;
			nbp = TAILQ_FIRST(&pcpu->bufqueues[BQUEUE_CLEAN]);
		}

		/*
		 * If we could not find or were not allowed to reuse a
		 * CLEAN buffer, check to see if it is ok to use an EMPTY
		 * buffer.  We can only use an EMPTY buffer if allocating
		 * its KVA would not otherwise run us out of buffer space.
		 */
		if (nbp == NULL && defrag == 0 &&
		    bufspace + maxsize < hibufspace) {
			nqindex = BQUEUE_EMPTY;
			nbp = TAILQ_FIRST(&pcpu->bufqueues[BQUEUE_EMPTY]);
		}
	}

	/*
	 * Run scan, possibly freeing data and/or kva mappings on the fly
	 * depending.
	 *
	 * WARNING! spin is held!
	 */
	while ((bp = nbp) != NULL) {
		int qindex = nqindex;

		nbp = TAILQ_NEXT(bp, b_freelist);

		/*
		 * BQUEUE_CLEAN - B_AGE special case.  If not set the bp
		 * cycles through the queue twice before being selected.
		 */
		if (qindex == BQUEUE_CLEAN && 
		    (bp->b_flags & B_AGE) == 0 && nbp) {
			bp->b_flags |= B_AGE;
			TAILQ_REMOVE(&pcpu->bufqueues[qindex],
				     bp, b_freelist);
			TAILQ_INSERT_TAIL(&pcpu->bufqueues[qindex],
					  bp, b_freelist);
			continue;
		}

		/*
		 * Calculate next bp ( we can only use it if we do not block
		 * or do other fancy things ).
		 */
		if (nbp == NULL) {
			switch(qindex) {
			case BQUEUE_EMPTY:
				nqindex = BQUEUE_EMPTYKVA;
				if ((nbp = TAILQ_FIRST(&pcpu->bufqueues[BQUEUE_EMPTYKVA])))
					break;
				/* fall through */
			case BQUEUE_EMPTYKVA:
				nqindex = BQUEUE_CLEAN;
				if ((nbp = TAILQ_FIRST(&pcpu->bufqueues[BQUEUE_CLEAN])))
					break;
				/* fall through */
			case BQUEUE_CLEAN:
				/*
				 * nbp is NULL. 
				 */
				break;
			}
		}

		/*
		 * Sanity Checks
		 */
		KASSERT(bp->b_qindex == qindex,
			("getnewbuf: inconsistent queue %d bp %p", qindex, bp));

		/*
		 * Note: we no longer distinguish between VMIO and non-VMIO
		 * buffers.
		 */
		KASSERT((bp->b_flags & B_DELWRI) == 0,
			("delwri buffer %p found in queue %d", bp, qindex));

		/*
		 * Do not try to reuse a buffer with a non-zero b_refs.
		 * This is an unsynchronized test.  A synchronized test
		 * is also performed after we lock the buffer.
		 */
		if (bp->b_refs)
			continue;

		/*
		 * If we are defragging then we need a buffer with 
		 * b_kvasize != 0.  XXX this situation should no longer
		 * occur, if defrag is non-zero the buffer's b_kvasize
		 * should also be non-zero at this point.  XXX
		 */
		if (defrag && bp->b_kvasize == 0) {
			kprintf("Warning: defrag empty buffer %p\n", bp);
			continue;
		}

		/*
		 * Start freeing the bp.  This is somewhat involved.  nbp
		 * remains valid only for BQUEUE_EMPTY[KVA] bp's.  Buffers
		 * on the clean list must be disassociated from their 
		 * current vnode.  Buffers on the empty[kva] lists have
		 * already been disassociated.
		 *
		 * b_refs is checked after locking along with queue changes.
		 * We must check here to deal with zero->nonzero transitions
		 * made by the owner of the buffer lock, which is used by
		 * VFS's to hold the buffer while issuing an unlocked
		 * uiomove()s.  We cannot invalidate the buffer's pages
		 * for this case.  Once we successfully lock a buffer the
		 * only 0->1 transitions of b_refs will occur via findblk().
		 *
		 * We must also check for queue changes after successful
		 * locking as the current lock holder may dispose of the
		 * buffer and change its queue.
		 */
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT) != 0) {
			spin_unlock(&pcpu->spin);
			tsleep(&bd_request, 0, "gnbxxx", (hz + 99) / 100);
			goto restart;
		}
		if (bp->b_qindex != qindex || bp->b_refs) {
			spin_unlock(&pcpu->spin);
			BUF_UNLOCK(bp);
			goto restart;
		}
		bremfree_locked(bp);
		spin_unlock(&pcpu->spin);

		/*
		 * Dependancies must be handled before we disassociate the
		 * vnode.
		 *
		 * NOTE: HAMMER will set B_LOCKED if the buffer cannot
		 * be immediately disassociated.  HAMMER then becomes
		 * responsible for releasing the buffer.
		 *
		 * NOTE: spin is UNLOCKED now.
		 */
		if (LIST_FIRST(&bp->b_dep) != NULL) {
			buf_deallocate(bp);
			if (bp->b_flags & B_LOCKED) {
				bqrelse(bp);
				goto restart;
			}
			KKASSERT(LIST_FIRST(&bp->b_dep) == NULL);
		}

		if (qindex == BQUEUE_CLEAN) {
			if (bp->b_flags & B_VMIO)
				vfs_vmio_release(bp);
			if (bp->b_vp)
				brelvp(bp);
		}

		/*
		 * NOTE:  nbp is now entirely invalid.  We can only restart
		 * the scan from this point on.
		 *
		 * Get the rest of the buffer freed up.  b_kva* is still
		 * valid after this operation.
		 */
		KASSERT(bp->b_vp == NULL,
			("bp3 %p flags %08x vnode %p qindex %d "
			 "unexpectededly still associated!",
			 bp, bp->b_flags, bp->b_vp, qindex));
		KKASSERT((bp->b_flags & B_HASHED) == 0);

		/*
		 * critical section protection is not required when
		 * scrapping a buffer's contents because it is already 
		 * wired.
		 */
		if (bp->b_bufsize)
			allocbuf(bp, 0);

		bp->b_flags = B_BNOCLIP;
		bp->b_cmd = BUF_CMD_DONE;
		bp->b_vp = NULL;
		bp->b_error = 0;
		bp->b_resid = 0;
		bp->b_bcount = 0;
		bp->b_xio.xio_npages = 0;
		bp->b_dirtyoff = bp->b_dirtyend = 0;
		bp->b_act_count = ACT_INIT;
		reinitbufbio(bp);
		KKASSERT(LIST_FIRST(&bp->b_dep) == NULL);
		buf_dep_init(bp);
		if (blkflags & GETBLK_BHEAVY)
			bp->b_flags |= B_HEAVY;

		/*
		 * If we are defragging then free the buffer.
		 */
		if (defrag) {
			bp->b_flags |= B_INVAL;
			bfreekva(bp);
			brelse(bp);
			defrag = 0;
			goto restart;
		}

		/*
		 * If we are overcomitted then recover the buffer and its
		 * KVM space.  This occurs in rare situations when multiple
		 * processes are blocked in getnewbuf() or allocbuf().
		 *
		 * (We don't have to recover the KVM space if
		 *  BKVASIZE == MAXBSIZE)
		 */
		if (bufspace >= hibufspace)
			flushingbufs = 1;
		if (flushingbufs && bp->b_kvasize != 0) {
			bp->b_flags |= B_INVAL;
			if (BKVASIZE != MAXBSIZE)
				bfreekva(bp);
			brelse(bp);
			goto restart;
		}
		if (bufspace < lobufspace)
			flushingbufs = 0;

		/*
		 * b_refs can transition to a non-zero value while we hold
		 * the buffer locked due to a findblk().  Our brelvp() above
		 * interlocked any future possible transitions due to
		 * findblk()s.
		 *
		 * If we find b_refs to be non-zero we can destroy the
		 * buffer's contents but we cannot yet reuse the buffer.
		 */
		if (bp->b_refs) {
			bp->b_flags |= B_INVAL;
			if (BKVASIZE != MAXBSIZE)
				bfreekva(bp);
			brelse(bp);
			goto restart;
		}
		break;
		/* NOT REACHED, spin not held */
	}

	/*
	 * If we exhausted our list, iterate other cpus.  If that fails,
	 * sleep as appropriate.  We may have to wakeup various daemons
	 * and write out some dirty buffers.
	 *
	 * Generally we are sleeping due to insufficient buffer space.
	 *
	 * NOTE: spin is held if bp is NULL, else it is not held.
	 */
	if (bp == NULL) {
		int flags;
		char *waitmsg;

		spin_unlock(&pcpu->spin);

		nqcpu = (nqcpu + 1) % ncpus;
		if (nqcpu != mycpu->gd_cpuid)
			goto restart;

		if (defrag) {
			flags = VFS_BIO_NEED_BUFSPACE;
			waitmsg = "nbufkv";
		} else if (bufspace >= hibufspace) {
			waitmsg = "nbufbs";
			flags = VFS_BIO_NEED_BUFSPACE;
		} else {
			waitmsg = "newbuf";
			flags = VFS_BIO_NEED_ANY;
		}

		bd_speedup();	/* heeeelp */
		atomic_set_int(&needsbuffer, flags);
		while (needsbuffer & flags) {
			int value;

			tsleep_interlock(&needsbuffer, 0);
			value = atomic_fetchadd_int(&needsbuffer, 0);
			if (value & flags) {
				if (tsleep(&needsbuffer, PINTERLOCKED|slpflags,
					   waitmsg, slptimeo)) {
					return (NULL);
				}
			}
		}
	} else {
		/*
		 * We finally have a valid bp.  We aren't quite out of the
		 * woods, we still have to reserve kva space.  In order
		 * to keep fragmentation sane we only allocate kva in
		 * BKVASIZE chunks.
		 *
		 * (spin is not held)
		 */
		maxsize = (maxsize + BKVAMASK) & ~BKVAMASK;

		if (maxsize != bp->b_kvasize) {
			vm_offset_t addr = 0;
			int count;

			bfreekva(bp);

			count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
			vm_map_lock(&buffer_map);

			if (vm_map_findspace(&buffer_map,
				    vm_map_min(&buffer_map), maxsize,
				    maxsize, 0, &addr)) {
				/*
				 * Uh oh.  Buffer map is too fragmented.  We
				 * must defragment the map.
				 */
				vm_map_unlock(&buffer_map);
				vm_map_entry_release(count);
				++bufdefragcnt;
				defrag = 1;
				bp->b_flags |= B_INVAL;
				brelse(bp);
				goto restart;
			}
			if (addr) {
				vm_map_insert(&buffer_map, &count,
					NULL, 0,
					addr, addr + maxsize,
					VM_MAPTYPE_NORMAL,
					VM_PROT_ALL, VM_PROT_ALL,
					MAP_NOFAULT);

				bp->b_kvabase = (caddr_t) addr;
				bp->b_kvasize = maxsize;
				bufspace += bp->b_kvasize;
				++bufreusecnt;
			}
			vm_map_unlock(&buffer_map);
			vm_map_entry_release(count);
		}
		bp->b_data = bp->b_kvabase;
	}
	return(bp);
}

/*
 * buf_daemon:
 *
 *	Buffer flushing daemon.  Buffers are normally flushed by the
 *	update daemon but if it cannot keep up this process starts to
 *	take the load in an attempt to prevent getnewbuf() from blocking.
 *
 *	Once a flush is initiated it does not stop until the number
 *	of buffers falls below lodirtybuffers, but we will wake up anyone
 *	waiting at the mid-point.
 */
static struct kproc_desc buf_kp = {
	"bufdaemon",
	buf_daemon,
	&bufdaemon_td
};
SYSINIT(bufdaemon, SI_SUB_KTHREAD_BUF, SI_ORDER_FIRST,
	kproc_start, &buf_kp)

static struct kproc_desc bufhw_kp = {
	"bufdaemon_hw",
	buf_daemon_hw,
	&bufdaemonhw_td
};
SYSINIT(bufdaemon_hw, SI_SUB_KTHREAD_BUF, SI_ORDER_FIRST,
	kproc_start, &bufhw_kp)

static void
buf_daemon1(struct thread *td, int queue, int (*buf_limit_fn)(long), 
	    int *bd_req)
{
	long limit;
	struct buf *marker;

	marker = kmalloc(sizeof(*marker), M_BIOBUF, M_WAITOK | M_ZERO);
	marker->b_flags |= B_MARKER;
	marker->b_qindex = BQUEUE_NONE;
	marker->b_qcpu = 0;

	/*
	 * This process needs to be suspended prior to shutdown sync.
	 */
	EVENTHANDLER_REGISTER(shutdown_pre_sync, shutdown_kproc,
			      td, SHUTDOWN_PRI_LAST);
	curthread->td_flags |= TDF_SYSTHREAD;

	/*
	 * This process is allowed to take the buffer cache to the limit
	 */
	for (;;) {
		kproc_suspend_loop();

		/*
		 * Do the flush as long as the number of dirty buffers
		 * (including those running) exceeds lodirtybufspace.
		 *
		 * When flushing limit running I/O to hirunningspace
		 * Do the flush.  Limit the amount of in-transit I/O we
		 * allow to build up, otherwise we would completely saturate
		 * the I/O system.  Wakeup any waiting processes before we
		 * normally would so they can run in parallel with our drain.
		 *
		 * Our aggregate normal+HW lo water mark is lodirtybufspace,
		 * but because we split the operation into two threads we
		 * have to cut it in half for each thread.
		 */
		waitrunningbufspace();
		limit = lodirtybufspace / 2;
		while (buf_limit_fn(limit)) {
			if (flushbufqueues(marker, queue) == 0)
				break;
			if (runningbufspace < hirunningspace)
				continue;
			waitrunningbufspace();
		}

		/*
		 * We reached our low water mark, reset the
		 * request and sleep until we are needed again.
		 * The sleep is just so the suspend code works.
		 */
		if (*bd_req == 0)
			tsleep(bd_req, 0, "psleep", hz);
		atomic_clear_int(bd_req, -1);
	}
	/* NOT REACHED */
	/*kfree(marker, M_BIOBUF);*/
}

static int
buf_daemon_limit(long limit)
{
	return (runningbufspace + dirtykvaspace > limit ||
		dirtybufcount - dirtybufcounthw >= nbuf / 2);
}

static int
buf_daemon_hw_limit(long limit)
{
	return (runningbufspace + dirtykvaspace > limit ||
		dirtybufcounthw >= nbuf / 2);
}

static void
buf_daemon(void)
{
	buf_daemon1(bufdaemon_td, BQUEUE_DIRTY, buf_daemon_limit, 
		    &bd_request);
}

static void
buf_daemon_hw(void)
{
	buf_daemon1(bufdaemonhw_td, BQUEUE_DIRTY_HW, buf_daemon_hw_limit,
		    &bd_request_hw);
}

/*
 * flushbufqueues:
 *
 *	Try to flush a buffer in the dirty queue.  We must be careful to
 *	free up B_INVAL buffers instead of write them, which NFS is 
 *	particularly sensitive to.
 *
 *	B_RELBUF may only be set by VFSs.  We do set B_AGE to indicate
 *	that we really want to try to get the buffer out and reuse it
 *	due to the write load on the machine.
 *
 *	We must lock the buffer in order to check its validity before we
 *	can mess with its contents.  spin isn't enough.
 */
static int
flushbufqueues(struct buf *marker, bufq_type_t q)
{
	struct bufpcpu *pcpu;
	struct buf *bp;
	int r = 0;
	int lcpu = marker->b_qcpu;

	KKASSERT(marker->b_qindex == BQUEUE_NONE);
	KKASSERT(marker->b_flags & B_MARKER);

again:
	/*
	 * Spinlock needed to perform operations on the queue and may be
	 * held through a non-blocking BUF_LOCK(), but cannot be held when
	 * BUF_UNLOCK()ing or through any other major operation.
	 */
	pcpu = &bufpcpu[marker->b_qcpu];
	spin_lock(&pcpu->spin);
	marker->b_qindex = q;
	TAILQ_INSERT_HEAD(&pcpu->bufqueues[q], marker, b_freelist);
	bp = marker;

	while ((bp = TAILQ_NEXT(bp, b_freelist)) != NULL) {
		/*
		 * NOTE: spinlock is always held at the top of the loop
		 */
		if (bp->b_flags & B_MARKER)
			continue;
		if ((bp->b_flags & B_DELWRI) == 0) {
			kprintf("Unexpected clean buffer %p\n", bp);
			continue;
		}
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT))
			continue;
		KKASSERT(bp->b_qcpu == marker->b_qcpu && bp->b_qindex == q);

		/*
		 * Once the buffer is locked we will have no choice but to
		 * unlock the spinlock around a later BUF_UNLOCK and re-set
		 * bp = marker when looping.  Move the marker now to make
		 * things easier.
		 */
		TAILQ_REMOVE(&pcpu->bufqueues[q], marker, b_freelist);
		TAILQ_INSERT_AFTER(&pcpu->bufqueues[q], bp, marker, b_freelist);

		/*
		 * Must recheck B_DELWRI after successfully locking
		 * the buffer.
		 */
		if ((bp->b_flags & B_DELWRI) == 0) {
			spin_unlock(&pcpu->spin);
			BUF_UNLOCK(bp);
			spin_lock(&pcpu->spin);
			bp = marker;
			continue;
		}

		/*
		 * Remove the buffer from its queue.  We still own the
		 * spinlock here.
		 */
		_bremfree(bp);

		/*
		 * Disposing of an invalid buffer counts as a flush op
		 */
		if (bp->b_flags & B_INVAL) {
			spin_unlock(&pcpu->spin);
			brelse(bp);
			spin_lock(&pcpu->spin);
			++r;
			break;
		}

		/*
		 * Release the spinlock for the more complex ops we
		 * are now going to do.
		 */
		spin_unlock(&pcpu->spin);
		lwkt_yield();

		/*
		 * This is a bit messy
		 */
		if (LIST_FIRST(&bp->b_dep) != NULL &&
		    (bp->b_flags & B_DEFERRED) == 0 &&
		    buf_countdeps(bp, 0)) {
			spin_lock(&pcpu->spin);
			TAILQ_INSERT_TAIL(&pcpu->bufqueues[q], bp, b_freelist);
			bp->b_qindex = q;
			bp->b_flags |= B_DEFERRED;
			spin_unlock(&pcpu->spin);
			BUF_UNLOCK(bp);
			spin_lock(&pcpu->spin);
			bp = marker;
			continue;
		}

		/*
		 * spinlock not held here.
		 *
		 * If the buffer has a dependancy, buf_checkwrite() must
		 * also return 0 for us to be able to initate the write.
		 *
		 * If the buffer is flagged B_ERROR it may be requeued
		 * over and over again, we try to avoid a live lock.
		 */
		if (LIST_FIRST(&bp->b_dep) != NULL && buf_checkwrite(bp)) {
			brelse(bp);
		} else if (bp->b_flags & B_ERROR) {
			tsleep(bp, 0, "bioer", 1);
			bp->b_flags &= ~B_AGE;
			cluster_awrite(bp);
		} else {
			bp->b_flags |= B_AGE;
			cluster_awrite(bp);
		}
		spin_lock(&pcpu->spin);
		++r;
		break;
	}

	TAILQ_REMOVE(&pcpu->bufqueues[q], marker, b_freelist);
	marker->b_qindex = BQUEUE_NONE;
	spin_unlock(&pcpu->spin);

	/*
	 * Advance the marker to be fair.
	 */
	marker->b_qcpu = (marker->b_qcpu + 1) % ncpus;
	if (bp == NULL) {
		if (marker->b_qcpu != lcpu)
			goto again;
	}

	return (r);
}

/*
 * inmem:
 *
 *	Returns true if no I/O is needed to access the associated VM object.
 *	This is like findblk except it also hunts around in the VM system for
 *	the data.
 *
 *	Note that we ignore vm_page_free() races from interrupts against our
 *	lookup, since if the caller is not protected our return value will not
 *	be any more valid then otherwise once we exit the critical section.
 */
int
inmem(struct vnode *vp, off_t loffset)
{
	vm_object_t obj;
	vm_offset_t toff, tinc, size;
	vm_page_t m;
	int res = 1;

	if (findblk(vp, loffset, FINDBLK_TEST))
		return 1;
	if (vp->v_mount == NULL)
		return 0;
	if ((obj = vp->v_object) == NULL)
		return 0;

	size = PAGE_SIZE;
	if (size > vp->v_mount->mnt_stat.f_iosize)
		size = vp->v_mount->mnt_stat.f_iosize;

	vm_object_hold(obj);
	for (toff = 0; toff < vp->v_mount->mnt_stat.f_iosize; toff += tinc) {
		m = vm_page_lookup(obj, OFF_TO_IDX(loffset + toff));
		if (m == NULL) {
			res = 0;
			break;
		}
		tinc = size;
		if (tinc > PAGE_SIZE - ((toff + loffset) & PAGE_MASK))
			tinc = PAGE_SIZE - ((toff + loffset) & PAGE_MASK);
		if (vm_page_is_valid(m,
		    (vm_offset_t) ((toff + loffset) & PAGE_MASK), tinc) == 0) {
			res = 0;
			break;
		}
	}
	vm_object_drop(obj);
	return (res);
}

/*
 * findblk:
 *
 *	Locate and return the specified buffer.  Unless flagged otherwise,
 *	a locked buffer will be returned if it exists or NULL if it does not.
 *
 *	findblk()'d buffers are still on the bufqueues and if you intend
 *	to use your (locked NON-TEST) buffer you need to bremfree(bp)
 *	and possibly do other stuff to it.
 *
 *	FINDBLK_TEST	- Do not lock the buffer.  The caller is responsible
 *			  for locking the buffer and ensuring that it remains
 *			  the desired buffer after locking.
 *
 *	FINDBLK_NBLOCK	- Lock the buffer non-blocking.  If we are unable
 *			  to acquire the lock we return NULL, even if the
 *			  buffer exists.
 *
 *	FINDBLK_REF	- Returns the buffer ref'd, which prevents normal
 *			  reuse by getnewbuf() but does not prevent
 *			  disassociation (B_INVAL).  Used to avoid deadlocks
 *			  against random (vp,loffset)s due to reassignment.
 *
 *	(0)		- Lock the buffer blocking.
 */
struct buf *
findblk(struct vnode *vp, off_t loffset, int flags)
{
	struct buf *bp;
	int lkflags;

	lkflags = LK_EXCLUSIVE;
	if (flags & FINDBLK_NBLOCK)
		lkflags |= LK_NOWAIT;

	for (;;) {
		/*
		 * Lookup.  Ref the buf while holding v_token to prevent
		 * reuse (but does not prevent diassociation).
		 */
		lwkt_gettoken_shared(&vp->v_token);
		bp = buf_rb_hash_RB_LOOKUP(&vp->v_rbhash_tree, loffset);
		if (bp == NULL) {
			lwkt_reltoken(&vp->v_token);
			return(NULL);
		}
		bqhold(bp);
		lwkt_reltoken(&vp->v_token);

		/*
		 * If testing only break and return bp, do not lock.
		 */
		if (flags & FINDBLK_TEST)
			break;

		/*
		 * Lock the buffer, return an error if the lock fails.
		 * (only FINDBLK_NBLOCK can cause the lock to fail).
		 */
		if (BUF_LOCK(bp, lkflags)) {
			atomic_subtract_int(&bp->b_refs, 1);
			/* bp = NULL; not needed */
			return(NULL);
		}

		/*
		 * Revalidate the locked buf before allowing it to be
		 * returned.
		 */
		if (bp->b_vp == vp && bp->b_loffset == loffset)
			break;
		atomic_subtract_int(&bp->b_refs, 1);
		BUF_UNLOCK(bp);
	}

	/*
	 * Success
	 */
	if ((flags & FINDBLK_REF) == 0)
		atomic_subtract_int(&bp->b_refs, 1);
	return(bp);
}

/*
 * getcacheblk:
 *
 *	Similar to getblk() except only returns the buffer if it is
 *	B_CACHE and requires no other manipulation.  Otherwise NULL
 *	is returned.
 *
 *	If B_RAM is set the buffer might be just fine, but we return
 *	NULL anyway because we want the code to fall through to the
 *	cluster read.  Otherwise read-ahead breaks.
 *
 *	If blksize is 0 the buffer cache buffer must already be fully
 *	cached.
 *
 *	If blksize is non-zero getblk() will be used, allowing a buffer
 *	to be reinstantiated from its VM backing store.  The buffer must
 *	still be fully cached after reinstantiation to be returned.
 */
struct buf *
getcacheblk(struct vnode *vp, off_t loffset, int blksize, int blkflags)
{
	struct buf *bp;
	int fndflags = (blkflags & GETBLK_NOWAIT) ? FINDBLK_NBLOCK : 0;

	if (blksize) {
		bp = getblk(vp, loffset, blksize, blkflags, 0);
		if (bp) {
			if ((bp->b_flags & (B_INVAL | B_CACHE | B_RAM)) ==
			    B_CACHE) {
				bp->b_flags &= ~B_AGE;
			} else {
				brelse(bp);
				bp = NULL;
			}
		}
	} else {
		bp = findblk(vp, loffset, fndflags);
		if (bp) {
			if ((bp->b_flags & (B_INVAL | B_CACHE | B_RAM)) ==
			    B_CACHE) {
				bp->b_flags &= ~B_AGE;
				bremfree(bp);
			} else {
				BUF_UNLOCK(bp);
				bp = NULL;
			}
		}
	}
	return (bp);
}

/*
 * getblk:
 *
 *	Get a block given a specified block and offset into a file/device.
 * 	B_INVAL may or may not be set on return.  The caller should clear
 *	B_INVAL prior to initiating a READ.
 *
 *	IT IS IMPORTANT TO UNDERSTAND THAT IF YOU CALL GETBLK() AND B_CACHE
 *	IS NOT SET, YOU MUST INITIALIZE THE RETURNED BUFFER, ISSUE A READ,
 *	OR SET B_INVAL BEFORE RETIRING IT.  If you retire a getblk'd buffer
 *	without doing any of those things the system will likely believe
 *	the buffer to be valid (especially if it is not B_VMIO), and the
 *	next getblk() will return the buffer with B_CACHE set.
 *
 *	For a non-VMIO buffer, B_CACHE is set to the opposite of B_INVAL for
 *	an existing buffer.
 *
 *	For a VMIO buffer, B_CACHE is modified according to the backing VM.
 *	If getblk()ing a previously 0-sized invalid buffer, B_CACHE is set
 *	and then cleared based on the backing VM.  If the previous buffer is
 *	non-0-sized but invalid, B_CACHE will be cleared.
 *
 *	If getblk() must create a new buffer, the new buffer is returned with
 *	both B_INVAL and B_CACHE clear unless it is a VMIO buffer, in which
 *	case it is returned with B_INVAL clear and B_CACHE set based on the
 *	backing VM.
 *
 *	getblk() also forces a bwrite() for any B_DELWRI buffer whos
 *	B_CACHE bit is clear.
 *	
 *	What this means, basically, is that the caller should use B_CACHE to
 *	determine whether the buffer is fully valid or not and should clear
 *	B_INVAL prior to issuing a read.  If the caller intends to validate
 *	the buffer by loading its data area with something, the caller needs
 *	to clear B_INVAL.  If the caller does this without issuing an I/O, 
 *	the caller should set B_CACHE ( as an optimization ), else the caller
 *	should issue the I/O and biodone() will set B_CACHE if the I/O was
 *	a write attempt or if it was a successfull read.  If the caller 
 *	intends to issue a READ, the caller must clear B_INVAL and B_ERROR
 *	prior to issuing the READ.  biodone() will *not* clear B_INVAL.
 *
 *	getblk flags:
 *
 *	GETBLK_PCATCH - catch signal if blocked, can cause NULL return
 *	GETBLK_BHEAVY - heavy-weight buffer cache buffer
 */
struct buf *
getblk(struct vnode *vp, off_t loffset, int size, int blkflags, int slptimeo)
{
	struct buf *bp;
	int slpflags = (blkflags & GETBLK_PCATCH) ? PCATCH : 0;
	int error;
	int lkflags;

	if (size > MAXBSIZE)
		panic("getblk: size(%d) > MAXBSIZE(%d)", size, MAXBSIZE);
	if (vp->v_object == NULL)
		panic("getblk: vnode %p has no object!", vp);

loop:
	if ((bp = findblk(vp, loffset, FINDBLK_REF | FINDBLK_TEST)) != NULL) {
		/*
		 * The buffer was found in the cache, but we need to lock it.
		 * We must acquire a ref on the bp to prevent reuse, but
		 * this will not prevent disassociation (brelvp()) so we
		 * must recheck (vp,loffset) after acquiring the lock.
		 *
		 * Without the ref the buffer could potentially be reused
		 * before we acquire the lock and create a deadlock
		 * situation between the thread trying to reuse the buffer
		 * and us due to the fact that we would wind up blocking
		 * on a random (vp,loffset).
		 */
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
			if (blkflags & GETBLK_NOWAIT) {
				bqdrop(bp);
				return(NULL);
			}
			lkflags = LK_EXCLUSIVE | LK_SLEEPFAIL;
			if (blkflags & GETBLK_PCATCH)
				lkflags |= LK_PCATCH;
			error = BUF_TIMELOCK(bp, lkflags, "getblk", slptimeo);
			if (error) {
				bqdrop(bp);
				if (error == ENOLCK)
					goto loop;
				return (NULL);
			}
			/* buffer may have changed on us */
		}
		bqdrop(bp);

		/*
		 * Once the buffer has been locked, make sure we didn't race
		 * a buffer recyclement.  Buffers that are no longer hashed
		 * will have b_vp == NULL, so this takes care of that check
		 * as well.
		 */
		if (bp->b_vp != vp || bp->b_loffset != loffset) {
			kprintf("Warning buffer %p (vp %p loffset %lld) "
				"was recycled\n",
				bp, vp, (long long)loffset);
			BUF_UNLOCK(bp);
			goto loop;
		}

		/*
		 * If SZMATCH any pre-existing buffer must be of the requested
		 * size or NULL is returned.  The caller absolutely does not
		 * want getblk() to bwrite() the buffer on a size mismatch.
		 */
		if ((blkflags & GETBLK_SZMATCH) && size != bp->b_bcount) {
			BUF_UNLOCK(bp);
			return(NULL);
		}

		/*
		 * All vnode-based buffers must be backed by a VM object.
		 */
		KKASSERT(bp->b_flags & B_VMIO);
		KKASSERT(bp->b_cmd == BUF_CMD_DONE);
		bp->b_flags &= ~B_AGE;

		/*
		 * Make sure that B_INVAL buffers do not have a cached
		 * block number translation.
		 */
		if ((bp->b_flags & B_INVAL) && (bp->b_bio2.bio_offset != NOOFFSET)) {
			kprintf("Warning invalid buffer %p (vp %p loffset %lld)"
				" did not have cleared bio_offset cache\n",
				bp, vp, (long long)loffset);
			clearbiocache(&bp->b_bio2);
		}

		/*
		 * The buffer is locked.  B_CACHE is cleared if the buffer is 
		 * invalid.
		 */
		if (bp->b_flags & B_INVAL)
			bp->b_flags &= ~B_CACHE;
		bremfree(bp);

		/*
		 * Any size inconsistancy with a dirty buffer or a buffer
		 * with a softupdates dependancy must be resolved.  Resizing
		 * the buffer in such circumstances can lead to problems.
		 *
		 * Dirty or dependant buffers are written synchronously.
		 * Other types of buffers are simply released and
		 * reconstituted as they may be backed by valid, dirty VM
		 * pages (but not marked B_DELWRI).
		 *
		 * NFS NOTE: NFS buffers which straddle EOF are oddly-sized
		 * and may be left over from a prior truncation (and thus
		 * no longer represent the actual EOF point), so we
		 * definitely do not want to B_NOCACHE the backing store.
		 */
		if (size != bp->b_bcount) {
			if (bp->b_flags & B_DELWRI) {
				bp->b_flags |= B_RELBUF;
				bwrite(bp);
			} else if (LIST_FIRST(&bp->b_dep)) {
				bp->b_flags |= B_RELBUF;
				bwrite(bp);
			} else {
				bp->b_flags |= B_RELBUF;
				brelse(bp);
			}
			goto loop;
		}
		KKASSERT(size <= bp->b_kvasize);
		KASSERT(bp->b_loffset != NOOFFSET, 
			("getblk: no buffer offset"));

		/*
		 * A buffer with B_DELWRI set and B_CACHE clear must
		 * be committed before we can return the buffer in
		 * order to prevent the caller from issuing a read
		 * ( due to B_CACHE not being set ) and overwriting
		 * it.
		 *
		 * Most callers, including NFS and FFS, need this to
		 * operate properly either because they assume they
		 * can issue a read if B_CACHE is not set, or because
		 * ( for example ) an uncached B_DELWRI might loop due 
		 * to softupdates re-dirtying the buffer.  In the latter
		 * case, B_CACHE is set after the first write completes,
		 * preventing further loops.
		 *
		 * NOTE!  b*write() sets B_CACHE.  If we cleared B_CACHE
		 * above while extending the buffer, we cannot allow the
		 * buffer to remain with B_CACHE set after the write
		 * completes or it will represent a corrupt state.  To
		 * deal with this we set B_NOCACHE to scrap the buffer
		 * after the write.
		 *
		 * XXX Should this be B_RELBUF instead of B_NOCACHE?
		 *     I'm not even sure this state is still possible
		 *     now that getblk() writes out any dirty buffers
		 *     on size changes.
		 *
		 * We might be able to do something fancy, like setting
		 * B_CACHE in bwrite() except if B_DELWRI is already set,
		 * so the below call doesn't set B_CACHE, but that gets real
		 * confusing.  This is much easier.
		 */

		if ((bp->b_flags & (B_CACHE|B_DELWRI)) == B_DELWRI) {
			kprintf("getblk: Warning, bp %p loff=%jx DELWRI set "
				"and CACHE clear, b_flags %08x\n",
				bp, (uintmax_t)bp->b_loffset, bp->b_flags);
			bp->b_flags |= B_NOCACHE;
			bwrite(bp);
			goto loop;
		}
	} else {
		/*
		 * Buffer is not in-core, create new buffer.  The buffer
		 * returned by getnewbuf() is locked.  Note that the returned
		 * buffer is also considered valid (not marked B_INVAL).
		 *
		 * Calculating the offset for the I/O requires figuring out
		 * the block size.  We use DEV_BSIZE for VBLK or VCHR and
		 * the mount's f_iosize otherwise.  If the vnode does not
		 * have an associated mount we assume that the passed size is 
		 * the block size.  
		 *
		 * Note that vn_isdisk() cannot be used here since it may
		 * return a failure for numerous reasons.   Note that the
		 * buffer size may be larger then the block size (the caller
		 * will use block numbers with the proper multiple).  Beware
		 * of using any v_* fields which are part of unions.  In
		 * particular, in DragonFly the mount point overloading 
		 * mechanism uses the namecache only and the underlying
		 * directory vnode is not a special case.
		 */
		int bsize, maxsize;

		if (vp->v_type == VBLK || vp->v_type == VCHR)
			bsize = DEV_BSIZE;
		else if (vp->v_mount)
			bsize = vp->v_mount->mnt_stat.f_iosize;
		else
			bsize = size;

		maxsize = size + (loffset & PAGE_MASK);
		maxsize = imax(maxsize, bsize);

		bp = getnewbuf(blkflags, slptimeo, size, maxsize);
		if (bp == NULL) {
			if (slpflags || slptimeo)
				return NULL;
			goto loop;
		}

		/*
		 * Atomically insert the buffer into the hash, so that it can
		 * be found by findblk().
		 *
		 * If bgetvp() returns non-zero a collision occured, and the
		 * bp will not be associated with the vnode.
		 *
		 * Make sure the translation layer has been cleared.
		 */
		bp->b_loffset = loffset;
		bp->b_bio2.bio_offset = NOOFFSET;
		/* bp->b_bio2.bio_next = NULL; */

		if (bgetvp(vp, bp, size)) {
			bp->b_flags |= B_INVAL;
			brelse(bp);
			goto loop;
		}

		/*
		 * All vnode-based buffers must be backed by a VM object.
		 */
		KKASSERT(vp->v_object != NULL);
		bp->b_flags |= B_VMIO;
		KKASSERT(bp->b_cmd == BUF_CMD_DONE);

		allocbuf(bp, size);
	}
	KKASSERT(dsched_is_clear_buf_priv(bp));
	return (bp);
}

/*
 * regetblk(bp)
 *
 * Reacquire a buffer that was previously released to the locked queue,
 * or reacquire a buffer which is interlocked by having bioops->io_deallocate
 * set B_LOCKED (which handles the acquisition race).
 *
 * To this end, either B_LOCKED must be set or the dependancy list must be
 * non-empty.
 */
void
regetblk(struct buf *bp)
{
	KKASSERT((bp->b_flags & B_LOCKED) || LIST_FIRST(&bp->b_dep) != NULL);
	BUF_LOCK(bp, LK_EXCLUSIVE | LK_RETRY);
	bremfree(bp);
}

/*
 * geteblk:
 *
 *	Get an empty, disassociated buffer of given size.  The buffer is
 *	initially set to B_INVAL.
 *
 *	critical section protection is not required for the allocbuf()
 *	call because races are impossible here.
 */
struct buf *
geteblk(int size)
{
	struct buf *bp;
	int maxsize;

	maxsize = (size + BKVAMASK) & ~BKVAMASK;

	while ((bp = getnewbuf(0, 0, size, maxsize)) == NULL)
		;
	allocbuf(bp, size);
	bp->b_flags |= B_INVAL;	/* b_dep cleared by getnewbuf() */
	KKASSERT(dsched_is_clear_buf_priv(bp));
	return (bp);
}


/*
 * allocbuf:
 *
 *	This code constitutes the buffer memory from either anonymous system
 *	memory (in the case of non-VMIO operations) or from an associated
 *	VM object (in the case of VMIO operations).  This code is able to
 *	resize a buffer up or down.
 *
 *	Note that this code is tricky, and has many complications to resolve
 *	deadlock or inconsistant data situations.  Tread lightly!!! 
 *	There are B_CACHE and B_DELWRI interactions that must be dealt with by 
 *	the caller.  Calling this code willy nilly can result in the loss of
 *	data.
 *
 *	allocbuf() only adjusts B_CACHE for VMIO buffers.  getblk() deals with
 *	B_CACHE for the non-VMIO case.
 *
 *	This routine does not need to be called from a critical section but you
 *	must own the buffer.
 */
int
allocbuf(struct buf *bp, int size)
{
	int newbsize, mbsize;
	int i;

	if (BUF_REFCNT(bp) == 0)
		panic("allocbuf: buffer not busy");

	if (bp->b_kvasize < size)
		panic("allocbuf: buffer too small");

	if ((bp->b_flags & B_VMIO) == 0) {
		caddr_t origbuf;
		int origbufsize;
		/*
		 * Just get anonymous memory from the kernel.  Don't
		 * mess with B_CACHE.
		 */
		mbsize = (size + DEV_BSIZE - 1) & ~(DEV_BSIZE - 1);
		if (bp->b_flags & B_MALLOC)
			newbsize = mbsize;
		else
			newbsize = round_page(size);

		if (newbsize < bp->b_bufsize) {
			/*
			 * Malloced buffers are not shrunk
			 */
			if (bp->b_flags & B_MALLOC) {
				if (newbsize) {
					bp->b_bcount = size;
				} else {
					kfree(bp->b_data, M_BIOBUF);
					if (bp->b_bufsize) {
						atomic_subtract_long(&bufmallocspace, bp->b_bufsize);
						bufspacewakeup();
						bp->b_bufsize = 0;
					}
					bp->b_data = bp->b_kvabase;
					bp->b_bcount = 0;
					bp->b_flags &= ~B_MALLOC;
				}
				return 1;
			}		
			vm_hold_free_pages(
			    bp,
			    (vm_offset_t) bp->b_data + newbsize,
			    (vm_offset_t) bp->b_data + bp->b_bufsize);
		} else if (newbsize > bp->b_bufsize) {
			/*
			 * We only use malloced memory on the first allocation.
			 * and revert to page-allocated memory when the buffer
			 * grows.
			 */
			if ((bufmallocspace < maxbufmallocspace) &&
				(bp->b_bufsize == 0) &&
				(mbsize <= PAGE_SIZE/2)) {

				bp->b_data = kmalloc(mbsize, M_BIOBUF, M_WAITOK);
				bp->b_bufsize = mbsize;
				bp->b_bcount = size;
				bp->b_flags |= B_MALLOC;
				atomic_add_long(&bufmallocspace, mbsize);
				return 1;
			}
			origbuf = NULL;
			origbufsize = 0;
			/*
			 * If the buffer is growing on its other-than-first
			 * allocation, then we revert to the page-allocation
			 * scheme.
			 */
			if (bp->b_flags & B_MALLOC) {
				origbuf = bp->b_data;
				origbufsize = bp->b_bufsize;
				bp->b_data = bp->b_kvabase;
				if (bp->b_bufsize) {
					atomic_subtract_long(&bufmallocspace,
							     bp->b_bufsize);
					bufspacewakeup();
					bp->b_bufsize = 0;
				}
				bp->b_flags &= ~B_MALLOC;
				newbsize = round_page(newbsize);
			}
			vm_hold_load_pages(
			    bp,
			    (vm_offset_t) bp->b_data + bp->b_bufsize,
			    (vm_offset_t) bp->b_data + newbsize);
			if (origbuf) {
				bcopy(origbuf, bp->b_data, origbufsize);
				kfree(origbuf, M_BIOBUF);
			}
		}
	} else {
		vm_page_t m;
		int desiredpages;

		newbsize = (size + DEV_BSIZE - 1) & ~(DEV_BSIZE - 1);
		desiredpages = ((int)(bp->b_loffset & PAGE_MASK) +
				newbsize + PAGE_MASK) >> PAGE_SHIFT;
		KKASSERT(desiredpages <= XIO_INTERNAL_PAGES);

		if (bp->b_flags & B_MALLOC)
			panic("allocbuf: VMIO buffer can't be malloced");
		/*
		 * Set B_CACHE initially if buffer is 0 length or will become
		 * 0-length.
		 */
		if (size == 0 || bp->b_bufsize == 0)
			bp->b_flags |= B_CACHE;

		if (newbsize < bp->b_bufsize) {
			/*
			 * DEV_BSIZE aligned new buffer size is less then the
			 * DEV_BSIZE aligned existing buffer size.  Figure out
			 * if we have to remove any pages.
			 */
			if (desiredpages < bp->b_xio.xio_npages) {
				for (i = desiredpages; i < bp->b_xio.xio_npages; i++) {
					/*
					 * the page is not freed here -- it
					 * is the responsibility of 
					 * vnode_pager_setsize
					 */
					m = bp->b_xio.xio_pages[i];
					KASSERT(m != bogus_page,
					    ("allocbuf: bogus page found"));
					vm_page_busy_wait(m, TRUE, "biodep");
					bp->b_xio.xio_pages[i] = NULL;
					vm_page_unwire(m, 0);
					vm_page_wakeup(m);
				}
				pmap_qremove((vm_offset_t) trunc_page((vm_offset_t)bp->b_data) +
				    (desiredpages << PAGE_SHIFT), (bp->b_xio.xio_npages - desiredpages));
				bp->b_xio.xio_npages = desiredpages;
			}
		} else if (size > bp->b_bcount) {
			/*
			 * We are growing the buffer, possibly in a 
			 * byte-granular fashion.
			 */
			struct vnode *vp;
			vm_object_t obj;
			vm_offset_t toff;
			vm_offset_t tinc;

			/*
			 * Step 1, bring in the VM pages from the object, 
			 * allocating them if necessary.  We must clear
			 * B_CACHE if these pages are not valid for the 
			 * range covered by the buffer.
			 *
			 * critical section protection is required to protect
			 * against interrupts unbusying and freeing pages
			 * between our vm_page_lookup() and our
			 * busycheck/wiring call.
			 */
			vp = bp->b_vp;
			obj = vp->v_object;

			vm_object_hold(obj);
			while (bp->b_xio.xio_npages < desiredpages) {
				vm_page_t m;
				vm_pindex_t pi;
				int error;

				pi = OFF_TO_IDX(bp->b_loffset) +
				     bp->b_xio.xio_npages;

				/*
				 * Blocking on m->busy might lead to a
				 * deadlock:
				 *
				 *  vm_fault->getpages->cluster_read->allocbuf
				 */
				m = vm_page_lookup_busy_try(obj, pi, FALSE,
							    &error);
				if (error) {
					vm_page_sleep_busy(m, FALSE, "pgtblk");
					continue;
				}
				if (m == NULL) {
					/*
					 * note: must allocate system pages
					 * since blocking here could intefere
					 * with paging I/O, no matter which
					 * process we are.
					 */
					m = bio_page_alloc(bp, obj, pi, desiredpages - bp->b_xio.xio_npages);
					if (m) {
						vm_page_wire(m);
						vm_page_flag_clear(m, PG_ZERO);
						vm_page_wakeup(m);
						bp->b_flags &= ~B_CACHE;
						bp->b_xio.xio_pages[bp->b_xio.xio_npages] = m;
						++bp->b_xio.xio_npages;
					}
					continue;
				}

				/*
				 * We found a page and were able to busy it.
				 */
				vm_page_flag_clear(m, PG_ZERO);
				vm_page_wire(m);
				vm_page_wakeup(m);
				bp->b_xio.xio_pages[bp->b_xio.xio_npages] = m;
				++bp->b_xio.xio_npages;
				if (bp->b_act_count < m->act_count)
					bp->b_act_count = m->act_count;
			}
			vm_object_drop(obj);

			/*
			 * Step 2.  We've loaded the pages into the buffer,
			 * we have to figure out if we can still have B_CACHE
			 * set.  Note that B_CACHE is set according to the
			 * byte-granular range ( bcount and size ), not the
			 * aligned range ( newbsize ).
			 *
			 * The VM test is against m->valid, which is DEV_BSIZE
			 * aligned.  Needless to say, the validity of the data
			 * needs to also be DEV_BSIZE aligned.  Note that this
			 * fails with NFS if the server or some other client
			 * extends the file's EOF.  If our buffer is resized, 
			 * B_CACHE may remain set! XXX
			 */

			toff = bp->b_bcount;
			tinc = PAGE_SIZE - ((bp->b_loffset + toff) & PAGE_MASK);

			while ((bp->b_flags & B_CACHE) && toff < size) {
				vm_pindex_t pi;

				if (tinc > (size - toff))
					tinc = size - toff;

				pi = ((bp->b_loffset & PAGE_MASK) + toff) >> 
				    PAGE_SHIFT;

				vfs_buf_test_cache(
				    bp, 
				    bp->b_loffset,
				    toff, 
				    tinc, 
				    bp->b_xio.xio_pages[pi]
				);
				toff += tinc;
				tinc = PAGE_SIZE;
			}

			/*
			 * Step 3, fixup the KVM pmap.  Remember that
			 * bp->b_data is relative to bp->b_loffset, but 
			 * bp->b_loffset may be offset into the first page.
			 */

			bp->b_data = (caddr_t)
			    trunc_page((vm_offset_t)bp->b_data);
			pmap_qenter(
			    (vm_offset_t)bp->b_data,
			    bp->b_xio.xio_pages, 
			    bp->b_xio.xio_npages
			);
			bp->b_data = (caddr_t)((vm_offset_t)bp->b_data | 
			    (vm_offset_t)(bp->b_loffset & PAGE_MASK));
		}
	}

	/* adjust space use on already-dirty buffer */
	if (bp->b_flags & B_DELWRI) {
		/* dirtykvaspace unchanged */
		atomic_add_long(&dirtybufspace, newbsize - bp->b_bufsize);
		if (bp->b_flags & B_HEAVY) {
			atomic_add_long(&dirtybufspacehw,
					newbsize - bp->b_bufsize);
		}
	}
	if (newbsize < bp->b_bufsize)
		bufspacewakeup();
	bp->b_bufsize = newbsize;	/* actual buffer allocation	*/
	bp->b_bcount = size;		/* requested buffer size	*/
	return 1;
}

/*
 * biowait:
 *
 *	Wait for buffer I/O completion, returning error status. B_EINTR
 *	is converted into an EINTR error but not cleared (since a chain
 *	of biowait() calls may occur).
 *
 *	On return bpdone() will have been called but the buffer will remain
 *	locked and will not have been brelse()'d.
 *
 *	NOTE!  If a timeout is specified and ETIMEDOUT occurs the I/O is
 *	likely still in progress on return.
 *
 *	NOTE!  This operation is on a BIO, not a BUF.
 *
 *	NOTE!  BIO_DONE is cleared by vn_strategy()
 */
static __inline int
_biowait(struct bio *bio, const char *wmesg, int to)
{
	struct buf *bp = bio->bio_buf;
	u_int32_t flags;
	u_int32_t nflags;
	int error;

	KKASSERT(bio == &bp->b_bio1);
	for (;;) {
		flags = bio->bio_flags;
		if (flags & BIO_DONE)
			break;
		nflags = flags | BIO_WANT;
		tsleep_interlock(bio, 0);
		if (atomic_cmpset_int(&bio->bio_flags, flags, nflags)) {
			if (wmesg)
				error = tsleep(bio, PINTERLOCKED, wmesg, to);
			else if (bp->b_cmd == BUF_CMD_READ)
				error = tsleep(bio, PINTERLOCKED, "biord", to);
			else
				error = tsleep(bio, PINTERLOCKED, "biowr", to);
			if (error) {
				kprintf("tsleep error biowait %d\n", error);
				return (error);
			}
		}
	}

	/*
	 * Finish up.
	 */
	KKASSERT(bp->b_cmd == BUF_CMD_DONE);
	bio->bio_flags &= ~(BIO_DONE | BIO_SYNC);
	if (bp->b_flags & B_EINTR)
		return (EINTR);
	if (bp->b_flags & B_ERROR)
		return (bp->b_error ? bp->b_error : EIO);
	return (0);
}

int
biowait(struct bio *bio, const char *wmesg)
{
	return(_biowait(bio, wmesg, 0));
}

int
biowait_timeout(struct bio *bio, const char *wmesg, int to)
{
	return(_biowait(bio, wmesg, to));
}

/*
 * This associates a tracking count with an I/O.  vn_strategy() and
 * dev_dstrategy() do this automatically but there are a few cases
 * where a vnode or device layer is bypassed when a block translation
 * is cached.  In such cases bio_start_transaction() may be called on
 * the bypassed layers so the system gets an I/O in progress indication 
 * for those higher layers.
 */
void
bio_start_transaction(struct bio *bio, struct bio_track *track)
{
	bio->bio_track = track;
	if (dsched_is_clear_buf_priv(bio->bio_buf))
		dsched_new_buf(bio->bio_buf);
	bio_track_ref(track);
}

/*
 * Initiate I/O on a vnode.
 *
 * SWAPCACHE OPERATION:
 *
 *	Real buffer cache buffers have a non-NULL bp->b_vp.  Unfortunately
 *	devfs also uses b_vp for fake buffers so we also have to check
 *	that B_PAGING is 0.  In this case the passed 'vp' is probably the
 *	underlying block device.  The swap assignments are related to the
 *	buffer cache buffer's b_vp, not the passed vp.
 *
 *	The passed vp == bp->b_vp only in the case where the strategy call
 *	is made on the vp itself for its own buffers (a regular file or
 *	block device vp).  The filesystem usually then re-calls vn_strategy()
 *	after translating the request to an underlying device.
 *
 *	Cluster buffers set B_CLUSTER and the passed vp is the vp of the
 *	underlying buffer cache buffers.
 *
 *	We can only deal with page-aligned buffers at the moment, because
 *	we can't tell what the real dirty state for pages straddling a buffer
 *	are.
 *
 *	In order to call swap_pager_strategy() we must provide the VM object
 *	and base offset for the underlying buffer cache pages so it can find
 *	the swap blocks.
 */
void
vn_strategy(struct vnode *vp, struct bio *bio)
{
	struct bio_track *track;
	struct buf *bp = bio->bio_buf;

	KKASSERT(bp->b_cmd != BUF_CMD_DONE);

	/*
	 * Set when an I/O is issued on the bp.  Cleared by consumers
	 * (aka HAMMER), allowing the consumer to determine if I/O had
	 * actually occurred.
	 */
	bp->b_flags |= B_IODEBUG;

	/*
	 * Handle the swap cache intercept.
	 */
	if (vn_cache_strategy(vp, bio))
		return;

	/*
	 * Otherwise do the operation through the filesystem
	 */
        if (bp->b_cmd == BUF_CMD_READ)
                track = &vp->v_track_read;
        else
                track = &vp->v_track_write;
	KKASSERT((bio->bio_flags & BIO_DONE) == 0);
	bio->bio_track = track;
	if (dsched_is_clear_buf_priv(bio->bio_buf))
		dsched_new_buf(bio->bio_buf);
	bio_track_ref(track);
        vop_strategy(*vp->v_ops, vp, bio);
}

static void vn_cache_strategy_callback(struct bio *bio);

int
vn_cache_strategy(struct vnode *vp, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct bio *nbio;
	vm_object_t object;
	vm_page_t m;
	int i;

	/*
	 * Is this buffer cache buffer suitable for reading from
	 * the swap cache?
	 */
	if (vm_swapcache_read_enable == 0 ||
	    bp->b_cmd != BUF_CMD_READ ||
	    ((bp->b_flags & B_CLUSTER) == 0 &&
	     (bp->b_vp == NULL || (bp->b_flags & B_PAGING))) ||
	    ((int)bp->b_loffset & PAGE_MASK) != 0 ||
	    (bp->b_bcount & PAGE_MASK) != 0) {
		return(0);
	}

	/*
	 * Figure out the original VM object (it will match the underlying
	 * VM pages).  Note that swap cached data uses page indices relative
	 * to that object, not relative to bio->bio_offset.
	 */
	if (bp->b_flags & B_CLUSTER)
		object = vp->v_object;
	else
		object = bp->b_vp->v_object;

	/*
	 * In order to be able to use the swap cache all underlying VM
	 * pages must be marked as such, and we can't have any bogus pages.
	 */
	for (i = 0; i < bp->b_xio.xio_npages; ++i) {
		m = bp->b_xio.xio_pages[i];
		if ((m->flags & PG_SWAPPED) == 0)
			break;
		if (m == bogus_page)
			break;
	}

	/*
	 * If we are good then issue the I/O using swap_pager_strategy().
	 *
	 * We can only do this if the buffer actually supports object-backed
	 * I/O.  If it doesn't npages will be 0.
	 */
	if (i && i == bp->b_xio.xio_npages) {
		m = bp->b_xio.xio_pages[0];
		nbio = push_bio(bio);
		nbio->bio_done = vn_cache_strategy_callback;
		nbio->bio_offset = ptoa(m->pindex);
		KKASSERT(m->object == object);
		swap_pager_strategy(object, nbio);
		return(1);
	}
	return(0);
}

/*
 * This is a bit of a hack but since the vn_cache_strategy() function can
 * override a VFS's strategy function we must make sure that the bio, which
 * is probably bio2, doesn't leak an unexpected offset value back to the
 * filesystem.  The filesystem (e.g. UFS) might otherwise assume that the
 * bio went through its own file strategy function and the the bio2 offset
 * is a cached disk offset when, in fact, it isn't.
 */
static void
vn_cache_strategy_callback(struct bio *bio)
{
	bio->bio_offset = NOOFFSET;
	biodone(pop_bio(bio));
}

/*
 * bpdone:
 *
 *	Finish I/O on a buffer after all BIOs have been processed.
 *	Called when the bio chain is exhausted or by biowait.  If called
 *	by biowait, elseit is typically 0.
 *
 *	bpdone is also responsible for setting B_CACHE in a B_VMIO bp.
 *	In a non-VMIO bp, B_CACHE will be set on the next getblk() 
 *	assuming B_INVAL is clear.
 *
 *	For the VMIO case, we set B_CACHE if the op was a read and no
 *	read error occured, or if the op was a write.  B_CACHE is never
 *	set if the buffer is invalid or otherwise uncacheable.
 *
 *	bpdone does not mess with B_INVAL, allowing the I/O routine or the
 *	initiator to leave B_INVAL set to brelse the buffer out of existance
 *	in the biodone routine.
 */
void
bpdone(struct buf *bp, int elseit)
{
	buf_cmd_t cmd;

	KASSERT(BUF_REFCNTNB(bp) > 0, 
		("biodone: bp %p not busy %d", bp, BUF_REFCNTNB(bp)));
	KASSERT(bp->b_cmd != BUF_CMD_DONE, 
		("biodone: bp %p already done!", bp));

	/*
	 * No more BIOs are left.  All completion functions have been dealt
	 * with, now we clean up the buffer.
	 */
	cmd = bp->b_cmd;
	bp->b_cmd = BUF_CMD_DONE;

	/*
	 * Only reads and writes are processed past this point.
	 */
	if (cmd != BUF_CMD_READ && cmd != BUF_CMD_WRITE) {
		if (cmd == BUF_CMD_FREEBLKS)
			bp->b_flags |= B_NOCACHE;
		if (elseit)
			brelse(bp);
		return;
	}

	/*
	 * Warning: softupdates may re-dirty the buffer, and HAMMER can do
	 * a lot worse.  XXX - move this above the clearing of b_cmd
	 */
	if (LIST_FIRST(&bp->b_dep) != NULL)
		buf_complete(bp);

	/*
	 * A failed write must re-dirty the buffer unless B_INVAL
	 * was set.  Only applicable to normal buffers (with VPs).
	 * vinum buffers may not have a vp.
	 */
	if (cmd == BUF_CMD_WRITE &&
	    (bp->b_flags & (B_ERROR | B_INVAL)) == B_ERROR) {
		bp->b_flags &= ~B_NOCACHE;
		if (bp->b_vp)
			bdirty(bp);
	}

	if (bp->b_flags & B_VMIO) {
		int i;
		vm_ooffset_t foff;
		vm_page_t m;
		vm_object_t obj;
		int iosize;
		struct vnode *vp = bp->b_vp;

		obj = vp->v_object;

#if defined(VFS_BIO_DEBUG)
		if (vp->v_auxrefs == 0)
			panic("biodone: zero vnode hold count");
		if ((vp->v_flag & VOBJBUF) == 0)
			panic("biodone: vnode is not setup for merged cache");
#endif

		foff = bp->b_loffset;
		KASSERT(foff != NOOFFSET, ("biodone: no buffer offset"));
		KASSERT(obj != NULL, ("biodone: missing VM object"));

#if defined(VFS_BIO_DEBUG)
		if (obj->paging_in_progress < bp->b_xio.xio_npages) {
			kprintf("biodone: paging in progress(%d) < "
				"bp->b_xio.xio_npages(%d)\n",
				obj->paging_in_progress,
				bp->b_xio.xio_npages);
		}
#endif

		/*
		 * Set B_CACHE if the op was a normal read and no error
		 * occured.  B_CACHE is set for writes in the b*write()
		 * routines.
		 */
		iosize = bp->b_bcount - bp->b_resid;
		if (cmd == BUF_CMD_READ &&
		    (bp->b_flags & (B_INVAL|B_NOCACHE|B_ERROR)) == 0) {
			bp->b_flags |= B_CACHE;
		}

		vm_object_hold(obj);
		for (i = 0; i < bp->b_xio.xio_npages; i++) {
			int bogusflag = 0;
			int resid;

			resid = ((foff + PAGE_SIZE) & ~(off_t)PAGE_MASK) - foff;
			if (resid > iosize)
				resid = iosize;

			/*
			 * cleanup bogus pages, restoring the originals.  Since
			 * the originals should still be wired, we don't have
			 * to worry about interrupt/freeing races destroying
			 * the VM object association.
			 */
			m = bp->b_xio.xio_pages[i];
			if (m == bogus_page) {
				bogusflag = 1;
				m = vm_page_lookup(obj, OFF_TO_IDX(foff));
				if (m == NULL)
					panic("biodone: page disappeared");
				bp->b_xio.xio_pages[i] = m;
				pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
					bp->b_xio.xio_pages, bp->b_xio.xio_npages);
			}
#if defined(VFS_BIO_DEBUG)
			if (OFF_TO_IDX(foff) != m->pindex) {
				kprintf("biodone: foff(%lu)/m->pindex(%ld) "
					"mismatch\n",
					(unsigned long)foff, (long)m->pindex);
			}
#endif

			/*
			 * In the write case, the valid and clean bits are
			 * already changed correctly (see bdwrite()), so we
			 * only need to do this here in the read case.
			 */
			vm_page_busy_wait(m, FALSE, "bpdpgw");
			if (cmd == BUF_CMD_READ && !bogusflag && resid > 0) {
				vfs_clean_one_page(bp, i, m);
			}
			vm_page_flag_clear(m, PG_ZERO);

			/*
			 * when debugging new filesystems or buffer I/O
			 * methods, this is the most common error that pops
			 * up.  if you see this, you have not set the page
			 * busy flag correctly!!!
			 */
			if (m->busy == 0) {
				kprintf("biodone: page busy < 0, "
				    "pindex: %d, foff: 0x(%x,%x), "
				    "resid: %d, index: %d\n",
				    (int) m->pindex, (int)(foff >> 32),
						(int) foff & 0xffffffff, resid, i);
				if (!vn_isdisk(vp, NULL))
					kprintf(" iosize: %ld, loffset: %lld, "
						"flags: 0x%08x, npages: %d\n",
					    bp->b_vp->v_mount->mnt_stat.f_iosize,
					    (long long)bp->b_loffset,
					    bp->b_flags, bp->b_xio.xio_npages);
				else
					kprintf(" VDEV, loffset: %lld, flags: 0x%08x, npages: %d\n",
					    (long long)bp->b_loffset,
					    bp->b_flags, bp->b_xio.xio_npages);
				kprintf(" valid: 0x%x, dirty: 0x%x, "
					"wired: %d\n",
					m->valid, m->dirty,
					m->wire_count);
				panic("biodone: page busy < 0");
			}
			vm_page_io_finish(m);
			vm_page_wakeup(m);
			vm_object_pip_wakeup(obj);
			foff = (foff + PAGE_SIZE) & ~(off_t)PAGE_MASK;
			iosize -= resid;
		}
		bp->b_flags &= ~B_HASBOGUS;
		vm_object_drop(obj);
	}

	/*
	 * Finish up by releasing the buffer.  There are no more synchronous
	 * or asynchronous completions, those were handled by bio_done
	 * callbacks.
	 */
	if (elseit) {
		if (bp->b_flags & (B_NOCACHE|B_INVAL|B_ERROR|B_RELBUF))
			brelse(bp);
		else
			bqrelse(bp);
	}
}

/*
 * Normal biodone.
 */
void
biodone(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;

	runningbufwakeup(bp);

	/*
	 * Run up the chain of BIO's.   Leave b_cmd intact for the duration.
	 */
	while (bio) {
		biodone_t *done_func;
		struct bio_track *track;

		/*
		 * BIO tracking.  Most but not all BIOs are tracked.
		 */
		if ((track = bio->bio_track) != NULL) {
			bio_track_rel(track);
			bio->bio_track = NULL;
		}

		/*
		 * A bio_done function terminates the loop.  The function
		 * will be responsible for any further chaining and/or
		 * buffer management.
		 *
		 * WARNING!  The done function can deallocate the buffer!
		 */
		if ((done_func = bio->bio_done) != NULL) {
			bio->bio_done = NULL;
			done_func(bio);
			return;
		}
		bio = bio->bio_prev;
	}

	/*
	 * If we've run out of bio's do normal [a]synchronous completion.
	 */
	bpdone(bp, 1);
}

/*
 * Synchronous biodone - this terminates a synchronous BIO.
 *
 * bpdone() is called with elseit=FALSE, leaving the buffer completed
 * but still locked.  The caller must brelse() the buffer after waiting
 * for completion.
 */
void
biodone_sync(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	int flags;
	int nflags;

	KKASSERT(bio == &bp->b_bio1);
	bpdone(bp, 0);

	for (;;) {
		flags = bio->bio_flags;
		nflags = (flags | BIO_DONE) & ~BIO_WANT;

		if (atomic_cmpset_int(&bio->bio_flags, flags, nflags)) {
			if (flags & BIO_WANT)
				wakeup(bio);
			break;
		}
	}
}

/*
 * vfs_unbusy_pages:
 *
 *	This routine is called in lieu of iodone in the case of
 *	incomplete I/O.  This keeps the busy status for pages
 *	consistant.
 */
void
vfs_unbusy_pages(struct buf *bp)
{
	int i;

	runningbufwakeup(bp);

	if (bp->b_flags & B_VMIO) {
		struct vnode *vp = bp->b_vp;
		vm_object_t obj;

		obj = vp->v_object;
		vm_object_hold(obj);

		for (i = 0; i < bp->b_xio.xio_npages; i++) {
			vm_page_t m = bp->b_xio.xio_pages[i];

			/*
			 * When restoring bogus changes the original pages
			 * should still be wired, so we are in no danger of
			 * losing the object association and do not need
			 * critical section protection particularly.
			 */
			if (m == bogus_page) {
				m = vm_page_lookup(obj, OFF_TO_IDX(bp->b_loffset) + i);
				if (!m) {
					panic("vfs_unbusy_pages: page missing");
				}
				bp->b_xio.xio_pages[i] = m;
				pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
					bp->b_xio.xio_pages, bp->b_xio.xio_npages);
			}
			vm_page_busy_wait(m, FALSE, "bpdpgw");
			vm_page_flag_clear(m, PG_ZERO);
			vm_page_io_finish(m);
			vm_page_wakeup(m);
			vm_object_pip_wakeup(obj);
		}
		bp->b_flags &= ~B_HASBOGUS;
		vm_object_drop(obj);
	}
}

/*
 * vfs_busy_pages:
 *
 *	This routine is called before a device strategy routine.
 *	It is used to tell the VM system that paging I/O is in
 *	progress, and treat the pages associated with the buffer
 *	almost as being PG_BUSY.  Also the object 'paging_in_progress'
 *	flag is handled to make sure that the object doesn't become
 *	inconsistant.
 *
 *	Since I/O has not been initiated yet, certain buffer flags
 *	such as B_ERROR or B_INVAL may be in an inconsistant state
 *	and should be ignored.
 */
void
vfs_busy_pages(struct vnode *vp, struct buf *bp)
{
	int i, bogus;
	struct lwp *lp = curthread->td_lwp;

	/*
	 * The buffer's I/O command must already be set.  If reading,
	 * B_CACHE must be 0 (double check against callers only doing
	 * I/O when B_CACHE is 0).
	 */
	KKASSERT(bp->b_cmd != BUF_CMD_DONE);
	KKASSERT(bp->b_cmd == BUF_CMD_WRITE || (bp->b_flags & B_CACHE) == 0);

	if (bp->b_flags & B_VMIO) {
		vm_object_t obj;

		obj = vp->v_object;
		KASSERT(bp->b_loffset != NOOFFSET,
			("vfs_busy_pages: no buffer offset"));

		/*
		 * Busy all the pages.  We have to busy them all at once
		 * to avoid deadlocks.
		 */
retry:
		for (i = 0; i < bp->b_xio.xio_npages; i++) {
			vm_page_t m = bp->b_xio.xio_pages[i];

			if (vm_page_busy_try(m, FALSE)) {
				vm_page_sleep_busy(m, FALSE, "vbpage");
				while (--i >= 0)
					vm_page_wakeup(bp->b_xio.xio_pages[i]);
				goto retry;
			}
		}

		/*
		 * Setup for I/O, soft-busy the page right now because
		 * the next loop may block.
		 */
		for (i = 0; i < bp->b_xio.xio_npages; i++) {
			vm_page_t m = bp->b_xio.xio_pages[i];

			vm_page_flag_clear(m, PG_ZERO);
			if ((bp->b_flags & B_CLUSTER) == 0) {
				vm_object_pip_add(obj, 1);
				vm_page_io_start(m);
			}
		}

		/*
		 * Adjust protections for I/O and do bogus-page mapping.
		 * Assume that vm_page_protect() can block (it can block
		 * if VM_PROT_NONE, don't take any chances regardless).
		 *
		 * In particular note that for writes we must incorporate
		 * page dirtyness from the VM system into the buffer's
		 * dirty range.
		 *
		 * For reads we theoretically must incorporate page dirtyness
		 * from the VM system to determine if the page needs bogus
		 * replacement, but we shortcut the test by simply checking
		 * that all m->valid bits are set, indicating that the page
		 * is fully valid and does not need to be re-read.  For any
		 * VM system dirtyness the page will also be fully valid
		 * since it was mapped at one point.
		 */
		bogus = 0;
		for (i = 0; i < bp->b_xio.xio_npages; i++) {
			vm_page_t m = bp->b_xio.xio_pages[i];

			vm_page_flag_clear(m, PG_ZERO);	/* XXX */
			if (bp->b_cmd == BUF_CMD_WRITE) {
				/*
				 * When readying a vnode-backed buffer for
				 * a write we must zero-fill any invalid
				 * portions of the backing VM pages, mark
				 * it valid and clear related dirty bits.
				 *
				 * vfs_clean_one_page() incorporates any
				 * VM dirtyness and updates the b_dirtyoff
				 * range (after we've made the page RO).
				 *
				 * It is also expected that the pmap modified
				 * bit has already been cleared by the
				 * vm_page_protect().  We may not be able
				 * to clear all dirty bits for a page if it
				 * was also memory mapped (NFS).
				 *
				 * Finally be sure to unassign any swap-cache
				 * backing store as it is now stale.
				 */
				vm_page_protect(m, VM_PROT_READ);
				vfs_clean_one_page(bp, i, m);
				swap_pager_unswapped(m);
			} else if (m->valid == VM_PAGE_BITS_ALL) {
				/*
				 * When readying a vnode-backed buffer for
				 * read we must replace any dirty pages with
				 * a bogus page so dirty data is not destroyed
				 * when filling gaps.
				 *
				 * To avoid testing whether the page is
				 * dirty we instead test that the page was
				 * at some point mapped (m->valid fully
				 * valid) with the understanding that
				 * this also covers the dirty case.
				 */
				bp->b_xio.xio_pages[i] = bogus_page;
				bp->b_flags |= B_HASBOGUS;
				bogus++;
			} else if (m->valid & m->dirty) {
				/*
				 * This case should not occur as partial
				 * dirtyment can only happen if the buffer
				 * is B_CACHE, and this code is not entered
				 * if the buffer is B_CACHE.
				 */
				kprintf("Warning: vfs_busy_pages - page not "
					"fully valid! loff=%jx bpf=%08x "
					"idx=%d val=%02x dir=%02x\n",
					(uintmax_t)bp->b_loffset, bp->b_flags,
					i, m->valid, m->dirty);
				vm_page_protect(m, VM_PROT_NONE);
			} else {
				/*
				 * The page is not valid and can be made
				 * part of the read.
				 */
				vm_page_protect(m, VM_PROT_NONE);
			}
			vm_page_wakeup(m);
		}
		if (bogus) {
			pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
				bp->b_xio.xio_pages, bp->b_xio.xio_npages);
		}
	}

	/*
	 * This is the easiest place to put the process accounting for the I/O
	 * for now.
	 */
	if (lp != NULL) {
		if (bp->b_cmd == BUF_CMD_READ)
			lp->lwp_ru.ru_inblock++;
		else
			lp->lwp_ru.ru_oublock++;
	}
}

/*
 * Tell the VM system that the pages associated with this buffer
 * are clean.  This is used for delayed writes where the data is
 * going to go to disk eventually without additional VM intevention.
 *
 * NOTE: While we only really need to clean through to b_bcount, we
 *	 just go ahead and clean through to b_bufsize.
 */
static void
vfs_clean_pages(struct buf *bp)
{
	vm_page_t m;
	int i;

	if ((bp->b_flags & B_VMIO) == 0)
		return;

	KASSERT(bp->b_loffset != NOOFFSET,
		("vfs_clean_pages: no buffer offset"));

	for (i = 0; i < bp->b_xio.xio_npages; i++) {
		m = bp->b_xio.xio_pages[i];
		vfs_clean_one_page(bp, i, m);
	}
}

/*
 * vfs_clean_one_page:
 *
 *	Set the valid bits and clear the dirty bits in a page within a
 *	buffer.  The range is restricted to the buffer's size and the
 *	buffer's logical offset might index into the first page.
 *
 *	The caller has busied or soft-busied the page and it is not mapped,
 *	test and incorporate the dirty bits into b_dirtyoff/end before
 *	clearing them.  Note that we need to clear the pmap modified bits
 *	after determining the the page was dirty, vm_page_set_validclean()
 *	does not do it for us.
 *
 *	This routine is typically called after a read completes (dirty should
 *	be zero in that case as we are not called on bogus-replace pages),
 *	or before a write is initiated.
 */
static void
vfs_clean_one_page(struct buf *bp, int pageno, vm_page_t m)
{
	int bcount;
	int xoff;
	int soff;
	int eoff;

	/*
	 * Calculate offset range within the page but relative to buffer's
	 * loffset.  loffset might be offset into the first page.
	 */
	xoff = (int)bp->b_loffset & PAGE_MASK;	/* loffset offset into pg 0 */
	bcount = bp->b_bcount + xoff;		/* offset adjusted */

	if (pageno == 0) {
		soff = xoff;
		eoff = PAGE_SIZE;
	} else {
		soff = (pageno << PAGE_SHIFT);
		eoff = soff + PAGE_SIZE;
	}
	if (eoff > bcount)
		eoff = bcount;
	if (soff >= eoff)
		return;

	/*
	 * Test dirty bits and adjust b_dirtyoff/end.
	 *
	 * If dirty pages are incorporated into the bp any prior
	 * B_NEEDCOMMIT state (NFS) must be cleared because the
	 * caller has not taken into account the new dirty data.
	 *
	 * If the page was memory mapped the dirty bits might go beyond the
	 * end of the buffer, but we can't really make the assumption that
	 * a file EOF straddles the buffer (even though this is the case for
	 * NFS if B_NEEDCOMMIT is also set).  So for the purposes of clearing
	 * B_NEEDCOMMIT we only test the dirty bits covered by the buffer.
	 * This also saves some console spam.
	 *
	 * When clearing B_NEEDCOMMIT we must also clear B_CLUSTEROK,
	 * NFS can handle huge commits but not huge writes.
	 */
	vm_page_test_dirty(m);
	if (m->dirty) {
		if ((bp->b_flags & B_NEEDCOMMIT) &&
		    (m->dirty & vm_page_bits(soff & PAGE_MASK, eoff - soff))) {
			if (debug_commit)
				kprintf("Warning: vfs_clean_one_page: bp %p "
				    "loff=%jx,%d flgs=%08x clr B_NEEDCOMMIT"
				    " cmd %d vd %02x/%02x x/s/e %d %d %d "
				    "doff/end %d %d\n",
				    bp, (uintmax_t)bp->b_loffset, bp->b_bcount,
				    bp->b_flags, bp->b_cmd,
				    m->valid, m->dirty, xoff, soff, eoff,
				    bp->b_dirtyoff, bp->b_dirtyend);
			bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
			if (debug_commit)
				print_backtrace(-1);
		}
		/*
		 * Only clear the pmap modified bits if ALL the dirty bits
		 * are set, otherwise the system might mis-clear portions
		 * of a page.
		 */
		if (m->dirty == VM_PAGE_BITS_ALL &&
		    (bp->b_flags & B_NEEDCOMMIT) == 0) {
			pmap_clear_modify(m);
		}
		if (bp->b_dirtyoff > soff - xoff)
			bp->b_dirtyoff = soff - xoff;
		if (bp->b_dirtyend < eoff - xoff)
			bp->b_dirtyend = eoff - xoff;
	}

	/*
	 * Set related valid bits, clear related dirty bits.
	 * Does not mess with the pmap modified bit.
	 *
	 * WARNING!  We cannot just clear all of m->dirty here as the
	 *	     buffer cache buffers may use a DEV_BSIZE'd aligned
	 *	     block size, or have an odd size (e.g. NFS at file EOF).
	 *	     The putpages code can clear m->dirty to 0.
	 *
	 *	     If a VOP_WRITE generates a buffer cache buffer which
	 *	     covers the same space as mapped writable pages the
	 *	     buffer flush might not be able to clear all the dirty
	 *	     bits and still require a putpages from the VM system
	 *	     to finish it off.
	 *
	 * WARNING!  vm_page_set_validclean() currently assumes vm_token
	 *	     is held.  The page might not be busied (bdwrite() case).
	 *	     XXX remove this comment once we've validated that this
	 *	     is no longer an issue.
	 */
	vm_page_set_validclean(m, soff & PAGE_MASK, eoff - soff);
}

#if 0
/*
 * Similar to vfs_clean_one_page() but sets the bits to valid and dirty.
 * The page data is assumed to be valid (there is no zeroing here).
 */
static void
vfs_dirty_one_page(struct buf *bp, int pageno, vm_page_t m)
{
	int bcount;
	int xoff;
	int soff;
	int eoff;

	/*
	 * Calculate offset range within the page but relative to buffer's
	 * loffset.  loffset might be offset into the first page.
	 */
	xoff = (int)bp->b_loffset & PAGE_MASK;	/* loffset offset into pg 0 */
	bcount = bp->b_bcount + xoff;		/* offset adjusted */

	if (pageno == 0) {
		soff = xoff;
		eoff = PAGE_SIZE;
	} else {
		soff = (pageno << PAGE_SHIFT);
		eoff = soff + PAGE_SIZE;
	}
	if (eoff > bcount)
		eoff = bcount;
	if (soff >= eoff)
		return;
	vm_page_set_validdirty(m, soff & PAGE_MASK, eoff - soff);
}
#endif

/*
 * vfs_bio_clrbuf:
 *
 *	Clear a buffer.  This routine essentially fakes an I/O, so we need
 *	to clear B_ERROR and B_INVAL.
 *
 *	Note that while we only theoretically need to clear through b_bcount,
 *	we go ahead and clear through b_bufsize.
 */

void
vfs_bio_clrbuf(struct buf *bp)
{
	int i, mask = 0;
	caddr_t sa, ea;
	if ((bp->b_flags & (B_VMIO | B_MALLOC)) == B_VMIO) {
		bp->b_flags &= ~(B_INVAL | B_EINTR | B_ERROR);
		if ((bp->b_xio.xio_npages == 1) && (bp->b_bufsize < PAGE_SIZE) &&
		    (bp->b_loffset & PAGE_MASK) == 0) {
			mask = (1 << (bp->b_bufsize / DEV_BSIZE)) - 1;
			if ((bp->b_xio.xio_pages[0]->valid & mask) == mask) {
				bp->b_resid = 0;
				return;
			}
			if (((bp->b_xio.xio_pages[0]->flags & PG_ZERO) == 0) &&
			    ((bp->b_xio.xio_pages[0]->valid & mask) == 0)) {
				bzero(bp->b_data, bp->b_bufsize);
				bp->b_xio.xio_pages[0]->valid |= mask;
				bp->b_resid = 0;
				return;
			}
		}
		sa = bp->b_data;
		for(i=0;i<bp->b_xio.xio_npages;i++,sa=ea) {
			int j = ((vm_offset_t)sa & PAGE_MASK) / DEV_BSIZE;
			ea = (caddr_t)trunc_page((vm_offset_t)sa + PAGE_SIZE);
			ea = (caddr_t)(vm_offset_t)ulmin(
			    (u_long)(vm_offset_t)ea,
			    (u_long)(vm_offset_t)bp->b_data + bp->b_bufsize);
			mask = ((1 << ((ea - sa) / DEV_BSIZE)) - 1) << j;
			if ((bp->b_xio.xio_pages[i]->valid & mask) == mask)
				continue;
			if ((bp->b_xio.xio_pages[i]->valid & mask) == 0) {
				if ((bp->b_xio.xio_pages[i]->flags & PG_ZERO) == 0) {
					bzero(sa, ea - sa);
				}
			} else {
				for (; sa < ea; sa += DEV_BSIZE, j++) {
					if (((bp->b_xio.xio_pages[i]->flags & PG_ZERO) == 0) &&
						(bp->b_xio.xio_pages[i]->valid & (1<<j)) == 0)
						bzero(sa, DEV_BSIZE);
				}
			}
			bp->b_xio.xio_pages[i]->valid |= mask;
			vm_page_flag_clear(bp->b_xio.xio_pages[i], PG_ZERO);
		}
		bp->b_resid = 0;
	} else {
		clrbuf(bp);
	}
}

/*
 * vm_hold_load_pages:
 *
 *	Load pages into the buffer's address space.  The pages are
 *	allocated from the kernel object in order to reduce interference
 *	with the any VM paging I/O activity.  The range of loaded
 *	pages will be wired.
 *
 *	If a page cannot be allocated, the 'pagedaemon' is woken up to
 *	retrieve the full range (to - from) of pages.
 */
void
vm_hold_load_pages(struct buf *bp, vm_offset_t from, vm_offset_t to)
{
	vm_offset_t pg;
	vm_page_t p;
	int index;

	to = round_page(to);
	from = round_page(from);
	index = (from - trunc_page((vm_offset_t)bp->b_data)) >> PAGE_SHIFT;

	pg = from;
	while (pg < to) {
		/*
		 * Note: must allocate system pages since blocking here
		 * could intefere with paging I/O, no matter which
		 * process we are.
		 */
		vm_object_hold(&kernel_object);
		p = bio_page_alloc(bp, &kernel_object, pg >> PAGE_SHIFT,
				   (vm_pindex_t)((to - pg) >> PAGE_SHIFT));
		vm_object_drop(&kernel_object);
		if (p) {
			vm_page_wire(p);
			p->valid = VM_PAGE_BITS_ALL;
			vm_page_flag_clear(p, PG_ZERO);
			pmap_kenter(pg, VM_PAGE_TO_PHYS(p));
			bp->b_xio.xio_pages[index] = p;
			vm_page_wakeup(p);

			pg += PAGE_SIZE;
			++index;
		}
	}
	bp->b_xio.xio_npages = index;
}

/*
 * Allocate a page for a buffer cache buffer.
 *
 * If NULL is returned the caller is expected to retry (typically check if
 * the page already exists on retry before trying to allocate one).
 *
 * NOTE! Low-memory handling is dealt with in b[q]relse(), not here.  This
 *	 function will use the system reserve with the hope that the page
 *	 allocations can be returned to PQ_CACHE/PQ_FREE when the caller
 *	 is done with the buffer.
 *
 * NOTE! However, TMPFS is a special case because flushing a dirty buffer
 *	 to TMPFS doesn't clean the page.  For TMPFS, only the pagedaemon
 *	 is capable of retiring pages (to swap).  For TMPFS we don't dig
 *	 into the system reserve because doing so could stall out pretty
 *	 much every process running on the system.
 */
static
vm_page_t
bio_page_alloc(struct buf *bp, vm_object_t obj, vm_pindex_t pg, int deficit)
{
	int vmflags = VM_ALLOC_NORMAL | VM_ALLOC_NULL_OK;
	vm_page_t p;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(obj));

	/*
	 * Try a normal allocation first.
	 */
	p = vm_page_alloc(obj, pg, vmflags);
	if (p)
		return(p);
	if (vm_page_lookup(obj, pg))
		return(NULL);
	vm_pageout_deficit += deficit;

	/*
	 * Try again, digging into the system reserve.
	 *
	 * Trying to recover pages from the buffer cache here can deadlock
	 * against other threads trying to busy underlying pages so we
	 * depend on the code in brelse() and bqrelse() to free/cache the
	 * underlying buffer cache pages when memory is low.
	 */
	if (curthread->td_flags & TDF_SYSTHREAD)
		vmflags |= VM_ALLOC_SYSTEM | VM_ALLOC_INTERRUPT;
	else if (bp->b_vp && bp->b_vp->v_tag == VT_TMPFS)
		vmflags |= 0;
	else
		vmflags |= VM_ALLOC_SYSTEM;

	/*recoverbufpages();*/
	p = vm_page_alloc(obj, pg, vmflags);
	if (p)
		return(p);
	if (vm_page_lookup(obj, pg))
		return(NULL);

	/*
	 * Wait for memory to free up and try again
	 */
	if (vm_page_count_severe())
		++lowmempgallocs;
	vm_wait(hz / 20 + 1);

	p = vm_page_alloc(obj, pg, vmflags);
	if (p)
		return(p);
	if (vm_page_lookup(obj, pg))
		return(NULL);

	/*
	 * Ok, now we are really in trouble.
	 */
	{
		static struct krate biokrate = { .freq = 1 };
		krateprintf(&biokrate,
			    "Warning: bio_page_alloc: memory exhausted "
			    "during bufcache page allocation from %s\n",
			    curthread->td_comm);
	}
	if (curthread->td_flags & TDF_SYSTHREAD)
		vm_wait(hz / 20 + 1);
	else
		vm_wait(hz / 2 + 1);
	return (NULL);
}

/*
 * vm_hold_free_pages:
 *
 *	Return pages associated with the buffer back to the VM system.
 *
 *	The range of pages underlying the buffer's address space will
 *	be unmapped and un-wired.
 */
void
vm_hold_free_pages(struct buf *bp, vm_offset_t from, vm_offset_t to)
{
	vm_offset_t pg;
	vm_page_t p;
	int index, newnpages;

	from = round_page(from);
	to = round_page(to);
	index = (from - trunc_page((vm_offset_t)bp->b_data)) >> PAGE_SHIFT;
	newnpages = index;

	for (pg = from; pg < to; pg += PAGE_SIZE, index++) {
		p = bp->b_xio.xio_pages[index];
		if (p && (index < bp->b_xio.xio_npages)) {
			if (p->busy) {
				kprintf("vm_hold_free_pages: doffset: %lld, "
					"loffset: %lld\n",
					(long long)bp->b_bio2.bio_offset,
					(long long)bp->b_loffset);
			}
			bp->b_xio.xio_pages[index] = NULL;
			pmap_kremove(pg);
			vm_page_busy_wait(p, FALSE, "vmhldpg");
			vm_page_unwire(p, 0);
			vm_page_free(p);
		}
	}
	bp->b_xio.xio_npages = newnpages;
}

/*
 * vmapbuf:
 *
 *	Map a user buffer into KVM via a pbuf.  On return the buffer's
 *	b_data, b_bufsize, and b_bcount will be set, and its XIO page array
 *	initialized.
 */
int
vmapbuf(struct buf *bp, caddr_t udata, int bytes)
{
	caddr_t addr;
	vm_offset_t va;
	vm_page_t m;
	int vmprot;
	int error;
	int pidx;
	int i;

	/* 
	 * bp had better have a command and it better be a pbuf.
	 */
	KKASSERT(bp->b_cmd != BUF_CMD_DONE);
	KKASSERT(bp->b_flags & B_PAGING);
	KKASSERT(bp->b_kvabase);

	if (bytes < 0)
		return (-1);

	/*
	 * Map the user data into KVM.  Mappings have to be page-aligned.
	 */
	addr = (caddr_t)trunc_page((vm_offset_t)udata);
	pidx = 0;

	vmprot = VM_PROT_READ;
	if (bp->b_cmd == BUF_CMD_READ)
		vmprot |= VM_PROT_WRITE;

	while (addr < udata + bytes) {
		/*
		 * Do the vm_fault if needed; do the copy-on-write thing
		 * when reading stuff off device into memory.
		 *
		 * vm_fault_page*() returns a held VM page.
		 */
		va = (addr >= udata) ? (vm_offset_t)addr : (vm_offset_t)udata;
		va = trunc_page(va);

		m = vm_fault_page_quick(va, vmprot, &error);
		if (m == NULL) {
			for (i = 0; i < pidx; ++i) {
			    vm_page_unhold(bp->b_xio.xio_pages[i]);
			    bp->b_xio.xio_pages[i] = NULL;
			}
			return(-1);
		}
		bp->b_xio.xio_pages[pidx] = m;
		addr += PAGE_SIZE;
		++pidx;
	}

	/*
	 * Map the page array and set the buffer fields to point to
	 * the mapped data buffer.
	 */
	if (pidx > btoc(MAXPHYS))
		panic("vmapbuf: mapped more than MAXPHYS");
	pmap_qenter((vm_offset_t)bp->b_kvabase, bp->b_xio.xio_pages, pidx);

	bp->b_xio.xio_npages = pidx;
	bp->b_data = bp->b_kvabase + ((int)(intptr_t)udata & PAGE_MASK);
	bp->b_bcount = bytes;
	bp->b_bufsize = bytes;
	return(0);
}

/*
 * vunmapbuf:
 *
 *	Free the io map PTEs associated with this IO operation.
 *	We also invalidate the TLB entries and restore the original b_addr.
 */
void
vunmapbuf(struct buf *bp)
{
	int pidx;
	int npages;

	KKASSERT(bp->b_flags & B_PAGING);

	npages = bp->b_xio.xio_npages;
	pmap_qremove(trunc_page((vm_offset_t)bp->b_data), npages);
	for (pidx = 0; pidx < npages; ++pidx) {
		vm_page_unhold(bp->b_xio.xio_pages[pidx]);
		bp->b_xio.xio_pages[pidx] = NULL;
	}
	bp->b_xio.xio_npages = 0;
	bp->b_data = bp->b_kvabase;
}

/*
 * Scan all buffers in the system and issue the callback.
 */
int
scan_all_buffers(int (*callback)(struct buf *, void *), void *info)
{
	int count = 0;
	int error;
	long n;

	for (n = 0; n < nbuf; ++n) {
		if ((error = callback(&buf[n], info)) < 0) {
			count = error;
			break;
		}
		count += error;
	}
	return (count);
}

/*
 * nestiobuf_iodone: biodone callback for nested buffers and propagate
 * completion to the master buffer.
 */
static void
nestiobuf_iodone(struct bio *bio)
{
	struct bio *mbio;
	struct buf *mbp, *bp;
	struct devstat *stats;
	int error;
	int donebytes;

	bp = bio->bio_buf;
	mbio = bio->bio_caller_info1.ptr;
	stats = bio->bio_caller_info2.ptr;
	mbp = mbio->bio_buf;

	KKASSERT(bp->b_bcount <= bp->b_bufsize);
	KKASSERT(mbp != bp);

	error = bp->b_error;
	if (bp->b_error == 0 &&
	    (bp->b_bcount < bp->b_bufsize || bp->b_resid > 0)) {
		/*
		 * Not all got transfered, raise an error. We have no way to
		 * propagate these conditions to mbp.
		 */
		error = EIO;
	}

	donebytes = bp->b_bufsize;

	relpbuf(bp, NULL);

	nestiobuf_done(mbio, donebytes, error, stats);
}

void
nestiobuf_done(struct bio *mbio, int donebytes, int error, struct devstat *stats)
{
	struct buf *mbp;

	mbp = mbio->bio_buf;	

	KKASSERT((int)(intptr_t)mbio->bio_driver_info > 0);

	/*
	 * If an error occured, propagate it to the master buffer.
	 *
	 * Several biodone()s may wind up running concurrently so
	 * use an atomic op to adjust b_flags.
	 */
	if (error) {
		mbp->b_error = error;
		atomic_set_int(&mbp->b_flags, B_ERROR);
	}

	/*
	 * Decrement the operations in progress counter and terminate the
	 * I/O if this was the last bit.
	 */
	if (atomic_fetchadd_int((int *)&mbio->bio_driver_info, -1) == 1) {
		mbp->b_resid = 0;
		if (stats)
			devstat_end_transaction_buf(stats, mbp);
		biodone(mbio);
	}
}

/*
 * Initialize a nestiobuf for use.  Set an initial count of 1 to prevent
 * the mbio from being biodone()'d while we are still adding sub-bios to
 * it.
 */
void
nestiobuf_init(struct bio *bio)
{
	bio->bio_driver_info = (void *)1;
}

/*
 * The BIOs added to the nestedio have already been started, remove the
 * count that placeheld our mbio and biodone() it if the count would
 * transition to 0.
 */
void
nestiobuf_start(struct bio *mbio)
{
	struct buf *mbp = mbio->bio_buf;

	/*
	 * Decrement the operations in progress counter and terminate the
	 * I/O if this was the last bit.
	 */
	if (atomic_fetchadd_int((int *)&mbio->bio_driver_info, -1) == 1) {
		if (mbp->b_flags & B_ERROR)
			mbp->b_resid = mbp->b_bcount;
		else
			mbp->b_resid = 0;
		biodone(mbio);
	}
}

/*
 * Set an intermediate error prior to calling nestiobuf_start()
 */
void
nestiobuf_error(struct bio *mbio, int error)
{
	struct buf *mbp = mbio->bio_buf;

	if (error) {
		mbp->b_error = error;
		atomic_set_int(&mbp->b_flags, B_ERROR);
	}
}

/*
 * nestiobuf_add: setup a "nested" buffer.
 *
 * => 'mbp' is a "master" buffer which is being divided into sub pieces.
 * => 'bp' should be a buffer allocated by getiobuf.
 * => 'offset' is a byte offset in the master buffer.
 * => 'size' is a size in bytes of this nested buffer.
 */
void
nestiobuf_add(struct bio *mbio, struct buf *bp, int offset, size_t size, struct devstat *stats)
{
	struct buf *mbp = mbio->bio_buf;
	struct vnode *vp = mbp->b_vp;

	KKASSERT(mbp->b_bcount >= offset + size);

	atomic_add_int((int *)&mbio->bio_driver_info, 1);

	/* kernel needs to own the lock for it to be released in biodone */
	BUF_KERNPROC(bp);
	bp->b_vp = vp;
	bp->b_cmd = mbp->b_cmd;
	bp->b_bio1.bio_done = nestiobuf_iodone;
	bp->b_data = (char *)mbp->b_data + offset;
	bp->b_resid = bp->b_bcount = size;
	bp->b_bufsize = bp->b_bcount;

	bp->b_bio1.bio_track = NULL;
	bp->b_bio1.bio_caller_info1.ptr = mbio;
	bp->b_bio1.bio_caller_info2.ptr = stats;
}

#ifdef DDB

DB_SHOW_COMMAND(buffer, db_show_buffer)
{
	/* get args */
	struct buf *bp = (struct buf *)addr;

	if (!have_addr) {
		db_printf("usage: show buffer <addr>\n");
		return;
	}

	db_printf("b_flags = 0x%b\n", (u_int)bp->b_flags, PRINT_BUF_FLAGS);
	db_printf("b_cmd = %d\n", bp->b_cmd);
	db_printf("b_error = %d, b_bufsize = %d, b_bcount = %d, "
		  "b_resid = %d\n, b_data = %p, "
		  "bio_offset(disk) = %lld, bio_offset(phys) = %lld\n",
		  bp->b_error, bp->b_bufsize, bp->b_bcount, bp->b_resid,
		  bp->b_data,
		  (long long)bp->b_bio2.bio_offset,
		  (long long)(bp->b_bio2.bio_next ?
				bp->b_bio2.bio_next->bio_offset : (off_t)-1));
	if (bp->b_xio.xio_npages) {
		int i;
		db_printf("b_xio.xio_npages = %d, pages(OBJ, IDX, PA): ",
			bp->b_xio.xio_npages);
		for (i = 0; i < bp->b_xio.xio_npages; i++) {
			vm_page_t m;
			m = bp->b_xio.xio_pages[i];
			db_printf("(%p, 0x%lx, 0x%lx)", (void *)m->object,
			    (u_long)m->pindex, (u_long)VM_PAGE_TO_PHYS(m));
			if ((i + 1) < bp->b_xio.xio_npages)
				db_printf(",");
		}
		db_printf("\n");
	}
}
#endif /* DDB */
