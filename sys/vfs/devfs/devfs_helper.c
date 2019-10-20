/*
 * Copyright (c) 2009-2019 The DragonFly Project.  All rights reserved.
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
#include <sys/malloc.h>
#include <machine/limits.h>
#include <sys/devfs.h>

MALLOC_DECLARE(M_DEVFS);

/*
 * Locallized lock to ensure the integrity of the bitmap/cloning
 * code.  Callers using chk/set sequences must still check for races
 * or have their own locks.
 *
 * We use a recursive lock only to allow *_get() to call *_set().
 */
static struct lock devfs_bitmap_lock =
	LOCK_INITIALIZER("dbmap", 0, LK_CANRECURSE);

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

/*
 * Extend the bitmap past (not just up to) 'newchunks' chunks.  While
 * probably not necessary, we go a little further and give ourselves
 * one extra chunk's worth of bitmap beyond what is required.
 */
static void
devfs_clone_bitmap_extend(struct devfs_bitmap *bitmap, int newchunks)
{
	int oldchunks = bitmap->chunks;

	bitmap->chunks = newchunks + 2;
	bitmap->bitmap = krealloc(bitmap->bitmap,
				  sizeof(u_long) * bitmap->chunks,
				  M_DEVFS, M_WAITOK);
	memset(bitmap->bitmap + oldchunks, -1,
	       sizeof(u_long) * (bitmap->chunks - oldchunks));
}

/*
 * This helper function determines the next available free unit in the
 * bitmap, extending the bitmap if necessary.  It cannot fail.
 */
static int
devfs_clone_bitmap_fff(struct devfs_bitmap *bitmap)
{
	u_long curbitmap;
	int bit, i;
	int chunks;

	chunks = bitmap->chunks;

	for (i = 0; i < chunks + 1; i++) {
		if (i == chunks)
			devfs_clone_bitmap_extend(bitmap, i);
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

/*
 * Caller wants to know if the specified unit has been allocated
 * or not.  Return 0, indicating that it has not been allocated.
 *
 * Caller must hold a lock to prevent chk-to-set races.  Devfs also
 * obtains a temporary lock, juse in case, to ensure structural
 * integrity.
 *
 * (the bitmap implements 0=allocated, 1=not-allocated but the
 * return value is inverted).
 */
int
devfs_clone_bitmap_chk(struct devfs_bitmap *bitmap, int unit)
{
	int chunk;
	int res;

	chunk = unit / (sizeof(u_long) * 8);
	unit -= chunk * (sizeof(u_long) * 8);

	lockmgr(&devfs_bitmap_lock, LK_EXCLUSIVE);
	if (chunk >= bitmap->chunks) {
		lockmgr(&devfs_bitmap_lock, LK_RELEASE);
		return 0;		/* device does not exist */
	}
	res = !((bitmap->bitmap[chunk]) & (1UL << unit));
	lockmgr(&devfs_bitmap_lock, LK_RELEASE);

	return res;
}

/*
 * Unconditionally mark the specified unit as allocated in the bitmap.
 *
 * Caller must hold a lock to prevent chk-to-set races, or otherwise
 * check for a return value < 0 from this routine.  If the unit had
 * never been allocated in the past, a token lock might not be sufficient
 * to avoid races (if this function must extend the bitmap it could block
 * temporary in kmalloc()).
 *
 * devfs acquires a temporary lock to ensure structural integrity.
 *
 * If the bit is already clear (indicating that the unit is already
 * allocated), return -1.  Otherwise return 0.
 */
int
devfs_clone_bitmap_set(struct devfs_bitmap *bitmap, int unit)
{
	int chunk;
	int res;

	chunk = unit / (sizeof(u_long) * 8);
	unit -= chunk * (sizeof(u_long) * 8);

	lockmgr(&devfs_bitmap_lock, LK_EXCLUSIVE);
	if (chunk >= bitmap->chunks)
		devfs_clone_bitmap_extend(bitmap, chunk);
	if (bitmap->bitmap[chunk] & (1UL << unit)) {
		bitmap->bitmap[chunk] &= ~(1UL << unit);
		res = 0;
	} else {
		res = -1;
	}
	lockmgr(&devfs_bitmap_lock, LK_RELEASE);

	return res;
}

/*
 * Unconditionally deallocate a unit number.  Caller must synchronize any
 * destroy_dev()'s the device called before clearing the bitmap bit to
 * avoid races against a new clone.
 */
void
devfs_clone_bitmap_put(struct devfs_bitmap *bitmap, int unit)
{
	int chunk;

	devfs_config();

	chunk = unit / (sizeof(u_long) * 8);
	unit -= chunk * (sizeof(u_long) * 8);

	lockmgr(&devfs_bitmap_lock, LK_EXCLUSIVE);
	if (chunk < bitmap->chunks)
		bitmap->bitmap[chunk] |= (1UL << unit);
	lockmgr(&devfs_bitmap_lock, LK_RELEASE);
}

/*
 * Conditionally allocate a unit from the bitmap.  Returns -1 if no
 * more units are available.
 *
 * Caller must hold a lock to avoid bitmap races.  Since *_fff() below
 * pre-extends the bitmap as necessary, a token lock is sufficient.
 */
int
devfs_clone_bitmap_get(struct devfs_bitmap *bitmap, int limit)
{
	int unit;

	lockmgr(&devfs_bitmap_lock, LK_EXCLUSIVE);
	unit = devfs_clone_bitmap_fff(bitmap);
	if (limit > 0 && unit > limit) {
		unit = -1;
	} else {
		devfs_clone_bitmap_set(bitmap, unit);
	}
	lockmgr(&devfs_bitmap_lock, LK_RELEASE);

	return unit;
}
