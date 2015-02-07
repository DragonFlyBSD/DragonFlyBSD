/*
 * Copyright (c) 2005-2012 The DragonFly Project.
 * Copyright (c) 2013 François Tigeot
 * Copyright (c) 2013 Matthew Dillon
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
 */

#ifdef USERLAND_TEST
/*
 * Testing:
 *
 * cc -I. -DUSERLAND_TEST libkern/linux_idr.c -o /tmp/idr -g
 */

#define _KERNEL
#define _KERNEL_STRUCTURES
#define KLD_MODULE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <sys/idr.h>
#include <sys/errno.h>

#undef MALLOC_DEFINE
#define MALLOC_DEFINE(a, b, c)
#define lwkt_gettoken(x)
#define lwkt_reltoken(x)
#define kmalloc(bytes, zone, flags)	calloc(bytes, 1)
#define lwkt_token_init(a, b)
#define lwkt_token_uninit(a)
#define kfree(ptr, flags)		free(ptr)
#define KKASSERT(a)
#define panic(str, ...)			assert(0)
#define min(a, b)	(((a) < (b)) ? (a) : (b))
#define max(a, b)	(((a) > (b)) ? (a) : (b))

int
main(int ac, char **av)
{
	char buf[256];
	struct idr idr;
	intptr_t generation = 0x10000000;
	int error;
	int id;

	idr_init(&idr);

	printf("cmd> ");
	fflush(stdout);
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
		if (sscanf(buf, "a %d", &id) == 1) {
			for (;;) {
				if (idr_pre_get(&idr, 0) == 0) {
					fprintf(stderr, "pre_get failed\n");
					exit(1);
				}
				error = idr_get_new_above(&idr,
							  (void *)generation,
							  id, &id);
				if (error == -EAGAIN)
					continue;
				if (error) {
					fprintf(stderr, "get_new err %d\n",
						error);
					exit(1);
				}
				printf("allocated %d value %08x\n",
					id, (int)generation);
				++generation;
				break;
			}
		} else if (sscanf(buf, "f %d", &id) == 1) {
			idr_remove(&idr, id);
		} else if (sscanf(buf, "g %d", &id) == 1) {
			void *res = idr_find(&idr, id);
			printf("find %d res %p\n", id, res);
		}
		printf("cmd> ");
		fflush(stdout);
	}
	return 0;
}

#else

#include <sys/idr.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/spinlock2.h>
#include <sys/limits.h>

#endif

/* Must be 2^n - 1 */
#define IDR_DEFAULT_SIZE    255

MALLOC_DEFINE(M_IDR, "idr", "Integer ID management");

static void	idr_grow(struct idr *idp, int want);
static void	idr_reserve(struct idr *idp, int id, int incr);
static int	idr_find_free(struct idr *idp, int want, int lim);

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
		idp->idr_nodes[idp->idr_lastindex].data == NULL
	) {
		--idp->idr_lastindex;
	}
}

