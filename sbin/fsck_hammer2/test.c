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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/tree.h>
#include <sys/queue.h>
#include <sys/ttycom.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <openssl/sha.h>

#include <vfs/hammer2/hammer2_disk.h>
#include <vfs/hammer2/hammer2_xxhash.h>

#include "fsck_hammer2.h"

struct blockref_msg {
	TAILQ_ENTRY(blockref_msg) entry;
	char *msg;
};

struct blockref_entry {
	RB_ENTRY(blockref_entry) entry;
	hammer2_off_t data_off;
	TAILQ_HEAD(, blockref_msg) head;
};

static int
blockref_cmp(struct blockref_entry *b1, struct blockref_entry *b2)
{
	if (b1->data_off < b2->data_off)
		return -1;
	if (b1->data_off > b2->data_off)
		return 1;
	return 0;
}

RB_HEAD(blockref_tree, blockref_entry);
RB_PROTOTYPE2(blockref_tree, blockref_entry, entry, blockref_cmp,
    hammer2_off_t);
RB_GENERATE2(blockref_tree, blockref_entry, entry, blockref_cmp, hammer2_off_t,
    data_off);

typedef struct {
	struct blockref_tree root;
	uint8_t type; /* HAMMER2_BREF_TYPE_VOLUME or FREEMAP */
	uint64_t total_blockref;
	uint64_t total_empty;
	uint64_t total_invalid;
	uint64_t total_bytes;
	union {
		/* use volume or freemap depending on type value */
		struct {
			uint64_t total_inode;
			uint64_t total_indirect;
			uint64_t total_data;
			uint64_t total_dirent;
		} volume;
		struct {
			uint64_t total_freemap_node;
			uint64_t total_freemap_leaf;
		} freemap;
	};
} blockref_stats_t;

static void init_blockref_stats(blockref_stats_t *, uint8_t);
static void cleanup_blockref_stats(blockref_stats_t *);
static void print_blockref_stats(const blockref_stats_t *, bool);
static int verify_volume_header(const hammer2_volume_data_t *);
static int verify_blockref(int, const hammer2_volume_data_t *,
    const hammer2_blockref_t *, bool, blockref_stats_t *);

static int best_zone = -1;

#define TAB 8

static void
tfprintf(FILE *fp, int tab, const char *ctl, ...)
{
	va_list va;

	tab *= TAB;
	fprintf(fp, "%*s", tab, "");
	//fflush(fp);

	va_start(va, ctl);
	vfprintf(fp, ctl, va);
	va_end(va);
}

static int
find_best_zone(int fd)
{
	hammer2_blockref_t best;
	int i, best_i = -1;

	memset(&best, 0, sizeof(best));

	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		hammer2_volume_data_t voldata;
		hammer2_blockref_t broot;
		size_t ret;

		memset(&broot, 0, sizeof(broot));
		broot.data_off = (i * HAMMER2_ZONE_BYTES64) | HAMMER2_PBUFRADIX;
		lseek(fd, broot.data_off & ~HAMMER2_OFF_MASK_RADIX, SEEK_SET);

		ret = read(fd, &voldata, HAMMER2_PBUFSIZE);
		if (ret == HAMMER2_PBUFSIZE) {
			if ((voldata.magic != HAMMER2_VOLUME_ID_HBO) &&
			    (voldata.magic != HAMMER2_VOLUME_ID_ABO))
				continue;
			broot.mirror_tid = voldata.mirror_tid;
			if (best_i < 0 || best.mirror_tid < broot.mirror_tid) {
				best_i = i;
				best = broot;
			}
		} else if (ret > 0) {
			tfprintf(stderr, 1, "Failed to read volume header\n");
			return -1;
		} else {
			perror("read");
			return -1;
		}
	}

	return best_i;
}

static int
test_volume_header(int fd)
{
	bool failed = false;
	int i;

	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		hammer2_volume_data_t voldata;
		hammer2_blockref_t broot;
		size_t ret;

		memset(&broot, 0, sizeof(broot));
		broot.data_off = (i * HAMMER2_ZONE_BYTES64) | HAMMER2_PBUFRADIX;
		lseek(fd, broot.data_off & ~HAMMER2_OFF_MASK_RADIX, SEEK_SET);

		ret = read(fd, &voldata, HAMMER2_PBUFSIZE);
		if (ret == HAMMER2_PBUFSIZE) {
			broot.mirror_tid = voldata.mirror_tid;

			printf("zone.%d %016lX%s\n", i, broot.data_off,
			    (i == best_zone) ? " (best)" : "");
			if (verify_volume_header(&voldata) == -1)
				failed = true;
		} else if (ret > 0) {
			tfprintf(stderr, 1, "Failed to read volume header\n");
			return -1;
		} else {
			perror("read");
			return -1;
		}
	}

	return failed ? -1 : 0;
}

