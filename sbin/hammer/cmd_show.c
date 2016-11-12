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
 */

#include <libutil.h>

#include "hammer.h"

#define FLAG_TOOFARLEFT		0x0001
#define FLAG_TOOFARRIGHT	0x0002
#define FLAG_BADTYPE		0x0004
#define FLAG_BADCHILDPARENT	0x0008
#define FLAG_BADMIRRORTID	0x0010

struct {
	struct hammer_base_elm base;
	int limit;	/* # of fields to test */
	int filter;	/* filter type (default -1) */
	int obfuscate;	/* obfuscate direntry name */
	int indent;	/* use depth indentation */
	struct zone_stat *stats;
} opt;

static __inline void print_btree(hammer_off_t node_offset);
static __inline void print_subtree(hammer_btree_elm_t elm);
static void print_btree_node(hammer_off_t node_offset, hammer_tid_t mirror_tid,
	hammer_btree_elm_t lbe);
static int test_node_count(hammer_node_ondisk_t node, char *badmp);
static void print_btree_elm(hammer_node_ondisk_t node, hammer_off_t node_offset,
	hammer_btree_elm_t elm, hammer_btree_elm_t lbe, const char *ext);
static int get_elm_flags(hammer_node_ondisk_t node, hammer_off_t node_offset,
	hammer_btree_elm_t elm, hammer_btree_elm_t lbe);
static int test_lr(hammer_btree_elm_t elm, hammer_btree_elm_t lbe);
static int test_rbn_lr(hammer_btree_elm_t elm, hammer_btree_elm_t lbe);
static void print_bigblock_fill(hammer_off_t offset);
static const char *check_data_crc(hammer_btree_elm_t elm);
static uint32_t get_buf_crc(hammer_off_t buf_offset, int32_t buf_len);
static void print_record(hammer_btree_elm_t elm);
static int init_btree_search(const char *arg);
static int test_btree_search(hammer_btree_elm_t elm);
static int test_btree_match(hammer_btree_elm_t elm);
static int test_btree_out_of_range(hammer_btree_elm_t elm);
static void hexdump_record(const void *ptr, int length, const char *hdr);

static int num_bad_node = 0;
static int num_bad_elm = 0;
static int num_bad_rec = 0;
static int depth;

#define _X	"\t"
static const char* _indents[] = {
	"",
	_X,
	_X _X,
	_X _X _X,
	_X _X _X _X,
	_X _X _X _X _X,
	_X _X _X _X _X _X,
	_X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X _X _X _X _X _X _X,
	_X _X _X _X _X _X _X _X _X _X _X _X _X _X _X _X,
	/* deep enough */
};
#define INDENT _indents[opt.indent ? depth : 0]

void
hammer_cmd_show(const char *arg, int filter, int obfuscate, int indent)
{
	struct volume_info *volume;
	hammer_volume_ondisk_t ondisk;
	struct zone_stat *stats = NULL;

	volume = get_root_volume();
	ondisk = volume->ondisk;

	print_blockmap(volume);

	if (VerboseOpt)
		stats = hammer_init_zone_stat_bits();

	bzero(&opt, sizeof(opt));
	opt.filter = filter;
	opt.obfuscate = obfuscate;
	opt.indent = indent;
	opt.stats = stats;

	if (init_btree_search(arg) > 0) {
		printf("arg=\"%s\"", arg);
		if (opt.limit > 0)
			printf(" lo=%08x", opt.base.localization);
		if (opt.limit > 1)
			printf(" objid=%016jx", (uintmax_t)opt.base.obj_id);
		if (opt.limit > 2)
			printf(" rt=%02x", opt.base.rec_type);
		if (opt.limit > 3)
			printf(" key=%016jx", (uintmax_t)opt.base.key);
		if (opt.limit > 4)
			printf(" tid=%016jx", (uintmax_t)opt.base.create_tid);
		printf("\n");
	}
	print_btree(ondisk->vol0_btree_root);

	if (stats) {
		hammer_print_zone_stat(stats);
		hammer_cleanup_zone_stat(stats);
	}

	if (num_bad_node || VerboseOpt) {
		printf("%d bad nodes\n", num_bad_node);
	}
	if (num_bad_elm || VerboseOpt) {
		printf("%d bad elms\n", num_bad_elm);
	}
	if (num_bad_rec || VerboseOpt) {
		printf("%d bad records\n", num_bad_rec);
	}
}

