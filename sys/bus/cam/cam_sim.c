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
 * $DragonFly: src/sys/bus/cam/cam_sim.c,v 1.6 2004/03/15 01:10:30 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include "cam.h"
#include "cam_ccb.h"
#include "cam_sim.h"
#include "cam_queue.h"

#define CAM_PATH_ANY (u_int32_t)-1

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
	      int max_dev_transactions,
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

	sim = malloc(sizeof(struct cam_sim), M_DEVBUF, M_INTWAIT | M_ZERO);
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
	callout_handle_init(&sim->c_handle);
	sim->devq = queue;

	return (sim);
}

void
cam_sim_free(struct cam_sim *sim)
{
	cam_sim_release(sim, CAM_SIM_DEVQ | CAM_SIM_SOFTC);
}

void
cam_sim_release(struct cam_sim *sim, int flags)
{
	if (flags & CAM_SIM_SOFTC)
		sim->softc = NULL;
	if (flags & CAM_SIM_DEVQ) {
		cam_simq_release(sim->devq);
		sim->devq = NULL;
	}
	if (sim->refcount == 1) {
		sim->refcount = 0;
		free(sim, M_DEVBUF);
	} else {
		--sim->refcount;
	}
}

void
cam_sim_set_path(struct cam_sim *sim, u_int32_t path_id)
{
	sim->path_id = path_id;
}
