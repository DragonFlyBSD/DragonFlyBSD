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
 * Most low-level chip related functions (other than attachment) reside in
 * this module.  Most functions assume that the caller is already holding
 * appropriate locks to prevent SMP collisions.
 */

#include "nvme.h"

MALLOC_DEFINE(M_NVME, "NVMe Storage Device", "NVME");

/*
 * DMA mapping callbacks.
 */
static
void
nvme_dmamem_saveseg(void *info, bus_dma_segment_t *segs, int nsegs, int error)
{
        KKASSERT(error == 0);
	KKASSERT(nsegs == 1);
	*(bus_addr_t *)info = segs->ds_addr;
}

/*
 * Low-level chip enable/disable.
 */
int
nvme_enable(nvme_softc_t *sc, int enable)
{
	uint32_t reg;
	int error = 0;
	int base_ticks;

	reg = nvme_read(sc, NVME_REG_CONFIG);
	if (enable == 0 && (reg & NVME_CONFIG_EN)) {
		/*
		 * Disable the chip so we can program it.
		 */
		reg &= ~NVME_CONFIG_EN;
		nvme_write(sc, NVME_REG_CONFIG, reg);
	} else if (enable && (reg & NVME_CONFIG_EN) == 0) {
		/*
		 * Enable the chip once programmed.
		 */
		reg |= NVME_CONFIG_EN;
		nvme_write(sc, NVME_REG_CONFIG, reg);
	}
	error = ENXIO;
	base_ticks = ticks;
	while ((int)(ticks - base_ticks) < sc->entimo) {
		reg = nvme_read(sc, NVME_REG_STATUS);
		if (enable == 0 && (reg & NVME_STATUS_RDY) == 0) {
			error = 0;
			break;
		}
		if (enable && (reg & NVME_STATUS_RDY)) {
			error = 0;
			break;
		}
		nvme_os_sleep(50);	/* 50ms poll */
	}
	if (error) {
		device_printf(sc->dev, "Cannot %s device\n",
			      (enable ? "enable" : "disable"));
	}
	return error;
}

/*
 * Allocate submission and completion queues.  If qid is 0 we are allocating
 * the ADMIN queues, otherwise we are allocating I/O queues.
 */
int
nvme_alloc_subqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_subqueue_t *queue = &sc->subqueues[qid];
	int error = 0;

	/*
	 * For now implement the maximum queue size negotiated in the
	 * attach.
	 */
	lockinit(&queue->lk, "nvqlk", 0, 0);
	queue->sc = sc;
	queue->nqe = sc->maxqe;
	queue->qid = qid;
	queue->subq_doorbell_reg = NVME_REG_SUBQ_BELL(qid, sc->dstrd4);

	/*
	 * dma memory for the submission queue
	 */
	if (error == 0) {
		error = bus_dmamem_alloc(sc->sque_tag, (void **)&queue->ksubq,
					 BUS_DMA_ZERO, &queue->sque_map);
	}
	if (error == 0) {
		error = bus_dmamap_load(sc->sque_tag, queue->sque_map,
					queue->ksubq,
					bus_dma_tag_getmaxsize(sc->sque_tag),
					nvme_dmamem_saveseg, &queue->psubq,
					0);
	}

	/*
	 * dma memory for enough PRPs to map MAXPHYS bytes of memory per
	 * request.  A MAXPHYS buffer which begins partially straddling
	 * a page boundary can still be accomodated because we have an
	 * additional PRP entry in cmd.head.
	 */
	if (error == 0) {
		error = bus_dmamem_alloc(sc->prps_tag, (void **)&queue->kprps,
					 BUS_DMA_ZERO, &queue->prps_map);
	}
	if (error == 0) {
		error = bus_dmamap_load(sc->prps_tag, queue->prps_map,
					queue->kprps,
					bus_dma_tag_getmaxsize(sc->prps_tag),
					nvme_dmamem_saveseg, &queue->pprps,
					0);
	}

	/*
	 * dma memory for admin data
	 */
	if (qid == 0 && error == 0) {
		error = bus_dmamem_alloc(sc->adm_tag,
					 (void **)&queue->kdatapgs,
					 BUS_DMA_ZERO, &queue->adm_map);
	}
	if (qid == 0 && error == 0) {
		error = bus_dmamap_load(sc->adm_tag, queue->adm_map,
					queue->kdatapgs,
					bus_dma_tag_getmaxsize(sc->adm_tag),
					nvme_dmamem_saveseg, &queue->pdatapgs,
					0);
	}

	/*
	 * Driver request structures
	 */
	if (error == 0) {
		nvme_request_t *req;
		uint32_t i;

		queue->reqary = kmalloc(sizeof(nvme_request_t) * queue->nqe,
					M_NVME, M_WAITOK | M_ZERO);
		for (i = 0; i < queue->nqe; ++i) {
			req = &queue->reqary[i];
			req->next_avail = queue->first_avail;
			queue->first_avail = req;
			req->subq = queue;
			req->comq = &sc->comqueues[queue->comqid];
			req->cmd_id = i;
			if (qid == 0) {
				req->info = &queue->kdatapgs[i];
				req->pinfo = queue->pdatapgs +
					     i * sizeof(nvme_admin_data_t);
			}
		}
	}

	/*
	 * Error handling
	 */
	if (error)
		nvme_free_subqueue(sc, qid);
	return error;
}

