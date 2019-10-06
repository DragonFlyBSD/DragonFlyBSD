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

#include "hammer2_subs.h"
#include "fsck_hammer2.h"

struct blockref_msg {
	TAILQ_ENTRY(blockref_msg) entry;
	hammer2_blockref_t bref;
	char *msg;
};

TAILQ_HEAD(blockref_list, blockref_msg);

struct blockref_entry {
	RB_ENTRY(blockref_entry) entry;
	hammer2_off_t data_off;
	struct blockref_list head;
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

static void print_blockref_entry(struct blockref_tree *);
static void init_blockref_stats(blockref_stats_t *, uint8_t);
static void cleanup_blockref_stats(blockref_stats_t *);
static void print_blockref_stats(const blockref_stats_t *, bool);
static int verify_volume_header(const hammer2_volume_data_t *);
static int verify_blockref(int, const hammer2_volume_data_t *,
    const hammer2_blockref_t *, bool, blockref_stats_t *);
static int init_pfs_blockref(int, const hammer2_volume_data_t *,
    const hammer2_blockref_t *, struct blockref_list *);
static void cleanup_pfs_blockref(struct blockref_list *);

static int best_zone = -1;

#define TAB 8

static void
tfprintf(FILE *fp, int tab, const char *ctl, ...)
{
	va_list va;
	int ret;

	ret = fprintf(fp, "%*s", tab * TAB, "");
	if (ret < 0)
		return;

	va_start(va, ctl);
	vfprintf(fp, ctl, va);
	va_end(va);
}

static void
tsnprintf(char *str, size_t siz, int tab, const char *ctl, ...)
{
	va_list va;
	int ret;

	ret = snprintf(str, siz, "%*s", tab * TAB, "");
	if (ret < 0 || ret >= (int)siz)
		return;

	va_start(va, ctl);
	vsnprintf(str + ret, siz, ctl, va);
	va_end(va);
}

static void
tprintf_zone(int tab, int i, const hammer2_blockref_t *bref)
{
	tfprintf(stdout, tab, "zone.%d %016jx%s\n",
	    i, (uintmax_t)bref->data_off,
	    (!ScanBest && i == best_zone) ? " (best)" : "");
}

static void
init_root_blockref(int fd, int i, uint8_t type, hammer2_blockref_t *bref)
{
	assert(type == HAMMER2_BREF_TYPE_EMPTY ||
		type == HAMMER2_BREF_TYPE_VOLUME ||
		type == HAMMER2_BREF_TYPE_FREEMAP);
	memset(bref, 0, sizeof(*bref));
	bref->type = type;
	bref->data_off = (i * HAMMER2_ZONE_BYTES64) | HAMMER2_PBUFRADIX;

	lseek(fd, bref->data_off & ~HAMMER2_OFF_MASK_RADIX, SEEK_SET);
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
		ssize_t ret;

		init_root_blockref(fd, i, HAMMER2_BREF_TYPE_EMPTY, &broot);
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
		} else if (ret == -1) {
			perror("read");
			return -1;
		} else {
			tfprintf(stderr, 1, "Failed to read volume header\n");
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
		ssize_t ret;

		if (ScanBest && i != best_zone)
			continue;
		init_root_blockref(fd, i, HAMMER2_BREF_TYPE_EMPTY, &broot);
		ret = read(fd, &voldata, HAMMER2_PBUFSIZE);
		if (ret == HAMMER2_PBUFSIZE) {
			tprintf_zone(0, i, &broot);
			if (verify_volume_header(&voldata) == -1)
				failed = true;
		} else if (ret == -1) {
			perror("read");
			return -1;
		} else {
			tfprintf(stderr, 1, "Failed to read volume header\n");
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
		ssize_t ret;

		if (ScanBest && i != best_zone)
			continue;
		init_root_blockref(fd, i, type, &broot);
		ret = read(fd, &voldata, HAMMER2_PBUFSIZE);
		if (ret == HAMMER2_PBUFSIZE) {
			blockref_stats_t bstats;
			init_blockref_stats(&bstats, type);
			tprintf_zone(0, i, &broot);
			if (verify_blockref(fd, &voldata, &broot, false,
			    &bstats) == -1)
				failed = true;
			print_blockref_stats(&bstats, true);
			print_blockref_entry(&bstats.root);
			cleanup_blockref_stats(&bstats);
		} else if (ret == -1) {
			perror("read");
			return -1;
		} else {
			tfprintf(stderr, 1, "Failed to read volume header\n");
			return -1;
		}
	}

	return failed ? -1 : 0;
}

static int
test_pfs_blockref(int fd, const char *name)
{
	uint8_t type = HAMMER2_BREF_TYPE_VOLUME;
	bool failed = false;
	int i;

	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		hammer2_volume_data_t voldata;
		hammer2_blockref_t broot;
		ssize_t ret;

		if (ScanBest && i != best_zone)
			continue;
		init_root_blockref(fd, i, type, &broot);
		ret = read(fd, &voldata, HAMMER2_PBUFSIZE);
		if (ret == HAMMER2_PBUFSIZE) {
			struct blockref_list blist;
			struct blockref_msg *p;
			int count = 0;

			tprintf_zone(0, i, &broot);
			TAILQ_INIT(&blist);
			if (init_pfs_blockref(fd, &voldata, &broot, &blist) ==
			    -1) {
				tfprintf(stderr, 1, "Failed to read PFS "
				    "blockref\n");
				failed = true;
				continue;
			}
			if (TAILQ_EMPTY(&blist)) {
				tfprintf(stderr, 1, "Failed to find PFS "
				    "blockref\n");
				failed = true;
				continue;
			}
			TAILQ_FOREACH(p, &blist, entry) {
				blockref_stats_t bstats;
				if (name && strcmp(name, p->msg))
					continue;
				count++;
				tfprintf(stdout, 1, "%s\n", p->msg);
				init_blockref_stats(&bstats, type);
				if (verify_blockref(fd, &voldata, &p->bref,
				    false, &bstats) == -1)
					failed = true;
				print_blockref_stats(&bstats, true);
				print_blockref_entry(&bstats.root);
				cleanup_blockref_stats(&bstats);
			}
			cleanup_pfs_blockref(&blist);
			if (name && !count) {
				tfprintf(stderr, 1, "PFS \"%s\" not found\n",
				    name);
				failed = true;
			}
		} else if (ret == -1) {
			perror("read");
			return -1;
		} else {
			tfprintf(stderr, 1, "Failed to read volume header\n");
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
cleanup_blockref_msg(struct blockref_list *head)
{
	struct blockref_msg *p;

	while ((p = TAILQ_FIRST(head)) != NULL) {
		TAILQ_REMOVE(head, p, entry);
		free(p->msg);
		free(p);
	}
	assert(TAILQ_EMPTY(head));
}

static void
cleanup_blockref_entry(struct blockref_tree *root)
{
	struct blockref_entry *e;

	while ((e = RB_ROOT(root)) != NULL) {
		RB_REMOVE(blockref_tree, root, e);
		cleanup_blockref_msg(&e->head);
		free(e);
	}
	assert(RB_EMPTY(root));
}

static void
add_blockref_msg(struct blockref_list *head, const hammer2_blockref_t *bref,
    const char *msg)
{
	struct blockref_msg *m;

	m = calloc(1, sizeof(*m));
	assert(m);
	m->bref = *bref;
	m->msg = strdup(msg);

	TAILQ_INSERT_TAIL(head, m, entry);
}

static void
add_blockref_entry(struct blockref_tree *root, const hammer2_blockref_t *bref,
    const char *msg)
{
	struct blockref_entry *e;

	e = RB_LOOKUP(blockref_tree, root, bref->data_off);
	if (!e) {
		e = calloc(1, sizeof(*e));
		assert(e);
		TAILQ_INIT(&e->head);
		e->data_off = bref->data_off;
	}

	add_blockref_msg(&e->head, bref, msg);

	RB_INSERT(blockref_tree, root, e);
}

static void
print_blockref_msg(struct blockref_list *head)
{
	struct blockref_msg *m;

	TAILQ_FOREACH(m, head, entry) {
		tfprintf(stderr, 1, "%016jx %3d %016jx/%-2d \"%s\"\n",
		    (uintmax_t)m->bref.data_off,
		    m->bref.type,
		    (uintmax_t)m->bref.key,
		    m->bref.keybits,
		    m->msg);
	}
}

static void
print_blockref_entry(struct blockref_tree *root)
{
	struct blockref_entry *e;

	RB_FOREACH(e, blockref_tree, root)
		print_blockref_msg(&e->head);
}

static void
init_blockref_stats(blockref_stats_t *bstats, uint8_t type)
{
	memset(bstats, 0, sizeof(*bstats));
	RB_INIT(&bstats->root);
	bstats->type = type;
}

static void
cleanup_blockref_stats(blockref_stats_t *bstats)
{
	cleanup_blockref_entry(&bstats->root);
}

static void
print_blockref_stats(const blockref_stats_t *bstats, bool newline)
{
	size_t siz = charsperline();
	char *buf = calloc(1, siz);

	assert(buf);

	switch (bstats->type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		tsnprintf(buf, siz, 1, "%ju blockref (%ju inode, %ju indirect, "
		    "%ju data, %ju dirent, %ju empty), %s",
		    (uintmax_t)bstats->total_blockref,
		    (uintmax_t)bstats->volume.total_inode,
		    (uintmax_t)bstats->volume.total_indirect,
		    (uintmax_t)bstats->volume.total_data,
		    (uintmax_t)bstats->volume.total_dirent,
		    (uintmax_t)bstats->total_empty,
		    sizetostr(bstats->total_bytes));
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		tsnprintf(buf, siz, 1, "%ju blockref (%ju node, %ju leaf, "
		    "%ju empty), %s",
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
	hammer2_crc32_t crc0, crc1;
	const char *p = (const char*)voldata;

	if ((voldata->magic != HAMMER2_VOLUME_ID_HBO) &&
	    (voldata->magic != HAMMER2_VOLUME_ID_ABO)) {
		tfprintf(stderr, 1, "Bad magic %jX\n", voldata->magic);
		return -1;
	}

	if (voldata->magic == HAMMER2_VOLUME_ID_ABO)
		tfprintf(stderr, 1, "Reverse endian\n");

	crc0 = voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT0];
	crc1 = hammer2_icrc32(p + HAMMER2_VOLUME_ICRC0_OFF,
	    HAMMER2_VOLUME_ICRC0_SIZE);
	if (crc0 != crc1) {
		tfprintf(stderr, 1, "Bad HAMMER2_VOL_ICRC_SECT0 CRC\n");
		return -1;
	}

	crc0 = voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT1];
	crc1 = hammer2_icrc32(p + HAMMER2_VOLUME_ICRC1_OFF,
	    HAMMER2_VOLUME_ICRC1_SIZE);
	if (crc0 != crc1) {
		tfprintf(stderr, 1, "Bad HAMMER2_VOL_ICRC_SECT1 CRC\n");
		return -1;
	}

	crc0 = voldata->icrc_volheader;
	crc1 = hammer2_icrc32(p + HAMMER2_VOLUME_ICRCVH_OFF,
	    HAMMER2_VOLUME_ICRCVH_SIZE);
	if (crc0 != crc1) {
		tfprintf(stderr, 1, "Bad volume header CRC\n");
		return -1;
	}

	return 0;
}

static int
read_media(int fd, const hammer2_blockref_t *bref, hammer2_media_data_t *media,
    size_t *media_bytes)
{
	hammer2_off_t io_off, io_base;
	size_t bytes, io_bytes, boff;

	bytes = (bref->data_off & HAMMER2_OFF_MASK_RADIX);
	if (bytes)
		bytes = (size_t)1 << bytes;
	*media_bytes = bytes;

	if (!bytes)
		return 0;

	io_off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
	io_base = io_off & ~(hammer2_off_t)(HAMMER2_MINIOSIZE - 1);
	boff = io_off - io_base;

	io_bytes = HAMMER2_MINIOSIZE;
	while (io_bytes + boff < bytes)
		io_bytes <<= 1;

	if (io_bytes > sizeof(*media))
		return -1;
	lseek(fd, io_base, SEEK_SET);
	if (read(fd, media, io_bytes) != (ssize_t)io_bytes)
		return -2;
	if (boff)
		memcpy(media, (char *)media + boff, bytes);

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
		snprintf(msg, sizeof(msg), "Invalid blockref type %d",
		    bref->type);
		add_blockref_entry(&bstats->root, bref, msg);
		failed = true;
		break;
	}

	switch (read_media(fd, bref, &media, &bytes)) {
	case -1:
		add_blockref_entry(&bstats->root, bref, "Bad I/O bytes");
		return -1;
	case -2:
		add_blockref_entry(&bstats->root, bref, "Failed to read media");
		return -1;
	default:
		break;
	}

	if (bref->type != HAMMER2_BREF_TYPE_VOLUME &&
	    bref->type != HAMMER2_BREF_TYPE_FREEMAP)
		bstats->total_bytes += bytes;

	if (QuietOpt <= 0 && (bstats->total_blockref % 100) == 0)
		print_blockref_stats(bstats, false);

	if (!bytes)
		return 0;

	switch (HAMMER2_DEC_CHECK(bref->methods)) {
	case HAMMER2_CHECK_ISCSI32:
		cv = hammer2_icrc32(&media, bytes);
		if (bref->check.iscsi32.value != cv) {
			add_blockref_entry(&bstats->root, bref,
			    "Bad HAMMER2_CHECK_ISCSI32");
			failed = true;
		}
		break;
	case HAMMER2_CHECK_XXHASH64:
		cv64 = XXH64(&media, bytes, XXH_HAMMER2_SEED);
		if (bref->check.xxhash64.value != cv64) {
			add_blockref_entry(&bstats->root, bref,
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
			add_blockref_entry(&bstats->root, bref,
			    "Bad HAMMER2_CHECK_SHA192");
			failed = true;
		}
		break;
	case HAMMER2_CHECK_FREEMAP:
		cv = hammer2_icrc32(&media, bytes);
		if (bref->check.freemap.icrc32 != cv) {
			add_blockref_entry(&bstats->root, bref,
			    "Bad HAMMER2_CHECK_FREEMAP");
			failed = true;
		}
		break;
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
		norecurse = false;
	/*
	 * If failed, no recurse, but still verify its direct children.
	 * Beyond that is probably garbage.
	 */
	for (i = 0; norecurse == false && i < bcount; ++i)
		if (bscan[i].type != HAMMER2_BREF_TYPE_EMPTY)
			if (verify_blockref(fd, voldata, &bscan[i], failed,
			    bstats) == -1)
				return -1;
	return failed ? -1 : 0;
}

static int
init_pfs_blockref(int fd, const hammer2_volume_data_t *voldata,
    const hammer2_blockref_t *bref, struct blockref_list *blist)
{
	hammer2_media_data_t media;
	hammer2_inode_data_t ipdata;
	hammer2_blockref_t *bscan;
	int i, bcount;
	size_t bytes;

	if (read_media(fd, bref, &media, &bytes))
		return -1;
	if (!bytes)
		return 0;

	switch (bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		ipdata = media.ipdata;
		if (ipdata.meta.pfs_type & HAMMER2_PFSTYPE_SUPROOT) {
			bscan = &ipdata.u.blockset.blockref[0];
			bcount = HAMMER2_SET_COUNT;
		} else {
			bscan = NULL;
			bcount = 0;
			if (ipdata.meta.op_flags & HAMMER2_OPFLAG_PFSROOT) {
				struct blockref_msg *p;
				p = calloc(1, sizeof(*p));
				assert(p);
				p->bref = *bref;
				p->msg = strdup(ipdata.filename);
				TAILQ_INSERT_TAIL(blist, p, entry);
			} else
				assert(0); /* should only see SUPROOT or PFS */
		}
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
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
			if (init_pfs_blockref(fd, voldata, &bscan[i], blist)
			    == -1)
				return -1;
	return 0;
}

static void
cleanup_pfs_blockref(struct blockref_list *blist)
{
	cleanup_blockref_msg(blist);
}

int
test_hammer2(const char *devpath)
{
	struct stat st;
	bool failed = false;
	int fd;

	fd = open(devpath, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -1;
	}

	if (fstat(fd, &st) == -1) {
		perror("fstat");
		failed = true;
		goto end;
	}
	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is not a block device\n", devpath);
		failed = true;
		goto end;
	}

	best_zone = find_best_zone(fd);
	if (best_zone == -1)
		fprintf(stderr, "Failed to find best zone\n");

	printf("volume header\n");
	if (test_volume_header(fd) == -1) {
		failed = true;
		if (!ForceOpt)
			goto end;
	}

	printf("freemap\n");
	if (test_blockref(fd, HAMMER2_BREF_TYPE_FREEMAP) == -1) {
		failed = true;
		if (!ForceOpt)
			goto end;
	}
	printf("volume\n");
	if (!ScanPFS) {
		if (test_blockref(fd, HAMMER2_BREF_TYPE_VOLUME) == -1) {
			failed = true;
			if (!ForceOpt)
				goto end;
		}
	} else {
		if (test_pfs_blockref(fd, PFSName) == -1) {
			failed = true;
			if (!ForceOpt)
				goto end;
		}
	}
end:
	close(fd);

	return failed ? -1 : 0;
}
