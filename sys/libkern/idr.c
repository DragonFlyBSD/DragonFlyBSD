/*
 * Copyright (c) 2005-2012 The DragonFly Project.
 * Copyright (c) 2013 François Tigeot
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey Hsu.
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
 *
 */

#include <sys/idr.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/spinlock2.h>
#include <sys/limits.h>

#define IDR_DEFAULT_SIZE    256

MALLOC_DEFINE(M_IDR, "idr", "Integer ID management");

static void	idr_grow(struct idr *idp, int want);
static void	idr_reserve(struct idr *idp, int id, int incr);
static void	idr_set(struct idr *idp, int id, void *ptr);
static int	idr_find_free(struct idr *idp, int want, int lim);
static int	idr_pre_get1(struct idr *idp, int want, int lim);

/*
 * Number of nodes in right subtree, including the root.
 */
static __inline int
right_subtree_size(int n)
{
	return (n ^ (n | (n + 1)));
}

/*
 * Bigger ancestor.
 */
static __inline int
right_ancestor(int n)
{
	return (n | (n + 1));
}

/*
 * Smaller ancestor.
 */
static __inline int
left_ancestor(int n)
{
	return ((n & (n + 1)) - 1);
}

static __inline void
idrfixup(struct idr *idp, int id)
{
	if (id < idp->idr_freeindex) {
		idp->idr_freeindex = id;
	}
	while (idp->idr_lastindex >= 0 &&
		idp->idr_nodes[idp->idr_lastindex].data == NULL &&
		idp->idr_nodes[idp->idr_lastindex].reserved == 0
	) {
		--idp->idr_lastindex;
	}
}

static __inline struct idr_node *
idr_get_node(struct idr *idp, int id)
{
	struct idr_node *idrnp;
	if (id >= idp->idr_count)
		return (NULL);
	idrnp = &idp->idr_nodes[id];
	if (idrnp->allocated == 0)
		return (NULL);
	return (idrnp);
}

static void
idr_reserve(struct idr *idp, int id, int incr)
{
	while (id >= 0) {
		idp->idr_nodes[id].allocated += incr;
		KKASSERT(idp->idr_nodes[id].allocated >= 0);
		id = left_ancestor(id);
	}
}

static int
idr_find_free(struct idr *idp, int want, int lim)
{
	int id, rsum, rsize, node;

	/*
	 * Search for a free descriptor starting at the higher
	 * of want or fd_freefile.  If that fails, consider
	 * expanding the ofile array.
	 *
	 * NOTE! the 'allocated' field is a cumulative recursive allocation
	 * count.  If we happen to see a value of 0 then we can shortcut
	 * our search.  Otherwise we run through through the tree going
	 * down branches we know have free descriptor(s) until we hit a
	 * leaf node.  The leaf node will be free but will not necessarily
	 * have an allocated field of 0.
	 */

	/* move up the tree looking for a subtree with a free node */
	for (id = max(want, idp->idr_freeindex); id < min(idp->idr_count, lim);
			id = right_ancestor(id)) {
		if (idp->idr_nodes[id].allocated == 0)
			return (id);

		rsize = right_subtree_size(id);
		if (idp->idr_nodes[id].allocated == rsize)
			continue;	/* right subtree full */

		/*
		 * Free fd is in the right subtree of the tree rooted at fd.
		 * Call that subtree R.  Look for the smallest (leftmost)
		 * subtree of R with an unallocated fd: continue moving
		 * down the left branch until encountering a full left
		 * subtree, then move to the right.
		 */
		for (rsum = 0, rsize /= 2; rsize > 0; rsize /= 2) {
			node = id + rsize;
			rsum += idp->idr_nodes[node].allocated;
			if (idp->idr_nodes[id].allocated == rsum + rsize) {
				id = node;	/* move to the right */
				if (idp->idr_nodes[node].allocated == 0)
					return (id);
				rsum = 0;
			}
		}
		return (id);
	}
	return (-1);
}

static int
idr_pre_get1(struct idr *idp, int want, int lim)
{
	int id;

	if (want >= idp->idr_count)
		idr_grow(idp, want);

retry:
	id = idr_find_free(idp, want, lim);
	if (id > -1)
		goto found;

	/*
	 * No space in current array.  Expand?
	 */
	if (idp->idr_count >= lim) {
		return (ENOSPC);
	}
	idr_grow(idp, want);
	goto retry;

found:
	return (0);
}

int
idr_pre_get(struct idr *idp, __unused unsigned gfp_mask)
{
	lwkt_gettoken(&idp->idr_token);
	int error = idr_pre_get1(idp, idp->idr_maxwant, INT_MAX);
	lwkt_reltoken(&idp->idr_token);
	return (error == 0);
}

int
idr_get_new(struct idr *idp, void *ptr, int *id)
{
	int resid;

	if (ptr == NULL)
		return (EINVAL);

	lwkt_gettoken(&idp->idr_token);
	resid = idr_find_free(idp, 0, INT_MAX);
	if (resid == -1) {
		lwkt_reltoken(&idp->idr_token);
		return (EAGAIN);
	}

	if (resid > idp->idr_lastindex)
		idp->idr_lastindex = resid;
	idp->idr_freeindex = resid;
	*id = resid;
	KKASSERT(idp->idr_nodes[resid].reserved == 0);
	idp->idr_nodes[resid].reserved = 1;
	idr_reserve(idp, resid, 1);
	idr_set(idp, resid, ptr);

	lwkt_reltoken(&idp->idr_token);
	return (0);
}