int
nvme_alloc_comqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_comqueue_t *queue = &sc->comqueues[qid];
	int error = 0;

	/*
	 * For now implement the maximum queue size negotiated in the
	 * attach.
	 */
	lockinit(&queue->lk, "nvqlk", 0, 0);
	queue->sc = sc;
	queue->nqe = sc->maxqe;
	queue->qid = qid;
	queue->phase = NVME_COMQ_STATUS_PHASE;
	queue->comq_doorbell_reg = NVME_REG_COMQ_BELL(qid, sc->dstrd4);

	if (error == 0) {
		error = bus_dmamem_alloc(sc->cque_tag, (void **)&queue->kcomq,
					 BUS_DMA_ZERO, &queue->cque_map);
	}
	if (error == 0) {
		error = bus_dmamap_load(sc->cque_tag, queue->cque_map,
					queue->kcomq,
					bus_dma_tag_getmaxsize(sc->cque_tag),
					nvme_dmamem_saveseg, &queue->pcomq,
					0);
	}

	/*
	 * Error handling
	 */
	if (error)
		nvme_free_comqueue(sc, qid);
	return error;
}

void
nvme_free_subqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_subqueue_t *queue = &sc->subqueues[qid];

	queue->first_avail = NULL;
	if (queue->reqary) {
		kfree(queue->reqary, M_NVME);
		queue->reqary = NULL;
	}
	if (queue->ksubq) {
		bus_dmamem_free(sc->sque_tag, queue->ksubq, queue->sque_map);
		bus_dmamap_unload(sc->sque_tag, queue->sque_map);
		bus_dmamap_destroy(sc->sque_tag, queue->sque_map);
	}
	if (queue->kprps) {
		bus_dmamem_free(sc->prps_tag, queue->kprps, queue->prps_map);
		bus_dmamap_unload(sc->prps_tag, queue->prps_map);
		bus_dmamap_destroy(sc->prps_tag, queue->prps_map);
	}
	if (queue->kdatapgs) {
		bus_dmamem_free(sc->adm_tag, queue->kdatapgs, queue->adm_map);
		bus_dmamap_unload(sc->adm_tag, queue->adm_map);
		bus_dmamap_destroy(sc->adm_tag, queue->adm_map);
	}
	bzero(queue, sizeof(*queue));
}

void
nvme_free_comqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_comqueue_t *queue = &sc->comqueues[qid];

	if (queue->kcomq) {
		bus_dmamem_free(sc->cque_tag, queue->kcomq, queue->cque_map);
		bus_dmamap_unload(sc->cque_tag, queue->cque_map);
		bus_dmamap_destroy(sc->cque_tag, queue->cque_map);
	}
	bzero(queue, sizeof(*queue));
}

/*
 * ADMIN AND I/O REQUEST HANDLING
 */

