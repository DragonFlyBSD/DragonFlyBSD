/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Module-global future scheduler for the native NVIDIA GPU driver.
 */

#include "nvgpu_future.h"
#include "nvgpu_sched.h"
#include "nvgpu_debug.h"

#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/systm.h>
#include <sys/thread.h>

TAILQ_HEAD(nvgpu_sched_queue, nvgpu_future);

struct nvgpu_sched {
	struct thread **threads;
	struct lwkt_token stop_token;
	struct spinlock queue_spin;
	struct nvgpu_sched_queue active;
	u_int queued_count;
	u_int polling_count;
	u_int worker_count;
	bool stopping;
};

static MALLOC_DEFINE(M_NVGPU_SCHED, "nvgpu_sched", "nvgpu scheduler");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, sched_put, 24,
    "sched put future=%p stopping=%u", void *future, u_int stopping);
KTR_INFO(KTR_NVGPU, nvgpu, sched_poll, 24,
    "sched poll future=%p ready=%u result=%d", void *future, u_int ready,
    int result);
KTR_INFO(KTR_NVGPU, nvgpu, sched_stop, 24,
    "sched stop workers=%u queued=%u polling=%u", u_int workers,
    u_int queued, u_int polling);

static struct nvgpu_sched *g_sched;

static void nvgpu_sched_run(void *argument);

int
nvgpu_sched_start(void)
{
	struct nvgpu_sched *sched;
	int error;

	if (g_sched != NULL)
		return (EALREADY);
	sched = kmalloc(sizeof(*sched), M_NVGPU_SCHED, M_WAITOK | M_ZERO);
	sched->threads = kmalloc(sizeof(*sched->threads) * ncpus,
	    M_NVGPU_SCHED, M_WAITOK | M_ZERO);
	lwkt_token_init(&sched->stop_token, "nvgpsd");
	spin_init(&sched->queue_spin, "nvgpu sched queue");
	TAILQ_INIT(&sched->active);

	for (int cpu = 0; cpu < ncpus; cpu++) {
		error = lwkt_create(nvgpu_sched_run, sched,
		    &sched->threads[cpu], NULL, TDF_NOSTART, cpu,
		    "nvgpu_sched/%d", cpu);
		if (error != 0) {
			lwkt_gettoken(&sched->stop_token);
			spin_lock(&sched->queue_spin);
			sched->stopping = true;
			spin_unlock(&sched->queue_spin);
			wakeup(&sched->active);
			while (sched->worker_count != 0)
				tsleep(&sched->worker_count, 0, "nvgpsx", 0);
			lwkt_reltoken(&sched->stop_token);
			spin_uninit(&sched->queue_spin);
			lwkt_token_uninit(&sched->stop_token);
			_kfree(sched->threads, M_NVGPU_SCHED);
			_kfree(sched, M_NVGPU_SCHED);
			return (error);
		}
		lwkt_setpri_initial(sched->threads[cpu], TDPRI_KERN_DAEMON);
		sched->worker_count++;
		lwkt_schedule(sched->threads[cpu]);
	}
	g_sched = sched;
	return (0);
}

