/*-
 * Copyright (c) 2002 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sbin/gpt/map.c,v 1.6 2005/08/31 01:47:19 marcel Exp $
 */

#include <err.h>
#include <stdlib.h>

#include "map.h"

static map_t *mediamap;

map_t *
mkmap(off_t start, off_t size, int type)
{
	map_t *m;

	m = malloc(sizeof(*m));
	if (m == NULL)
		return (NULL);
	m->map_start = start;
	m->map_size = size;
	m->map_next = m->map_prev = NULL;
	m->map_type = type;
	m->map_index = MAP_NOENTRY;
	m->map_data = NULL;
	return (m);
}

map_t *
map_add(off_t start, off_t size, int type, void *data)
{
	map_t *m, *n, *p;

	n = mediamap;
	while (n != NULL && n->map_start + n->map_size <= start)
		n = n->map_next;
	if (n == NULL)
		return (NULL);

	if (n->map_start + n->map_size < start + size) {
		warnx("error: map entry doesn't fit media: "
		      "new_start + new_size < start + size "
		      "(%lld + %lld < %lld + %lld)",
		      (long long)n->map_start, (long long)n->map_size,
		      (long long)start, (long long)size);
		return (NULL);
	}

	if (n->map_start == start && n->map_size == size) {
		if (n->map_type != MAP_TYPE_UNUSED) {
			if (n->map_type != MAP_TYPE_MBR_PART ||
			    type != MAP_TYPE_GPT_PART) {
				warnx("warning: partition(%llu,%llu) mirrored",
				    (long long)start, (long long)size);
			}
		}
		n->map_type = type;
		n->map_data = data;
		return (n);
	}

	if (n->map_type != MAP_TYPE_UNUSED) {
		if (n->map_type != MAP_TYPE_MBR_PART ||
		    type != MAP_TYPE_GPT_PART) {
			warnx("error: bogus map: current=%d, new=%d",
			      n->map_type, type);
			return (NULL);
		}
		n->map_type = MAP_TYPE_UNUSED;
	}

	m = mkmap(start, size, type);
	if (m == NULL)
		return (NULL);

	m->map_data = data;

	if (start == n->map_start) {
		m->map_prev = n->map_prev;
		m->map_next = n;
		if (m->map_prev != NULL)
			m->map_prev->map_next = m;
		else
			mediamap = m;
		n->map_prev = m;
		n->map_start += size;
		n->map_size -= size;
	} else if (start + size == n->map_start + n->map_size) {
		p = n;
		m->map_next = p->map_next;
		m->map_prev = p;
		if (m->map_next != NULL)
			m->map_next->map_prev = m;
		p->map_next = m;
		p->map_size -= size;
	} else {
		p = mkmap(n->map_start, start - n->map_start, n->map_type);
		n->map_start += p->map_size + m->map_size;
		n->map_size -= (p->map_size + m->map_size);
		p->map_prev = n->map_prev;
		m->map_prev = p;
		n->map_prev = m;
		m->map_next = n;
		p->map_next = m;
		if (p->map_prev != NULL)
			p->map_prev->map_next = p;
		else
			mediamap = p;
	}

	return (m);
}

/*
 * Try to allocate a new partition from the free space.
 *
 * If <start> is zero, use the first region that has enough free space.
 * If <size> is zero, use all the free space in the matched region.
 * If <alignment> is given, align both the <start> and <size>.
 */
map_t *
map_alloc(off_t start, off_t size, off_t alignment)
{
	off_t delta, msize;
	map_t *m;

	if (alignment > 1) {
		start = (start + alignment - 1) / alignment * alignment;
		size = (size + alignment - 1) / alignment * alignment;
	}

	for (m = mediamap; m != NULL; m = m->map_next) {
		if (m->map_type != MAP_TYPE_UNUSED || m->map_start < 2)
			continue;
		if (start != 0 && m->map_start > start)
			return (NULL);

		if (start != 0)
			delta = start - m->map_start;
		else if (alignment > 1)
			delta = ((m->map_start + alignment - 1) /
				 alignment * alignment) - m->map_start;
		else
			delta = 0;
		if (m->map_size <= delta)
			continue;

		msize = (size != 0) ? size : m->map_size - delta;
		if (alignment > 1)
			msize = msize / alignment * alignment;
		if (msize == 0 || msize + delta > m->map_size)
			continue;

		return map_add(m->map_start + delta, msize,
			       MAP_TYPE_GPT_PART, NULL);
	}

	return (NULL);
}

map_t *
map_find(int type)
{
	map_t *m;

	m = mediamap;
	while (m != NULL && m->map_type != type)
		m = m->map_next;
	return (m);
}

map_t *
map_first(void)
{
	return mediamap;
}

map_t *
map_last(void)
{
	map_t *m;

	m = mediamap;
	while (m != NULL && m->map_next != NULL)
		m = m->map_next;
	return (m);
}

/*
 * Get the number of free blocks after position <start>.
 */
off_t
map_free(off_t start)
{
	map_t *m;

	m = mediamap;
	while (m != NULL && m->map_start + m->map_size <= start)
		m = m->map_next;
	if (m == NULL || m->map_type != MAP_TYPE_UNUSED)
		return (0LL);
	return (m->map_size - (start - m->map_start));
}

void
map_init(off_t size)
{
	mediamap = mkmap(0LL, size, MAP_TYPE_UNUSED);
}
