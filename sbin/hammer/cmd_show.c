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

#include "hammer.h"
#include <libutil.h>

#define FLAG_TOOFARLEFT		0x0001
#define FLAG_TOOFARRIGHT	0x0002
#define FLAG_BADTYPE		0x0004
#define FLAG_BADCHILDPARENT	0x0008
#define FLAG_BADMIRRORTID	0x0010

typedef struct btree_search {
	u_int32_t	lo;
	int64_t		obj_id;
} *btree_search_t;

static void print_btree_node(hammer_off_t node_offset, btree_search_t search,
			int depth, hammer_tid_t mirror_tid,
			hammer_base_elm_t left_bound,
			hammer_base_elm_t right_bound,
			int this_index);
static const char *check_data_crc(hammer_btree_elm_t elm);
static void print_record(hammer_btree_elm_t elm);
static void print_btree_elm(hammer_btree_elm_t elm, int i, u_int8_t type,
			int flags, const char *label, const char *ext);
static int get_elm_flags(hammer_node_ondisk_t node, hammer_off_t node_offset,
			hammer_btree_elm_t elm, u_int8_t btype,
			hammer_base_elm_t left_bound,
			hammer_base_elm_t right_bound);
static void print_bigblock_fill(hammer_off_t offset);

void
hammer_cmd_show(hammer_off_t node_offset, u_int32_t lo, int64_t obj_id,
		int depth,
		hammer_base_elm_t left_bound, hammer_base_elm_t right_bound)
{
	struct volume_info *volume;
	struct btree_search search;
	btree_search_t searchp;
	int zone;

	AssertOnFailure = 0;

	if (node_offset == (hammer_off_t)-1) {
		volume = get_volume(RootVolNo);
		node_offset = volume->ondisk->vol0_btree_root;
		if (QuietOpt < 3) {
			printf("Volume header\trecords=%jd next_tid=%016jx\n",
			       (intmax_t)volume->ondisk->vol0_stat_records,
			       (uintmax_t)volume->ondisk->vol0_next_tid);
			printf("\t\tbufoffset=%016jx\n",
			       (uintmax_t)volume->ondisk->vol_buf_beg);
			for (zone = 0; zone < HAMMER_MAX_ZONES; ++zone) {
				printf("\t\tzone %d\tnext_offset=%016jx\n",
					zone,
					(uintmax_t)volume->ondisk->vol0_blockmap[zone].next_offset
				);
			}
		}
		rel_volume(volume);
	}

	if (lo == 0 && obj_id == (int64_t)HAMMER_MIN_OBJID) {
		searchp = NULL;
		printf("show %016jx depth %d\n", (uintmax_t)node_offset, depth);
	} else {
		search.lo = lo;
		search.obj_id = obj_id;
		searchp = &search;
		printf("show %016jx lo %08x obj_id %016jx depth %d\n",
			(uintmax_t)node_offset, lo, (uintmax_t)obj_id, depth);
	}
	print_btree_node(node_offset, searchp, depth, HAMMER_MAX_TID,
			 left_bound, right_bound, -1);

	AssertOnFailure = 1;
}

