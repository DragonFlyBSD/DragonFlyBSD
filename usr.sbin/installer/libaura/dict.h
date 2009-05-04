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
 * $Id: dict.h,v 1.3 2005/02/06 06:57:30 cpressey Exp $
 * Routines to manipulate dictionaries.
 */

#ifndef	__AURA_DICT_H_
#define	__AURA_DICT_H_

#include <stdlib.h>

#define AURA_DICT_HASH		1
#define AURA_DICT_LIST		2
#define AURA_DICT_SORTED_LIST	3

struct aura_dict {
	struct aura_bucket **b;
	size_t num_buckets;
	void (*fetch)(struct aura_dict *, const void *, size_t, void **, size_t *);
	void (*store)(struct aura_dict *, const void *, size_t, const void *, size_t);
	struct aura_bucket *cursor;
	size_t cur_bucket;
};

struct aura_bucket {
	struct aura_bucket *next;
	void *key;
	size_t key_size;
	void *data;
	size_t data_size;
};

struct aura_dict	*aura_dict_new(size_t, int);
void			 aura_dict_free(struct aura_dict *);

void			 aura_dict_fetch(struct aura_dict *, const void *, size_t,
					 void **, size_t *);
int			 aura_dict_exists(struct aura_dict *, const void *, size_t);
void			 aura_dict_store(struct aura_dict *, const void *, size_t,
					 const void *, size_t);

void			 aura_dict_rewind(struct aura_dict *);
int			 aura_dict_eof(struct aura_dict *);
void			 aura_dict_get_current_key(struct aura_dict *,
						   void **, size_t *);
void			 aura_dict_next(struct aura_dict *);
size_t			 aura_dict_size(struct aura_dict *);

#endif /* !__AURA_DICT_H_ */
