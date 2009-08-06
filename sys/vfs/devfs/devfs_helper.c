/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/param.h>
#include <machine/limits.h>
#include <vfs/devfs/devfs.h>

MALLOC_DECLARE(M_DEVFS);

static struct devfs_unit_hash *devfs_clone_hash_get(int, cdev_t);
#if 0
static void devfs_clone_hash_put(struct devfs_unit_hash *);
#endif
static int devfs_clone_hash_add(struct devfs_unit_hash **, struct devfs_unit_hash *);
static struct devfs_unit_hash *devfs_clone_hash_del(struct devfs_unit_hash **, int);

/*
 * DEVFS clone hash functions
 */

static struct devfs_unit_hash *
devfs_clone_hash_get(int unit_no, cdev_t dev)
{
	struct devfs_unit_hash *hash = (struct devfs_unit_hash *)kmalloc(sizeof(struct devfs_unit_hash), M_DEVFS, M_WAITOK);
	hash->next = NULL;
	hash->unit_no = unit_no;
	hash->dev = dev;

	return hash;
}


#if 0

static void
devfs_clone_hash_put(struct devfs_unit_hash *hash)
{
	kfree(hash, M_DEVFS);
}

#endif

static int
devfs_clone_hash_add(struct devfs_unit_hash **devfs_hash_array, struct devfs_unit_hash *hash)
{
        struct devfs_unit_hash **hashp;
        hashp = &devfs_hash_array[hash->unit_no &
                          DEVFS_UNIT_HMASK];
        while (*hashp) {
                if ((*hashp)->unit_no ==
                          hash->unit_no)
                        return(EEXIST);
                hashp = &(*hashp)->next;
        }
        hash->next = NULL;
        *hashp = hash;
        return (0);
}


static struct devfs_unit_hash *
devfs_clone_hash_del(struct devfs_unit_hash **devfs_hash_array, int unit_no)
{
        struct devfs_unit_hash **hashp;
		struct devfs_unit_hash *hash;
        hashp = &devfs_hash_array[unit_no &
                          DEVFS_UNIT_HMASK];
		hash = *hashp;
        while ((*hashp)->unit_no != unit_no) {
                KKASSERT(*hashp != NULL);
                hashp = &(*hashp)->next;
				hash = *hashp;
        }
        *hashp = hash->next;

		return hash;
}

/*
 * DEVFS clone bitmap functions
 */
void
devfs_clone_bitmap_init(struct devfs_bitmap *bitmap)
{
	bitmap->bitmap = (unsigned long *)kmalloc(DEVFS_BITMAP_INITIAL_SIZE*sizeof(unsigned long), M_DEVFS, M_WAITOK);
	bitmap->chunks = DEVFS_BITMAP_INITIAL_SIZE;
	memset(bitmap->bitmap, ULONG_MAX, DEVFS_BITMAP_INITIAL_SIZE*sizeof(unsigned long));
}


void
devfs_clone_bitmap_uninit(struct devfs_bitmap *bitmap)
{
	kfree(bitmap->bitmap, M_DEVFS);
}


void
devfs_clone_bitmap_resize(struct devfs_bitmap *bitmap, int newchunks)
{
	int oldchunks = bitmap->chunks;
	bitmap->chunks = newchunks+2;
	bitmap->bitmap = (unsigned long *)krealloc(bitmap->bitmap, sizeof(unsigned long)*bitmap->chunks, M_DEVFS, M_WAITOK);

	devfs_debug(DEVFS_DEBUG_DEBUG, "%d vs %d (oldchunks=%d)\n", bitmap->bitmap, bitmap->bitmap + oldchunks, oldchunks);
	memset(bitmap->bitmap + oldchunks, ULONG_MAX, sizeof(unsigned long)*(bitmap->chunks - oldchunks));
}