static void
print_btree_node(hammer_off_t node_offset, btree_search_t search,
		int depth, hammer_tid_t mirror_tid,
		hammer_base_elm_t left_bound, hammer_base_elm_t right_bound,
		int this_index)
{
	struct buffer_info *buffer = NULL;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int i;
	int flags;
	int maxcount;
	char badc;
	char badm;
	const char *ext;

	node = get_node(node_offset, &buffer);

	if (node == NULL) {
		printf("BI   NODE %016jx (IO ERROR)\n",
		       (uintmax_t)node_offset);
		return;
	}

	if (crc32(&node->crc + 1, HAMMER_BTREE_CRCSIZE) == node->crc)
		badc = ' ';
	else
		badc = 'B';

	/*
	 * Workaround for the root split that is not an error
	 */
	if (this_index == 0 && depth == 1 && mirror_tid == 0) {
		badm = ' ';  /* could use unique mark */
	} else {
		if (node->mirror_tid <= mirror_tid) {
			badm = ' ';
		} else {
			badm = 'M';
			badc = 'B';
		}
	}

	printf("%c%c   NODE %016jx cnt=%02d p=%016jx "
	       "type=%c depth=%d",
	       badc,
	       badm,
	       (uintmax_t)node_offset, node->count,
	       (uintmax_t)node->parent,
	       (node->type ? node->type : '?'), depth);
	printf(" mirror %016jx", (uintmax_t)node->mirror_tid);
	if (QuietOpt < 3) {
		printf(" fill=");
		print_bigblock_fill(node_offset);
	}
	printf(" {\n");

	maxcount = (node->type == HAMMER_BTREE_TYPE_INTERNAL) ?
		   HAMMER_BTREE_INT_ELMS : HAMMER_BTREE_LEAF_ELMS;

	for (i = 0; i < node->count && i < maxcount; ++i) {
		elm = &node->elms[i];

		if (node->type != HAMMER_BTREE_TYPE_INTERNAL) {
			ext = NULL;
			if (search &&
			    elm->base.localization == search->lo &&
			     elm->base.obj_id == search->obj_id) {
				ext = " *";
			}
		} else if (search) {
			ext = " *";
			if (elm->base.localization > search->lo ||
			    (elm->base.localization == search->lo &&
			     elm->base.obj_id > search->obj_id)) {
				ext = NULL;
			}
			if (elm[1].base.localization < search->lo ||
			    (elm[1].base.localization == search->lo &&
			     elm[1].base.obj_id < search->obj_id)) {
				ext = NULL;
			}
		} else {
			ext = NULL;
		}

		flags = get_elm_flags(node, node_offset,
					elm, elm->base.btype,
					left_bound, right_bound);
		print_btree_elm(elm, i, node->type, flags, "ELM", ext);
	}
	if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
		elm = &node->elms[i];

		flags = get_elm_flags(node, node_offset,
					elm, 'I',
					left_bound, right_bound);
		print_btree_elm(elm, i, node->type, flags, "RBN", NULL);
	}
	printf("     }\n");

	for (i = 0; i < node->count; ++i) {
		elm = &node->elms[i];

		switch(node->type) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			if (search) {
				if (elm->base.localization > search->lo ||
				    (elm->base.localization == search->lo &&
				     elm->base.obj_id > search->obj_id)) {
					break;
				}
				if (elm[1].base.localization < search->lo ||
				    (elm[1].base.localization == search->lo &&
				     elm[1].base.obj_id < search->obj_id)) {
					break;
				}
			}
			if (elm->internal.subtree_offset) {
				print_btree_node(elm->internal.subtree_offset,
						 search, depth + 1,
						 elm->internal.mirror_tid,
						 &elm[0].base, &elm[1].base, i);
				/*
				 * Cause show to iterate after seeking to
				 * the lo:objid
				 */
				search = NULL;
			}
			break;
		default:
			break;
		}
	}
	rel_buffer(buffer);
}

