/*
 * Copyright (c) 2001 Daniel Eischen <deischen@freebsd.org>
 * Copyright (c) 2000-2001 Jason Evans <jasone@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libpthread/thread/thr_stack.c,v 1.9 2004/10/06 08:11:07 davidxu Exp $
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <machine/tls.h>
#include <machine/vmparam.h>
#include <stdlib.h>
#include <pthread.h>
#include "thr_private.h"

/* Spare thread stack. */
struct stack {
	LIST_ENTRY(stack)	qe;		/* Stack queue linkage. */
	size_t			stacksize;	/* Stack size (rounded up). */
	size_t			guardsize;	/* Guard size. */
	void			*stackaddr;	/* Stack address. */
};

/*
 * Default sized (stack and guard) spare stack queue.  Stacks are cached
 * to avoid additional complexity managing mmap()ed stack regions.  Spare
 * stacks are used in LIFO order to increase cache locality.
 */
static LIST_HEAD(, stack)	dstackq = LIST_HEAD_INITIALIZER(dstackq);

/*
 * Miscellaneous sized (non-default stack and/or guard) spare stack queue.
 * Stacks are cached to avoid additional complexity managing mmap()ed
 * stack regions.  This list is unordered, since ordering on both stack
 * size and guard size would be more trouble than it's worth.  Stacks are
 * allocated from this cache on a first size match basis.
 */
static LIST_HEAD(, stack)	mstackq = LIST_HEAD_INITIALIZER(mstackq);

/*
 * Thread stack base for mmap() hint, starts
 * at _usrstack - kern.maxssiz - kern.maxthrssiz
 */
static char *base_stack = NULL;

/*
 * Round size up to the nearest multiple of
 * _thr_page_size.
 */
static inline size_t
round_up(size_t size)
{
	if (size % _thr_page_size != 0)
		size = ((size / _thr_page_size) + 1) *
		    _thr_page_size;
	return size;
}

int
_thr_stack_alloc(struct pthread_attr *attr)
{
	struct pthread *curthread = tls_get_curthread();
	struct stack *spare_stack;
	size_t stacksize;
	size_t guardsize;
	char *stackaddr;

	/*
	 * Round up stack size to nearest multiple of _thr_page_size so
	 * that mmap() * will work.  If the stack size is not an even
	 * multiple, we end up initializing things such that there is
	 * unused space above the beginning of the stack, so the stack
	 * sits snugly against its guard.
	 */
	stacksize = round_up(attr->stacksize_attr);
	guardsize = round_up(attr->guardsize_attr);

	attr->stackaddr_attr = NULL;
	attr->flags &= ~THR_STACK_USER;

	/*
	 * Use the garbage collector lock for synchronization of the
	 * spare stack lists and allocations from usrstack.
	 */
	THREAD_LIST_LOCK(curthread);
	/*
	 * If the stack and guard sizes are default, try to allocate a stack
	 * from the default-size stack cache:
	 */
	if ((stacksize == THR_STACK_DEFAULT) &&
	    (guardsize == _thr_guard_default)) {
		if ((spare_stack = LIST_FIRST(&dstackq)) != NULL) {
			/* Use the spare stack. */
			LIST_REMOVE(spare_stack, qe);
			attr->stackaddr_attr = spare_stack->stackaddr;
		}
	}
	/*
	 * The user specified a non-default stack and/or guard size, so try to
	 * allocate a stack from the non-default size stack cache, using the
	 * rounded up stack size (stack_size) in the search:
	 */
	else {
		LIST_FOREACH(spare_stack, &mstackq, qe) {
			if (spare_stack->stacksize == stacksize &&
			    spare_stack->guardsize == guardsize) {
				LIST_REMOVE(spare_stack, qe);
				attr->stackaddr_attr = spare_stack->stackaddr;
				break;
			}
		}
	}
	if (attr->stackaddr_attr != NULL) {
		/* A cached stack was found.  Release the lock. */
		THREAD_LIST_UNLOCK(curthread);
	} else {
		/*
		 * Calculate base_stack on first use (race ok).
		 * If base _stack
		 */
		if (base_stack == NULL) {
			int64_t maxssiz;
			int64_t maxthrssiz;
			struct rlimit rl;
			size_t len;

			if (getrlimit(RLIMIT_STACK, &rl) == 0)
				maxssiz = rl.rlim_max;
			else
				maxssiz = MAXSSIZ;
			len = sizeof(maxssiz);
			sysctlbyname("kern.maxssiz", &maxssiz, &len, NULL, 0);
			len = sizeof(maxthrssiz);
			if (sysctlbyname("kern.maxthrssiz",
					 &maxthrssiz, &len, NULL, 0) < 0) {
				maxthrssiz = MAXTHRSSIZ;
			}
			base_stack = _usrstack - maxssiz - maxthrssiz;
		}

		/* Release the lock before mmap'ing it. */
		THREAD_LIST_UNLOCK(curthread);

		/*
		 * Map the stack and guard page together then split the
		 * guard page from allocated space.
		 *
		 * We no longer use MAP_STACK and we define an area far
		 * away from the default user stack (even though this will
		 * cost us another few 4K page-table pages).  DFly no longer
		 * allows new MAP_STACK mappings to be made inside ungrown
		 * portions of existing mappings.
		 */
		stackaddr = mmap(base_stack, stacksize + guardsize,
				 PROT_READ | PROT_WRITE,
				 MAP_ANON | MAP_PRIVATE, -1, 0);
		if (stackaddr != MAP_FAILED && guardsize) {
			if (mmap(stackaddr, guardsize, 0,
				 MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) {
				munmap(stackaddr, stacksize + guardsize);
				stackaddr = MAP_FAILED;
			} else {
				stackaddr += guardsize;
			}
		}
		if (stackaddr == MAP_FAILED)
			stackaddr = NULL;
		attr->stackaddr_attr = stackaddr;
	}
	if (attr->stackaddr_attr != NULL)
		return (0);
	else
		return (-1);
}

/* This function must be called with _thread_list_lock held. */
void
_thr_stack_free(struct pthread_attr *attr)
{
	struct stack *spare_stack;

	if ((attr != NULL) && ((attr->flags & THR_STACK_USER) == 0)
	    && (attr->stackaddr_attr != NULL)) {
		spare_stack = (struct stack *)((char *)attr->stackaddr_attr +
			attr->stacksize_attr - sizeof(struct stack));
		spare_stack->stacksize = round_up(attr->stacksize_attr);
		spare_stack->guardsize = round_up(attr->guardsize_attr);
		spare_stack->stackaddr = attr->stackaddr_attr;

		if (spare_stack->stacksize == THR_STACK_DEFAULT &&
		    spare_stack->guardsize == _thr_guard_default) {
			/* Default stack/guard size. */
			LIST_INSERT_HEAD(&dstackq, spare_stack, qe);
		} else {
			/* Non-default stack/guard size. */
			LIST_INSERT_HEAD(&mstackq, spare_stack, qe);
		}
		attr->stackaddr_attr = NULL;
	}
}

void
_thr_stack_cleanup(void)
{
	struct stack *spare;

	while ((spare = LIST_FIRST(&dstackq)) != NULL) {
		LIST_REMOVE(spare, qe);
		munmap(spare->stackaddr,
		       spare->stacksize + spare->guardsize);
	}
}
