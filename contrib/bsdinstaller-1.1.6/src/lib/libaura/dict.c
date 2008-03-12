/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Chris Pressey <cpressey@catseye.mine.nu>.
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
 * dict.c
 * $Id: dict.c,v 1.4 2005/02/06 06:57:30 cpressey Exp $
 * Routines to manipulate dictionaries.
 */

#include <stdlib.h>
#include <string.h>

#include "mem.h"

#include "dict.h"

/*** INTERNAL PROTOTYPES ***/

size_t	hashpjw(const void *key, size_t key_size, size_t table_size);
int	keycmp(const void *key, size_t key_size, struct aura_bucket *b);

struct aura_bucket *aura_bucket_new(const void *key, size_t key_size,
				const void *data, size_t data_size);
void	aura_bucket_free(struct aura_bucket *b);
void	aura_dict_locate_hash(struct aura_dict *d, const void *key, size_t key_size,
				size_t *b_index, struct aura_bucket **b);
void	aura_dict_locate_list(struct aura_dict *d, const void *key, size_t key_size,
				struct aura_bucket **b);
void	aura_dict_advance(struct aura_dict *d);

void	aura_dict_fetch_hash(struct aura_dict *d, const void *key, size_t key_size,
				void **data, size_t *data_size);
void	aura_dict_store_hash(struct aura_dict *d, const void *key, size_t key_size,
				const void *data, size_t data_size);
void	aura_dict_fetch_list(struct aura_dict *d, const void *key, size_t key_size,
				void **data, size_t *data_size);
void	aura_dict_store_list(struct aura_dict *d, const void *key, size_t key_size,
				const void *data, size_t data_size);
void	aura_dict_store_list_sorted(struct aura_dict *d, const void *key, size_t key_size,
				const void *data, size_t data_size);

/*** CONSTRUCTOR ***/

/*
 * Create a new dictionary with the given number of buckets.
 */
struct aura_dict *
aura_dict_new(size_t num_buckets, int method)
{
	struct aura_dict *d;
	size_t i;

	AURA_MALLOC(d, aura_dict);

	d->num_buckets = num_buckets;
	d->b = malloc(sizeof(struct bucket *) * num_buckets);
	for (i = 0; i < num_buckets; i++) {
		d->b[i] = NULL;
	}
	d->cursor = NULL;
	d->cur_bucket = 0;

	switch (method) {
	case AURA_DICT_HASH:
		d->fetch = aura_dict_fetch_hash;
		d->store = aura_dict_store_hash;
		break;
	case AURA_DICT_LIST:
		d->fetch = aura_dict_fetch_list;
		d->store = aura_dict_store_list;
		break;
	case AURA_DICT_SORTED_LIST:
		d->fetch = aura_dict_fetch_list;
		d->store = aura_dict_store_list_sorted;
		break;
	}

	return(d);
}

/*** DESTRUCTORS ***/

void
aura_bucket_free(struct aura_bucket *b)
{
	if (b == NULL)
		return;
	if (b->key != NULL)
		free(b->key);
	if (b->data != NULL)
		free(b->data);
	AURA_FREE(b, aura_bucket);
}

void
aura_dict_free(struct aura_dict *d)
{
	struct aura_bucket *b;
	size_t bucket_no = 0;

	while (bucket_no < d->num_buckets) {
		b = d->b[bucket_no];
		while (b != NULL) {
			d->b[bucket_no] = b->next;
			aura_bucket_free(b);
			b = d->b[bucket_no];
		}
		bucket_no++;
	}
	AURA_FREE(d, aura_dict);
}

/*** UTILITIES ***/

/*
 * Hash function, taken from "Compilers: Principles, Techniques, and Tools"
 * by Aho, Sethi, & Ullman (a.k.a. "The Dragon Book", 2nd edition.)
 */
size_t
hashpjw(const void *key, size_t key_size, size_t table_size) {
	const char *k = (const char *)key;
	const char *p;
	size_t h = 0, g;

	for (p = k; p < k + key_size; p++) {
		h = (h << 4) + (*p);
		if ((g = h & 0xf0000000))
			h = (h ^ (g >> 24)) ^ g;
	}
	
	return(h % table_size);
}

