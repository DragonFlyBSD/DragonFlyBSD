/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Ákos Kovács <akoskovacs@gmx.com>
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

#include <sys/stat.h>

#include <errno.h>

#include "libfsid.h"

static struct fs_type fs_types[] = {
    { "HAMMER", hammer_probe,	hammer_volname	},
    { "UFS",	ufs_probe,	ufs_volname	},
    { "CD9660",	cd9660_probe,	cd9660_volname	},
    { "EXT2",	ext2_probe,	ext2_volname	},
    { "MSDOSFS",msdosfs_probe,	msdosfs_volname	},
    { NULL,	NULL,		NULL		}
};

const char *
fsid_fsname(fsid_t id)
{
	if (id < 1)
		return 0;

	return fs_types[id-1].fs_name;
}

int
fsid_fs_count(void)
{
	int count;

	for (count = 0; fs_types[count].fs_name != NULL; count++)
		;	/* nothing */

	return count;
}

fsid_t
fsid_probe(const char *dev, const char *fs_type)
{
	int i;

	if (dev == NULL || fs_type == NULL)
		return FSID_UNKNOWN;

	for (i = 0; fs_types[i].fs_name != NULL; i++)  {
		if ((strcmp(fs_type, fs_types[i].fs_name)) == 0)
			return fs_types[i].fs_probe(dev);
	}
	return FSID_UNKNOWN;
}

fsid_t
fsid_probe_all(const char *dev)
{
	int i;
	fsid_t ret;

	if (dev == NULL)
		return FSID_UNKNOWN;

	for (i = 0; fs_types[i].fs_name != NULL; i++) {
		if ((ret = fs_types[i].fs_probe(dev)) != FSID_UNKNOWN)
			return ret;
	}
	return FSID_UNKNOWN;
}

char *
fsid_volname(const char *dev, const char *fs_type)
{
	int i;

	if (dev == NULL || fs_type == NULL)
		return NULL;

	for (i = 0; fs_types[i].fs_name != NULL; i++) {
		if ((strcmp(fs_type, fs_types[i].fs_name)) == 0) {
			return fs_types[i].fs_volname(dev);
		}
	}
	return NULL;
}

char *
fsid_volname_all(const char *dev)
{
	int fs_id;

	if (dev == NULL)
		return NULL;

	if ((fs_id = fsid_probe_all(dev)) != 0)
		return fs_types[fs_id - 1].fs_volname(dev);
	else
		return NULL;
}

int
fsid_dev_read(const char *dev, off_t off, size_t len, char *buf)
{
	int fd;

	if ((fd = open(dev, O_RDONLY)) < 0)
		return -1;

	if ((lseek(fd, off, SEEK_SET)) < 0) {
		close(fd);
		return -1;
	}

	bzero(buf, len);
	if ((read(fd, buf, len)) < 0) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}
