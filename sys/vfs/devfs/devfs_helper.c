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
#include <machine/limits.h>
#include <sys/devfs.h>

MALLOC_DECLARE(M_DEVFS);

/*
 * DEVFS clone bitmap functions
 */
void
devfs_clone_bitmap_init(struct devfs_bitmap *bitmap)
{
	bitmap->chunks = DEVFS_BITMAP_INITIAL_SIZE;
	bitmap->bitmap = kmalloc(bitmap->chunks * sizeof(u_long),
				 M_DEVFS, M_WAITOK);
	memset(bitmap->bitmap, -1, bitmap->chunks * sizeof(u_long));
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

	bitmap->chunks = newchunks + 2;
	bitmap->bitmap = krealloc(bitmap->bitmap,
				  sizeof(u_long) * bitmap->chunks,
				  M_DEVFS, M_WAITOK);
	memset(bitmap->bitmap + oldchunks, -1,
	       sizeof(u_long) * (bitmap->chunks - oldchunks));
}


int
devfs_clone_bitmap_fff(struct devfs_bitmap *bitmap)
{
	u_long curbitmap;
	int bit, i;
	int chunks;

	chunks = bitmap->chunks;

	for (i = 0; i < chunks + 1; i++) {
		if (i == chunks)
			devfs_clone_bitmap_resize(bitmap, i);
		curbitmap = bitmap->bitmap[i];

		if (curbitmap) {
			curbitmap &= (~curbitmap)+1;
			for (bit = 1; curbitmap != 1; bit++)
				curbitmap >>= 1;

			return (bit - 1 + (i << 3) * sizeof(curbitmap));
		}
	}

	/*
	 * NOT REACHED
	 */
	panic("devfs_clone_bitmap_fff");
	return -1;
}


int
devfs_clone_bitmap_chk(struct devfs_bitmap *bitmap, int unit)
{
	int chunk;

	chunk = unit / (sizeof(u_long) * 8);
	unit -= chunk * (sizeof(u_long) * 8);

	if (chunk >= bitmap->chunks)
		return 1;

	return !((bitmap->bitmap[chunk]) & (1 << unit));
}


void
devfs_clone_bitmap_set(struct devfs_bitmap *bitmap, int unit)
{
	int chunk;

	chunk = unit / (sizeof(u_long) * 8);
	unit -= chunk * (sizeof(u_long) * 8);

	if (chunk >= bitmap->chunks)
		devfs_clone_bitmap_resize(bitmap, chunk);
	bitmap->bitmap[chunk] &= ~(1<<unit);
}

/*
 * Deallocate a unit number.  We must synchronize any destroy_dev()'s
 * the device called before we clear the bitmap bit to avoid races
 * against a new clone.
 */
void
devfs_clone_bitmap_put(struct devfs_bitmap *bitmap, int unit)
{
	int chunk;

	devfs_config();

	chunk = unit / (sizeof(u_long) * 8);
	unit -= chunk * (sizeof(u_long) * 8);

	if (chunk >= bitmap->chunks)
		return;
	bitmap->bitmap[chunk] |= (1 << unit);
}

/*
 * Allocate a unit number for a device
 */
int
devfs_clone_bitmap_get(struct devfs_bitmap *bitmap, int limit)
{
	int unit;

	unit = devfs_clone_bitmap_fff(bitmap);
	if ((limit > 0) && (unit > limit))
		return -1;
	devfs_clone_bitmap_set(bitmap, unit);

	return unit;
}