/*
 * Obtain a request and handle DMA mapping the supplied kernel buffer.
 * Fields in cmd.head will be initialized and remaining fields will be zero'd.
 * Caller is responsible for filling in remaining fields as appropriate.
 *
 * Caller must hold the queue lock.
 */
nvme_request_t *
nvme_get_admin_request(nvme_softc_t *sc, uint8_t opcode)
{
	nvme_request_t *req;

	req = nvme_get_request(&sc->subqueues[0], opcode, NULL, 0);
	req->cmd.head.prp1 = req->pinfo;
	req->callback = NULL;

	return req;
}

/*
 * ADMIN AND I/O REQUEST HANDLING
 */

/*
 * Obtain a request and handle DMA mapping the supplied kernel buffer.
 * Fields in cmd.head will be initialized and remaining fields will be zero'd.
 * Caller is responsible for filling in remaining fields as appropriate.
 *
 * May return NULL if no requests are available.
 *
 * Caller does NOT have to hold the queue lock.
 */
nvme_request_t *
nvme_get_request(nvme_subqueue_t *queue, uint8_t opcode,
		 char *kva, size_t bytes)
{
	nvme_request_t *req;
	nvme_request_t *next;

	/*
	 * The lock is currently needed because a another cpu could pull
	 * a request off, use it, finish, and put it back (and next pointer
	 * might then be different) all inbetween our req = and our atomic
	 * op.  This would assign the wrong 'next' field.
	 *
	 * XXX optimize this.
	 */
	lockmgr(&queue->lk, LK_EXCLUSIVE);
	for (;;) {
		req = queue->first_avail;
		cpu_ccfence();
		if (req == NULL) {
			queue->signal_requeue = 1;
			lockmgr(&queue->lk, LK_RELEASE);
			return NULL;
		}
		next = req->next_avail;
		if (atomic_cmpset_ptr(&queue->first_avail, req, next))
			break;
	}
	lockmgr(&queue->lk, LK_RELEASE);
	req->next_avail = NULL;
	KKASSERT(req->state == NVME_REQ_AVAIL);
	req->state = NVME_REQ_ALLOCATED;
	req->callback = NULL;
	req->waiting = 0;

	req->cmd.head.opcode = opcode;
	req->cmd.head.flags = NVME_SUBQFLG_PRP | NVME_SUBQFLG_NORM;
	req->cmd.head.cid = req->cmd_id;
	req->cmd.head.nsid = 0;
	req->cmd.head.mptr = 0;
	req->cmd.head.prp1 = 0;
	req->cmd.head.prp2 = 0;
	req->cmd.dw10 = 0;
	req->cmd.dw11 = 0;
	req->cmd.dw12 = 0;
	req->cmd.dw13 = 0;
	req->cmd.dw14 = 0;
	req->cmd.dw15 = 0;

	if (kva) {
		size_t count = 0;
		size_t idx = 0;
		vm_paddr_t paddr;
		vm_paddr_t pprptab;
		uint64_t *kprptab;
		KKASSERT(bytes >= 0 && bytes <= MAXPHYS);

		kprptab = queue->kprps +
			  (MAXPHYS / PAGE_SIZE) * req->cmd_id;
		pprptab = queue->pprps +
			  (MAXPHYS / PAGE_SIZE) * req->cmd_id *
			  sizeof(uint64_t);

		while (count < bytes) {
			paddr = vtophys(kva + count);
			if (idx == 0) {
				KKASSERT((paddr & 3) == 0);
				req->cmd.head.prp1 = paddr;
				count += (((intptr_t)kva + PAGE_SIZE) &
					  ~(intptr_t)PAGE_MASK) -
					 (intptr_t)kva;
			} else if (idx == 1 && count + PAGE_SIZE >= bytes) {
				KKASSERT((paddr & PAGE_MASK) == 0);
				req->cmd.head.prp2 = paddr;
				count += PAGE_SIZE;
			} else {
				KKASSERT((paddr & PAGE_MASK) == 0);
				/* if (idx == 1) -- not needed, just repeat */
				req->cmd.head.prp2 = pprptab; /* repeat */
				kprptab[idx - 1] = paddr;
				count += PAGE_SIZE;
			}
			++idx;
		}
	}
	return req;
}

