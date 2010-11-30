/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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
#include "fsid.h"
#include <vfs/ufs/ufs_types.h>
#include <vfs/ufs/fs.h>

static char buffer[MAXBSIZE];

int
ufs_probe(const char *dev)
{
	static struct fs *fs;
	int ret, fd;

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		return 0;
	}

	ret = lseek(fd, SBOFF, SEEK_SET);
	if (ret < 0)
		return 0;

	bzero(buffer, sizeof(buffer));
	ret = read(fd, &buffer, SBSIZE);
	if (ret < 0) {
		close(fd);
		return 0;
	}

	close(fd);
	fs = (struct fs *)&buffer;

	if (fs->fs_magic != FS_MAGIC || fs->fs_bsize > MAXBSIZE ||
		(unsigned)fs->fs_bsize < sizeof(struct fs)) {
		return 0;
	}

	return 1;
}

char *
ufs_volname(const char *dev)
{
	static struct fs *fs;
	int ret, fd;

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}

	ret = lseek(fd, SBOFF, SEEK_SET);
	if (ret < 0)
		return NULL;

	bzero(buffer, sizeof(buffer));
	ret = read(fd, &buffer, SBSIZE);
	if (ret < 0) {
		close(fd);
		return NULL;
	}

	close(fd);
	fs = (struct fs *)&buffer;

	if (fs->fs_magic != FS_MAGIC || fs->fs_bsize > MAXBSIZE ||
		(unsigned)fs->fs_bsize < sizeof(struct fs)) {
		return NULL;
	}

	if (fs->fs_volname[0] == '\0')
		return NULL;

	fs->fs_volname[MAXVOLLEN - 1] = '\0';
	return fs->fs_volname;
}