static __inline
void
print_btree(hammer_off_t node_offset)
{
	depth = -1;
	print_btree_node(node_offset, HAMMER_MAX_TID, NULL);
	assert(depth == -1);
}

static __inline
void
print_subtree(hammer_btree_elm_t elm)
{
	hammer_btree_internal_elm_t i = &elm->internal;
	print_btree_node(i->subtree_offset, i->mirror_tid, elm);
}

static void
print_btree_node(hammer_off_t node_offset, hammer_tid_t mirror_tid,
	hammer_btree_elm_t lbe)
{
	struct buffer_info *buffer = NULL;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int i;
	char badc = ' ';  /* good */
	char badm = ' ';  /* good */
	const char *ext;

	depth++;
	node = get_buffer_data(node_offset, &buffer, 0);

	if (node == NULL) {
		badc = 'B';
		badm = 'I';
	} else {
		if (!hammer_crc_test_btree(node))
			badc = 'B';
		if (node->mirror_tid > mirror_tid) {
			badc = 'B';
			badm = 'M';
		}
		if (test_node_count(node, &badm) == -1) {
			badc = 'B';
			assert(badm != ' ');
		}
	}

	if (badm != ' ' || badc != ' ')  /* not good */
		++num_bad_node;

	printf("%s%c%c   NODE %016jx ",
	       INDENT, badc, badm, (uintmax_t)node_offset);
	printf("cnt=%02d p=%016jx type=%c depth=%d mirror=%016jx",
	       node->count,
	       (uintmax_t)node->parent,
	       (node->type ? node->type : '?'),
	       depth,
	       (uintmax_t)node->mirror_tid);
	printf(" fill=");
	print_bigblock_fill(node_offset);
	printf(" {\n");

	if (opt.stats)
		hammer_add_zone_stat(opt.stats, node_offset, sizeof(*node));

	for (i = 0; i < node->count; ++i) {
		elm = &node->elms[i];
		ext = NULL;
		if (opt.limit) {
			switch (node->type) {
			case HAMMER_BTREE_TYPE_INTERNAL:
				if (!test_btree_out_of_range(elm))
					ext = "*";
				break;
			case HAMMER_BTREE_TYPE_LEAF:
				if (test_btree_match(elm))
					ext = "*";
				break;
			}
		}
		print_btree_elm(node, node_offset, elm, lbe, ext);
	}
	if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
		assert(i == node->count);  /* boundary */
		elm = &node->elms[i];
		print_btree_elm(node, node_offset, elm, lbe, NULL);
	}
	printf("%s     }\n", INDENT);

	if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
		for (i = 0; i < node->count; ++i) {
			elm = &node->elms[i];
			if (opt.limit && opt.filter) {
				if (test_btree_out_of_range(elm))
					continue;
			}
			if (elm->internal.subtree_offset) {
				print_subtree(elm);
				/*
				 * Cause show to do normal iteration after
				 * seeking to the lo:objid:rt:key:tid
				 * by default
				 */
				if (opt.limit && opt.filter == -1)  /* default */
					opt.filter = 0;
			}
		}
	}
	rel_buffer(buffer);
	depth--;
}

static int
test_node_count(hammer_node_ondisk_t node, char *badmp)
{
	hammer_node_ondisk_t parent_node;
	struct buffer_info *buffer = NULL;
	int maxcount;

	maxcount = hammer_node_max_elements(node->type);

	if (maxcount == -1) {
		*badmp = 'U';
		return(-1);
	} else if (node->count > maxcount) {
		*badmp = 'C';
		return(-1);
	} else if (node->count == 0) {
		parent_node = get_buffer_data(node->parent, &buffer, 0);
		if (parent_node->count != 1) {
			*badmp = 'C';
			rel_buffer(buffer);
			return(-1);
		}
		rel_buffer(buffer);
	}

	return(0);
}

