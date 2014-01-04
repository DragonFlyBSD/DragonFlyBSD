/*-
 * Copyright (c) 2013 Larisa Grigore<larisagrigore@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "sysvipc_hash.h"

struct hashtable *
_hash_init(int nr_elems) {
	long hashsize;
	struct hashtable *hashtable;
	int i;

	if (nr_elems <= 0)
		return NULL;
	for (hashsize = 2; hashsize < nr_elems; hashsize <<= 1)
		continue;

	hashtable = malloc(sizeof(struct hashtable));
	if (!hashtable) {
		return NULL;
	}

	hashtable->entries = malloc(hashsize * sizeof(struct entries_list));
	if (!hashtable->entries) {
		free(hashtable);
		hashtable = NULL;
		goto out;
	}

	hashtable->nr_elems = hashsize;

	for (i = 0; i < hashsize; i++)
		LIST_INIT(&hashtable->entries[i]);

out:
	return hashtable;
}

int
_hash_destroy(struct hashtable *hashtable) {
	struct entries_list *tmp;
	u_long hashmask = hashtable->nr_elems -1;

	for (tmp = &hashtable->entries[0]; tmp <= &hashtable->entries[hashmask]; tmp++) {
		if (!LIST_EMPTY(tmp))
			return -1;
	}
	free(hashtable->entries);
	free(hashtable);
	hashtable = NULL;
	return 0;
}

void
_hash_insert(struct hashtable *hashtable,
		u_long key,
		void *value) {

	u_long hashmask = hashtable->nr_elems -1;
	struct entries_list *list =
		&hashtable->entries[key & hashmask];
	struct hashentry *new_entry = malloc(sizeof(struct hashentry));
	new_entry->value = value;
	new_entry->key = key;
	LIST_INSERT_HEAD(list, new_entry, entry_link);
}

void *
_hash_lookup(struct hashtable *hashtable, u_long key) {

	u_long hashmask = hashtable->nr_elems -1;
	struct entries_list *list =
		&hashtable->entries[key & hashmask];
	struct hashentry *tmp;

	LIST_FOREACH(tmp, list, entry_link) {
		if (tmp->key == key) {
			return tmp->value;
		}
	}

	return NULL;
}

void *
_hash_remove(struct hashtable *hashtable,
		u_long key) {

	void *value;
	u_long hashmask = hashtable->nr_elems -1;
	struct entries_list *list =
		&hashtable->entries[key & hashmask];
	struct hashentry *tmp;

	LIST_FOREACH(tmp, list, entry_link) {
		if (tmp->key == key)
			goto done;
	}

	return NULL;

done:
	LIST_REMOVE(tmp, entry_link);
	value = tmp->value;
	free(tmp);
	return value;
}

int
get_hash_size(int nr_elems) {
	long hashsize = 0;

	for (hashsize = 2; hashsize < nr_elems; hashsize <<= 1)
		continue;

	return hashsize;
}
