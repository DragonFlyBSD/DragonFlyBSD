/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
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
 * Administration thread
 *
 * - Handles resetting, features, iteration of namespaces, and disk
 *   attachments.  Most admin operations are serialized by the admin thread.
 *
 * - Ioctls as well as any BIOs which require more sophisticated processing
 *   are handed to this thread as well.
 *
 * - Can freeze/resume other queues for various purposes.
 */

#include "nvme.h"

static void nvme_admin_thread(void *arg);
static int nvme_admin_state_identify_ctlr(nvme_softc_t *sc);
static int nvme_admin_state_make_queues(nvme_softc_t *sc);
static int nvme_admin_state_identify_ns(nvme_softc_t *sc);
static int nvme_admin_state_operating(nvme_softc_t *sc);
static int nvme_admin_state_failed(nvme_softc_t *sc);

/*
 * Start the admin thread and block until it says it is running.
 */
int
nvme_start_admin_thread(nvme_softc_t *sc)
{
	int error;

	lockinit(&sc->admin_lk, "admlk", 0, 0);
	sc->admin_signal = 0;

	error = bus_setup_intr(sc->dev, sc->irq[0], INTR_MPSAFE,
			       nvme_intr, &sc->comqueues[0],
			       &sc->irq_handle[0], NULL);
	if (error) {
		device_printf(sc->dev, "unable to install interrupt\n");
		return error;
	}
	lockmgr(&sc->admin_lk, LK_EXCLUSIVE);
	kthread_create(nvme_admin_thread, sc, &sc->admintd, "nvme_admin");
	while ((sc->admin_signal & ADMIN_SIG_RUNNING) == 0)
		lksleep(&sc->admin_signal, &sc->admin_lk, 0, "nvwbeg", 0);
	lockmgr(&sc->admin_lk, LK_RELEASE);

	return 0;
}

/*
 * Stop the admin thread and block until it says it is done.
 */
void
nvme_stop_admin_thread(nvme_softc_t *sc)
{
	uint32_t i;

	atomic_set_int(&sc->admin_signal, ADMIN_SIG_STOP);

	/*
	 * We have to wait for the admin thread to finish its probe
	 * before shutting it down.
	 */
	lockmgr(&sc->admin_lk, LK_EXCLUSIVE);
	while ((sc->admin_signal & ADMIN_SIG_PROBED) == 0)
		lksleep(&sc->admin_signal, &sc->admin_lk, 0, "nvwend", 0);
	lockmgr(&sc->admin_lk, LK_RELEASE);

	/*
	 * Disconnect our disks while the admin thread is still running,
	 * ensuring that the poll works even if interrupts are broken.
	 * Otherwise we could deadlock in the devfs core.
	 */
	for (i = 0; i < NVME_MAX_NAMESPACES; ++i) {
		nvme_softns_t *nsc;

		if ((nsc = sc->nscary[i]) != NULL) {
			nvme_disk_detach(nsc);

			kfree(nsc, M_NVME);
			sc->nscary[i] = NULL;
		}
	}

	/*
	 * Ask the admin thread to shut-down.
	 */
	lockmgr(&sc->admin_lk, LK_EXCLUSIVE);
	wakeup(&sc->admin_signal);
	while (sc->admin_signal & ADMIN_SIG_RUNNING)
		lksleep(&sc->admin_signal, &sc->admin_lk, 0, "nvwend", 0);
	lockmgr(&sc->admin_lk, LK_RELEASE);
	if (sc->irq_handle[0]) {
		bus_teardown_intr(sc->dev, sc->irq[0], sc->irq_handle[0]);
		sc->irq_handle[0] = NULL;
	}
	lockuninit(&sc->admin_lk);

	/*
	 * Thread might be running on another cpu, give it time to actually
	 * exit before returning in case the caller is about to unload the
	 * module.  Otherwise we don't need this.
	 */
	nvme_os_sleep(1);
}

