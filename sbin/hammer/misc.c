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
 * $DragonFly: src/sbin/hammer/misc.c,v 1.5 2008/06/26 04:07:57 dillon Exp $
 */

#include "hammer.h"

const char *ScoreBoardFile;

/*
 * (taken from /usr/src/sys/vfs/hammer/hammer_btree.c)
 *
 * Compare two B-Tree elements, return -N, 0, or +N (e.g. similar to strcmp).
 *
 * Note that for this particular function a return value of -1, 0, or +1
 * can denote a match if delete_tid is otherwise discounted.  A delete_tid
 * of zero is considered to be 'infinity' in comparisons.
 *
 * See also hammer_rec_rb_compare() and hammer_rec_cmp() in hammer_object.c.
 */
int
hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2)
{
	if (key1->localization < key2->localization)
		return(-5);
	if (key1->localization > key2->localization)
		return(5);

	if (key1->obj_id < key2->obj_id)
		return(-4);
	if (key1->obj_id > key2->obj_id)
		return(4);

	if (key1->rec_type < key2->rec_type)
		return(-3);
	if (key1->rec_type > key2->rec_type)
		return(3);

	if (key1->key < key2->key)
		return(-2);
	if (key1->key > key2->key)
		return(2);

	if (key1->create_tid == 0) {
		if (key2->create_tid == 0)
			return(0);
		return(1);
	}
	if (key2->create_tid == 0)
		return(-1);
	if (key1->create_tid < key2->create_tid)
		return(-1);
	if (key1->create_tid > key2->create_tid)
		return(1);
	return(0);
}

void
hammer_key_beg_init(hammer_base_elm_t base)
{
	bzero(base, sizeof(*base));

	base->localization = HAMMER_MIN_LOCALIZATION;
	base->obj_id = HAMMER_MIN_OBJID;
	base->key = HAMMER_MIN_KEY;
	base->create_tid = 1;
	base->rec_type = HAMMER_MIN_RECTYPE;
}

void
hammer_key_end_init(hammer_base_elm_t base)
{
	bzero(base, sizeof(*base));

	base->localization = HAMMER_MAX_LOCALIZATION;
	base->obj_id = HAMMER_MAX_OBJID;
	base->key = HAMMER_MAX_KEY;
	base->create_tid = HAMMER_MAX_TID;
	base->rec_type = HAMMER_MAX_RECTYPE;
}

int
hammer_crc_test_leaf(void *data, hammer_btree_leaf_elm_t leaf)
{
	hammer_crc_t crc;

	if (leaf->data_len == 0) {
		crc = 0;
	} else {
		switch(leaf->base.rec_type) {
		case HAMMER_RECTYPE_INODE:
			if (leaf->data_len != sizeof(struct hammer_inode_data))
				return(0);
			crc = crc32(data, HAMMER_INODE_CRCSIZE);
			break;
		default:
			crc = crc32(data, leaf->data_len);
			break;
		}
	}
	return (leaf->data_crc == crc);
}

void
score_printf(size_t i, size_t w, const char *ctl, ...)
{
	va_list va;
	size_t n;
	static size_t SSize;
	static int SFd = -1;
	static char ScoreBuf[1024];

	if (ScoreBoardFile == NULL)
		return;
	assert(i + w < sizeof(ScoreBuf));
	if (SFd < 0) {
		SFd = open(ScoreBoardFile, O_RDWR|O_CREAT|O_TRUNC, 0644);
		if (SFd < 0)
			return;
		SSize = 0;
	}
	for (n = 0; n < i; ++n) {
		if (ScoreBuf[n] == 0)
			ScoreBuf[n] = ' ';
	}
	va_start(va, ctl);
	vsnprintf(ScoreBuf + i, w - 1, ctl, va);
	va_end(va);
	n = strlen(ScoreBuf + i);
	while (n < w - 1) {
		ScoreBuf[i + n] = ' ';
		++n;
	}
	ScoreBuf[i + n] = '\n';
	if (SSize < i + w)
		SSize = i + w;
	pwrite(SFd, ScoreBuf, SSize, 0);
}

void
hammer_check_restrict(const char *filesystem)
{
	size_t rlen;
	int atslash;

	if (RestrictTarget == NULL)
		return;
	rlen = strlen(RestrictTarget);
	if (strncmp(filesystem, RestrictTarget, rlen) != 0) {
		fprintf(stderr, "hammer-remote: restricted target\n");
		exit(1);
	}
	atslash = 1;
	while (filesystem[rlen]) {
		if (atslash &&
		    filesystem[rlen] == '.' &&
		    filesystem[rlen+1] == '.') {
			fprintf(stderr, "hammer-remote: '..' not allowed\n");
			exit(1);
		}
		if (filesystem[rlen] == '/')
			atslash = 1;
		else
			atslash = 0;
		++rlen;
	}
}

/*
 * Functions and data structure for zone statistics
 */
/*
 * Each layer1 needs ((2^19) / 64) = 8192 uint64_t.
 */
#define HAMMER_LAYER1_UINT64 8192
#define HAMMER_LAYER1_BYTES (HAMMER_LAYER1_UINT64 * sizeof(uint64_t))

static int *l1_max = NULL;
static uint64_t **l1_bits = NULL;

static __inline
int
hammer_set_layer_bits(uint64_t *bits, int i)
{
	int q, r;

	q = i >> 6;
	r = i & ((1 << 6) - 1);

	bits += q;
	if (!((*bits) & ((uint64_t)1 << r))) {
		(*bits) |= ((uint64_t)1 << r);
		return(1);
	}
	return(0);  /* already seen this block */
}

