/*
 * Copyright (c) 2010, Venkatesh Srinivas <me@endeavour.zapto.org>
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Lightweight reference counts
 */

#include <cpu/atomic.h>
#include <sys/ref.h>

void 
kref_init(struct kref *ref, int count)
{
	ref->refcount = count;
}

void
kref_inc(struct kref *ref)
{
	atomic_add_int(&ref->refcount, 1);
}

/*
 * Decrement a reference count; on a 1 -> 0 transition, call the
 * deconstruct function (if any) with priv1 and priv2. Returns 0
 * if it sees a 1 -> 0 transition, 1 otherwise.
 *
 * "An object cannot synchronize its own visibility." It is not safe
 * to interleave kref_inc and kref_dec without other synchronization.
 */
int
kref_dec(struct kref *ref, void (*deconstruct)(void *, void *),
	 void *priv1, void *priv2)
{
	int val;

	val = atomic_fetchadd_int(&ref->refcount, -1);
	if (val == 1)  {
		if (deconstruct)
			deconstruct(priv1, priv2);
		return (0);
	}

	return (1);
}
