/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Timeline fence chains for DragonFly's DRM port.
 *
 * Native implementation following the semantics of the Linux
 * dma_fence_chain API (drivers/dma-buf/dma-fence-chain.c, GPL-2.0,
 * not copied): a chain node is itself a dma_fence that signals once
 * its payload fence and every earlier node have signalled, so a
 * drm_syncobj can expose one fence per timeline point.
 */

#ifndef _LINUX_DMA_FENCE_CHAIN_H_
#define _LINUX_DMA_FENCE_CHAIN_H_

#include <linux/dma-fence.h>
#include <linux/irq_work.h>

struct dma_fence_chain {
	struct dma_fence	base;
	struct lock		lock;		/* base.lock points here */
	struct lock		prev_lock;	/* serializes the prev edge */
	struct dma_fence	*prev;		/* earlier chain/fence; ref */
	u64			prev_seqno;	/* prev node's point, 0 if none */
	u64			point;		/* this node's timeline point */
	struct dma_fence	*fence;		/* payload; ref */
	struct dma_fence_cb	cb;		/* armed on current blocker */
	struct irq_work		work;		/* deferred self-signal */
};

extern const struct dma_fence_ops dma_fence_chain_ops;

static inline struct dma_fence_chain *
to_dma_fence_chain(struct dma_fence *fence)
{
	if (fence == NULL || fence->ops != &dma_fence_chain_ops)
		return (NULL);
	return (container_of(fence, struct dma_fence_chain, base));
}

struct dma_fence_chain *dma_fence_chain_alloc(void);
void dma_fence_chain_free(struct dma_fence_chain *chain);

/*
 * Initialize @chain as the fence for timeline @point.  Consumes the
 * caller's references on @prev (may be NULL) and @fence.
 */
void dma_fence_chain_init(struct dma_fence_chain *chain,
	    struct dma_fence *prev, struct dma_fence *fence, u64 point);

/* Reference-taking accessor for the (mutable) prev link. */
struct dma_fence *dma_fence_chain_prev_get(struct dma_fence_chain *chain);

/*
 * Walk to the previous fence while compacting signalled history.  Consumes the
 * caller's reference to @fence and returns a referenced previous fence, or NULL
 * at the end of the chain.
 */
struct dma_fence *dma_fence_chain_walk(struct dma_fence *fence);

/* Drop the history below @chain once it is known to be signalled. */
void dma_fence_chain_truncate_prev(struct dma_fence_chain *chain);

/*
 * Resolve the chain node covering @point.  On entry *pfence holds a
 * referenced chain head; on success the reference is moved to the returned
 * node.  If @point is in a collected, already signalled prefix, *pfence is
 * set to NULL.  Returns -EINVAL if *pfence is not a chain or @point has not
 * been materialized yet.
 */
int dma_fence_chain_find_seqno(struct dma_fence **pfence, u64 point);

#endif /* _LINUX_DMA_FENCE_CHAIN_H_ */