static int
test_blockref(int fd, uint8_t type)
{
	bool failed = false;
	int i;

	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		hammer2_volume_data_t voldata;
		hammer2_blockref_t broot;
		size_t ret;

		memset(&broot, 0, sizeof(broot));
		broot.type = type;
		broot.data_off = (i * HAMMER2_ZONE_BYTES64) | HAMMER2_PBUFRADIX;
		lseek(fd, broot.data_off & ~HAMMER2_OFF_MASK_RADIX, SEEK_SET);

		ret = read(fd, &voldata, HAMMER2_PBUFSIZE);
		if (ret == HAMMER2_PBUFSIZE) {
			blockref_stats_t bstats;
			struct blockref_entry *e;
			broot.mirror_tid = voldata.mirror_tid;
			init_blockref_stats(&bstats, type);

			printf("zone.%d %016lX%s\n", i, broot.data_off,
			    (i == best_zone) ? " (best)" : "");
			if (verify_blockref(fd, &voldata, &broot, false,
			    &bstats) == -1)
				failed = true;
			print_blockref_stats(&bstats, true);

			RB_FOREACH(e, blockref_tree, &bstats.root) {
				struct blockref_msg *m;
				TAILQ_FOREACH(m, &e->head, entry) {
					tfprintf(stderr, 1, "%016lX %s\n",
					    e->data_off, m->msg);
				}
			}
			cleanup_blockref_stats(&bstats);
		} else if (ret > 0) {
			tfprintf(stderr, 1, "Failed to read volume header\n");
			return -1;
		} else {
			perror("read");
			return -1;
		}
	}

	return failed ? -1 : 0;
}

static int
charsperline(void)
{
	int columns;
	char *cp;
	struct winsize ws;

	columns = 0;
	if (ioctl(0, TIOCGWINSZ, &ws) != -1)
		columns = ws.ws_col;
	if (columns == 0 && (cp = getenv("COLUMNS")))
		columns = atoi(cp);
	if (columns == 0)
		columns = 80;	/* last resort */

	return columns;
}

static void
add_blockref_entry(blockref_stats_t *bstats, hammer2_off_t data_off,
    const char *msg)
{
	struct blockref_entry *e;
	struct blockref_msg *m;

	e = RB_LOOKUP(blockref_tree, &bstats->root, data_off);
	if (!e) {
		e = calloc(1, sizeof(*e));
		assert(e);
		TAILQ_INIT(&e->head);
	}

	m = calloc(1, sizeof(*m));
	assert(m);
	m->msg = strdup(msg);

	e->data_off = data_off;
	TAILQ_INSERT_TAIL(&e->head, m, entry);
	RB_INSERT(blockref_tree, &bstats->root, e);
}

static void
init_blockref_stats(blockref_stats_t *bstats, uint8_t type)
{
	memset(bstats, 0, sizeof(*bstats));
	bstats->type = type;
	RB_INIT(&bstats->root);
}

static void
cleanup_blockref_stats(blockref_stats_t *bstats)
{
	struct blockref_entry *e;

	while ((e = RB_ROOT(&bstats->root)) != NULL) {
		struct blockref_msg *m;
		RB_REMOVE(blockref_tree, &bstats->root, e);
		while ((m = TAILQ_FIRST(&e->head)) != NULL) {
			TAILQ_REMOVE(&e->head, m, entry);
			free(m->msg);
			free(m);
		}
		assert(TAILQ_EMPTY(&e->head));
		free(e);
	}
	assert(RB_EMPTY(&bstats->root));
}