static
void
nvme_admin_thread(void *arg)
{
	nvme_softc_t *sc = arg;
	uint32_t i;

	lockmgr(&sc->admin_lk, LK_EXCLUSIVE);
	atomic_set_int(&sc->admin_signal, ADMIN_SIG_RUNNING);
	wakeup(&sc->admin_signal);

	sc->admin_func = nvme_admin_state_identify_ctlr;

	while ((sc->admin_signal & ADMIN_SIG_STOP) == 0) {
		for (i = 0; i <= sc->niocomqs; ++i) {
			nvme_comqueue_t *comq = &sc->comqueues[i];

			if (comq->nqe == 0)	/* not configured */
				continue;

			lockmgr(&comq->lk, LK_EXCLUSIVE);
			nvme_poll_completions(comq, &comq->lk);
			lockmgr(&comq->lk, LK_RELEASE);
		}
		if (sc->admin_signal & ADMIN_SIG_REQUEUE) {
			atomic_clear_int(&sc->admin_signal, ADMIN_SIG_REQUEUE);
			nvme_disk_requeues(sc);
		}
		if (sc->admin_func(sc) == 0 &&
		    (sc->admin_signal & ADMIN_SIG_RUN_MASK) == 0) {
			lksleep(&sc->admin_signal, &sc->admin_lk, 0,
				"nvidle", hz);
		}
	}

	/*
	 * Cleanup state.
	 *
	 * Note that we actually issue delete queue commands here.  The NVME
	 * spec says that for a normal shutdown the I/O queues should be
	 * deleted prior to issuing the shutdown in the CONFIG register.
	 */
	for (i = 1; i <= sc->niosubqs; ++i) {
		nvme_delete_subqueue(sc, i);
		nvme_free_subqueue(sc, i);
	}
	for (i = 1; i <= sc->niocomqs; ++i) {
		nvme_delete_comqueue(sc, i);
		nvme_free_comqueue(sc, i);
	}

	/*
	 * Signal that we are done.
	 */
	atomic_clear_int(&sc->admin_signal, ADMIN_SIG_RUNNING);
	wakeup(&sc->admin_signal);
	lockmgr(&sc->admin_lk, LK_RELEASE);
}

/*
 * Identify the controller
 */
static
int
nvme_admin_state_identify_ctlr(nvme_softc_t *sc)
{
	nvme_request_t *req;
	nvme_ident_ctlr_data_t *rp;
	int status;
	uint64_t mempgsize;
	char serial[20+16];
	char model[40+16];

	/*
	 * Identify Controller
	 */
	mempgsize = NVME_CAP_MEMPG_MIN_GET(sc->cap);

	req = nvme_get_admin_request(sc, NVME_OP_IDENTIFY);
	req->cmd.identify.cns = NVME_CNS_CTLR;
	req->cmd.identify.cntid = 0;
	bzero(req->info, sizeof(*req->info));
	nvme_submit_request(req);
	status = nvme_wait_request(req);
	/* XXX handle status */

	sc->idctlr = req->info->idctlr;
	nvme_put_request(req);

	rp = &sc->idctlr;

	KKASSERT(sizeof(sc->idctlr.serialno) == 20);
	KKASSERT(sizeof(sc->idctlr.modelno) == 40);
	bzero(serial, sizeof(serial));
	bzero(model, sizeof(model));
	bcopy(rp->serialno, serial, sizeof(rp->serialno));
	bcopy(rp->modelno, model, sizeof(rp->modelno));
	string_cleanup(serial, 0);
	string_cleanup(model, 0);

	device_printf(sc->dev, "Model %s BaseSerial %s nscount=%d\n",
		      model, serial, rp->ns_count);

	sc->admin_func = nvme_admin_state_make_queues;

	return 1;
}

/*
 * Request and create the I/O queues.  Figure out CPU mapping optimizations.
 */
