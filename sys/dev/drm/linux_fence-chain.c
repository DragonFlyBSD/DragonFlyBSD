/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Timeline fence chains -- see include/linux/dma-fence-chain.h.
 *
 * A chain node signals once its payload fence and its entire prefix
 * (everything reachable through ->prev) have signalled.  Signal
 * propagation keeps exactly one callback armed per node, hopping from
 * blocker to blocker until none remains.
 */

#include <linux/dma-fence-chain.h>
#include <linux/slab.h>

struct dma_fence_chain *
dma_fence_chain_alloc(void)
{
	return (kzalloc(sizeof(struct dma_fence_chain), GFP_KERNEL));
}
EXPORT_SYMBOL(dma_fence_chain_alloc);

void
dma_fence_chain_free(struct dma_fence_chain *chain)
{
	kfree(chain);
}
EXPORT_SYMBOL(dma_fence_chain_free);

static const char *
dma_fence_chain_get_driver_name(struct dma_fence *fence)
{
	return ("dma_fence_chain");
}

static const char *
dma_fence_chain_get_timeline_name(struct dma_fence *fence)
{
	return ("timeline");
}

struct dma_fence *
dma_fence_chain_prev_get(struct dma_fence_chain *chain)
{
	struct dma_fence *prev;

	lockmgr(&chain->prev_lock, LK_EXCLUSIVE);
	prev = chain->prev;
	if (prev != NULL)
		dma_fence_get(prev);
	lockmgr(&chain->prev_lock, LK_RELEASE);
	return (prev);
}

struct dma_fence *
dma_fence_chain_walk(struct dma_fence *fence)
{
	struct dma_fence_chain *chain, *prev_chain;
	struct dma_fence *prev, *replacement;

	chain = to_dma_fence_chain(fence);
	if (chain == NULL) {
		dma_fence_put(fence);
		return (NULL);
	}

	for (;;) {
		bool replaced = false;

		prev = dma_fence_chain_prev_get(chain);
		if (prev == NULL)
			break;

		prev_chain = to_dma_fence_chain(prev);
		if (prev_chain != NULL) {
			if (prev_chain->fence != NULL &&
			    !dma_fence_is_signaled(prev_chain->fence))
				break;
			replacement = dma_fence_chain_prev_get(prev_chain);
		} else {
			if (!dma_fence_is_signaled(prev))
				break;
			replacement = NULL;
		}

		lockmgr(&chain->prev_lock, LK_EXCLUSIVE);
		if (chain->prev == prev) {
			chain->prev = replacement;
			replacement = NULL;
			replaced = true;
		}
		lockmgr(&chain->prev_lock, LK_RELEASE);

		if (replaced)
			dma_fence_put(prev);
		else
			dma_fence_put(replacement);
		dma_fence_put(prev);
	}

	dma_fence_put(fence);
	return (prev);
}
EXPORT_SYMBOL(dma_fence_chain_walk);

void
dma_fence_chain_truncate_prev(struct dma_fence_chain *chain)
{
	struct dma_fence *prev;

	lockmgr(&chain->prev_lock, LK_EXCLUSIVE);
	prev = chain->prev;
	chain->prev = NULL;
	lockmgr(&chain->prev_lock, LK_RELEASE);
	dma_fence_put(prev);
}

/*
 * Return the current blocker (referenced) or NULL when the node's
 * signal condition is met.  The payload is checked first so the chain
 * drains front to back and prev links can be truncated early.
 */
static struct dma_fence *
dma_fence_chain_blocker(struct dma_fence_chain *chain)
{
	struct dma_fence *prev;

	if (chain->fence != NULL && !dma_fence_is_signaled(chain->fence))
		return (dma_fence_get(chain->fence));

	prev = dma_fence_chain_walk(dma_fence_get(&chain->base));
	if (prev != NULL) {
		if (!dma_fence_is_signaled(prev))
			return (prev);
		dma_fence_put(prev);
	}
	return (NULL);
}

static void dma_fence_chain_cb(struct dma_fence *blocker,
	    struct dma_fence_cb *cb);

static void dma_fence_chain_signal_work(struct irq_work *wrk);

static void
dma_fence_chain_queue_signal(struct dma_fence_chain *chain)
{
	irq_work_queue(&chain->work);
}