static
void
hammer_extend_layer1_bits(int vol, int newsiz, int oldsiz)
{
	uint64_t *p;

	assert(newsiz > oldsiz);
	assert(newsiz > 0 && oldsiz >= 0);

	p = l1_bits[vol];
	if (p == NULL)
		p = malloc(HAMMER_LAYER1_BYTES * newsiz);
	else
		p = realloc(p, HAMMER_LAYER1_BYTES * newsiz);
	if (p == NULL)
		err(1, "alloc");
	l1_bits[vol] = p;

	p += HAMMER_LAYER1_UINT64 * oldsiz;
	bzero((void*)p, HAMMER_LAYER1_BYTES * (newsiz - oldsiz));
}

static
void
hammer_dump_layer1_bits(void)
{
	int i, j, n;

	assert(l1_bits);
	printf("Layer1 bitmaps\n");

	for (i = 0; i < HAMMER_MAX_VOLUMES; i++) {
		if (l1_max[i] != -1) {
			printf("volume=%d %p\n", i, l1_bits[i]);
			n = (l1_max[i] + 1) * HAMMER_LAYER1_UINT64;
			for (j = 0; j < n; j++) {
				printf("\tblock[%d][%d]=0x%016lX\n",
					i, j << 6, *(l1_bits[i] + j));
			}
		}
	}
}

struct zone_stat*
hammer_init_zone_stat(void)
{
	return calloc(HAMMER_MAX_ZONES, sizeof(struct zone_stat));
}

struct zone_stat*
hammer_init_zone_stat_bits(void)
{
	int i;

	l1_max = calloc(HAMMER_MAX_VOLUMES, sizeof(int));
	if (l1_max == NULL)
		err(1, "calloc");

	l1_bits = calloc(HAMMER_MAX_VOLUMES, sizeof(uint64_t*));
	if (l1_bits == NULL)
		err(1, "calloc");

	for (i = 0; i < HAMMER_MAX_VOLUMES; i++) {
		l1_max[i] = -1;  /* +1 needs to be 0 */
		l1_bits[i] = NULL;
	}
	return(hammer_init_zone_stat());
}

void
hammer_cleanup_zone_stat(struct zone_stat *stats)
{
	int i;

	if (l1_bits) {
		if (DebugOpt)
			hammer_dump_layer1_bits();
		for (i = 0; i < HAMMER_MAX_VOLUMES; i++) {
			free(l1_bits[i]);
			l1_bits[i] = NULL;
		}
	}

	free(l1_bits);
	l1_bits = NULL;

	free(l1_max);
	l1_max = NULL;

	free(stats);
}

static
void
_hammer_add_zone_stat(struct zone_stat *stats, int zone,
			hammer_off_t bytes, int new_block, int new_item)
{
	struct zone_stat *sp = stats + zone;

	if (new_block)
		sp->blocks++;
	if (new_item)
		sp->items++;
	sp->used += bytes;
}

void
hammer_add_zone_stat(struct zone_stat *stats, hammer_off_t offset,
			hammer_off_t bytes)
{
	int zone, vol, i, j, new_block;
	uint64_t *p;

	offset &= ~HAMMER_BIGBLOCK_MASK64;
	zone = HAMMER_ZONE_DECODE(offset);
	vol = HAMMER_VOL_DECODE(offset);

	offset &= HAMMER_OFF_SHORT_MASK;  /* cut off volume bits from layer1 */
	i = (int)HAMMER_BLOCKMAP_LAYER1_INDEX(offset);
	j = (int)HAMMER_BLOCKMAP_LAYER2_INDEX(offset);

	if (i > l1_max[vol]) {
		assert(i < 1024);  /* XXX hardcoded */
		hammer_extend_layer1_bits(vol, i + 1, l1_max[vol] + 1);
		l1_max[vol] = i;
	}

	p = l1_bits[vol] + i * HAMMER_LAYER1_UINT64;
	new_block = hammer_set_layer_bits(p, j);
	_hammer_add_zone_stat(stats, zone, bytes, new_block, 1);
}

/*
 * If the same layer2 is used more than once the result will be wrong.
 */
void
hammer_add_zone_stat_layer2(struct zone_stat *stats,
			struct hammer_blockmap_layer2 *layer2)
{
	_hammer_add_zone_stat(stats, layer2->zone,
		HAMMER_BIGBLOCK_SIZE - layer2->bytes_free, 1, 0);
}

void
hammer_print_zone_stat(const struct zone_stat *stats)
{
	int i;
	double per;
	hammer_off_t total_blocks = 0;
	hammer_off_t total_items = 0;
	hammer_off_t total_used = 0;
	const struct zone_stat *p = stats;

	printf("HAMMER zone statistics\n");
	printf("\tzone #  blocks       items              used[B]             used[%%]\n");

	for (i = 0; i < HAMMER_MAX_ZONES; i++) {
		if (p->blocks)
			per = ((double)(p->used * 100)) /
				(p->blocks * HAMMER_BIGBLOCK_SIZE);
		else
			per = 0;
		printf("\tzone %-2d %-12ju %-18ju %-19ju %g\n",
			i, p->blocks, p->items, p->used, per);
		total_blocks += p->blocks;
		total_items += p->items;
		total_used += p->used;
		p++;
	}

	/*
	 * Remember that zone0 is always 0% used and zone15 is
	 * always 100% used.
	 */
	if (total_blocks)
		per = ((double)(total_used * 100)) /
			(total_blocks * HAMMER_BIGBLOCK_SIZE);
	else
		per = 0;

	printf("\t----------------------------------------------------------------------\n");
	printf("\ttotal   %-12ju %-18ju %-19ju %g\n",
		(uintmax_t)total_blocks, (uintmax_t)total_items,
		(uintmax_t)total_used, per);
}