static __inline
int
is_root_btree_beg(uint8_t type, int i, hammer_btree_elm_t elm)
{
	/*
	 * elm->base.btype depends on what the original node had
	 * so it could be anything but HAMMER_BTREE_TYPE_NONE.
	 */
	return (type == HAMMER_BTREE_TYPE_INTERNAL &&
		i == 0 &&
		elm->base.localization == HAMMER_MIN_ONDISK_LOCALIZATION &&
		elm->base.obj_id == (int64_t)HAMMER_MIN_OBJID &&
		elm->base.key == (int64_t)HAMMER_MIN_KEY &&
		elm->base.create_tid == 1 &&
		elm->base.delete_tid == 1 &&
		elm->base.rec_type == HAMMER_MIN_RECTYPE &&
		elm->base.obj_type == 0 &&
		elm->base.btype != HAMMER_BTREE_TYPE_NONE);
}

static __inline
int
is_root_btree_end(uint8_t type, int i, hammer_btree_elm_t elm)
{
	return (type == HAMMER_BTREE_TYPE_INTERNAL &&
		i != 0 &&
		elm->base.localization == HAMMER_MAX_ONDISK_LOCALIZATION &&
		elm->base.obj_id == HAMMER_MAX_OBJID &&
		elm->base.key == HAMMER_MAX_KEY &&
		elm->base.create_tid == HAMMER_MAX_TID &&
		elm->base.delete_tid == 0 &&
		elm->base.rec_type == HAMMER_MAX_RECTYPE &&
		elm->base.obj_type == 0 &&
		elm->base.btype == HAMMER_BTREE_TYPE_NONE);
}

static
void
print_btree_elm(hammer_node_ondisk_t node, hammer_off_t node_offset,
	hammer_btree_elm_t elm, hammer_btree_elm_t lbe, const char *ext)
{
	char flagstr[8] = { 0, '-', '-', '-', '-', '-', '-', 0 };
	char deleted;
	char rootelm;
	const char *label;
	const char *p;
	int flags;
	int i = ((char*)elm - (char*)node) / (int)sizeof(*elm) - 1;

	flags = get_elm_flags(node, node_offset, elm, lbe);
	flagstr[0] = flags ? 'B' : 'G';
	if (flags & FLAG_TOOFARLEFT)
		flagstr[2] = 'L';
	if (flags & FLAG_TOOFARRIGHT)
		flagstr[3] = 'R';
	if (flags & FLAG_BADTYPE)
		flagstr[4] = 'T';
	if (flags & FLAG_BADCHILDPARENT)
		flagstr[5] = 'C';
	if (flags & FLAG_BADMIRRORTID)
		flagstr[6] = 'M';
	if (flagstr[0] == 'B')
		++num_bad_elm;

	/*
	 * Check if elm is derived from root split
	 */
	if (is_root_btree_beg(node->type, i, elm))
		rootelm = '>';
	else if (is_root_btree_end(node->type, i, elm))
		rootelm = '<';
	else
		rootelm = ' ';

	if (elm->base.delete_tid)
		deleted = 'd';
	else
		deleted = ' ';

	if (node->type == HAMMER_BTREE_TYPE_INTERNAL && node->count == i)
		label = "RBN";
	else
		label = "ELM";

	printf("%s%s %s %2d %c ",
	       INDENT, flagstr, label, i, hammer_elm_btype(elm));
	printf("lo=%08x objid=%016jx rt=%02x key=%016jx tid=%016jx\n",
	       elm->base.localization,
	       (uintmax_t)elm->base.obj_id,
	       elm->base.rec_type,
	       (uintmax_t)elm->base.key,
	       (uintmax_t)elm->base.create_tid);
	printf("%s               %c del=%016jx ot=%02x",
	       INDENT,
	       (rootelm == ' ' ? deleted : rootelm),
	       (uintmax_t)elm->base.delete_tid,
	       elm->base.obj_type);

	switch(node->type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		printf(" suboff=%016jx mirror=%016jx",
		       (uintmax_t)elm->internal.subtree_offset,
		       (uintmax_t)elm->internal.mirror_tid);
		if (ext)
			printf(" %s", ext);
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		switch(elm->base.btype) {
		case HAMMER_BTREE_TYPE_RECORD:
			printf(" dataoff=%016jx/%d",
			       (uintmax_t)elm->leaf.data_offset,
			       elm->leaf.data_len);
			p = check_data_crc(elm);
			printf(" crc=%08x", elm->leaf.data_crc);
			if (p) {
				printf(" error=%s", p);
				++num_bad_rec;
			}
			printf(" fill=");
			print_bigblock_fill(elm->leaf.data_offset);
			if (QuietOpt < 2)
				print_record(elm);
			if (opt.stats)
				hammer_add_zone_stat(opt.stats,
					elm->leaf.data_offset,
					elm->leaf.data_len);
			break;
		default:
			printf(" badtype=%d", elm->base.btype);
			break;
		}
		if (ext)
			printf(" %s", ext);
		break;
	}
	printf("\n");
}