static
void
print_btree_elm(hammer_btree_elm_t elm, int i, u_int8_t type,
		int flags, const char *label, const char *ext)
{
	char flagstr[8] = { 0, '-', '-', '-', '-', '-', '-', 0 };
	char deleted;

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

	/*
	 * Workaround for the root split that is not actual delete
	 */
	if (elm->base.delete_tid) {
		if (type == HAMMER_BTREE_TYPE_INTERNAL &&
		    i == 0 &&
		    elm->base.create_tid == 1 &&
		    elm->base.delete_tid == 1)
			deleted = ' ';  /* could use unique mark */
		else
			deleted = 'd';
	} else {
		deleted = ' ';
	}

	printf("%s\t%s %2d %c ",
	       flagstr, label, i,
	       (elm->base.btype ? elm->base.btype : '?'));
	printf("lo=%08x obj=%016jx rt=%02x key=%016jx ot=%02x\n",
	       elm->base.localization,
	       (uintmax_t)elm->base.obj_id,
	       elm->base.rec_type,
	       (uintmax_t)elm->base.key,
	       elm->base.obj_type);
	printf("\t       %c tids %016jx:%016jx ",
	       deleted,
	       (uintmax_t)elm->base.create_tid,
	       (uintmax_t)elm->base.delete_tid);

	switch(type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		printf("suboff=%016jx",
		       (uintmax_t)elm->internal.subtree_offset);
		if (QuietOpt < 3) {
			printf(" mirror %016jx",
			       (uintmax_t)elm->internal.mirror_tid);
		}
		if (ext)
			printf(" %s", ext);
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		if (ext)
			printf(" %s", ext);
		switch(elm->base.btype) {
		case HAMMER_BTREE_TYPE_RECORD:
			if (QuietOpt < 3)
				printf("\n%s\t         ", check_data_crc(elm));
			else
				printf("\n\t         ");
			printf("dataoff=%016jx/%d",
			       (uintmax_t)elm->leaf.data_offset,
			       elm->leaf.data_len);
			if (QuietOpt < 3) {
				printf(" crc=%04x", elm->leaf.data_crc);
				printf("\n\t         fills=");
				print_bigblock_fill(elm->leaf.data_offset);
			}
			if (QuietOpt < 2)
				print_record(elm);
			break;
		}
		break;
	default:
		break;
	}
	printf("\n");
}

static
int
get_elm_flags(hammer_node_ondisk_t node, hammer_off_t node_offset,
		hammer_btree_elm_t elm, u_int8_t btype,
		hammer_base_elm_t left_bound, hammer_base_elm_t right_bound)
{
	int flags = 0;

	switch(node->type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		if (elm->internal.subtree_offset) {
			struct buffer_info *buffer = NULL;
			hammer_node_ondisk_t subnode;

			subnode = get_node(elm->internal.subtree_offset,
					   &buffer);
			if (subnode == NULL)
				flags |= FLAG_BADCHILDPARENT;
			else if (subnode->parent != node_offset)
				flags |= FLAG_BADCHILDPARENT;
			rel_buffer(buffer);
		}
		if (elm->internal.mirror_tid > node->mirror_tid)
			flags |= FLAG_BADMIRRORTID;

		switch(btype) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			if (left_bound == NULL || right_bound == NULL)
				break;
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) > 0)
				flags |= FLAG_TOOFARRIGHT;
			break;
		case HAMMER_BTREE_TYPE_LEAF:
			if (left_bound == NULL || right_bound == NULL)
				break;
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) >= 0)
				flags |= FLAG_TOOFARRIGHT;
			break;
		default:
			flags |= FLAG_BADTYPE;
			break;
		}
		break;
	case HAMMER_BTREE_TYPE_LEAF:
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
		switch(btype) {
		case HAMMER_BTREE_TYPE_RECORD:
			if (left_bound == NULL || right_bound == NULL)
				break;
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) >= 0)
				flags |= FLAG_TOOFARRIGHT;
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

