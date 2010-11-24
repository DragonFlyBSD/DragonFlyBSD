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
#include <vfs/hammer/hammer_disk.h>

static char buffer[16384];

int
hammer_probe(const char *dev)
{
	static struct hammer_volume_ondisk *fs;
	int ret, fd;

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("open in hammer_probe failed");
		return 0;
	}

	bzero(buffer, sizeof(buffer));
	ret = read(fd, &buffer, sizeof(buffer));
	if (ret < 0) {
		close(fd);
		perror("read in hammer_probe failed");
		return 0;
	}

	close(fd);
	fs = (struct hammer_volume_ondisk *)&buffer;

	if (fs->vol_signature != HAMMER_FSBUF_VOLUME) {
		return 0;
	}

	return 1;
}

char *
hammer_volname(const char *dev)
{
	static struct hammer_volume_ondisk *fs;
	int ret, fd;

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("open in hammer_volname failed");
		return NULL;
	}

	bzero(buffer, sizeof(buffer));
	ret = read(fd, &buffer, sizeof(buffer));
	if (ret < 0) {
		close(fd);
		perror("read in hammer_volname failed");
		return NULL;
	}

	close(fd);
	fs = (struct hammer_volume_ondisk *)&buffer;

	if (fs->vol_signature != HAMMER_FSBUF_VOLUME) {
		return NULL;
	}

	if (fs->vol_name[0] == '\0')
		return NULL;

	fs->vol_name[63] = '\0';
	return fs->vol_name;
}