static void
dma_fence_chain_signal_work(struct irq_work *wrk)
{
	struct dma_fence_chain *chain =
	    container_of(wrk, struct dma_fence_chain, work);

	dma_fence_signal(&chain->base);
	dma_fence_put(&chain->base);
}

/*
 * Re-arm the callback on the next blocker; signal the node when none
 * is left.  Runs from enable_signaling (node lock held by core) and
 * from blocker callbacks (blocker lock held) -- it only ever takes
 * OTHER fences' locks, except the final dma_fence_signal which takes
 * the node's own lock and therefore must not run with it held; the
 * enable_signaling path returns false instead and lets the core
 * handle the already-signalled case.
 *
 * Ownership:
 * - On success, transfers the referenced blocker returned by
 *   dma_fence_chain_blocker() to chain->cb.  The callback must release that
 *   blocker reference when it runs.
 * - On failure to arm because the blocker already signalled, releases the
 *   transient blocker reference before retrying.
 *
 * Lifetime:
 * - The caller must keep chain->base alive for the whole callback lifetime.
 *   dma_fence_chain_enable_signaling() owns that extra reference until a later
 *   callback either rearms on another blocker or completes the chain.
 *
 * Threading:
 * - The callback node is single-shot.  It is re-used only after the previous
 *   blocker has invoked dma_fence_chain_cb().
 *
 * Returns true if a callback was armed, false if the node is ready to
 * signal.
 */
static bool
dma_fence_chain_arm(struct dma_fence_chain *chain)
{
	struct dma_fence *blocker;

	for (;;) {
		blocker = dma_fence_chain_blocker(chain);
		if (blocker == NULL)
			return (false);
		if (dma_fence_add_callback(blocker, &chain->cb,
		    dma_fence_chain_cb) == 0)
			return (true);
		/* Blocker signalled between the check and the add. */
		dma_fence_put(blocker);
	}
}

static void
dma_fence_chain_cb(struct dma_fence *blocker, struct dma_fence_cb *cb)
{
	struct dma_fence_chain *chain =
	    container_of(cb, struct dma_fence_chain, cb);

	dma_fence_put(blocker);
	if (!dma_fence_chain_arm(chain))
		dma_fence_chain_queue_signal(chain);
}

static bool
dma_fence_chain_enable_signaling(struct dma_fence *fence)
{
	struct dma_fence_chain *chain =
	    container_of(fence, struct dma_fence_chain, base);

	/*
	 * Ownership:
	 *   Takes one signaling reference on chain->base before publishing
	 *   chain->cb into another fence's callback list.  If arming succeeds, that
	 *   reference is owned by the callback chain and is released when the chain
	 *   finally signals.  If no blocker remains, this helper releases the
	 *   reference and returns false so the core handles the already-signalled
	 *   case.
	 *
	 * Lifetime:
	 *   The signaling reference keeps chain and chain->cb valid even if the
	 *   syncobj drops its head reference before the blocker signals.
	 *
	 * Threading:
	 *   The node lock is held by the core here; arming only touches other
	 *   fences' locks.
	 */
	dma_fence_get(&chain->base);
	if (dma_fence_chain_arm(chain))
		return (true);
	dma_fence_chain_queue_signal(chain);
	return (true);
}

static bool
dma_fence_chain_signaled(struct dma_fence *fence)
{
	struct dma_fence_chain *chain =
	    container_of(fence, struct dma_fence_chain, base);
	struct dma_fence *prev;
	bool ret = true;

	if (chain->fence != NULL && !dma_fence_is_signaled(chain->fence))
		return (false);
	prev = dma_fence_chain_walk(dma_fence_get(&chain->base));
	if (prev != NULL) {
		ret = dma_fence_is_signaled(prev);
		dma_fence_put(prev);
	}
	return (ret);
}

static void dma_fence_chain_put(struct dma_fence *fence);