static
int
get_elm_flags(hammer_node_ondisk_t node, hammer_off_t node_offset,
	hammer_btree_elm_t elm, hammer_btree_elm_t lbe)
{
	hammer_off_t child_offset;
	int flags = 0;
	int i = ((char*)elm - (char*)node) / (int)sizeof(*elm) - 1;

	switch(node->type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		child_offset = elm->internal.subtree_offset;
		if (elm->internal.mirror_tid > node->mirror_tid)
			flags |= FLAG_BADMIRRORTID;

		if (i == node->count) {
			if (child_offset != 0)
				flags |= FLAG_BADCHILDPARENT;
			switch(elm->base.btype) {
			case HAMMER_BTREE_TYPE_NONE:
				flags |= test_rbn_lr(elm, lbe);
				break;
			default:
				flags |= FLAG_BADTYPE;
				break;
			}
		} else {
			if (child_offset == 0) {
				flags |= FLAG_BADCHILDPARENT;
			} else {
				struct buffer_info *buffer = NULL;
				hammer_node_ondisk_t subnode;
				subnode = get_buffer_data(child_offset, &buffer, 0);
				if (subnode == NULL)
					flags |= FLAG_BADCHILDPARENT;
				else if (subnode->parent != node_offset)
					flags |= FLAG_BADCHILDPARENT;
				rel_buffer(buffer);
			}
			switch(elm->base.btype) {
			case HAMMER_BTREE_TYPE_INTERNAL:
			case HAMMER_BTREE_TYPE_LEAF:
				flags |= test_lr(elm, lbe);
				break;
			default:
				flags |= FLAG_BADTYPE;
				break;
			}
		}
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		if (elm->leaf.data_offset == 0) {
			flags |= FLAG_BADCHILDPARENT;
		}
		if (elm->leaf.data_len == 0) {
			flags |= FLAG_BADCHILDPARENT;
		}

		if (node->mirror_tid == 0 &&
		    !(node->parent == 0 && node->count == 2)) {
			flags |= FLAG_BADMIRRORTID;
		}
		if (elm->base.create_tid && node->mirror_tid &&
		    elm->base.create_tid > node->mirror_tid) {
			flags |= FLAG_BADMIRRORTID;
		}
		if (elm->base.delete_tid && node->mirror_tid &&
		    elm->base.delete_tid > node->mirror_tid) {
			flags |= FLAG_BADMIRRORTID;
		}
		switch(elm->base.btype) {
		case HAMMER_BTREE_TYPE_RECORD:
			flags |= test_lr(elm, lbe);
			break;
		default:
			flags |= FLAG_BADTYPE;
			break;
		}
		break;
	default:
		flags |= FLAG_BADTYPE;
		break;
	}
	return(flags);
}

/*
 * Taken from /usr/src/sys/vfs/hammer/hammer_btree.c.
 */
static
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