static
int
nvme_admin_state_make_queues(nvme_softc_t *sc)
{
	nvme_request_t *req;
	uint16_t niosubqs;
	uint16_t niocomqs;
	uint32_t i;
	uint16_t qno;
	int status;
	int error;

	/*
	 * Calculate how many I/O queues (non-inclusive of admin queue)
	 * we want to have, up to 65535.  dw0 in the response returns the
	 * number of queues the controller gives us.  Submission and
	 * Completion queues are specified separately.
	 *
	 * This driver runs optimally with 4 submission queues and one
	 * completion queue per cpu (rdhipri, rdlopri, wrhipri, wrlopri),
	 *
	 * +1 for dumps
	 * +1 for async events
	 */
	req = nvme_get_admin_request(sc, NVME_OP_SET_FEATURES);

	niosubqs = ncpus * 4 + 2;
	niocomqs = ncpus + 2;
	if (niosubqs > NVME_MAX_QUEUES)
		niosubqs = NVME_MAX_QUEUES;
	if (niocomqs > NVME_MAX_QUEUES)
		niocomqs = NVME_MAX_QUEUES;
	device_printf(sc->dev, "Request %u/%u queues, ", niosubqs, niocomqs);

	req->cmd.setfeat.flags = NVME_FID_NUMQUEUES;
	req->cmd.setfeat.numqs.nsqr = niosubqs - 1;	/* 0's based 0=1 */
	req->cmd.setfeat.numqs.ncqr = niocomqs - 1;	/* 0's based 0=1 */

	nvme_submit_request(req);

	/*
	 * Get response and set our operations mode.
	 */
	status = nvme_wait_request(req);
	/* XXX handle status */

	if (status == 0) {
		sc->niosubqs = 1 + (req->res.setfeat.dw0 & 0xFFFFU);
		sc->niocomqs = 1 + ((req->res.setfeat.dw0 >> 16) & 0xFFFFU);
	} else {
		sc->niosubqs = 0;
		sc->niocomqs = 0;
	}
	kprintf("Returns %u/%u queues, ", sc->niosubqs, sc->niocomqs);

	nvme_put_request(req);

	if (sc->niosubqs >= ncpus * 4 + 2 && sc->niocomqs >= ncpus + 2) {
		/*
		 * If we got all the queues we wanted do a full-bore setup of
		 * qmap[cpu][type].
		 */
		kprintf("optimal map\n");
		sc->dumpqno = 1;
		sc->eventqno = 2;
		sc->subqueues[1].comqid = 1;
		sc->subqueues[2].comqid = 2;
		qno = 3;
		for (i = 0; i < ncpus; ++i) {
			int cpuqno = sc->cputovect[i];
			if (cpuqno == 0)	/* don't use admincomq */
				cpuqno = 1;
			sc->qmap[i][0] = qno + 0;
			sc->qmap[i][1] = qno + 1;
			sc->qmap[i][2] = qno + 2;
			sc->qmap[i][3] = qno + 3;
			sc->subqueues[qno + 0].comqid = cpuqno;
			sc->subqueues[qno + 1].comqid = cpuqno;
			sc->subqueues[qno + 2].comqid = cpuqno;
			sc->subqueues[qno + 3].comqid = cpuqno;
			qno += 4;
		}
		sc->niosubqs = ncpus * 4 + 2;
		sc->niocomqs = ncpus + 2;
	} else if (sc->niosubqs >= ncpus && sc->niocomqs >= ncpus) {
		/*
		 * We have enough to give each cpu its own submission
		 * and completion queue.
		 */
		kprintf("nominal map 1:1 cpu\n");
		sc->dumpqno = 0;
		sc->eventqno = 0;
		for (i = 0; i < ncpus; ++i) {
			qno = sc->cputovect[i];
			if (qno == 0)		/* don't use admincomq */
				qno = 1;
			sc->qmap[i][0] = qno + 0;
			sc->qmap[i][1] = qno + 0;
			sc->qmap[i][2] = qno + 0;
			sc->qmap[i][3] = qno + 0;
			sc->subqueues[qno + 0].comqid = qno + 0;
			sc->subqueues[qno + 1].comqid = qno + 0;
			sc->subqueues[qno + 2].comqid = qno + 0;
			sc->subqueues[qno + 3].comqid = qno + 0;
		}
		sc->niosubqs = ncpus;
		sc->niocomqs = ncpus;
	} else if (sc->niosubqs >= 6 && sc->niocomqs >= 3) {
		/*
		 * We have enough queues to separate and prioritize reads
		 * and writes, plus dumps and async events.
		 *
		 * We may or may not have enough comqs to match up cpus.
		 */
		kprintf("rw-sep map\n");
		sc->dumpqno = 1;
		sc->eventqno = 2;
		sc->subqueues[1].comqid = 1;
		sc->subqueues[2].comqid = 2;
		qno = 3;
		for (i = 0; i < ncpus; ++i) {
			int cpuqno = sc->cputovect[i];
			if (cpuqno == 0)	/* don't use admincomq */
				cpuqno = 1;
			sc->qmap[i][0] = qno + 0;
			sc->qmap[i][1] = qno + 1;
			sc->qmap[i][2] = qno + 2;
			sc->qmap[i][3] = qno + 3;
			sc->subqueues[qno + 0].comqid = cpuqno;
			sc->subqueues[qno + 1].comqid = cpuqno;
			sc->subqueues[qno + 2].comqid = cpuqno;
			sc->subqueues[qno + 3].comqid = cpuqno;
		}
		sc->niosubqs = 6;
		sc->niocomqs = 3;
	} else if (sc->niosubqs >= 2) {
		/*
		 * We have enough to have separate read and write queues.
		 */
		kprintf("basic map\n");
		qno = 1;
		sc->dumpqno = 0;
		sc->eventqno = 0;
		for (i = 0; i < ncpus; ++i) {
			int cpuqno = sc->cputovect[i];
			if (cpuqno == 0)	/* don't use admincomq */
				cpuqno = 1;
			sc->qmap[i][0] = qno + 0;	/* read low pri */
			sc->qmap[i][1] = qno + 0;	/* read high pri */
			sc->qmap[i][2] = qno + 1;	/* write low pri */
			sc->qmap[i][3] = qno + 1;	/* write high pri */
			sc->subqueues[qno + 0].comqid = cpuqno;
			sc->subqueues[qno + 1].comqid = cpuqno;
		}
		sc->niosubqs = 2;
		sc->niocomqs = 1;
	} else {
		/*
		 * Minimal configuration, all cpus and I/O types use the
		 * same queue.
		 */
		kprintf("minimal map\n");
		sc->dumpqno = 0;
		sc->eventqno = 0;
		for (i = 0; i < ncpus; ++i) {
			sc->qmap[i][0] = 1;
			sc->qmap[i][1] = 1;
			sc->qmap[i][2] = 1;
			sc->qmap[i][3] = 1;
		}
		sc->subqueues[1].comqid = 1;
		sc->niosubqs = 1;
		sc->niocomqs = 1;
	}

	/*
	 * Create all I/O submission and completion queues.  The I/O
	 * queues start at 1 and are inclusive of niosubqs and niocomqs.
	 *
	 * NOTE: Completion queues must be created before submission queues.
	 *	 That is, the completion queue specified when creating a
	 *	 submission queue must already exist.
	 */
	error = 0;
	for (i = 1; i <= sc->niocomqs; ++i) {
		error += nvme_alloc_comqueue(sc, i);
		if (error) {
			device_printf(sc->dev, "Unable to allocate comqs\n");
			break;
		}
		error += nvme_create_comqueue(sc, i);
	}
	for (i = 1; i <= sc->niosubqs; ++i) {
		error += nvme_alloc_subqueue(sc, i);
		if (error) {
			device_printf(sc->dev, "Unable to allocate subqs\n");
			break;
		}
		error += nvme_create_subqueue(sc, i);
	}

	if (error) {
		device_printf(sc->dev, "Failed to initialize device!\n");
		sc->admin_func = nvme_admin_state_failed;
	} else {
		sc->admin_func = nvme_admin_state_identify_ns;
	}

	return 1;
}

