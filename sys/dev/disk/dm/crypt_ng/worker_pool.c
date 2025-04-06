/*
 * Copyright (c) 2025 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Michael Neumann <mneumann@ntecs.de>.
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

#include <dev/disk/dm/crypt_ng/worker_pool.h>

#include <cpu/atomic.h>

MALLOC_DECLARE(M_DMCRYPT);

static void worker_main(void *worker_arg);

static void worker_pool_stop_worker(struct worker *worker);

static void worker_pool_free_worker(struct worker *worker);

static __inline struct bio *
bio_request_queue_next(struct bio *bio)
{
	return (struct bio *)bio->bio_caller_info2.ptr;
}

static __inline void
bio_request_queue_init(struct bio_request_queue *queue)
{
	queue->first = NULL;
	queue->last = &queue->first;
}

static __inline bool
bio_request_queue_is_empty(struct bio_request_queue *queue)
{
	return (queue->first == NULL);
}

static __inline void
bio_request_queue_push_back(struct bio_request_queue *queue,
    struct bio *bio_request)
{
	bio_request->bio_caller_info2.ptr = NULL;
	*(queue->last) = bio_request;
	queue->last = (struct bio **)&bio_request->bio_caller_info2.ptr;
}

static __inline struct bio *
bio_request_queue_take_all(struct bio_request_queue *queue)
{
	struct bio *first = queue->first;
	if (first) {
		queue->first = NULL;
		queue->last = &queue->first;
	}
	return first;
}

int
worker_pool_submit_bio_request(struct worker_pool *pool, struct bio *bio)
{
	struct worker *worker;
	int worker_idx;
	int error = 0;

	worker_idx = atomic_fetchadd_int(&pool->wp_next_worker_idx, 1) %
	    pool->wp_num_workers;
	worker = &pool->wp_workers[worker_idx];

	lockmgr(&worker->w_lock, LK_EXCLUSIVE);

	if (worker->w_is_closing) {
		error = EPIPE;
		goto out;
	}

	/*
	 * Enqueue bio request
	 */

	bio_request_queue_push_back(&worker->w_bio_requests, bio);

	if (worker->w_is_sleeping)
		wakeup_one(worker);

out:
	lockmgr(&worker->w_lock, LK_RELEASE);
	return (error);
}

/**
 * This is the main() function of the worker thread.
 */
void
worker_main(void *worker_arg)
{
	struct worker *worker = worker_arg;
	worker_bio_request_handler *handler = worker->w_bio_request_handler;
	void *context = worker->w_context;
	struct bio *chain;

	lockmgr(&worker->w_lock, LK_EXCLUSIVE);

	while (true) {
		chain = bio_request_queue_take_all(&worker->w_bio_requests);

		if (chain == NULL && worker->w_is_closing)
			break;

		if (chain == NULL) {
			worker->w_is_sleeping = true;
			lksleep(worker, &worker->w_lock, 0,
			    "dm_target_crypt: worker queue empty", 0);
			worker->w_is_sleeping = false;
			continue;
		}

		/**
		 * Release lock while processing the bios.
		 */
		lockmgr(&worker->w_lock, LK_RELEASE);

		while (chain) {
			struct bio *bio = chain;
			chain = bio_request_queue_next(chain);
			handler(bio, context);

			/*
			 * Give other threads of the same priority a chance to
			 * run.
			 */
			lwkt_yield();
		}

		KKASSERT(chain == NULL);

		/**
		 * Done with processing the bio's -> acquire lock again
		 */
		lockmgr(&worker->w_lock, LK_EXCLUSIVE);
	}

	KKASSERT(worker->w_is_closing);
	lockmgr(&worker->w_lock, LK_RELEASE);

	/**
	 * This notifies worker_pool_stop() that the worker has been terminated.
	 */
	wakeup(&worker->w_is_closing);
}

void
worker_pool_start(struct worker_pool *pool, int cpu_offset)
{
	for (int i = 0; i < pool->wp_num_workers; ++i) {
		kthread_create_cpu(worker_main, &pool->wp_workers[i],
		    &pool->wp_workers[i].w_thread, (cpu_offset + i) % ncpus,
		    "dm_target_crypt: crypto worker");
	}
}

static void
worker_pool_stop_worker(struct worker *worker)
{
	worker->w_is_closing = true;

	while (true) {
		wakeup(worker);
		if (tsleep(&worker->w_is_closing, 0, "shutdown workqueue",
			500) == 0)
			break;
	}
}

void
worker_pool_stop(struct worker_pool *pool)
{
	for (int i = 0; i < pool->wp_num_workers; ++i) {
		worker_pool_stop_worker(&pool->wp_workers[i]);
	}
}

void
worker_pool_init(struct worker_pool *pool, int num_workers,
    int per_worker_local_memory,
    worker_bio_request_handler *bio_request_handler)
{
	bzero(pool, sizeof(*pool));
	pool->wp_num_workers = num_workers;
	pool->wp_next_worker_idx = 0;
	pool->wp_workers = kmalloc(sizeof(struct worker) * num_workers,
	    M_DMCRYPT, M_WAITOK | M_ZERO);

	for (int i = 0; i < num_workers; ++i) {
		pool->wp_workers[i].w_thread = NULL;
		pool->wp_workers[i].w_bio_request_handler = bio_request_handler;
		pool->wp_workers[i].w_context_size = per_worker_local_memory;
		pool->wp_workers[i].w_context = (per_worker_local_memory > 0) ?
		    kmalloc(per_worker_local_memory, M_DMCRYPT, M_WAITOK) :
		    NULL;
		lockinit(&pool->wp_workers[i].w_lock, "dm_target_crypt: worker",
		    0, LK_CANRECURSE);
		pool->wp_workers[i].w_is_closing = false;
		pool->wp_workers[i].w_is_sleeping = false;
		bio_request_queue_init(&pool->wp_workers[i].w_bio_requests);
	}
}

static void
worker_pool_free_worker(struct worker *worker)
{
	KKASSERT(bio_request_queue_is_empty(&worker->w_bio_requests));
	lockuninit(&worker->w_lock);

	if (worker->w_context) {
		explicit_bzero(worker->w_context, worker->w_context_size);
		kfree(worker->w_context, M_DMCRYPT);
	}
	bzero(worker, sizeof(*worker));
}

/**
 * Free the worker pool.
 */
void
worker_pool_free(struct worker_pool *pool)
{
	for (int i = 0; i < pool->wp_num_workers; ++i) {
		worker_pool_free_worker(&pool->wp_workers[i]);
	}
	kfree(pool->wp_workers, M_DMCRYPT);
	bzero(pool, sizeof(*pool));
}