int
idr_get_new_above(struct idr *idp, void *ptr, int sid, int *id)
{
	int resid;

	if (ptr == NULL)
		return (EINVAL);

	lwkt_gettoken(&idp->idr_token);
	if (sid >= idp->idr_count) {
		idp->idr_maxwant = max(idp->idr_maxwant, sid);
		lwkt_reltoken(&idp->idr_token);
		return (EAGAIN);
	}

	resid = idr_find_free(idp, sid, INT_MAX);
	if (resid == -1) {
		lwkt_reltoken(&idp->idr_token);
		return (EAGAIN);
	}

	if (resid >= idp->idr_count)
		idr_grow(idp, resid);
	if (resid > idp->idr_lastindex)
		idp->idr_lastindex = resid;
	if (sid <= idp->idr_freeindex)
		idp->idr_freeindex = resid;
	*id = resid;
	KKASSERT(idp->idr_nodes[resid].reserved == 0);
	idp->idr_nodes[resid].reserved = 1;
	idr_reserve(idp, resid, 1);
	idr_set(idp, resid, ptr);

	lwkt_reltoken(&idp->idr_token);
	return (0);
}

/*
 * Grow the file table so it can hold through descriptor (want).
 */
static void
idr_grow(struct idr *idp, int want)
{
	struct idr_node *newnodes;
	struct idr_node *oldnodes;
	int nf, extra;

	nf = idp->idr_count;
	do {
		/* nf has to be of the form 2^n - 1 */
		nf = 2 * nf + 1;
	} while (nf <= want);

	newnodes = kmalloc(nf * sizeof(struct idr_node), M_IDR, M_WAITOK);

	/*
	 * Copy the existing ofile and ofileflags arrays
	 * and zero the new portion of each array.
	 */
	extra = nf - idp->idr_count;
	if (idp->idr_nodes != NULL)
		bcopy(idp->idr_nodes, newnodes, idp->idr_count * sizeof(struct idr_node));
	bzero(&newnodes[idp->idr_count], extra * sizeof(struct idr_node));

	oldnodes = idp->idr_nodes;
	idp->idr_nodes = newnodes;
	idp->idr_count = nf;

	if (oldnodes != NULL) {
		kfree(oldnodes, M_IDR);
	}

	idp->idr_nexpands++;
}

void
idr_remove(struct idr *idp, int id)
{
	void *ptr;

	lwkt_gettoken(&idp->idr_token);

	if (id >= idp->idr_count)
		goto out;
	if ((ptr = idp->idr_nodes[id].data) == NULL)
		goto out;
	idp->idr_nodes[id].data = NULL;

	idr_reserve(idp, id, -1);
	idrfixup(idp, id);

out:
	lwkt_reltoken(&idp->idr_token);
}

void
idr_remove_all(struct idr *idp)
{
	kfree(idp->idr_nodes, M_IDR);
	idp->idr_nodes = kmalloc(idp->idr_count * sizeof *idp, M_IDR, M_WAITOK | M_ZERO);
	idp->idr_lastindex = -1;
	idp->idr_freeindex = 0;
	idp->idr_nexpands = 0;
	idp->idr_maxwant = 0;
}

void
idr_destroy(struct idr *idp)
{
	lwkt_token_uninit(&idp->idr_token);
	kfree(idp->idr_nodes, M_IDR);
	memset(idp, 0, sizeof(struct idr));
}

void *
idr_find(struct idr *idp, int id)
{
	void * ret = NULL;

	lwkt_gettoken(&idp->idr_token);

	if (id > idp->idr_count) {
		goto out;
	} else if (idp->idr_nodes[id].allocated == 0) {
		goto out;
	}
	KKASSERT(idp->idr_nodes[id].data != NULL);
	ret = idp->idr_nodes[id].data;

out:
	lwkt_reltoken(&idp->idr_token);
	return ret;
}

static void
idr_set(struct idr *idp, int id, void *ptr)
{
	KKASSERT(id < idp->idr_count);
	KKASSERT(idp->idr_nodes[id].reserved != 0);
	if (ptr) {
		idp->idr_nodes[id].data = ptr;
		idp->idr_nodes[id].reserved = 0;
	} else {
		idp->idr_nodes[id].reserved = 0;
		idr_reserve(idp, id, -1);
		idrfixup(idp, id);
	}
}

int
idr_for_each(struct idr *idp, int (*fn)(int id, void *p, void *data), void *data)
{
	int i, error = 0;
	struct idr_node *nodes = idp->idr_nodes;

	lwkt_gettoken(&idp->idr_token);
	for (i = 0; i < idp->idr_count; i++) {
		if (nodes[i].data != NULL && nodes[i].allocated > 0) {
			error = fn(i, nodes[i].data, data);
			if (error != 0)
				goto out;
		}
	}
out:
	lwkt_reltoken(&idp->idr_token);
	return error;
}

void *
idr_replace(struct idr *idp, void *ptr, int id)
{
	struct idr_node *idrnp;
	void *ret = NULL;

	lwkt_gettoken(&idp->idr_token);

	idrnp = idr_get_node(idp, id);
	if (idrnp == NULL || ptr == NULL)
		goto out;

	ret = idrnp->data;
	idrnp->data = ptr;

out:
	lwkt_reltoken(&idp->idr_token);
	return (ret);
}

void
idr_init(struct idr *idp)
{
	bzero(idp, sizeof(struct idr));
	idp->idr_nodes = kmalloc(IDR_DEFAULT_SIZE * sizeof(*idp),
						M_IDR, M_WAITOK | M_ZERO);
	idp->idr_count = IDR_DEFAULT_SIZE;
	idp->idr_lastindex = -1;
	idp->idr_maxwant = 0;
	lwkt_token_init(&idp->idr_token, "idr token");
}
