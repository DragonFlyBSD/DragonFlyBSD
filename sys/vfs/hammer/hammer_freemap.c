/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_freemap.c,v 1.1 2008/02/10 09:51:01 dillon Exp $
 */

/*
 * HAMMER freemap - bigblock allocator
 */
#include "hammer.h"

hammer_off_t
hammer_freemap_alloc(hammer_mount_t hmp, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_volume_ondisk_t ondisk;
	hammer_off_t raw_offset;

	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	ondisk = root_volume->ondisk;

	hammer_modify_volume(root_volume, &ondisk->vol0_free_off,
			     sizeof(ondisk->vol0_free_off));
	raw_offset = ondisk->vol0_free_off;
	ondisk->vol0_free_off += HAMMER_LARGEBLOCK_SIZE;
	KKASSERT(ondisk->vol0_free_off <= root_volume->maxbuf_off);
	hammer_rel_volume(root_volume, 0);
	return(raw_offset);
}