int
devfs_clone_bitmap_fff(struct devfs_bitmap *bitmap)
{
	unsigned long 	curbitmap;
	int bit, i;
	int chunks = bitmap->chunks;

	for (i = 0; i < chunks+1; i++) {
		if (i == chunks)
			devfs_clone_bitmap_resize(bitmap, i);
		curbitmap = bitmap->bitmap[i];

		if (curbitmap > 0) {
			curbitmap &= (~curbitmap)+1;
			for (bit = 1; curbitmap != 1; bit++)
				curbitmap = (unsigned long)curbitmap >> 1;

			return bit-1 + (i<<3) * sizeof(unsigned long);
		}
	}

	/* Should never happen as we dynamically resize as needed */
	return -1;
}


int
devfs_clone_bitmap_chk(struct devfs_bitmap *bitmap, int unit)
{
	int chunk = unit / (sizeof(unsigned long)<<3);
	unit -= chunk<<3 * sizeof(unsigned long);

	if (chunk >= bitmap->chunks)
		return 1;

	return !((bitmap->bitmap[chunk]) & (1<<(unit)));
}


void
devfs_clone_bitmap_set(struct devfs_bitmap *bitmap, int unit)
{
	int chunk = unit / (sizeof(unsigned long)<<3);
	unit -= chunk<<3 * sizeof(unsigned long);

	if (chunk >= bitmap->chunks) {
		devfs_clone_bitmap_resize(bitmap, chunk);
	}

	bitmap->bitmap[chunk] &= ~(1<<unit);
}


void
devfs_clone_bitmap_rst(struct devfs_bitmap *bitmap, int unit)
{
	int chunk = unit / (sizeof(unsigned long)<<3);
	unit -= chunk<<3 * sizeof(unsigned long);

	if (chunk >= bitmap->chunks)
		return;

	bitmap->bitmap[chunk] |= (1<<unit);
}


int
devfs_clone_bitmap_get(struct devfs_bitmap *bitmap, int limit)
{
	int unit;
	unit = devfs_clone_bitmap_fff(bitmap);
	KKASSERT(unit != -1);

	if ((limit > 0) && (unit > limit))
		return -1;

	devfs_clone_bitmap_set(bitmap, unit);

	return unit;
}

/*
 * DEVFS clone helper functions
 */

void
devfs_clone_helper_init(struct devfs_clone_helper *helper)
{
	devfs_clone_bitmap_init(&helper->DEVFS_CLONE_BITMAP(generic));
	memset(&helper->DEVFS_CLONE_HASHLIST(generic), 0, DEVFS_UNIT_HSIZE*sizeof(void *));
}


void
devfs_clone_helper_uninit(struct devfs_clone_helper *helper)
{
	devfs_clone_bitmap_uninit(&helper->DEVFS_CLONE_BITMAP(generic));
	//XXX: free all elements in helper->DEVFS_HASHLIST(generic)
}


int
devfs_clone_helper_insert(struct devfs_clone_helper *helper, cdev_t dev)
{
	struct devfs_unit_hash *hash;
	int error = 0;
	int unit_no;

try_again:
	unit_no = devfs_clone_bitmap_fff(&helper->DEVFS_CLONE_BITMAP(generic));

	devfs_clone_bitmap_set(&helper->DEVFS_CLONE_BITMAP(generic), unit_no);
	hash = devfs_clone_hash_get(unit_no, dev);

	error = devfs_clone_hash_add(helper->DEVFS_CLONE_HASHLIST(generic), hash);
	KKASSERT(!error);

	if (error)
		goto try_again;

	dev->si_uminor = unit_no;
	return unit_no;
}


int
devfs_clone_helper_remove(struct devfs_clone_helper *helper, int unit_no)
{
	struct devfs_unit_hash *hash;
	hash = devfs_clone_hash_del(helper->DEVFS_CLONE_HASHLIST(generic), unit_no);
	devfs_clone_bitmap_rst(&helper->DEVFS_CLONE_BITMAP(generic), unit_no);
	kfree(hash, M_DEVFS);

	return 0;
}