static __inline struct idr_node *
idr_get_node(struct idr *idp, int id)
{
	struct idr_node *idrnp;
	if (id < 0 || id >= idp->idr_count)
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

/*
 * Blocking pre-get support, allows callers to use idr_pre_get() in
 * combination with idr_get_new_above() such that idr_get_new_above()
 * can be called safely with a spinlock held.
 *
 * Returns 0 on failure, 1 on success.
 *
 * Caller must hold a blockable lock.
 */
int
idr_pre_get(struct idr *idp, __unused unsigned gfp_mask)
{
	int want = idp->idr_maxwant;
	int lim = INT_MAX;
	int result = 1;				/* success */
	int id;

	KKASSERT(mycpu->gd_spinlocks == 0);
	lwkt_gettoken(&idp->idr_token);
	for (;;) {
		/*
		 * Grow if necessary (or if forced by the loop)
		 */
		if (want >= idp->idr_count)
			idr_grow(idp, want);

		/*
		 * Check if a spot is available, break and return 0 if true,
		 * unless the available spot is beyond our limit.  It is
		 * possible to exceed the limit due to the way array growth
		 * works.
		 *
		 * XXX we assume that the caller uses a consistent <sid> such
		 *     that the idr_maxwant field is correct, otherwise we
		 *     may believe that a slot is available but the caller then
		 *     fails in idr_get_new_above() and loops.
		 */
		id = idr_find_free(idp, idp->idr_maxwant, lim);
		if (id != -1) {
			if (id >= lim)
				result = 0;	/* failure */
			break;
		}

		/*
		 * Return ENOSPC if our limit has been reached, otherwise
		 * loop and force growth.
		 */
		if (idp->idr_count >= lim) {
			result = 0;		/* failure */
			break;
		}
		want = idp->idr_count;
	}
	lwkt_reltoken(&idp->idr_token);
	return result;
}

/*
 * Allocate an integer.  If -EAGAIN is returned the caller should loop
 * and call idr_pre_get() with no locks held, and then retry the call
 * to idr_get_new_above().
 *
 * Can be safely called with spinlocks held.
 */
int
idr_get_new_above(struct idr *idp, void *ptr, int sid, int *id)
{
	int resid;

	/*
	 * NOTE! Because the idp is initialized with a non-zero count,
	 *	 sid might be < idp->idr_count but idr_maxwant might not
	 *	 yet be initialized.  So check both cases.
	 */
	lwkt_gettoken(&idp->idr_token);
	if (sid >= idp->idr_count || idp->idr_maxwant < sid) {
		idp->idr_maxwant = max(idp->idr_maxwant, sid);
		lwkt_reltoken(&idp->idr_token);
		return -EAGAIN;
	}

	resid = idr_find_free(idp, sid, INT_MAX);
	if (resid == -1) {
		lwkt_reltoken(&idp->idr_token);
		return -EAGAIN;
	}

	if (resid >= idp->idr_count)
		panic("idr_get_new_above(): illegal resid %d", resid);
	if (resid > idp->idr_lastindex)
		idp->idr_lastindex = resid;
	if (sid <= idp->idr_freeindex)
		idp->idr_freeindex = resid;
	*id = resid;
	idr_reserve(idp, resid, 1);
	idp->idr_nodes[resid].data = ptr;

	lwkt_reltoken(&idp->idr_token);
	return (0);
}

/*
 * start: minimum id, inclusive
 * end:   maximum id, exclusive or INT_MAX if end is negative
 */
int
idr_alloc(struct idr *idp, void *ptr, int start, int end, unsigned gfp_mask)
{
	int lim = end > 0 ? end - 1 : INT_MAX;
	int result, id;

	kprintf("idr_alloc: %p, %p, %d, %d\n", idp, ptr, start, end);

	if (start < 0)
		return -EINVAL;

	if (lim < start)
		return -ENOSPC;

	lwkt_gettoken(&idp->idr_token);

	/*
	 * Grow if necessary (or if forced by the loop)
	 */
	if (start >= idp->idr_count)
		idr_grow(idp, start);

	/*
	 * Check if a spot is available, break and return 0 if true,
	 * unless the available spot is beyond our limit.  It is
	 * possible to exceed the limit due to the way array growth
	 * works.
	 */
	id = idr_find_free(idp, start, lim);
	if (id == -1) {
		result = -ENOSPC;
		goto done;
	}

	if (id >= lim) {
		result = -ENOSPC;
		goto done;
	}

	if (id >= idp->idr_count)
		panic("idr_get_new_above(): illegal resid %d", id);
	if (id > idp->idr_lastindex)
		idp->idr_lastindex = id;
	if (start <= idp->idr_freeindex)
		idp->idr_freeindex = id;
	result = id;
	idr_reserve(idp, id, 1);
	idp->idr_nodes[id].data = ptr;

done:
	lwkt_reltoken(&idp->idr_token);
	return result;
}

int
idr_get_new(struct idr *idp, void *ptr, int *id)
{
	return idr_get_new_above(idp, ptr, 0, id);
}

/*
 * Grow the file table so it can hold through descriptor (want).
 *
 * Caller must hold a blockable lock.
 */
static void
idr_grow(struct idr *idp, int want)
{
	struct idr_node *oldnodes, *newnodes;
	int nf;

	/* We want 2^n - 1 descriptors */
	nf = idp->idr_count;
	do {
		nf = 2 * nf + 1;
	} while (nf <= want);

#ifdef USERLAND_TEST
	printf("idr_grow: %d -> %d\n", idp->idr_count, nf);
#endif

	/* Allocate a new zero'ed node array */
	newnodes = kmalloc(nf * sizeof(struct idr_node),
			   M_IDR, M_ZERO | M_WAITOK);

	/* We might race another grow */
	if (nf <= idp->idr_count) {
		kfree(newnodes, M_IDR);
		return;
	}

	/*
	 * Copy existing nodes to the beginning of the new array
	 */
	oldnodes = idp->idr_nodes;
	if (oldnodes) {
		bcopy(oldnodes, newnodes,
		      idp->idr_count * sizeof(struct idr_node));
	}
	idp->idr_nodes = newnodes;
	idp->idr_count = nf;

	if (oldnodes) {
		kfree(oldnodes, M_IDR);
	}
	idp->idr_nexpands++;
}

void
idr_remove(struct idr *idp, int id)
{
	void *ptr;

	lwkt_gettoken(&idp->idr_token);
	if (id < 0 || id >= idp->idr_count) {
		lwkt_reltoken(&idp->idr_token);
		return;
	}
	if ((ptr = idp->idr_nodes[id].data) == NULL) {
		lwkt_reltoken(&idp->idr_token);
		return;
	}
	idp->idr_nodes[id].data = NULL;
	idr_reserve(idp, id, -1);
	idrfixup(idp, id);
	lwkt_reltoken(&idp->idr_token);
}

/*
 * Remove all int allocations, leave array intact.
 *
 * Caller must hold a blockable lock (or be in a context where holding
 * the spinlock is not relevant).
 */
void
idr_remove_all(struct idr *idp)
{
	lwkt_gettoken(&idp->idr_token);
	bzero(idp->idr_nodes, idp->idr_count * sizeof(struct idr_node));
	idp->idr_lastindex = -1;
	idp->idr_freeindex = 0;
	idp->idr_nexpands = 0;
	idp->idr_maxwant = 0;
	lwkt_reltoken(&idp->idr_token);
}

void
idr_destroy(struct idr *idp)
{
	lwkt_token_uninit(&idp->idr_token);
	if (idp->idr_nodes) {
		kfree(idp->idr_nodes, M_IDR);
		idp->idr_nodes = NULL;
	}
	bzero(idp, sizeof(*idp));
}

void *
idr_find(struct idr *idp, int id)
{
	void *ret;

	if (id < 0 || id >= idp->idr_count) {
		ret = NULL;
	} else if (idp->idr_nodes[id].allocated == 0) {
		ret = NULL;
	} else {
		ret = idp->idr_nodes[id].data;
	}
	return ret;
}

int
idr_for_each(struct idr *idp, int (*fn)(int id, void *p, void *data),
	     void *data)
{
	int i, error = 0;
	struct idr_node *nodes;

	nodes = idp->idr_nodes;
	for (i = 0; i < idp->idr_count; i++) {
		if (nodes[i].data != NULL && nodes[i].allocated > 0) {
			error = fn(i, nodes[i].data, data);
			if (error != 0)
				break;
		}
	}
	return error;
}

void *
idr_replace(struct idr *idp, void *ptr, int id)
{
	struct idr_node *idrnp;
	void *ret;

	lwkt_gettoken(&idp->idr_token);
	idrnp = idr_get_node(idp, id);
	if (idrnp == NULL || ptr == NULL) {
		ret = NULL;
	} else {
		ret = idrnp->data;
		idrnp->data = ptr;
	}
	lwkt_reltoken(&idp->idr_token);
	return (ret);
}

void
idr_init(struct idr *idp)
{
	bzero(idp, sizeof(struct idr));
	idp->idr_nodes = kmalloc(IDR_DEFAULT_SIZE * sizeof(struct idr_node),
						M_IDR, M_WAITOK | M_ZERO);
	idp->idr_count = IDR_DEFAULT_SIZE;
	idp->idr_lastindex = -1;
	idp->idr_maxwant = 0;
	lwkt_token_init(&idp->idr_token, "idr token");
}
