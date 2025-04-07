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

#ifndef _WORKER_POOL_H_
#define _WORKER_POOL_H_

/**
 * This file implements a worker pool implementation for dm_target_crypt_ng
 * for offloading encrypting/decrypting BIOs to a pool of threads.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>

#include <dev/disk/dm/crypt_ng/worker_pool.h>

typedef void worker_bio_request_handler(struct bio *bio, void *worker_context);

/**
 * BIO request queue (FIFO, singly-linked tail queue)
 */
struct bio_request_queue {
	struct bio *first;
	struct bio **last;
};

/**
 * Represents a worker.
 *
 * Each worker is bound to a CPU and associated with it's own job queue.
 */
struct worker {
	/**
	 * Points to the thread that runs the worker.
	 */
	struct thread *w_thread;

	/**
	 * Function called to handle a bio request.
	 */
	worker_bio_request_handler *w_bio_request_handler;

	/**
	 * Private data usable from within the worker.
	 */
	void *w_context;

	/**
	 * The size of the w_context buffer.
	 */
	int w_context_size;

	/**
	 * Protects the remaining fields of the struct.
	 */
	struct lock w_lock;

	/**
	 * Set to true if the worker is shutting down in which
	 * case no further jobs can be submitted to the queue.
	 * Already submitted jobs are still processed.
	 */
	bool w_is_closing;

	/**
	 * True, if the worker is "sleeping", waiting for
	 * work to arrive in it's queue.
	 */
	bool w_is_sleeping;

	/**
	 * bio request queue (Singly-linked Tail queue)
	 */
	struct bio_request_queue w_bio_requests;
};

/**
 * A pool of workers.
 */
struct worker_pool {
	/*
	 * number of workers
	 */
	int wp_num_workers;

	/**
	 * Atomic counter used to distribute jobs
	 * to a worker using round robin.
	 */
	int wp_next_worker_idx;

	/*
	 * Array of workers.
	 */
	struct worker *wp_workers;
};

/**
 * Initializes the worker pool and allocates memory.
 */
void worker_pool_init(struct worker_pool *pool, int num_workers,
    int per_worker_local_memory,
    worker_bio_request_handler *bio_request_handler);

/**
 * Starts the worker pool.
 */
void worker_pool_start(struct worker_pool *pool, int cpu_offset);

/**
 * Stops the worker pool.
 */
void worker_pool_stop(struct worker_pool *pool);

/**
 * Frees the worker pool.
 */
void worker_pool_free(struct worker_pool *pool);

/**
 * Submits a BIO request to the worker pool.
 */
int worker_pool_submit_bio_request(struct worker_pool *pool, struct bio *bio);

#endif
