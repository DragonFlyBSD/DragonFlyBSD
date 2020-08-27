/*
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

// # gcc -Wall -g -I../../sys -I../hammer2
// ../../sys/libkern/icrc32.c ../hammer2/subs.c ./destroy.c -o destroy

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>

#include <vfs/hammer2/hammer2_disk.h>

#include "hammer2_subs.h"

static int modify_blockref(int, const hammer2_volume_data_t *, int,
    hammer2_blockref_t *, hammer2_blockref_t *);
static int modify_inode(int, const hammer2_blockref_t *,
    hammer2_media_data_t *, size_t);
static int modify_dirent_embedded(int, int, hammer2_blockref_t *);
static int modify_dirent(int, int, hammer2_blockref_t *,
    const hammer2_blockref_t *, hammer2_media_data_t *, size_t);

static hammer2_tid_t src_inode = 0;
static hammer2_tid_t dst_inode = 0;
static const char *src_dirent = NULL;
static const char *dst_dirent = NULL;
static bool ForceOpt = false;

static int
destroy_blockref(int fd, uint8_t type)
{
	bool failed = false;
	int i;

	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		hammer2_volume_data_t voldata;
		hammer2_blockref_t broot;
		ssize_t ret;

		memset(&broot, 0, sizeof(broot));
		broot.type = type;
		broot.data_off = (i * HAMMER2_ZONE_BYTES64) | HAMMER2_PBUFRADIX;
		if (lseek(fd, broot.data_off & ~HAMMER2_OFF_MASK_RADIX,
		    SEEK_SET) == -1) {
			perror("lseek");
			return -1;
		}

		ret = read(fd, &voldata, HAMMER2_PBUFSIZE);
		if (ret == HAMMER2_PBUFSIZE) {
			fprintf(stdout, "zone.%d %016jx\n",
			    i, (uintmax_t)broot.data_off);
			if (modify_blockref(fd, &voldata, -1, &broot, NULL)
			    == -1)
				failed = true;
		} else if (ret == -1) {
			perror("read");
			return -1;
		} else {
			fprintf(stderr, "Failed to read volume header\n");
			return -1;
		}
	}

	return failed ? -1 : 0;
}

static int
read_media(int fd, const hammer2_blockref_t *bref, hammer2_media_data_t *media,
    size_t *media_bytes)
{
	hammer2_off_t io_off, io_base;
	size_t bytes, io_bytes, boff;
	ssize_t ret;

	bytes = (bref->data_off & HAMMER2_OFF_MASK_RADIX);
	if (bytes)
		bytes = (size_t)1 << bytes;
	if (media_bytes)
		*media_bytes = bytes;

	if (!bytes)
		return 0;

	io_off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
	io_base = io_off & ~(hammer2_off_t)(HAMMER2_LBUFSIZE - 1);
	boff = io_off - io_base;

	io_bytes = HAMMER2_LBUFSIZE;
	while (io_bytes + boff < bytes)
		io_bytes <<= 1;

	if (io_bytes > sizeof(*media)) {
		fprintf(stderr, "Bad I/O bytes\n");
		return -1;
	}
	if (lseek(fd, io_base, SEEK_SET) == -1) {
		perror("lseek");
		return -1;
	}
	ret = read(fd, media, io_bytes);
	if (ret == -1) {
		perror("read");
		return -1;
	} else if (ret != (ssize_t)io_bytes) {
		fprintf(stderr, "Failed to read media\n");
		return -1;
	}
	if (boff)
		memmove(media, (char *)media + boff, bytes);

	return 0;
}

static int
write_media(int fd, const hammer2_blockref_t *bref,
    const hammer2_media_data_t *media, size_t media_bytes)
{
	hammer2_off_t io_off, io_base;
	char buf[HAMMER2_PBUFSIZE];
	size_t bytes, io_bytes, boff;
	ssize_t ret;

	bytes = (bref->data_off & HAMMER2_OFF_MASK_RADIX);
	if (bytes)
		bytes = (size_t)1 << bytes;
	assert(bytes != 0);
	assert(bytes == media_bytes);

	io_off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
	io_base = io_off & ~(hammer2_off_t)(HAMMER2_LBUFSIZE - 1);
	boff = io_off - io_base;

	io_bytes = HAMMER2_LBUFSIZE;
	while (io_bytes + boff < bytes)
		io_bytes <<= 1;

	if (io_bytes > sizeof(buf)) {
		fprintf(stderr, "Bad I/O bytes\n");
		return -1;
	}
	if (lseek(fd, io_base, SEEK_SET) == -1) {
		perror("lseek");
		return -1;
	}
	if (read(fd, buf, io_bytes) != (ssize_t)io_bytes) {
		perror("read");
		return -1;
	}

	memcpy(buf + boff, media, media_bytes);
	if (lseek(fd, io_base, SEEK_SET) == -1) {
		perror("lseek");
		return -1;
	}
	ret = write(fd, buf, io_bytes);
	if (ret == -1) {
		perror("write");
		return -1;
	} else if (ret != (ssize_t)io_bytes) {
		fprintf(stderr, "Failed to write media\n");
		return -1;
	}
	if (fsync(fd) == -1) {
		perror("fsync");
		return -1;
	}

	return 0;
}

static int
modify_blockref(int fd, const hammer2_volume_data_t *voldata, int bi,
    hammer2_blockref_t *bref, hammer2_blockref_t *prev_bref)
{
	hammer2_media_data_t media;
	hammer2_blockref_t *bscan;
	int i, bcount, namlen;
	size_t bytes;

	if (read_media(fd, bref, &media, &bytes) == -1)
		return -1;

	switch (bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		if (!(media.ipdata.meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA)) {
			bscan = &media.ipdata.u.blockset.blockref[0];
			bcount = HAMMER2_SET_COUNT;
		} else {
			bscan = NULL;
			bcount = 0;
		}
		if (src_inode && media.ipdata.meta.inum == src_inode)
			if (modify_inode(fd, bref, &media, bytes) == -1)
				return -1;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_DIRENT:
		bscan = NULL;
		bcount = 0;
		namlen = bref->embed.dirent.namlen;
		if (src_dirent && namlen == strlen(src_dirent)) {
			if (namlen <= sizeof(bref->check.buf) &&
			    !memcmp(bref->check.buf, src_dirent, namlen)) {
				if (modify_dirent_embedded(fd, bi, prev_bref)
				    == -1)
					return -1;
			} else if (!memcmp(media.buf, src_dirent, namlen)) {
				if (modify_dirent(fd, bi, prev_bref, bref,
				    &media, bytes) == -1)
					return -1;
			}
		}
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bscan = &media.voldata.sroot_blockset.blockref[0];
		bcount = HAMMER2_SET_COUNT;
		break;
	default:
		bscan = NULL;
		bcount = 0;
		break;
	}

	for (i = 0; i < bcount; ++i)
		if (bscan[i].type != HAMMER2_BREF_TYPE_EMPTY)
			if (modify_blockref(fd, voldata, i, &bscan[i], bref)
			    == -1)
				return -1;
	return 0;
}

static int
modify_inode(int fd, const hammer2_blockref_t *bref,
    hammer2_media_data_t *media, size_t media_bytes)
{
	assert(src_inode == media->ipdata.meta.inum);

	if (ForceOpt) {
		media->ipdata.meta.inum = dst_inode;
		if (write_media(fd, bref, media, media_bytes) == -1)
			return -1;
	}

	printf("%sinode# 0x%016jx -> 0x%016jx\n", ForceOpt ? "Modified " : "",
	    src_inode, dst_inode);

	return 0;
}

static int
modify_dirent_embedded(int fd, int bi, hammer2_blockref_t *prev_bref)
{
	hammer2_media_data_t bscan_media;
	hammer2_blockref_t *bscan;
	size_t bytes;

	if (read_media(fd, prev_bref, &bscan_media, &bytes) == -1)
		return -1;
	assert(bytes);

	switch (prev_bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		bscan = &bscan_media.ipdata.u.blockset.blockref[bi];
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &bscan_media.npdata[bi];
		break;
	default:
		assert(0);
		break;
	}
	assert(!memcmp(src_dirent, bscan->check.buf, strlen(src_dirent)));

	if (strlen(dst_dirent) > sizeof(bscan->check.buf)) {
		fprintf(stderr, "embedded dirent %s (%d bytes) can't exceed "
		    "%lu bytes\n", dst_dirent, (int)strlen(dst_dirent),
		    sizeof(bscan->check.buf));
		return -1;
	}

	if (ForceOpt) {
		memset(bscan->check.buf, 0, sizeof(bscan->check.buf));
		memcpy(bscan->check.buf, dst_dirent, strlen(dst_dirent));
		bscan->embed.dirent.namlen = strlen(dst_dirent);
		bscan->key = dirhash((const unsigned char*)dst_dirent,
		    strlen(dst_dirent));
		if (write_media(fd, prev_bref, &bscan_media, bytes) == -1)
			return -1;
	}

	printf("%sembedded dirent %s (%d bytes) -> %s (%d bytes)\n",
	    ForceOpt ? "Modified " : "",
	    src_dirent, (int)strlen(src_dirent),
	    dst_dirent, (int)strlen(dst_dirent));

	return 0;
}

static int
modify_dirent(int fd, int bi, hammer2_blockref_t *prev_bref,
    const hammer2_blockref_t *bref, hammer2_media_data_t *media,
    size_t media_bytes)
{
	hammer2_media_data_t bscan_media;
	hammer2_blockref_t *bscan;
	size_t bytes;

	assert(!memcmp(src_dirent, media->buf, strlen(src_dirent)));
	if (read_media(fd, prev_bref, &bscan_media, &bytes) == -1)
		return -1;
	assert(bytes);

	switch (prev_bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		bscan = &bscan_media.ipdata.u.blockset.blockref[bi];
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &bscan_media.npdata[bi];
		break;
	default:
		assert(0);
		break;
	}

	if (memcmp(bref, bscan, sizeof(*bref))) {
		fprintf(stderr, "Blockref contents mismatch\n");
		return -1;
	}
	if (strlen(dst_dirent) > sizeof(media->buf)) {
		fprintf(stderr, "dirent %s (%d bytes) can't exceed %lu bytes\n",
		    dst_dirent, (int)strlen(dst_dirent), sizeof(media->buf));
		return -1;
	}
	if (strlen(dst_dirent) <= sizeof(bscan->check.buf)) {
		fprintf(stderr, "dirent %s (%d bytes) must exceed %lu bytes\n",
		    dst_dirent, (int)strlen(dst_dirent),
		    sizeof(bscan->check.buf));
		return -1;
	}

	if (ForceOpt) {
		memset(media->buf, 0, sizeof(media->buf));
		memcpy(media->buf, dst_dirent, strlen(dst_dirent));
		bscan->embed.dirent.namlen = strlen(dst_dirent);
		bscan->key = dirhash((const unsigned char*)dst_dirent,
		    strlen(dst_dirent));
		if (write_media(fd, bref, media, media_bytes) == -1)
			return -1;
		if (write_media(fd, prev_bref, &bscan_media, bytes) == -1) {
			memset(media->buf, 0, sizeof(media->buf));
			memcpy(media->buf, src_dirent, strlen(src_dirent));
			if (write_media(fd, bref, media, media_bytes) == -1)
				return -1;
			return -1;
		}
	}

	printf("%sdirent %s (%d bytes) -> %s (%d bytes)\n",
	    ForceOpt ? "Modified " : "",
	    src_dirent, (int)strlen(src_dirent),
	    dst_dirent, (int)strlen(dst_dirent));

	return 0;
}

static int
init_args(int argc, char **argv, const char **devpathp)
{
	const char *devpath, *type;

	*devpathp = devpath = argv[0];
	type = argv[1];

	if (!strcmp(type, "inode")) {
		errno = 0;
		src_inode = strtoull(argv[2], NULL, 16);
		if (errno == ERANGE && src_inode == ULLONG_MAX) {
			perror("strtoull");
			return -1;
		}
		if (src_inode == 0) {
			fprintf(stderr, "Invalid src inode# %ju\n",
			    (uintmax_t)src_inode);
			return -1;
		}
		errno = 0;
		dst_inode = strtoull(argv[3], NULL, 16);
		if (errno == ERANGE && dst_inode == ULLONG_MAX) {
			perror("strtoull");
			return -1;
		}
		if (dst_inode == 0) {
			fprintf(stderr, "Invalid dst inode# %ju\n",
			    (uintmax_t)dst_inode);
			return -1;
		}
		if (src_inode == dst_inode) {
			fprintf(stderr, "src equals dst\n");
			return -1;
		}
		printf("%s 0x%016jx 0x%016jx\n", devpath, (uintmax_t)src_inode,
		    (uintmax_t)dst_inode);
	} else if (!strcmp(type, "dirent")) {
		src_dirent = argv[2];
		if (strlen(src_dirent) > HAMMER2_PBUFSIZE) {
			fprintf(stderr, "src dirent too long\n");
			return -1;
		}
		dst_dirent = argv[3];
		if (strlen(dst_dirent) > HAMMER2_PBUFSIZE) {
			fprintf(stderr, "dst dirent too long\n");
			return -1;
		}
		if (!strcmp(src_dirent, dst_dirent)) {
			fprintf(stderr, "src equals dst\n");
			return -1;
		}
		printf("%s %s %s\n", devpath, src_dirent, dst_dirent);
	} else {
		fprintf(stderr, "Invalid blockref type %s\n", type);
		return -1;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	struct stat st;
	int ch, fd;
	const char *binpath = argv[0];
	const char *devpath;

	while ((ch = getopt(argc, argv, "f")) != -1) {
		switch(ch) {
		case 'f':
			ForceOpt = true;
			break;
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 4) {
		fprintf(stderr, "%s [-f] special type src dst\n", binpath);
		exit(1);
	}

	if (init_args(argc, argv, &devpath) == -1)
		exit(1);

	fd = open(devpath, O_RDWR);
	if (fd == -1) {
		perror("open");
		exit(1);
	}

	if (fstat(fd, &st) == -1) {
		perror("fstat");
		exit(1);
	}
	if (!S_ISCHR(st.st_mode) && !S_ISREG(st.st_mode)) {
		fprintf(stderr, "Unsupported file type\n");
		exit(1);
	}

	if (destroy_blockref(fd, HAMMER2_BREF_TYPE_VOLUME) == -1)
		exit(1);

	close(fd);

	return 0;
}