static void
print_blockref_stats(const blockref_stats_t *bstats, bool newline)
{
	size_t siz = charsperline();
	char *buf = calloc(1, siz);

	assert(buf);

	switch (bstats->type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		snprintf(buf, siz, "%*s%ju blockref (%ju inode, %ju indirect, "
		    "%ju data, %ju dirent, %ju empty), %s",
		    TAB, "",
		    (uintmax_t)bstats->total_blockref,
		    (uintmax_t)bstats->volume.total_inode,
		    (uintmax_t)bstats->volume.total_indirect,
		    (uintmax_t)bstats->volume.total_data,
		    (uintmax_t)bstats->volume.total_dirent,
		    (uintmax_t)bstats->total_empty,
		    sizetostr(bstats->total_bytes));
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		snprintf(buf, siz, "%*s%ju blockref (%ju node, %ju leaf, "
		    "%ju empty), %s",
		    TAB, "",
		    (uintmax_t)bstats->total_blockref,
		    (uintmax_t)bstats->freemap.total_freemap_node,
		    (uintmax_t)bstats->freemap.total_freemap_leaf,
		    (uintmax_t)bstats->total_empty,
		    sizetostr(bstats->total_bytes));
		break;
	default:
		assert(0);
		break;
	}

	if (newline) {
		printf("%s\n", buf);
	} else {
		printf("%s\r", buf);
		fflush(stdout);
	}
	free(buf);
}

static int
verify_volume_header(const hammer2_volume_data_t *voldata)
{
	hammer2_crc32_t crc0, crc, bcrc0, bcrc;
	const char *p = (const char*)voldata;

	if ((voldata->magic != HAMMER2_VOLUME_ID_HBO) &&
	    (voldata->magic != HAMMER2_VOLUME_ID_ABO)) {
		tfprintf(stderr, 1, "Bad magic %jX\n", voldata->magic);
		return -1;
	}

	if (voldata->magic == HAMMER2_VOLUME_ID_ABO)
		tfprintf(stderr, 1, "Reverse endian\n");

	crc = voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT0];
	crc0 = hammer2_icrc32(p + HAMMER2_VOLUME_ICRC0_OFF,
	    HAMMER2_VOLUME_ICRC0_SIZE);
	if (crc0 != crc) {
		tfprintf(stderr, 1, "Bad HAMMER2_VOL_ICRC_SECT0 CRC\n");
		return -1;
	}

	bcrc = voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT1];
	bcrc0 = hammer2_icrc32(p + HAMMER2_VOLUME_ICRC1_OFF,
	    HAMMER2_VOLUME_ICRC1_SIZE);
	if (bcrc0 != bcrc) {
		tfprintf(stderr, 1, "Bad HAMMER2_VOL_ICRC_SECT1 CRC\n");
		return -1;
	}

	return 0;
}

