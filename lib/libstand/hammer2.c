/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uuid.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include <hammer2/hammer2_disk.h>

struct hammer2 {
	int 				fd;	/* Device fd */
	struct hammer2_blockref		sroot;	/* Superroot blockref */
};

struct inode {
	struct hammer2_inode_data	dat;	/* raw inode data */
	off_t				doff;	/* disk inode offset */
};

off_t blockoff(ref)
	struct hammer2_blockref ref;
{

}

hinit(hfs)
	struct hammer2 *hfs;
{
	struct hammer2_volume_data volhdr;
	ssize_t rc;
	hammer2_crc_t crc0;

	rc = pread(hfs->fd, &volhdr, HAMMER2_VOLUME_SIZE, 0);
	if (volhdr.magic == HAMMER2_VOLUME_ID_HBO) {
		printf("Valid HAMMER2 filesystem\n");
	} else {
		return (-1);
	}

	hfs->sroot = volhdr.sroot_blockref;
	return (0);
}

shread(hfs, ino, buf, off, len)
	struct hammer2 *hfs;
	struct inode *ino;
	char *buf;
	off_t off;
	size_t len;
{
	/*
	 * Read [off, off+len) from inode ino rather than from disk
	 * offsets; correctly decodes blockrefs/indirs/...
	 */
}

struct inode *hlookup1(hfs, ino, name)
	struct hammer2 *hfs;
	struct inode *ino;
	char *name;
{
	static struct inode filino;
	off_t off;
	int rc;

	bzero(&filino, sizeof(struct inode));

	for (off = 0;
	     off < ino->dat.size;
	     off += sizeof(struct hammer2_inode_data))
	{
		rc = shread(hfs, ino, &filino.dat, off,
			    sizeof(struct hammer2_inode_data));
		if (rc != sizeof(struct hammer2_inode_data))
			continue;
		if (strcmp(name, &filino.dat.filename) == 0)
			return (&filino);
	}

	return (NULL);
}

struct inode *hlookup(hfs, name)
	struct hammer2 *hfs;
	char *name;
{
	/* Name is of form /SUPERROOT/a/b/c/file */

}

void hstat(hfs, ino, sb)
	struct hammer2 *hfs;
	struct inode *ino;
	struct stat *sb;
{

}

main(argc, argv)
	int argc;
	char *argv[];
{
	struct hammer2 hammer2;
	struct inode *ino;
	struct stat sb;
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: hammer2 <dev>\n");
		exit(1);
	}

	hammer2.fd = open(argv[1], O_RDONLY);
	if (hammer2.fd < 0) {
		fprintf(stderr, "unable to open %s\n", argv[1]);
		exit(1);
	}

	if (hinit(&hammer2)) {
		fprintf(stderr, "invalid fs\n");
		close(hammer2.fd);
		exit(1);
	}

	for (i = 2; i < argc; i++) {
		ino = hlookup(&hammer2, argv[i]);
		if (ino == NULL) {
			fprintf(stderr, "hlookup %s\n", argv[i]);
			continue;
		}
		hstat(&hammer2, ino, &sb);

		printf("%s %lld", argv[i], sb.st_size);

	}
}