static
int
test_lr(hammer_btree_elm_t elm, hammer_btree_elm_t lbe)
{
	if (lbe) {
		hammer_btree_elm_t rbe = lbe + 1;
		if (hammer_btree_cmp(&elm->base, &lbe->base) < 0)
			return(FLAG_TOOFARLEFT);
		if (hammer_btree_cmp(&elm->base, &rbe->base) >= 0)
			return(FLAG_TOOFARRIGHT);
	}
	return(0);
}

static
int
test_rbn_lr(hammer_btree_elm_t rbn, hammer_btree_elm_t lbe)
{
	if (lbe) {
		hammer_btree_elm_t rbe = lbe + 1;
		if (hammer_btree_cmp(&rbn->base, &lbe->base) < 0)
			return(FLAG_TOOFARLEFT);
		if (hammer_btree_cmp(&rbn->base, &rbe->base) > 0)
			return(FLAG_TOOFARRIGHT);
	}
	return(0);
}

static
void
print_bigblock_fill(hammer_off_t offset)
{
	struct hammer_blockmap_layer1 layer1;
	struct hammer_blockmap_layer2 layer2;
	int fill;
	int error;

	blockmap_lookup(offset, &layer1, &layer2, &error);
	printf("z%d:v%d:%d:%d:%lu=",
		HAMMER_ZONE_DECODE(offset),
		HAMMER_VOL_DECODE(offset),
		HAMMER_BLOCKMAP_LAYER1_INDEX(offset),
		HAMMER_BLOCKMAP_LAYER2_INDEX(offset),
		offset & HAMMER_BIGBLOCK_MASK64);

	if (error) {
		printf("B%d", error);
	} else {
		fill = layer2.bytes_free * 100 / HAMMER_BIGBLOCK_SIZE;
		printf("%d%%", 100 - fill);
	}
}

/*
 * Check the generic crc on a data element.  Inodes record types are
 * special in that some of their fields are not CRCed.
 *
 * Also check that the zone is valid.
 */
static
const char *
check_data_crc(hammer_btree_elm_t elm)
{
	struct buffer_info *data_buffer;
	hammer_off_t data_offset;
	hammer_off_t buf_offset;
	int32_t data_len;
	uint32_t crc;
	int error;
	char *ptr;
	static char bo[5];

	data_offset = elm->leaf.data_offset;
	data_len = elm->leaf.data_len;
	data_buffer = NULL;
	if (data_offset == 0 || data_len == 0)
		return("ZO");  /* zero offset or length */

	error = 0;
	buf_offset = blockmap_lookup(data_offset, NULL, NULL, &error);
	if (error) {
		bzero(bo, sizeof(bo));
		snprintf(bo, sizeof(bo), "BO%d", -error);
		return(bo);
	}

	crc = 0;
	switch (elm->leaf.base.rec_type) {
	case HAMMER_RECTYPE_INODE:
		if (data_len != sizeof(struct hammer_inode_data))
			return("BI");  /* bad inode size */
		ptr = get_buffer_data(buf_offset, &data_buffer, 0);
		crc = hammer_crc_get_leaf(ptr, &elm->leaf);
		rel_buffer(data_buffer);
		break;
	default:
		crc = get_buf_crc(buf_offset, data_len);
		break;
	}

	if (crc == 0)
		return("Bx");  /* bad crc */
	if (crc != elm->leaf.data_crc)
		return("BX");  /* bad crc */
	return(NULL);  /* success */
}

static
uint32_t
get_buf_crc(hammer_off_t buf_offset, int32_t buf_len)
{
	struct buffer_info *data_buffer = NULL;
	int32_t len;
	uint32_t crc = 0;
	char *ptr;

	while (buf_len) {
		ptr = get_buffer_data(buf_offset, &data_buffer, 0);
		len = HAMMER_BUFSIZE - ((int)buf_offset & HAMMER_BUFMASK);
		if (len > buf_len)
			len = (int)buf_len;
		assert(len <= HAMMER_BUFSIZE);
		crc = crc32_ext(ptr, len, crc);
		buf_len -= len;
		buf_offset += len;
	}
	rel_buffer(data_buffer);

	return(crc);
}