/*
 * Create a new bucket (not called directly by client code.)
 * Uses a copy of key and data for the bucket, so the dictionary
 * code is responsible for cleaning it up itself.
 */
struct aura_bucket *
aura_bucket_new(const void *key, size_t key_size, const void *data, size_t data_size)
{
	struct aura_bucket *b;

	AURA_MALLOC(b, aura_bucket);

	b->next = NULL;
	b->key = aura_malloc(key_size, "dictionary key");
	memcpy(b->key, key, key_size);
	b->key_size = key_size;
	b->data = aura_malloc(data_size, "dictionary value");
	memcpy(b->data, data, data_size);
	b->data_size = data_size;

	return(b);
}

/*
 * Locate the bucket number a particular key would be located in, and the
 * bucket itself if such a key exists (or NULL if it could not be found.)
 */
void
aura_dict_locate_hash(struct aura_dict *d, const void *key, size_t key_size,
		      size_t *b_index, struct aura_bucket **b)
{
	*b_index = hashpjw(key, key_size, d->num_buckets);
	for (*b = d->b[*b_index]; *b != NULL; *b = (*b)->next) {
		if (key_size == (*b)->key_size && memcmp(key, (*b)->key, key_size) == 0)
			break;
	}
}

/*
 * Locate the bucket a particular key would be located in
 * if such a key exists (or NULL if it could not be found.)
 */
void
aura_dict_locate_list(struct aura_dict *d, const void *key, size_t key_size,
		      struct aura_bucket **b)
{
	for (*b = d->b[0]; *b != NULL; *b = (*b)->next) {
		if (key_size == (*b)->key_size && memcmp(key, (*b)->key, key_size) == 0)
			break;
	}
}

/*** IMPLEMENTATIONS ***/

/***** HASH TABLE *****/

void
aura_dict_fetch_hash(struct aura_dict *d, const void *key, size_t key_size,
		     void **data, size_t *data_size)
{
	struct aura_bucket *b;
	size_t i;

	aura_dict_locate_hash(d, key, key_size, &i, &b);
	if (b != NULL) {
		*data = b->data;
		*data_size = b->data_size;
	} else {
		*data = NULL;
	}
}

void
aura_dict_store_hash(struct aura_dict *d, const void *key, size_t key_size,
		     const void *data, size_t data_size)
{
	struct aura_bucket *b;
	size_t i;

	aura_dict_locate_hash(d, key, key_size, &i, &b);
	if (b == NULL) {
		/* Bucket does not exist, add a new one. */
		b = aura_bucket_new(key, key_size, data, data_size);
		b->next = d->b[i];
		d->b[i] = b;
	} else {
		/* Bucket already exists, replace the value. */
		aura_free(b->data, "dictionary value");
		b->data = aura_malloc(data_size, "dictionary value");
		memcpy(b->data, data, data_size);
		b->data_size = data_size;
	}
}

/***** LIST *****/

void
aura_dict_fetch_list(struct aura_dict *d, const void *key, size_t key_size,
		     void **data, size_t *data_size)
{
	struct aura_bucket *b;

	for (b = d->b[0]; b != NULL; b = b->next) {
		if (key_size == b->key_size && memcmp(key, b->key, key_size) == 0) {
			*data = b->data;
			*data_size = b->data_size;
			return;
		}
	}
	*data = NULL;
}

void
aura_dict_store_list(struct aura_dict *d, const void *key, size_t key_size,
		     const void *data, size_t data_size)
{
	struct aura_bucket *b;

	aura_dict_locate_list(d, key, key_size, &b);
	if (b == NULL) {
		/* Bucket does not exist, add a new one. */
		b = aura_bucket_new(key, key_size, data, data_size);
		b->next = d->b[0];
		d->b[0] = b;
	} else {
		/* Bucket already exists, replace the value. */
		aura_free(b->data, "dictionary value");
		b->data = aura_malloc(data_size, "dictionary value");
		memcpy(b->data, data, data_size);
		b->data_size = data_size;
	}
}

