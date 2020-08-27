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

// # gcc -Wall -g -I../../sys -I../hammer2 ../../sys/vfs/hammer2/xxhash/xxhash.c
// ../../sys/libkern/icrc32.c ../hammer2/subs.c ./reconstruct.c -o reconstruct

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <openssl/sha.h>

#include <vfs/hammer2/hammer2_disk.h>
#include <vfs/hammer2/hammer2_xxhash.h>

#include "hammer2_subs.h"

static int modify_volume_header(int, hammer2_volume_data_t *,
    const hammer2_blockref_t *);
static int modify_blockref(int, const hammer2_volume_data_t *, int,
    hammer2_blockref_t *, hammer2_blockref_t *, int);
static int modify_check(int, int, hammer2_blockref_t *,
    const hammer2_blockref_t *, hammer2_media_data_t *, size_t, int);

static bool ForceOpt = false;

static int
reconstruct_volume_header(int fd)
{
	bool failed = false;
	int i;

	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		hammer2_volume_data_t voldata;
		hammer2_blockref_t broot;
		ssize_t ret;

		memset(&broot, 0, sizeof(broot));
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
			if (modify_volume_header(fd, &voldata, &broot) == -1)
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
reconstruct_blockref(int fd, uint8_t type)
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
			if (modify_blockref(fd, &voldata, -1, &broot, NULL, -1)
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
modify_volume_header(int fd, hammer2_volume_data_t *voldata,
    const hammer2_blockref_t *bref)
{
	hammer2_crc32_t crc0, crc1;
	const char *s = NULL;
	bool found = false;

	if ((voldata->magic != HAMMER2_VOLUME_ID_HBO) &&
	    (voldata->magic != HAMMER2_VOLUME_ID_ABO)) {
		fprintf(stderr, "Bad magic %jX\n", voldata->magic);
		return -1;
	}

	if (voldata->magic == HAMMER2_VOLUME_ID_ABO)
		fprintf(stderr, "Reverse endian\n");

	/* Need to test HAMMER2_VOL_ICRC_SECT1 first. */
	crc0 = voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT1];
	crc1 = hammer2_icrc32((char*)voldata + HAMMER2_VOLUME_ICRC1_OFF,
	    HAMMER2_VOLUME_ICRC1_SIZE);
	if (crc0 != crc1) {
		if (ForceOpt)
			voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT1] = crc1;
		found = true;
		s = "HAMMER2_VOL_ICRC_SECT1";
		printf("%s%016jx %s\n", ForceOpt ? "Modified " : "",
		    (uintmax_t)bref->data_off, s);
	}

	crc0 = voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT0];
	crc1 = hammer2_icrc32((char*)voldata + HAMMER2_VOLUME_ICRC0_OFF,
	    HAMMER2_VOLUME_ICRC0_SIZE);
	if (crc0 != crc1) {
		if (ForceOpt)
			voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT0] = crc1;
		found = true;
		s = "HAMMER2_VOL_ICRC_SECT0";
		printf("%s%016jx %s\n", ForceOpt ? "Modified " : "",
		    (uintmax_t)bref->data_off, s);
	}

	crc0 = voldata->icrc_volheader;
	crc1 = hammer2_icrc32((char*)voldata + HAMMER2_VOLUME_ICRCVH_OFF,
	    HAMMER2_VOLUME_ICRCVH_SIZE);
	if (crc0 != crc1) {
		if (ForceOpt)
			voldata->icrc_volheader = crc1;
		found = true;
		s = "volume header CRC";
		printf("%s%016jx %s\n", ForceOpt ? "Modified " : "",
		    (uintmax_t)bref->data_off, s);
	}

	if (found && ForceOpt) {
		ssize_t ret;
		if (lseek(fd, bref->data_off & ~HAMMER2_OFF_MASK_RADIX,
		    SEEK_SET) == -1) {
			perror("lseek");
			return -1;
		}
		ret = write(fd, voldata, HAMMER2_PBUFSIZE);
		if (ret == -1) {
			perror("write");
			return -1;
		} else if (ret != (ssize_t)HAMMER2_PBUFSIZE) {
			fprintf(stderr, "Failed to write volume header\n");
			return -1;
		}
		if (fsync(fd) == -1) {
			perror("fsync");
			return -1;
		}
	}

	return 0;
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
    hammer2_blockref_t *bref, hammer2_blockref_t *prev_bref, int depth)
{
	hammer2_media_data_t media;
	hammer2_blockref_t *bscan;
	int i, bcount;
	size_t bytes;

	if (read_media(fd, bref, &media, &bytes) == -1)
		return -1;

	if (!bytes)
		return 0;

	switch (bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		if (!(media.ipdata.meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA)) {
			bscan = &media.ipdata.u.blockset.blockref[0];
			bcount = HAMMER2_SET_COUNT;
		} else {
			bscan = NULL;
			bcount = 0;
		}
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bscan = &media.voldata.sroot_blockset.blockref[0];
		bcount = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		bscan = &media.voldata.freemap_blockset.blockref[0];
		bcount = HAMMER2_SET_COUNT;
		break;
	default:
		bscan = NULL;
		bcount = 0;
		break;
	}

	for (i = 0; i < bcount; ++i)
		if (bscan[i].type != HAMMER2_BREF_TYPE_EMPTY)
			if (modify_blockref(fd, voldata, i, &bscan[i], bref,
			    depth + 1) == -1)
				return -1;

	if (ForceOpt)
		if (read_media(fd, bref, &media, &bytes) == -1)
			return -1;
	if (modify_check(fd, bi, prev_bref, bref, &media, bytes, depth) == -1)
		return -1;

	return 0;
}

