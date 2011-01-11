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

#ifndef _SYS_REF_H_
#define _SYS_REF_H_

struct kref {
	int refcount;
};

void kref_init(struct kref *ref, int count);
void kref_inc(struct kref *ref);
int kref_dec(struct kref *ref, void (*deconstruct)(void*, void*), 
	     void *priv1, void *priv2);

#define KREF_DEC(refp, deconstruct)					\
	({								\
		int _val = atomic_fetchadd_int(&(refp)->refcount, -1);	\
		if (_val == 1)						\
			deconstruct;					\
		(_val != 1);						\
	})

#endif 

