/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Brills Peng <brillsp@gmail.com>
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
 * Augment RB-tree for B-WF2Q+ queuing algorithm:
 *  - The key of the binary tree is the virtual eligible time (start time)
 *  - Each node maintains an additional min_vd value, which
 *    is the minimum virtual deadline (finish time) among the node and its
 *    children
 *  - Every operation on the tree changing the childs of a node will
 *    trigger RB_AUGMENT() marco, which change min_vd along the path to
 *    the root
 */

#include <kern/dsched/bfq/wf2q.h>


#undef RB_AUGMENT
#define RB_AUGMENT(x) wf2q_augment_func(x);

static void
wf2q_augment_func(struct bfq_thread_io *node)
{
	struct bfq_thread_io *tmp = node, *tmp2;
	int min_vd;
	do{
		min_vd = tmp->vd;
		tmp2 = RB_LEFT(tmp, entry);
		min_vd = tmp2 ? MIN(tmp2->min_vd, min_vd) : min_vd;
		tmp2 = RB_RIGHT(tmp, entry);
		min_vd = tmp2 ? MIN(tmp2->min_vd, min_vd) : min_vd;
		tmp->min_vd = min_vd;
	}while((tmp = RB_PARENT(tmp,entry)));
}

/*
 * The rb-tree is indexed by the virtual eligible (start) time
 */
static int
bfq_thread_io_cmp(struct bfq_thread_io *a, struct bfq_thread_io *b)
{
	if (a->ve - b->ve <= 0)
		return -1;
	return 1;
}

RB_PROTOTYPE(wf2q_augtree_t, bfq_thread_io, entry,);
RB_GENERATE(wf2q_augtree_t, bfq_thread_io, entry, bfq_thread_io_cmp);

/*
 * The algorithm is from
 *	I. Stoica and H. Abdel-Wahab, ``Earliest Eligible Virtual Deadline
 *  First: A Flexible and Accurate Mechanism for Proportional Share
 *  Resource Allocation,'' technical report.
 *
 *  http://www.cs.berkeley.edu/~istoica/papers/eevdf-tr-95.pdf
 *
 *  - Partition the tree into two parts by ve:
 *  - One part contains nodes with ve smaller than vtime
 *  - The other part contains nodes with ve larger than vtime
 *  - In the first part, find the node with minimum vd, along the
 *    min_vd value path
 *
 *  Returns
 *	NULL, if no node with ve smaller than vtime
 *	or the elegible node with minimum vd.
 */
static struct bfq_thread_io *
wf2q_augtree_get_eligible_with_min_vd(struct wf2q_augtree_t *tree, int vtime)
{
	struct bfq_thread_io *node = RB_ROOT(tree), *st_tree = NULL, *path_req = NULL;
	while (node) {
		if (node->ve <= vtime) {
			/* update node with earliest deadline along path. */
			if ((!path_req) || (path_req->vd > node->vd))
				path_req = node;
			/* update root of subtree containing earliest deadline */
			if ((!st_tree) || (RB_LEFT(node,entry) && st_tree->min_vd > RB_LEFT(node,entry)->min_vd))
				st_tree = RB_LEFT(node,entry);
			node = RB_RIGHT(node, entry);
		} else
			node = RB_LEFT(node, entry);
	}
	/* check whether node with earliest deadline was along path */
	if ((!st_tree) || (st_tree->min_vd >= path_req->vd))
		return path_req;
	/* return node with earliest deadline from subtree */
	for (node = st_tree; node; ) {
		/* if node found, return it */
		if (st_tree->min_vd == node->vd)
			return node;
		/* XXX: modified temporarily */
		if (RB_LEFT(node, entry) && node->min_vd == RB_LEFT(node, entry)->min_vd)
			node = RB_LEFT(node, entry);
		else
			node = RB_RIGHT(node, entry);
	}
	return NULL;
}

/*
 * This function initializes a wf2q structure
 */
void
wf2q_init(struct wf2q_t *pwf2q)
{
	RB_INIT(&pwf2q->wf2q_augtree);
	pwf2q->wf2q_virtual_time = 0;
	pwf2q->wf2q_tdio_count = 0;
}

/*
 * Insert a tdio into a wf2q queue.
 * The virtual eligible (start) time and deadline is handled
 * according to the current virtual time (in wf2q_t).
 */
void
wf2q_insert_thread_io(struct wf2q_t *wf2q, struct bfq_thread_io *tdio)
{
	/*
	 * TODO: The anticipatory parts
	 * start time varies on whether the tdio is being waited
	 */
	tdio->ve = MAX(wf2q->wf2q_virtual_time, tdio->vd);
	tdio->vd = tdio->ve + tdio->budget / tdio->weight;
	tdio->min_vd = tdio->vd;
	RB_INSERT(wf2q_augtree_t, &wf2q->wf2q_augtree, tdio);
	wf2q->wf2q_tdio_count++;
}

/*
 * Remove a thread_io struct from the augment tree,
 * called before a thread is destroyed.
 */
void
wf2q_remove_thread_io(struct wf2q_t *wf2q, struct bfq_thread_io *tdio)
{
	RB_REMOVE(wf2q_augtree_t, &wf2q->wf2q_augtree, tdio);
	wf2q->wf2q_tdio_count--;
}

/*
 * Increase the current virtual time as services are provided
 */
void
wf2q_inc_tot_service(struct wf2q_t *wf2q, int amount)
{
	wf2q->wf2q_virtual_time += amount;
}

/*
 * Update a tdio's virtual deadline as it received service
 */
void
wf2q_update_vd(struct bfq_thread_io *tdio, int received_service)
{
	tdio->vd = tdio->ve + received_service / tdio->weight;
}

static void
wf2q_tree_dump(struct bfq_thread_io *root, int level)
{
	int i;
	if (!root) return;
	for (i = 0; i < level; i++)
		kprintf("-");
	kprintf("vd: %d; ve: %d; min_vd: %d\n", root->vd, root->ve, root->min_vd);
	wf2q_tree_dump(RB_LEFT(root,entry), level + 1);
	wf2q_tree_dump(RB_RIGHT(root, entry), level + 1);
}

/*
 * Get a tdio with minimum virtual deadline and virtual eligible
 * time smaller than the current virtual time.
 * If there is no such tdio, update the current virtual time to
 * the minimum ve in the queue. (And there must be one eligible then)
 */
struct bfq_thread_io *
wf2q_get_next_thread_io(struct wf2q_t *wf2q)
{
	struct bfq_thread_io *tdio;
	struct wf2q_augtree_t *tree = &wf2q->wf2q_augtree;
	if (!(tdio = wf2q_augtree_get_eligible_with_min_vd(tree, wf2q->wf2q_virtual_time))) {
		tdio = RB_MIN(wf2q_augtree_t, tree);
		if (!tdio)
			return NULL;
		wf2q->wf2q_virtual_time = tdio->ve;
		tdio = wf2q_augtree_get_eligible_with_min_vd(tree, wf2q->wf2q_virtual_time);
	}
	if (!tdio) {
		kprintf("!!!wf2q: wf2q_tdio_count=%d\n", wf2q->wf2q_tdio_count);
		wf2q_tree_dump(RB_ROOT(tree), 0);
		KKASSERT(0);
	}
	RB_REMOVE(wf2q_augtree_t, tree, tdio);
	wf2q->wf2q_tdio_count--;
	return tdio;
}