static
void
print_bigblock_fill(hammer_off_t offset)
{
	struct hammer_blockmap_layer1 layer1;
	struct hammer_blockmap_layer2 layer2;
	int fill;
	int error;

	blockmap_lookup(offset, &layer1, &layer2, &error);
	if (error) {
		printf("z%d:%lld=BADZ",
			HAMMER_ZONE_DECODE(offset),
			(offset & ~HAMMER_OFF_ZONE_MASK) /
			    HAMMER_BIGBLOCK_SIZE
		);
	} else {
		fill = layer2.bytes_free * 100 / HAMMER_BIGBLOCK_SIZE;
		fill = 100 - fill;

		printf("z%d:%lld=%d%%",
			HAMMER_ZONE_DECODE(offset),
			(offset & ~HAMMER_OFF_ZONE_MASK) /
			    HAMMER_BIGBLOCK_SIZE,
			fill
		);
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
	int32_t data_len;
	int32_t len;
	u_int32_t crc;
	int error;
	char *ptr;

	data_offset = elm->leaf.data_offset;
	data_len = elm->leaf.data_len;
	data_buffer = NULL;
	if (data_offset == 0 || data_len == 0)
		return("Z");

	crc = 0;
	error = 0;
	while (data_len) {
		blockmap_lookup(data_offset, NULL, NULL, &error);
		if (error)
			break;

		ptr = get_buffer_data(data_offset, &data_buffer, 0);
		len = HAMMER_BUFSIZE - ((int)data_offset & HAMMER_BUFMASK);
		if (len > data_len)
			len = (int)data_len;
		if (elm->leaf.base.rec_type == HAMMER_RECTYPE_INODE &&
		    data_len == sizeof(struct hammer_inode_data)) {
			crc = crc32_ext(ptr, HAMMER_INODE_CRCSIZE, crc);
		} else {
			crc = crc32_ext(ptr, len, crc);
		}
		data_len -= len;
		data_offset += len;
	}
	if (data_buffer)
		rel_buffer(data_buffer);
	if (error)
		return("BO");		/* bad offset */
	if (crc == elm->leaf.data_crc)
		return("");
	return("BX");			/* bad crc */
}

static
void
print_config(char *cfgtxt)
{
	char *token;

	printf("\n%17stext=\"\n", "");
	while((token = strsep(&cfgtxt, "\r\n")) != NULL) {
		printf("%17s  %s\n", "", token);
	}
	printf("%17s\"", "");
}

static
void
print_record(hammer_btree_elm_t elm)
{
	struct buffer_info *data_buffer;
	hammer_off_t data_offset;
	int32_t data_len;
	hammer_data_ondisk_t data;
	u_int32_t status;
	char *str1 = NULL;
	char *str2 = NULL;

	data_offset = elm->leaf.data_offset;
	data_len = elm->leaf.data_len;
	data_buffer = NULL;

	if (data_offset)
		data = get_buffer_data(data_offset, &data_buffer, 0);
	else
		data = NULL;

	switch(elm->leaf.base.rec_type) {
	case HAMMER_RECTYPE_UNKNOWN:
		printf("\n%17s", "");
		printf("unknown");
		break;
	case HAMMER_RECTYPE_INODE:
		printf("\n%17s", "");
		printf("size=%jd nlinks=%jd",
		       (intmax_t)data->inode.size,
		       (intmax_t)data->inode.nlinks);
		if (QuietOpt < 1) {
			printf(" mode=%05o uflags=%08x\n",
				data->inode.mode,
				data->inode.uflags);
			printf("%17s", "");
			printf("ctime=%016jx pobjid=%016jx obj_type=%d\n",
				(uintmax_t)data->inode.ctime,
				(uintmax_t)data->inode.parent_obj_id,
				data->inode.obj_type);
			printf("%17s", "");
			printf("mtime=%016jx", (uintmax_t)data->inode.mtime);
			printf(" caps=%02x", data->inode.cap_flags);
		}
		break;
	case HAMMER_RECTYPE_DIRENTRY:
		printf("\n%17s", "");
		data_len -= HAMMER_ENTRY_NAME_OFF;
		printf("dir-entry ino=%016jx lo=%08x name=\"%*.*s\"",
		       (uintmax_t)data->entry.obj_id,
		       data->entry.localization,
		       data_len, data_len, data->entry.name);
		break;
	case HAMMER_RECTYPE_FIX:
		switch(elm->leaf.base.key) {
		case HAMMER_FIXKEY_SYMLINK:
			data_len -= HAMMER_SYMLINK_NAME_OFF;
			printf("\n%17s", "");
			printf("symlink=\"%*.*s\"", data_len, data_len,
				data->symlink.name);
			break;
		default:
			break;
		}
		break;
	case HAMMER_RECTYPE_PFS:
		printf("\n%17s", "");
		printf("sync_beg_tid=%016jx sync_end_tid=%016jx\n",
			(intmax_t)data->pfsd.sync_beg_tid,
			(intmax_t)data->pfsd.sync_end_tid);
		uuid_to_string(&data->pfsd.shared_uuid, &str1, &status);
		uuid_to_string(&data->pfsd.unique_uuid, &str2, &status);
		printf("%17s", "");
		printf("shared_uuid=%s\n", str1);
		printf("%17s", "");
		printf("unique_uuid=%s\n", str2);
		printf("%17s", "");
		printf("mirror_flags=%08x label=\"%s\"",
			data->pfsd.mirror_flags, data->pfsd.label);
		if (data->pfsd.snapshots[0])
			printf(" snapshots=\"%s\"", data->pfsd.snapshots);
		free(str1);
		free(str2);
		break;
	case HAMMER_RECTYPE_SNAPSHOT:
		printf("\n%17s", "");
		printf("tid=%016jx label=\"%s\"",
			(intmax_t)data->snap.tid, data->snap.label);
		break;
	case HAMMER_RECTYPE_CONFIG:
		if (VerboseOpt > 2) {
			print_config(data->config.text);
		}
		break;
	case HAMMER_RECTYPE_DATA:
		if (VerboseOpt > 3) {
			printf("\n");
			hexdump(data, data_len, "\t\t  ", 0);
		}
		break;
	case HAMMER_RECTYPE_EXT:
	case HAMMER_RECTYPE_DB:
		if (VerboseOpt > 2) {
			printf("\n");
			hexdump(data, data_len, "\t\t  ", 0);
		}
		break;
	default:
		break;
	}
	if (data_buffer)
		rel_buffer(data_buffer);
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
	struct buffer_info *data_buffer = NULL;

	volume = get_volume(RootVolNo);
	rootmap = &volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	printf("Volume header UNDO %016jx-%016jx/%016jx\n",
		(intmax_t)rootmap->first_offset,
		(intmax_t)rootmap->next_offset,
		(intmax_t)rootmap->alloc_offset);
	printf("Undo map is %jdMB\n",
		(intmax_t)((rootmap->alloc_offset & HAMMER_OFF_LONG_MASK) /
			   (1024 * 1024)));
	scan_offset = HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);
	while (scan_offset < rootmap->alloc_offset) {
		head = get_buffer_data(scan_offset, &data_buffer, 0);
		printf("%016jx ", scan_offset);

		switch(head->head.hdr_type) {
		case HAMMER_HEAD_TYPE_PAD:
			printf("PAD(%04x)\n", head->head.hdr_size);
			break;
		case HAMMER_HEAD_TYPE_DUMMY:
			printf("DUMMY(%04x) seq=%08x\n",
				head->head.hdr_size, head->head.hdr_seq);
			break;
		case HAMMER_HEAD_TYPE_UNDO:
			printf("UNDO(%04x) seq=%08x "
			       "dataoff=%016jx bytes=%d\n",
				head->head.hdr_size, head->head.hdr_seq,
				(intmax_t)head->undo.undo_offset,
				head->undo.undo_data_bytes);
			break;
		case HAMMER_HEAD_TYPE_REDO:
			printf("REDO(%04x) seq=%08x flags=%08x "
			       "objid=%016jx logoff=%016jx bytes=%d\n",
				head->head.hdr_size, head->head.hdr_seq,
				head->redo.redo_flags,
				(intmax_t)head->redo.redo_objid,
				(intmax_t)head->redo.redo_offset,
				head->redo.redo_data_bytes);
			break;
		default:
			printf("UNKNOWN(%04x,%04x) seq=%08x\n",
				head->head.hdr_type,
				head->head.hdr_size,
				head->head.hdr_seq);
			break;
		}
		if ((head->head.hdr_size & HAMMER_HEAD_ALIGN_MASK) ||
		    head->head.hdr_size == 0 ||
		    head->head.hdr_size > HAMMER_UNDO_ALIGN -
				    ((u_int)scan_offset & HAMMER_UNDO_MASK)) {
			printf("Illegal size field, skipping to "
			       "next boundary\n");
			scan_offset = (scan_offset + HAMMER_UNDO_MASK) &
					~HAMMER_UNDO_MASK64;
		} else {
			scan_offset += head->head.hdr_size;
		}
	}
	if (data_buffer)
		rel_buffer(data_buffer);
}
