/*
 * Common functions for SCSI Interface Modules (SIMs).
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/cam/cam_sim.c,v 1.3 1999/08/28 00:40:42 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/spinlock.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#include "cam.h"
#include "cam_ccb.h"
#include "cam_sim.h"
#include "cam_queue.h"
#include "cam_xpt_sim.h"

#define CAM_PATH_ANY (u_int32_t)-1

MALLOC_DEFINE(M_CAMSIM, "CAM SIM", "CAM SIM buffers");

/* Drivers will use sim_mplock if they need the BGL */
sim_lock	sim_mplock;

void
cam_sim_lock(sim_lock *lock)
{
	if (lock == &sim_mplock)
		get_mplock();
	else
		lockmgr(lock, LK_EXCLUSIVE);
}

void
cam_sim_unlock(sim_lock *lock)
{
	if (lock == &sim_mplock)
		rel_mplock();
	else
		lockmgr(lock, LK_RELEASE);
}

int
cam_sim_cond_lock(sim_lock *lock)
{
	if (lock == &sim_mplock) {
		get_mplock();
		return(1);
	} else if (lockstatus(lock, curthread) != LK_EXCLUSIVE) {
		lockmgr(lock, LK_EXCLUSIVE);
		return(1);
	}
	return(0);
}

void
cam_sim_cond_unlock(sim_lock *lock, int doun)
{
	if (doun) {
		if (lock == &sim_mplock)
			rel_mplock();
		else
			lockmgr(lock, LK_RELEASE);
	}
}

/*
 * lock can be NULL if sim was &dead_sim
 */
void
sim_lock_assert_owned(sim_lock *lock)
{
	if (lock) {
		if (lock == &sim_mplock)
			ASSERT_MP_LOCK_HELD();
		else
			KKASSERT(lockstatus(lock, curthread) != 0);
	}
}

void
sim_lock_assert_unowned(sim_lock *lock)
{
	if (lock) {
		if (lock != &sim_mplock)
			KKASSERT(lockstatus(lock, curthread) == 0);
	}
}

int
sim_lock_sleep(void *ident, int flags, const char *wmesg, int timo,
	       sim_lock *lock)
{
	int retval;

	if (lock != &sim_mplock) {
		/* lock should be held already */
		KKASSERT(lockstatus(lock, curthread) != 0);
		tsleep_interlock(ident, flags);
		lockmgr(lock, LK_RELEASE);
		retval = tsleep(ident, flags | PINTERLOCKED, wmesg, timo);
	} else {
		retval = tsleep(ident, flags, wmesg, timo);
	}

	if (lock != &sim_mplock) {
		lockmgr(lock, LK_EXCLUSIVE);
	}

	return (retval);
}

struct cam_devq *
cam_simq_alloc(u_int32_t max_sim_transactions)
{
	return (cam_devq_alloc(/*size*/0, max_sim_transactions));
}

void
cam_simq_release(struct cam_devq *devq)
{
	cam_devq_release(devq);
}

/*
 * cam_sim_alloc() may potentially be called from an interrupt (?) but
 * unexpected things happen to the system if malloc() returns NULL so we
 * use M_INTWAIT anyway.
 */
struct cam_sim *
cam_sim_alloc(sim_action_func sim_action, sim_poll_func sim_poll,
	      const char *sim_name, void *softc, u_int32_t unit,
	      sim_lock *lock, int max_dev_transactions,
	      int max_tagged_dev_transactions, struct cam_devq *queue)
{
	struct cam_sim *sim;

	/*
	 * XXX ahd was limited to 256 instead of 512 for unknown reasons,
	 * move that to a global limit here.  We may be able to remove this
	 * code, needs testing.
	 */
	if (max_dev_transactions > 256)
		max_dev_transactions = 256;
	if (max_tagged_dev_transactions > 256)
		max_tagged_dev_transactions = 256;

	/*
	 * Allocate a simq or use the supplied (possibly shared) simq.
	 */
	if (queue == NULL)
		queue = cam_simq_alloc(max_tagged_dev_transactions);
	else
		cam_devq_reference(queue);

	if (lock == NULL)
		return (NULL);

	sim = kmalloc(sizeof(struct cam_sim), M_CAMSIM, M_INTWAIT | M_ZERO);
	sim->sim_action = sim_action;
	sim->sim_poll = sim_poll;
	sim->sim_name = sim_name;
	sim->softc = softc;
	sim->path_id = CAM_PATH_ANY;
	sim->unit_number = unit;
	sim->bus_id = 0;	/* set in xpt_bus_register */
	sim->max_tagged_dev_openings = max_tagged_dev_transactions;
	sim->max_dev_openings = max_dev_transactions;
	sim->flags = 0;
	sim->refcount = 1;
	sim->devq = queue;
	sim->lock = lock;
	if (lock == &sim_mplock) {
		sim->flags |= 0;
		callout_init(&sim->callout);
	} else {
		sim->flags |= CAM_SIM_MPSAFE;
		callout_init_mp(&sim->callout);
	}

	SLIST_INIT(&sim->ccb_freeq);
	TAILQ_INIT(&sim->sim_doneq);
	spin_init(&sim->sim_spin, "cam_sim_alloc");

	return (sim);
}

void
cam_sim_set_max_tags(struct cam_sim *sim, int max_tags)
{
	if (max_tags > 256)
		max_tags = 256;
        sim->max_tagged_dev_openings = max_tags;
	cam_devq_set_openings(sim->devq, max_tags);
}

static void deadsim_poll(struct cam_sim *sim);
static void deadsim_action(struct cam_sim *sim, union ccb *ccb);

void
cam_sim_free(struct cam_sim *sim)
{
	sim->sim_action = deadsim_action;
	sim->sim_poll = deadsim_poll;
	cam_sim_release(sim, CAM_SIM_SOFTC);
}

/*
 * Note: the devq is still used by individual peripherals even if the
 * backend sim disappears, so do not destroy it until the sim itself
 * is no longer needed.
 */
void
cam_sim_release(struct cam_sim *sim, int flags)
{
	if (flags & CAM_SIM_SOFTC)
		sim->softc = NULL;
	if (sim->refcount == 1) {
		if (sim->devq) {
			cam_simq_release(sim->devq);
			sim->devq = NULL;
		}
		sim->refcount = 0;
		kfree(sim, M_CAMSIM);
	} else {
		--sim->refcount;
	}
}

void
cam_sim_set_path(struct cam_sim *sim, u_int32_t path_id)
{
	sim->path_id = path_id;
}

/*
 * Dead sim poll and action functions.  The backend to the sim has gone
 * away, aka usb, scsi device, etc... deal with it.
 */
static void
deadsim_poll(struct cam_sim *sim)
{
	/* empty */
}

static void
deadsim_action(struct cam_sim *sim, union ccb *ccb)
{
	ccb->ccb_h.status = CAM_TID_INVALID;
	xpt_done(ccb);
}