static
void
print_config(char *cfgtxt)
{
	char *token;

	printf("\n%s%17s", INDENT, "");
	printf("config text=\"\n");
	if (cfgtxt != NULL) {
		while((token = strsep(&cfgtxt, "\r\n")) != NULL)
			if (strlen(token))
				printf("%s%17s            %s\n",
					INDENT, "", token);
	}
	printf("%s%17s            \"", INDENT, "");
}

static
void
print_record(hammer_btree_elm_t elm)
{
	struct buffer_info *data_buffer;
	hammer_off_t data_offset;
	int32_t data_len;
	hammer_data_ondisk_t data;
	uint32_t status;
	char *str1 = NULL;
	char *str2 = NULL;

	data_offset = elm->leaf.data_offset;
	data_len = elm->leaf.data_len;
	assert(data_offset != 0);
	assert(data_len != 0);

	data_buffer = NULL;
	data = get_buffer_data(data_offset, &data_buffer, 0);
	assert(data != NULL);

	switch(elm->leaf.base.rec_type) {
	case HAMMER_RECTYPE_UNKNOWN:
		printf("\n%s%17s", INDENT, "");
		printf("unknown");
		break;
	case HAMMER_RECTYPE_INODE:
		printf("\n%s%17s", INDENT, "");
		printf("inode size=%jd nlinks=%jd",
		       (intmax_t)data->inode.size,
		       (intmax_t)data->inode.nlinks);
		printf(" mode=%05o uflags=%08x caps=%02x",
			data->inode.mode,
			data->inode.uflags,
			data->inode.cap_flags);
		printf(" pobjid=%016jx ot=%02x\n",
			(uintmax_t)data->inode.parent_obj_id,
			data->inode.obj_type);
		printf("%s%17s", INDENT, "");
		printf("      ctime=%016jx mtime=%016jx atime=%016jx",
			(uintmax_t)data->inode.ctime,
			(uintmax_t)data->inode.mtime,
			(uintmax_t)data->inode.atime);
		if (data->inode.ext.symlink[0])
			printf(" symlink=\"%s\"",
				data->inode.ext.symlink);
		break;
	case HAMMER_RECTYPE_DIRENTRY:
		data_len -= HAMMER_ENTRY_NAME_OFF;
		printf("\n%s%17s", INDENT, "");
		printf("dir-entry objid=%016jx lo=%08x",
		       (uintmax_t)data->entry.obj_id,
		       data->entry.localization);
		if (!opt.obfuscate)
			printf(" name=\"%*.*s\"",
			       data_len, data_len, data->entry.name);
		break;
	case HAMMER_RECTYPE_FIX:
		switch(elm->leaf.base.key) {
		case HAMMER_FIXKEY_SYMLINK:
			data_len -= HAMMER_SYMLINK_NAME_OFF;
			printf("\n%s%17s", INDENT, "");
			printf("fix-symlink name=\"%*.*s\"",
				data_len, data_len, data->symlink.name);
			break;
		}
		break;
	case HAMMER_RECTYPE_PFS:
		printf("\n%s%17s", INDENT, "");
		printf("pfs sync_beg_tid=%016jx sync_end_tid=%016jx\n",
			(intmax_t)data->pfsd.sync_beg_tid,
			(intmax_t)data->pfsd.sync_end_tid);
		uuid_to_string(&data->pfsd.shared_uuid, &str1, &status);
		uuid_to_string(&data->pfsd.unique_uuid, &str2, &status);
		printf("%17s", "");
		printf("    shared_uuid=%s\n", str1);
		printf("%17s", "");
		printf("    unique_uuid=%s\n", str2);
		printf("%17s", "");
		printf("    mirror_flags=%08x label=\"%s\"",
			data->pfsd.mirror_flags, data->pfsd.label);
		if (data->pfsd.snapshots[0])
			printf(" snapshots=\"%s\"", data->pfsd.snapshots);
		free(str1);
		free(str2);
		break;
	case HAMMER_RECTYPE_SNAPSHOT:
		printf("\n%s%17s", INDENT, "");
		printf("snapshot tid=%016jx label=\"%s\"",
			(intmax_t)data->snap.tid, data->snap.label);
		break;
	case HAMMER_RECTYPE_CONFIG:
		if (VerboseOpt > 2) {
			char *p = strdup(data->config.text);
			print_config(p);
			free(p);
		}
		break;
	case HAMMER_RECTYPE_DATA:
		if (VerboseOpt > 3) {
			printf("\n");
			hexdump_record(data, data_len, "\t\t  ");
		}
		break;
	case HAMMER_RECTYPE_EXT:
	case HAMMER_RECTYPE_DB:
		if (VerboseOpt > 2) {
			printf("\n");
			hexdump_record(data, data_len, "\t\t  ");
		}
		break;
	default:
		assert(0);
		break;
	}
	rel_buffer(data_buffer);
}

