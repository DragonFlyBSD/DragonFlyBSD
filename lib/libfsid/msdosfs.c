/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com
 * and Ákos Kovács <akoskovacs@gmx.com>
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

#include "libfsid.h"

#include <vfs/msdosfs/bootsect.h>

#define MSDOS_BOOT_BLOCK_SIZE 512

static char *get_volname(char *buf);
static char buffer[MSDOS_BOOT_BLOCK_SIZE * 4];

fsid_t
msdosfs_probe(const char *dev)
{
	if (fsid_dev_read(dev, 0L, sizeof(buffer), buffer) != 0)
		return FSID_UNKNOWN;

	if (get_volname(buffer) != NULL)
		return FSID_MSDOSFS;

	return FSID_MSDOSFS;
}
char *
msdosfs_volname(const char *dev)
{
	char *volname;

	if (fsid_dev_read(dev, 0L, sizeof(buffer), buffer) != 0)
		return NULL;

	volname = get_volname(buffer);

	if (volname == NULL || volname[0] == '\0')
		return NULL;

	volname[sizeof(volname) - 1] = '\0';

	return volname;
}

/*
 * This function used to get the offset address of
 * the volume name, of the FAT partition.
 * It also checks, that is a real FAT partition.
*/
static char *
get_volname(char *buff)
{
	struct bootsector710 *bpb7;
	struct bootsector50 *bpb4;
	struct extboot *extb;

	bpb7 = (struct bootsector710 *)buff;
	bpb4 = (struct bootsector50 *)buff;

	/*
	 * First, assume BPB v7
	 */
	extb = (struct extboot *)bpb7->bsExt;
	if ((extb->exBootSignature == 0x28 || extb->exBootSignature == 0x29) &&
	    strncmp(extb->exFileSysType, "FAT", 3) == 0)
		return extb->exVolumeLabel;

	/*
	 * If this is not a BPB v7, try it as a bpb v4.
	 */
	extb = (struct extboot *)bpb4->bsExt;
	if ((extb->exBootSignature == EXBOOTSIG ||
	     extb->exBootSignature == EXBOOTSIG2) &&
	    strncmp(extb->exFileSysType, "FAT", 3) == 0)
		return extb->exVolumeLabel;

	/*
	 * If previous checks failed, it may not a FAT filesystem, or
	 * it may an older one without a volume name.
	 */
	return NULL;
}