/*
 * Identify available namespaces, iterate, and attach to disks.
 */
static
int
nvme_admin_state_identify_ns(nvme_softc_t *sc)
{
	nvme_request_t *req;
	nvme_nslist_data_t *rp;
	int status;
	uint32_t i;
	uint32_t j;

	/*
	 * Identify Namespace List
	 */
	req = nvme_get_admin_request(sc, NVME_OP_IDENTIFY);
	req->cmd.identify.cns = NVME_CNS_ACT_NSLIST;
	req->cmd.identify.cntid = 0;
	bzero(req->info, sizeof(*req->info));
	nvme_submit_request(req);
	status = nvme_wait_request(req);
	/* XXX handle status */

	sc->nslist = req->info->nslist;
	nvme_put_request(req);

	/*
	 * Identify each Namespace
	 */
	rp = &sc->nslist;
	for (i = 0; i < sc->idctlr.ns_count; ++i) {
		nvme_softns_t *nsc;
		nvme_lba_fmt_data_t *lbafmt;

		if (rp->nids[i] == 0)
			continue;

		req = nvme_get_admin_request(sc, NVME_OP_IDENTIFY);
		req->cmd.identify.cns = NVME_CNS_ACT_NS;
		req->cmd.identify.cntid = 0;
		req->cmd.identify.head.nsid = rp->nids[i];
		bzero(req->info, sizeof(*req->info));
		nvme_submit_request(req);
		status = nvme_wait_request(req);
		if (status != 0)
			continue;

		for (j = 0; j < NVME_MAX_NAMESPACES; ++j) {
			if (sc->nscary[j] &&
			    sc->nscary[j]->nsid == rp->nids[i])
				break;
		}
		if (j == NVME_MAX_NAMESPACES) {
			j = i;
			if (sc->nscary[j] != NULL) {
				for (j = NVME_MAX_NAMESPACES - 1; j >= 0; --j) {
					if (sc->nscary[j] == NULL)
						break;
				}
			}
		}
		if (j < 0) {
			device_printf(sc->dev, "not enough room in nscary for "
					       "namespace %08x\n", rp->nids[i]);
			nvme_put_request(req);
			continue;
		}
		nsc = sc->nscary[j];
		if (nsc == NULL) {
			nsc = kmalloc(sizeof(*nsc), M_NVME, M_WAITOK | M_ZERO);
			nsc->unit = nvme_alloc_disk_unit();
			sc->nscary[j] = nsc;
		}
		if (sc->nscmax <= j)
			sc->nscmax = j + 1;
		nsc->sc = sc;
		nsc->nsid = rp->nids[i];
		nsc->state = NVME_NSC_STATE_UNATTACHED;
		nsc->idns = req->info->idns;
		bioq_init(&nsc->bioq);
		lockinit(&nsc->lk, "nvnsc", 0, 0);

		nvme_put_request(req);

		j = NVME_FLBAS_SEL_GET(nsc->idns.flbas);
		lbafmt = &nsc->idns.lba_fmt[j];
		nsc->blksize = 1 << lbafmt->sect_size;

		/*
		 * Attach the namespace
		 */
		nvme_disk_attach(nsc);
	}

	sc->admin_func = nvme_admin_state_operating;
	return 1;
}

static
int
nvme_admin_state_operating(nvme_softc_t *sc)
{
	if ((sc->admin_signal & ADMIN_SIG_PROBED) == 0) {
		atomic_set_int(&sc->admin_signal, ADMIN_SIG_PROBED);
		wakeup(&sc->admin_signal);
	}

	return 0;
}

static
int
nvme_admin_state_failed(nvme_softc_t *sc)
{
	if ((sc->admin_signal & ADMIN_SIG_PROBED) == 0) {
		atomic_set_int(&sc->admin_signal, ADMIN_SIG_PROBED);
		wakeup(&sc->admin_signal);
	}

	return 0;
}