/*
 * HAMMER userspace only supports buffer size upto HAMMER_BUFSIZE
 * which is 16KB.  Passing record data length larger than 16KB to
 * hexdump(3) is invalid even if the leaf node elm says >16KB data.
 */
static void
hexdump_record(const void *ptr, int length, const char *hdr)
{
	int data_len = length;

	if (data_len > HAMMER_BUFSIZE)  /* XXX */
		data_len = HAMMER_BUFSIZE;
	hexdump(ptr, data_len, hdr, 0);

	if (length > data_len)
		printf("%s....\n", hdr);
}

static __inline __always_inline
unsigned long
_strtoul(const char *p, int base)
{
	unsigned long retval;

	errno = 0;  /* clear */
	retval = strtoul(p, NULL, base);
	if (errno == ERANGE && retval == ULONG_MAX)
		err(1, "strtoul");
	return retval;
}

static __inline __always_inline
unsigned long long
_strtoull(const char *p, int base)
{
	unsigned long long retval;

	errno = 0;  /* clear */
	retval = strtoull(p, NULL, base);
	if (errno == ERANGE && retval == ULLONG_MAX)
		err(1, "strtoull");
	return retval;
}

static int
init_btree_search(const char *arg)
{
	char *s, *p;
	int i = 0;

	bzero(&opt.base, sizeof(opt.base));
	opt.limit = 0;

	if (arg == NULL)
		return(-1);
	if (strcmp(arg, "none") == 0)
		return(-1);

	s = strdup(arg);
	if (s == NULL)
		return(-1);

	while ((p = s) != NULL) {
		if ((s = strchr(s, ':')) != NULL)
			*s++ = 0;
		if (++i == 1) {
			opt.base.localization = _strtoul(p, 16);
		} else if (i == 2) {
			opt.base.obj_id = _strtoull(p, 16);
		} else if (i == 3) {
			opt.base.rec_type = _strtoul(p, 16);
		} else if (i == 4) {
			opt.base.key = _strtoull(p, 16);
		} else if (i == 5) {
			opt.base.create_tid = _strtoull(p, 16);
			break;
		}
	}
	opt.limit = i;
	free(s);

	return(i);
}

static int
test_btree_search(hammer_btree_elm_t elm)
{
	hammer_base_elm_t base1 = &elm->base;
	hammer_base_elm_t base2 = &opt.base;
	int limit = opt.limit;

	if (base1->localization < base2->localization)
		return(-5);
	if (base1->localization > base2->localization)
		return(5);
	if (limit == 1)
		return(0);  /* ignore below */

	if (base1->obj_id < base2->obj_id)
		return(-4);
	if (base1->obj_id > base2->obj_id)
		return(4);
	if (limit == 2)
		return(0);  /* ignore below */

	if (base1->rec_type < base2->rec_type)
		return(-3);
	if (base1->rec_type > base2->rec_type)
		return(3);
	if (limit == 3)
		return(0);  /* ignore below */

	if (base1->key < base2->key)
		return(-2);
	if (base1->key > base2->key)
		return(2);
	if (limit == 4)
		return(0);  /* ignore below */

	if (base1->create_tid == 0) {
		if (base2->create_tid == 0)
			return(0);
		return(1);
	}
	if (base2->create_tid == 0)
		return(-1);
	if (base1->create_tid < base2->create_tid)
		return(-1);
	if (base1->create_tid > base2->create_tid)
		return(1);
	return(0);
}