/*
 * Submit request for execution.  This will doorbell the subq.
 *
 * Caller must hold the queue lock.
 */
void
nvme_submit_request(nvme_request_t *req)
{
	nvme_subqueue_t *queue = req->subq;
	nvme_allcmd_t *cmd;

	cmd = &queue->ksubq[queue->subq_tail];
	if (++queue->subq_tail == queue->nqe)
		queue->subq_tail = 0;
	*cmd = req->cmd;
	cpu_sfence();	/* needed? */
	req->state = NVME_REQ_SUBMITTED;
	nvme_write(queue->sc, queue->subq_doorbell_reg, queue->subq_tail);
}

/*
 * Wait for a request to complete.
 *
 * Caller does not need to hold the queue lock.  If it does, or if it
 * holds some other lock, it should pass it in so it can be released across
 * sleeps, else pass NULL.
 */
int
nvme_wait_request(nvme_request_t *req)
{
	struct lock *lk;
	int code;

	req->waiting = 1;
	if (req->state != NVME_REQ_COMPLETED) {
		lk = &req->comq->lk;
		cpu_lfence();
		lockmgr(lk, LK_EXCLUSIVE);
		while (req->state == NVME_REQ_SUBMITTED) {
			nvme_poll_completions(req->comq, lk);
			if (req->state != NVME_REQ_SUBMITTED)
				break;
			lksleep(req, lk, 0, "nvwait", hz);
		}
		lockmgr(lk, LK_RELEASE);
		KKASSERT(req->state == NVME_REQ_COMPLETED);
	}
	cpu_lfence();
	code = NVME_COMQ_STATUS_CODE_GET(req->res.tail.status);

	return code;
}

/*
 * Put request away, making it available for reuse.  If this is an admin
 * request its auxillary data page is also being released for reuse.
 *
 * Caller does NOT have to hold the queue lock.
 */
void
nvme_put_request(nvme_request_t *req)
{
	nvme_subqueue_t *queue = req->subq;
	nvme_request_t *next;

	/*
	 * Insert on head for best cache reuse.
	 */
	KKASSERT(req->state == NVME_REQ_COMPLETED);
	req->state = NVME_REQ_AVAIL;
	for (;;) {
		next = queue->first_avail;
		cpu_ccfence();
		req->next_avail = next;
		if (atomic_cmpset_ptr(&queue->first_avail, next, req))
			break;
	}

	/*
	 * If BIOs were deferred due to lack of request space signal the
	 * admin thread to requeue them.  This is a bit messy and normally
	 * should not happen due to the large number of queue entries nvme
	 * usually has.  Let it race for now (admin has a 1hz tick).
	 */
	if (queue->signal_requeue) {
		queue->signal_requeue = 0;
		atomic_set_int(&queue->sc->admin_signal, ADMIN_SIG_REQUEUE);
		wakeup(&queue->sc->admin_signal);
	}
}

/*
 * Poll for completions on queue, copy the 16-byte hw result entry
 * into the request and poke the doorbell to update the controller's
 * understanding of comq_head.
 *
 * If lk is non-NULL it will be passed to the callback which typically
 * releases it temporarily when calling biodone() or doing other complex
 * work on the result.
 *
 * Caller must usually hold comq->lk.
 */