/*
 * dma_fence_chain_release_detach - free one chain node and return its prefix.
 *
 * Ownership:
 * - The caller has exclusive ownership of @chain. Its base fence is either
 *   already in dma_fence_release(), or the caller proved that the chain's
 *   incoming prefix edge is its final reference.
 * - The chain->prev reference is moved to the return value. The caller owns
 *   that reference and must drop it.
 * - The chain->fence payload reference is consumed here.
 *
 * Lifetime:
 * - @chain is freed before this function returns.
 * - The returned fence, when non-NULL, remains alive until the caller drops
 *   the moved reference.
 *
 * Threading:
 * - No external strong reference may remain to @chain.
 * - prev_lock is still taken while detaching chain->prev so this helper keeps
 *   the same mutable-edge invariant as dma_fence_chain_walk().
 */
static struct dma_fence *
dma_fence_chain_release_detach(struct dma_fence_chain *chain)
{
	struct dma_fence *payload;
	struct dma_fence *prev;

	lockmgr(&chain->prev_lock, LK_EXCLUSIVE);
	prev = chain->prev;
	chain->prev = NULL;
	lockmgr(&chain->prev_lock, LK_RELEASE);

	payload = chain->fence;
	chain->fence = NULL;
	dma_fence_chain_put(payload);

	lockuninit(&chain->prev_lock);
	lockuninit(&chain->lock);
	dma_fence_free(&chain->base);
	return (prev);
}

static void
dma_fence_chain_put(struct dma_fence *fence)
{
	struct dma_fence_chain *chain;

	while (fence != NULL) {
		chain = to_dma_fence_chain(fence);
		if (chain == NULL || kref_read(&fence->refcount) != 1) {
			dma_fence_put(fence);
			return;
		}

		fence = dma_fence_chain_release_detach(chain);
	}
}

static void
dma_fence_chain_release(struct dma_fence *fence)
{
	struct dma_fence_chain *chain =
	    container_of(fence, struct dma_fence_chain, base);

	fence = dma_fence_chain_release_detach(chain);
	dma_fence_chain_put(fence);
}

const struct dma_fence_ops dma_fence_chain_ops = {
	.get_driver_name = dma_fence_chain_get_driver_name,
	.get_timeline_name = dma_fence_chain_get_timeline_name,
	.enable_signaling = dma_fence_chain_enable_signaling,
	.signaled = dma_fence_chain_signaled,
	.wait = dma_fence_default_wait,
	.release = dma_fence_chain_release,
};
EXPORT_SYMBOL(dma_fence_chain_ops);

void
dma_fence_chain_init(struct dma_fence_chain *chain, struct dma_fence *prev,
    struct dma_fence *fence, u64 point)
{
	struct dma_fence_chain *prev_chain = to_dma_fence_chain(prev);
	u64 context;

	/* Nodes of one timeline share a fence context. */
	if (prev_chain != NULL) {
		context = prev->context;
		chain->prev_seqno = prev_chain->point;
	} else {
		context = dma_fence_context_alloc(1);
		chain->prev_seqno = 0;
	}

	chain->prev = prev;
	chain->fence = fence;
	chain->point = point;
	INIT_LIST_HEAD(&chain->cb.node);
	init_irq_work(&chain->work, dma_fence_chain_signal_work);
	lockinit(&chain->lock, "dfchn", 0, 0);
	lockinit(&chain->prev_lock, "dfchnp", 0, 0);
	dma_fence_init(&chain->base, &dma_fence_chain_ops, &chain->lock,
	    context, point);
}
EXPORT_SYMBOL(dma_fence_chain_init);

int
dma_fence_chain_find_seqno(struct dma_fence **pfence, u64 point)
{
	struct dma_fence_chain *chain;
	struct dma_fence *cur, *prev;
	u64 context;

	if (point == 0)
		return (0);

	chain = to_dma_fence_chain(*pfence);
	if (chain == NULL)
		return (-EINVAL);
	if (chain->point < point)
		return (-EINVAL);	/* not materialized yet */

	context = (*pfence)->context;
	cur = *pfence;
	for (;;) {
		chain = to_dma_fence_chain(cur);
		if (cur->context != context || chain == NULL ||
		    chain->prev_seqno < point)
			break;
		prev = dma_fence_chain_walk(dma_fence_get(cur));
		if (prev == NULL) {
			dma_fence_put(cur);
			cur = NULL;
			break;
		}
		dma_fence_put(cur);
		cur = prev;
	}
	*pfence = cur;
	return (0);
}
EXPORT_SYMBOL(dma_fence_chain_find_seqno);