static __inline
int
test_btree_match(hammer_btree_elm_t elm)
{
	if (test_btree_search(elm) == 0)
		return(1);
	return(0);
}

static
int
test_btree_out_of_range(hammer_btree_elm_t elm)
{
	if (test_btree_search(elm) > 0)
		return(1);  /* conditions < this elm */

	if (opt.limit >= 5) {
		if (test_btree_search(elm + 1) <= 0)
			return(1);  /* next elm <= conditions */
	} else {
		if (test_btree_search(elm + 1) < 0)
			return(1);  /* next elm < conditions */
	}
	return(0);
}

/*
 * Dump the UNDO FIFO
 */
void
hammer_cmd_show_undo(void)
{
	struct volume_info *volume;
	hammer_blockmap_t rootmap;
	hammer_off_t scan_offset;
	hammer_fifo_any_t head;
	hammer_fifo_head_t hdr;
	struct buffer_info *data_buffer = NULL;
	struct zone_stat *stats = NULL;

	volume = get_root_volume();
	rootmap = &volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];

	print_blockmap(volume);

	if (VerboseOpt)
		stats = hammer_init_zone_stat_bits();

	scan_offset = HAMMER_ENCODE_UNDO(0);
	while (scan_offset < rootmap->alloc_offset) {
		head = get_buffer_data(scan_offset, &data_buffer, 0);
		hdr = &head->head;
		printf("%016jx ", scan_offset);

		switch(hdr->hdr_type) {
		case HAMMER_HEAD_TYPE_PAD:
			printf("PAD(%04x)", hdr->hdr_size);
			break;
		case HAMMER_HEAD_TYPE_DUMMY:
			printf("DUMMY(%04x) seq=%08x",
				hdr->hdr_size, hdr->hdr_seq);
			break;
		case HAMMER_HEAD_TYPE_UNDO:
			printf("UNDO(%04x) seq=%08x dataoff=%016jx bytes=%d",
				hdr->hdr_size, hdr->hdr_seq,
				(intmax_t)head->undo.undo_offset,
				head->undo.undo_data_bytes);
			break;
		case HAMMER_HEAD_TYPE_REDO:
			printf("REDO(%04x) seq=%08x flags=%08x "
			       "objid=%016jx logoff=%016jx bytes=%d",
				hdr->hdr_size, hdr->hdr_seq,
				head->redo.redo_flags,
				(intmax_t)head->redo.redo_objid,
				(intmax_t)head->redo.redo_offset,
				head->redo.redo_data_bytes);
			break;
		default:
			printf("UNKNOWN(%04x,%04x) seq=%08x",
				hdr->hdr_type, hdr->hdr_size, hdr->hdr_seq);
			break;
		}

		if (scan_offset == rootmap->first_offset)
			printf(" >");
		if (scan_offset == rootmap->next_offset)
			printf(" <");
		printf("\n");

		if (stats)
			hammer_add_zone_stat(stats, scan_offset, hdr->hdr_size);

		if ((hdr->hdr_size & HAMMER_HEAD_ALIGN_MASK) ||
		    hdr->hdr_size == 0 ||
		    hdr->hdr_size > HAMMER_UNDO_ALIGN -
				    ((u_int)scan_offset & HAMMER_UNDO_MASK)) {
			printf("Illegal size field, skipping to "
			       "next boundary\n");
			scan_offset = HAMMER_UNDO_DOALIGN(scan_offset);
		} else {
			scan_offset += hdr->hdr_size;
		}
	}
	rel_buffer(data_buffer);

	if (stats) {
		hammer_print_zone_stat(stats);
		hammer_cleanup_zone_stat(stats);
	}
}