void
nvme_poll_completions(nvme_comqueue_t *comq, struct lock *lk)
{
	nvme_softc_t *sc = comq->sc;
	nvme_request_t *req;
	nvme_subqueue_t *subq;
	nvme_allres_t *res;
#if 0
	int didwork = 0;
#endif

	KKASSERT(comq->comq_tail < comq->nqe);
	cpu_lfence();		/* needed prior to first phase test */
	for (;;) {
		/*
		 * WARNING! LOCK MAY HAVE BEEN TEMPORARILY LOST DURING LOOP.
		 */
		res = &comq->kcomq[comq->comq_tail];
		if ((res->tail.status ^ comq->phase) & NVME_COMQ_STATUS_PHASE)
			break;

		/*
		 * Process result on completion queue.
		 *
		 * Bump comq_tail, flip the phase detect when we roll-over.
		 * doorbell every 1/4 queue and at the end of the loop.
		 */
		if (++comq->comq_tail == comq->nqe) {
			comq->comq_tail = 0;
			comq->phase ^= NVME_COMQ_STATUS_PHASE;
		}

		/*
		 * WARNING! I imploded the chip by reusing a command id
		 *	    before it was discarded in the completion queue
		 *	    via the doorbell, so for now we always write
		 *	    the doorbell before marking the request as
		 *	    COMPLETED (it can be reused instantly upon
		 *	    being marked).
		 */
#if 0
		if (++didwork == (comq->nqe >> 2)) {
			didwork = 0;
			nvme_write(comq->sc, comq->comq_doorbell_reg,
				   comq->comq_tail);
		}
#endif
		cpu_lfence();	/* needed prior to content check */

		/*
		 * Locate the request.  The request could be on a different
		 * queue.  Copy the fields and wakeup anyone waiting on req.
		 * The response field in the completion queue can be reused
		 * once we doorbell which is why we make a copy.
		 */
		subq = &sc->subqueues[res->tail.subq_id];
		req = &subq->reqary[res->tail.cmd_id];
		KKASSERT(req->state == NVME_REQ_SUBMITTED &&
			 req->comq == comq);
		req->res = *res;
		nvme_write(comq->sc, comq->comq_doorbell_reg, comq->comq_tail);
		cpu_sfence();
		req->state = NVME_REQ_COMPLETED;
		if (req->callback) {
			req->callback(req, lk);
		} else if (req->waiting) {
			wakeup(req);
		}
	}
#if 0
	if (didwork)
		nvme_write(comq->sc, comq->comq_doorbell_reg, comq->comq_tail);
#endif
}

void
nvme_intr(void *arg)
{
	nvme_comqueue_t *comq = arg;
	nvme_softc_t *sc;
	int i;
	int skip;

	sc = comq->sc;
	if (sc->nirqs == 1)
		skip = 1;
	else
		skip = sc->nirqs - 1;

	for (i = comq->qid; i <= sc->niocomqs; i += skip) {
		if (comq->nqe) {
			lockmgr(&comq->lk, LK_EXCLUSIVE);
			nvme_poll_completions(comq, &comq->lk);
			lockmgr(&comq->lk, LK_RELEASE);
		}
		comq += skip;
	}

#if 0
	for (i = 0; i <= sc->niocomqs; ++i) {
		comq = &sc->comqueues[i];
		if (comq->nqe == 0)     /* not configured */
			continue;
		lockmgr(&comq->lk, LK_EXCLUSIVE);
                nvme_poll_completions(comq, &comq->lk);
		lockmgr(&comq->lk, LK_RELEASE);
	}
#endif
}

/*
 * ADMIN HELPER COMMAND ROLLUP FUNCTIONS
 */
/*
 * Issue command to create a submission queue.
 */
int
nvme_create_subqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_request_t *req;
	nvme_subqueue_t *subq = &sc->subqueues[qid];
	int status;

	req = nvme_get_admin_request(sc, NVME_OP_CREATE_SUBQ);
	req->cmd.head.prp1 = subq->psubq;
	req->cmd.crsub.subq_id = qid;
	req->cmd.crsub.subq_size = subq->nqe - 1;	/* 0's based value */
	req->cmd.crsub.flags = NVME_CREATESUB_PC | NVME_CREATESUB_PRI_URG;
	req->cmd.crsub.comq_id = subq->comqid;

	nvme_submit_request(req);
	status = nvme_wait_request(req);
	nvme_put_request(req);

	return status;
}

/*
 * Issue command to create a completion queue.
 */