static int
modify_check(int fd, int bi, hammer2_blockref_t *prev_bref,
    const hammer2_blockref_t *bref, hammer2_media_data_t *media,
    size_t media_bytes, int depth)
{
	hammer2_media_data_t bscan_media;
	hammer2_blockref_t *bscan;
	bool found = false;
	size_t bytes;
	uint32_t cv;
	uint64_t cv64;

	//SHA256_CTX hash_ctx;
	union {
		uint8_t digest[SHA256_DIGEST_LENGTH];
		uint64_t digest64[SHA256_DIGEST_LENGTH/8];
	} u;


	if (!prev_bref)
		return 0;
	if (read_media(fd, prev_bref, &bscan_media, &bytes) == -1)
		return -1;
	assert(bytes);

	switch (prev_bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		if (!(bscan_media.ipdata.meta.op_flags &
		    HAMMER2_OPFLAG_DIRECTDATA))
			bscan = &bscan_media.ipdata.u.blockset.blockref[bi];
		else
			bscan = NULL;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &bscan_media.npdata[bi];
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bscan = &bscan_media.voldata.sroot_blockset.blockref[bi];
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		bscan = &bscan_media.voldata.freemap_blockset.blockref[bi];
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
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

	switch (HAMMER2_DEC_CHECK(bscan->methods)) {
	case HAMMER2_CHECK_ISCSI32:
		cv = hammer2_icrc32(media, media_bytes);
		if (bscan->check.iscsi32.value != cv) {
			if (ForceOpt)
				bscan->check.iscsi32.value = cv;
			found = true;
		}
		break;
	case HAMMER2_CHECK_XXHASH64:
		cv64 = XXH64(media, media_bytes, XXH_HAMMER2_SEED);
		if (bscan->check.xxhash64.value != cv64) {
			if (ForceOpt)
				bscan->check.xxhash64.value = cv64;
			found = true;
		}
		break;
	case HAMMER2_CHECK_SHA192:
#if 0
		SHA256_Init(&hash_ctx);
		SHA256_Update(&hash_ctx, &media, bytes);
		SHA256_Final(u.digest, &hash_ctx);
#endif
		u.digest64[2] ^= u.digest64[3];
		if (memcmp(u.digest, bscan->check.sha192.data,
		    sizeof(bscan->check.sha192.data))) {
			if (ForceOpt)
				memcpy(&bscan->check.sha192.data, u.digest,
				    sizeof(bscan->check.sha192.data));
			found = true;
		}
		fprintf(stderr, "HAMMER2_CHECK_SHA192 unsupported\n");
		assert(0);
		break;
	case HAMMER2_CHECK_FREEMAP:
		cv = hammer2_icrc32(media, media_bytes);
		if (bscan->check.freemap.icrc32 != cv) {
			if (ForceOpt)
				bscan->check.freemap.icrc32 = cv;
			found = true;
		}
		break;
	}

	if (found) {
		if (ForceOpt) {
			if (write_media(fd, prev_bref, &bscan_media, bytes)
			    == -1)
				return -1;
		}
		/* If !ForceOpt, only first bad blockref is printed. */
		printf("%s%2d %-8s blockref[%-3d] %016jx %02x %s\n",
		    ForceOpt ? "Modified " : "",
		    depth, hammer2_breftype_to_str(prev_bref->type), bi,
		    (uintmax_t)bscan->data_off, bscan->methods,
		    hammer2_breftype_to_str(bscan->type));
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

	if (argc < 1) {
		fprintf(stderr, "%s [-f] special\n", binpath);
		exit(1);
	}
	devpath = argv[0];

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

	printf("freemap\n");
	if (reconstruct_blockref(fd, HAMMER2_BREF_TYPE_FREEMAP) == -1)
		exit(1);
	printf("volume\n");
	if (reconstruct_blockref(fd, HAMMER2_BREF_TYPE_VOLUME) == -1)
		exit(1);

	printf("volume header\n");
	if (reconstruct_volume_header(fd) == -1)
		exit(1);

	close(fd);

	return 0;
}