/***** SORTED LIST *****/

int
keycmp(const void *key, size_t key_size, struct aura_bucket *b)
{
	int r;

	if ((r = memcmp(key, b->key,
	    b->key_size < key_size ? b->key_size : key_size)) == 0) {
		if (key_size < b->key_size)
			return(-1);
		if (key_size > b->key_size)
			return(1);
		return(0);
	}
	return(r);
}

void
aura_dict_store_list_sorted(struct aura_dict *d, const void *key, size_t key_size,
			    const void *data, size_t data_size)
{
	struct aura_bucket *b, *new_b, *prev = NULL;
	int added = 0;

	/* XXX could be more efficient. */
	aura_dict_locate_list(d, key, key_size, &b);
	if (b == NULL) {
		new_b = aura_bucket_new(key, key_size, data, data_size);
		if (d->b[0] == NULL) {
			/*
			 * Special case: insert at head
			 * if bucket is empty.
			 */
			new_b->next = NULL;
			d->b[0] = new_b;
		} else {
			for (b = d->b[0]; b != NULL; b = b->next) {
				/* XXX if identical - no need for above fetch */
				if (keycmp(key, key_size, b) < 0) {
					if (prev != NULL)
						prev->next = new_b;
					else
						d->b[0] = new_b;
					new_b->next = b;
					added = 1;
					break;
				}
				prev = b;
			}
			if (!added) {
				prev->next = new_b;
				new_b->next = NULL;
			}
		}
	} else {
		/* Bucket already exists, replace the value. */
		aura_free(b->data, "dictionary value");
		b->data = aura_malloc(data_size, "dictionary value");
		memcpy(b->data, data, data_size);
		b->data_size = data_size;
	}
}

/*** INTERFACE ***/

/*
 * Retrieve a value from a dictionary, give its key.  The value and its
 * size are returned in *data and *data_size.  If no value could be
 * found, *data is set to NULL.
 */
void
aura_dict_fetch(struct aura_dict *d, const void *key, size_t key_size,
		void **data, size_t *data_size)
{
	d->fetch(d, key, key_size, data, data_size);
}

int
aura_dict_exists(struct aura_dict *d, const void *key, size_t key_size)
{
	void *data;
	size_t data_size;

	d->fetch(d, key, key_size, &data, &data_size);
	return(data != NULL);
}

/*
 * Insert a value into a dictionary, if it does not exist.
 */
void
aura_dict_store(struct aura_dict *d, const void *key, size_t key_size,
		const void *data, size_t data_size)
{
	d->store(d, key, key_size, data, data_size);
}

/*
 * Finds the next bucket with data in it.
 * If d->cursor == NULL after this, there is no more data.
 */
void
aura_dict_advance(struct aura_dict *d)
{
	while (d->cursor == NULL) {
		if (d->cur_bucket == d->num_buckets - 1) {
			/* We're at eof.  Do nothing. */
			break;
		} else {
			d->cur_bucket++;
			d->cursor = d->b[d->cur_bucket];
		}
	}
}

void
aura_dict_rewind(struct aura_dict *d)
{
	d->cur_bucket = 0;
	d->cursor = d->b[d->cur_bucket];
	aura_dict_advance(d);
}

int
aura_dict_eof(struct aura_dict *d)
{
	return(d->cursor == NULL);
}

void
aura_dict_get_current_key(struct aura_dict *d, void **key, size_t *key_size)
{
	if (d->cursor == NULL) {
		*key = NULL;
	} else {
		*key = d->cursor->key;
		*key_size = d->cursor->key_size;
	}
}

void
aura_dict_next(struct aura_dict *d)
{
	if (d->cursor != NULL)
		d->cursor = d->cursor->next;
	aura_dict_advance(d);
}

size_t
aura_dict_size(struct aura_dict *d)
{
	struct aura_bucket *b;
	size_t bucket_no = 0;
	size_t count = 0;

	while (bucket_no < d->num_buckets) {
		b = d->b[bucket_no];
		while (b != NULL) {
			b = b->next;
			count++;
		}
		bucket_no++;
	}

	return(count);
}