int
nvme_create_comqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_request_t *req;
	nvme_comqueue_t *comq = &sc->comqueues[qid];
	int status;
	int error;
	uint16_t ivect;

	error = 0;
	if (sc->nirqs > 1) {
		ivect = 1 + (qid - 1) % (sc->nirqs - 1);
		if (qid && ivect == qid) {
			error = bus_setup_intr(sc->dev, sc->irq[ivect],
						INTR_MPSAFE,
						nvme_intr,
						&sc->comqueues[ivect],
						&sc->irq_handle[ivect],
						NULL);
		}
	} else {
		ivect = 0;
	}
	if (error)
		return error;

	req = nvme_get_admin_request(sc, NVME_OP_CREATE_COMQ);
	req->cmd.head.prp1 = comq->pcomq;
	req->cmd.crcom.comq_id = qid;
	req->cmd.crcom.comq_size = comq->nqe - 1;	/* 0's based value */
	req->cmd.crcom.ivect = ivect;
	req->cmd.crcom.flags = NVME_CREATECOM_PC | NVME_CREATECOM_IEN;

	nvme_submit_request(req);
	status = nvme_wait_request(req);
	nvme_put_request(req);

	return status;
}

/*
 * Issue command to delete a submission queue.
 */
int
nvme_delete_subqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_request_t *req;
	/*nvme_subqueue_t *subq = &sc->subqueues[qid];*/
	int status;

	req = nvme_get_admin_request(sc, NVME_OP_DELETE_SUBQ);
	req->cmd.head.prp1 = 0;
	req->cmd.delete.qid = qid;

	nvme_submit_request(req);
	status = nvme_wait_request(req);
	nvme_put_request(req);

	return status;
}

/*
 * Issue command to delete a completion queue.
 */
int
nvme_delete_comqueue(nvme_softc_t *sc, uint16_t qid)
{
	nvme_request_t *req;
	/*nvme_comqueue_t *comq = &sc->comqueues[qid];*/
	int status;
	uint16_t ivect;

	req = nvme_get_admin_request(sc, NVME_OP_DELETE_COMQ);
	req->cmd.head.prp1 = 0;
	req->cmd.delete.qid = qid;

	nvme_submit_request(req);
	status = nvme_wait_request(req);
	nvme_put_request(req);

	if (qid && sc->nirqs > 1) {
		ivect = 1 + (qid - 1) % (sc->nirqs - 1);
		if (ivect == qid) {
			bus_teardown_intr(sc->dev,
					  sc->irq[ivect],
					  sc->irq_handle[ivect]);
		}
	}

	return status;
}

/*
 * Issue friendly shutdown to controller.
 */
int
nvme_issue_shutdown(nvme_softc_t *sc)
{
	uint32_t reg;
	int base_ticks;
	int error;

	/*
	 * Put us in shutdown
	 */
	reg = nvme_read(sc, NVME_REG_CONFIG);
	reg &= ~NVME_CONFIG_SHUT_MASK;
	reg |= NVME_CONFIG_SHUT_NORM;
	nvme_write(sc, NVME_REG_CONFIG, reg);

	/*
	 * Wait up to 10 seconds for acknowlegement
	 */
	error = ENXIO;
	base_ticks = ticks;
	while ((int)(ticks - base_ticks) < 10 * 20) {
		reg = nvme_read(sc, NVME_REG_STATUS);
		if ((reg & NVME_STATUS_SHUT_MASK) & NVME_STATUS_SHUT_DONE) {
			error = 0;
			break;
		}
		nvme_os_sleep(50);	/* 50ms poll */
	}
	if (error)
		device_printf(sc->dev, "Unable to shutdown chip nicely\n");
	else
		device_printf(sc->dev, "Normal chip shutdown succeeded\n");

	return error;
}

/*
 * Make space-padded string serial and model numbers more readable.
 */
size_t
string_cleanup(char *str, int domiddle)
{
	size_t i;
	size_t j;
	int atbeg = 1;

	for (i = j = 0; str[i]; ++i) {
		if ((str[i] == ' ' || str[i] == '\r') &&
		    (atbeg || domiddle)) {
			continue;
		} else {
			atbeg = 0;
		}
		str[j] = str[i];
		++j;
	}
	while (domiddle == 0 && j > 0 && (str[j-1] == ' ' || str[j-1] == '\r'))
		--j;
	str[j] = 0;
	if (domiddle == 0) {
		for (j = 0; str[j]; ++j) {
			if (str[j] == ' ')
				str[j] = '_';
		}
	}

	return j;
}