void
nvgpu_sched_stop(void)
{
	struct nvgpu_sched *sched;
	u_int queued, polling;

	sched = g_sched;
	if (sched == NULL)
		return;
	g_sched = NULL;
	lwkt_gettoken(&sched->stop_token);
	spin_lock(&sched->queue_spin);
	sched->stopping = true;
	queued = sched->queued_count;
	polling = sched->polling_count;
	spin_unlock(&sched->queue_spin);
	KTR_LOG(nvgpu_sched_stop, sched->worker_count, queued, polling);
	nvgpu_log(NVGPU_LOG_INFO,
	    "scheduler stop begin workers=%u queued=%u polling=%u\n",
	    sched->worker_count, queued, polling);
	while (sched->worker_count != 0) {
		wakeup(&sched->active);
		if (tsleep(&sched->worker_count, 0, "nvgpsx", hz) == EWOULDBLOCK) {
			spin_lock(&sched->queue_spin);
			queued = sched->queued_count;
			polling = sched->polling_count;
			spin_unlock(&sched->queue_spin);
			KTR_LOG(nvgpu_sched_stop, sched->worker_count, queued, polling);
			nvgpu_log(NVGPU_LOG_INFO,
			    "scheduler stop wait workers=%u queued=%u polling=%u\n",
			    sched->worker_count, queued, polling);
		}
	}
	lwkt_reltoken(&sched->stop_token);
	KASSERT(TAILQ_EMPTY(&sched->active),
	    ("stopped scheduler with active futures"));
	KASSERT(sched->queued_count == 0 && sched->polling_count == 0,
	    ("stopped scheduler with busy futures"));
	spin_uninit(&sched->queue_spin);
	lwkt_token_uninit(&sched->stop_token);
	_kfree(sched->threads, M_NVGPU_SCHED);
	_kfree(sched, M_NVGPU_SCHED);
}

uint32_t
nvgpu_sched_busy_count(void)
{
	struct nvgpu_sched *sched;
	uint32_t count;

	sched = g_sched;
	if (sched == NULL)
		return (0);
	spin_lock(&sched->queue_spin);
	count = sched->queued_count + sched->polling_count;
	spin_unlock(&sched->queue_spin);
	return (count);
}

int
nvgpu_sched_put(struct nvgpu_future *future)
{
	struct nvgpu_sched *sched;

	if (future == NULL)
		return (EINVAL);
	sched = g_sched;
	if (sched == NULL)
		return (ENODEV);
	spin_lock(&sched->queue_spin);
	if (sched->stopping) {
		KTR_LOG(nvgpu_sched_put, future, 1u);
		spin_unlock(&sched->queue_spin);
		return (ENODEV);
	}
	KTR_LOG(nvgpu_sched_put, future, 0u);
	sched->queued_count++;
	TAILQ_INSERT_TAIL(&sched->active, future, link);
	spin_unlock(&sched->queue_spin);
	wakeup_one(&sched->active);
	return (0);
}

static void
nvgpu_sched_run(void *argument)
{
	struct nvgpu_sched *sched;
	struct nvgpu_future *future;
	bool stopping;

	sched = argument;
	for (;;) {
		spin_lock(&sched->queue_spin);
		future = TAILQ_FIRST(&sched->active);
		if (future != NULL) {
			TAILQ_REMOVE(&sched->active, future, link);
			KASSERT(sched->queued_count != 0,
			    ("nvgpu scheduler queued count underflow"));
			sched->queued_count--;
			sched->polling_count++;
		}
		stopping = sched->stopping;
		if (future == NULL && !stopping)
			tsleep_interlock(&sched->active, 0);
		spin_unlock(&sched->queue_spin);
		if (future == NULL) {
			if (stopping)
				break;
			tsleep(&sched->active, PINTERLOCKED, "nvgpsd", 0);
			continue;
		}
		{
			void *logged_future = future;
			struct nvgpu_future_result result;

			result = future->poll(future);
			KTR_LOG(nvgpu_sched_poll, logged_future,
			    result.ready ? 1u : 0u, result.result);
		}
		spin_lock(&sched->queue_spin);
		KASSERT(sched->polling_count != 0,
		    ("nvgpu scheduler polling count underflow"));
		sched->polling_count--;
		spin_unlock(&sched->queue_spin);
	}

	lwkt_gettoken(&sched->stop_token);
	KASSERT(sched->worker_count != 0,
	    ("nvgpu scheduler worker count underflow"));
	sched->worker_count--;
	if (sched->worker_count == 0)
		wakeup(&sched->worker_count);
	lwkt_reltoken(&sched->stop_token);
	lwkt_exit();
}