static int
verify_blockref(int fd, const hammer2_volume_data_t *voldata,
    const hammer2_blockref_t *bref, bool norecurse, blockref_stats_t *bstats)
{
	hammer2_media_data_t media;
	hammer2_blockref_t *bscan;
	int i, bcount;
	bool failed = false;
	size_t bytes;
	uint32_t cv;
	uint64_t cv64;
	char msg[256];

	SHA256_CTX hash_ctx;
	union {
		uint8_t digest[SHA256_DIGEST_LENGTH];
		uint64_t digest64[SHA256_DIGEST_LENGTH/8];
	} u;

	bstats->total_blockref++;

	switch (bref->type) {
	case HAMMER2_BREF_TYPE_EMPTY:
		bstats->total_empty++;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		bstats->volume.total_inode++;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bstats->volume.total_indirect++;
		break;
	case HAMMER2_BREF_TYPE_DATA:
		bstats->volume.total_data++;
		break;
	case HAMMER2_BREF_TYPE_DIRENT:
		bstats->volume.total_dirent++;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		bstats->freemap.total_freemap_node++;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		bstats->freemap.total_freemap_leaf++;
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bstats->total_blockref--;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		bstats->total_blockref--;
		break;
	default:
		bstats->total_invalid++;
		snprintf(msg, sizeof(msg),
		    "Invalid blockref type %d", bref->type);
		add_blockref_entry(bstats, bref->data_off, msg);
		failed = true;
		break;
	}

	bytes = (bref->data_off & HAMMER2_OFF_MASK_RADIX);
	if (bytes)
		bytes = (size_t)1 << bytes;
	if (bref->type != HAMMER2_BREF_TYPE_VOLUME &&
	    bref->type != HAMMER2_BREF_TYPE_FREEMAP)
		bstats->total_bytes += bytes;

	if ((bstats->total_blockref % 100) == 0)
		print_blockref_stats(bstats, false);

	if (bytes) {
		hammer2_off_t io_off;
		hammer2_off_t io_base;
		size_t io_bytes;
		size_t boff;

		io_off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
		io_base = io_off & ~(hammer2_off_t)(HAMMER2_MINIOSIZE - 1);
		boff = io_off - io_base;

		io_bytes = HAMMER2_MINIOSIZE;
		while (io_bytes + boff < bytes)
			io_bytes <<= 1;

		if (io_bytes > sizeof(media)) {
			snprintf(msg, sizeof(msg),
			    "Bad I/O bytes %ju", io_bytes);
			add_blockref_entry(bstats, bref->data_off, msg);
			return -1;
		}
		lseek(fd, io_base, SEEK_SET);
		if (read(fd, &media, io_bytes) != (ssize_t)io_bytes) {
			add_blockref_entry(bstats, bref->data_off,
			    "Failed to read media");
			return -1;
		}
		if (boff)
			memcpy(&media, (char *)&media + boff, bytes);

		switch (HAMMER2_DEC_CHECK(bref->methods)) {
		case HAMMER2_CHECK_ISCSI32:
			cv = hammer2_icrc32(&media, bytes);
			if (bref->check.iscsi32.value != cv) {
				add_blockref_entry(bstats, bref->data_off,
				    "Bad HAMMER2_CHECK_ISCSI32");
				failed = true;
			}
			break;
		case HAMMER2_CHECK_XXHASH64:
			cv64 = XXH64(&media, bytes, XXH_HAMMER2_SEED);
			if (bref->check.xxhash64.value != cv64) {
				add_blockref_entry(bstats, bref->data_off,
				    "Bad HAMMER2_CHECK_XXHASH64");
				failed = true;
			}
			break;
		case HAMMER2_CHECK_SHA192:
			SHA256_Init(&hash_ctx);
			SHA256_Update(&hash_ctx, &media, bytes);
			SHA256_Final(u.digest, &hash_ctx);
			u.digest64[2] ^= u.digest64[3];
			if (memcmp(u.digest, bref->check.sha192.data,
			    sizeof(bref->check.sha192.data))) {
				add_blockref_entry(bstats, bref->data_off,
				    "Bad HAMMER2_CHECK_SHA192");
				failed = true;
			}
			break;
		case HAMMER2_CHECK_FREEMAP:
			cv = hammer2_icrc32(&media, bytes);
			if (bref->check.freemap.icrc32 != cv) {
				add_blockref_entry(bstats, bref->data_off,
				    "Bad HAMMER2_CHECK_FREEMAP");
				failed = true;
			}
			break;
		}
	}

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

	if (ForceOpt)
		norecurse = 0;
	/*
	 * If failed, no recurse, but still verify its direct children.
	 * Beyond that is probably garbage.
	 */
	for (i = 0; norecurse == 0 && i < bcount; ++i)
		if (bscan[i].type != HAMMER2_BREF_TYPE_EMPTY)
			if (verify_blockref(fd, voldata, &bscan[i], failed,
			    bstats) == -1)
				return -1;
	return failed ? -1 : 0;
}

int
test_hammer2(const char *devpath)
{
	struct stat st;
	int fd;

	fd = open(devpath, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -1;
	}

	if (fstat(fd, &st) == -1) {
		perror("fstat");
		close(fd);
		return -1;
	}
	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is not a block device\n", devpath);
		close(fd);
		return -1;
	}

	best_zone = find_best_zone(fd);
	if (best_zone == -1)
		fprintf(stderr, "Failed to find best zone\n");

	printf("volume header\n");
	if (test_volume_header(fd) == -1) {
		if (!ForceOpt) {
			close(fd);
			return -1;
		}
	}

	printf("freemap\n");
	if (test_blockref(fd, HAMMER2_BREF_TYPE_FREEMAP) == -1) {
		if (!ForceOpt) {
			close(fd);
			return -1;
		}
	}
	printf("volume\n");
	if (test_blockref(fd, HAMMER2_BREF_TYPE_VOLUME) == -1) {
		if (!ForceOpt) {
			close(fd);
			return -1;
		}
	}
	close(fd);

	return 0;
}
