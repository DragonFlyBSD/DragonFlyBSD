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

#include "hammer_util.h"

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

int
getyn(void)
{
	char buf[256];
	int len;

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		return(0);
	len = strlen(buf);
	while (len && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		--len;
	buf[len] = 0;
	if (strcmp(buf, "y") == 0 ||
	    strcmp(buf, "yes") == 0 ||
	    strcmp(buf, "Y") == 0 ||
	    strcmp(buf, "YES") == 0) {
		return(1);
	}
	return(0);
}

const char *
sizetostr(off_t size)
{
	static char buf[32];

	if (size < 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2fB", (double)size);
	} else if (size < 1024 * 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2fKB",
			(double)size / 1024);
	} else if (size < 1024 * 1024 * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fMB",
			(double)size / (1024 * 1024));
	} else if (size < 1024 * 1024 * 1024LL * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fGB",
			(double)size / (1024 * 1024 * 1024LL));
	} else {
		snprintf(buf, sizeof(buf), "%6.2fTB",
			(double)size / (1024 * 1024 * 1024LL * 1024LL));
	}
	return(buf);
}

int
hammer_fs_to_vol(const char *fs, struct hammer_ioc_volume_list *p)
{
	struct hammer_ioc_volume_list ioc;
	int fd;

	fd = open(fs, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return(-1);
	}

	bzero(&ioc, sizeof(ioc));
	ioc.nvols = HAMMER_MAX_VOLUMES;
	ioc.vols = malloc(ioc.nvols * sizeof(*ioc.vols));
	if (ioc.vols == NULL) {
		perror("malloc");
		close(fd);
		return(-1);
	}

	if (ioctl(fd, HAMMERIOC_LIST_VOLUMES, &ioc) < 0) {
		perror("ioctl");
		close(fd);
		free(ioc.vols);
		return(-1);
	}

	bcopy(&ioc, p, sizeof(ioc));
	close(fd);

	return(0);
}

int
hammer_fs_to_rootvol(const char *fs, char *buf, int len)
{
	struct hammer_ioc_volume_list ioc;
	int i;

	if (hammer_fs_to_vol(fs, &ioc) == -1)
		return(-1);

	for (i = 0; i < ioc.nvols; i++) {
		if (ioc.vols[i].vol_no == HAMMER_ROOT_VOLNO) {
			strlcpy(buf, ioc.vols[i].device_name, len);
			break;
		}
	}
	assert(i != ioc.nvols);  /* root volume must exist */

	free(ioc.vols);
	return(0);
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
	bzero(p, HAMMER_LAYER1_BYTES * (newsiz - oldsiz));
}

struct zone_stat*
hammer_init_zone_stat(void)
{
	return(calloc(HAMMER_MAX_ZONES, sizeof(struct zone_stat)));
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
_hammer_add_zone_stat(struct zone_stat *stats, int zone, int bytes,
	int new_block, int new_item)
{
	struct zone_stat *sp = stats + zone;

	if (new_block)
		sp->blocks++;
	if (new_item)
		sp->items++;
	sp->used += bytes;
}

void
hammer_add_zone_stat(struct zone_stat *stats, hammer_off_t offset, int bytes)
{
	int zone, vol, i, j, new_block;
	uint64_t *p;

	offset &= ~HAMMER_BIGBLOCK_MASK64;
	zone = HAMMER_ZONE_DECODE(offset);
	vol = HAMMER_VOL_DECODE(offset);

	offset &= HAMMER_OFF_SHORT_MASK;  /* cut off volume bits from layer1 */
	i = HAMMER_BLOCKMAP_LAYER1_INDEX(offset);
	j = HAMMER_BLOCKMAP_LAYER2_INDEX(offset);

	if (i > l1_max[vol]) {
		assert(i < (HAMMER_BLOCKMAP_RADIX1 / HAMMER_MAX_VOLUMES));
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
	hammer_blockmap_layer2_t layer2)
{
	_hammer_add_zone_stat(stats, layer2->zone,
		HAMMER_BIGBLOCK_SIZE - layer2->bytes_free, 1, 0);
}

static __inline
double
_calc_used_percentage(int64_t blocks, int64_t used)
{
	double res;

	if (blocks)
		res = ((double)(used * 100)) / (blocks << HAMMER_BIGBLOCK_BITS);
	else
		res = 0;
	return(res);
}

void
hammer_print_zone_stat(const struct zone_stat *stats)
{
	int i;
	int64_t total_blocks = 0;
	int64_t total_items = 0;
	int64_t total_used = 0;
	const struct zone_stat *p = stats;
#define INDENT ""

	printf("HAMMER zone statistics\n");
	printf(INDENT"zone #             "
		"blocks       items              used[B]             used[%%]\n");
	for (i = 0; i < HAMMER_MAX_ZONES; i++) {
		printf(INDENT"zone %-2d %-10s %-12ju %-18ju %-19ju %g\n",
			i, zone_labels[i], p->blocks, p->items, p->used,
			_calc_used_percentage(p->blocks, p->used));
		total_blocks += p->blocks;
		total_items += p->items;
		total_used += p->used;
		p++;
	}

	/*
	 * Remember that zone0 is always 0% used and zone15 is
	 * always 100% used.
	 */
	printf(INDENT"--------------------------------------------------------------------------------\n");
	printf(INDENT"total              %-12ju %-18ju %-19ju %g\n",
		(uintmax_t)total_blocks,
		(uintmax_t)total_items,
		(uintmax_t)total_used,
		_calc_used_percentage(total_blocks, total_used));
}
