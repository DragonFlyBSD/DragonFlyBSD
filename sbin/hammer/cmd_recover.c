/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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

struct recover_dict {
	struct recover_dict *next;
	struct recover_dict *parent;
	int64_t	obj_id;
	uint8_t obj_type;
	uint8_t flags;
	uint16_t llid;
	int64_t	size;
	char	*name;
};

#define DICTF_MADEDIR	0x01
#define DICTF_MADEFILE	0x02
#define DICTF_PARENT	0x04	/* parent attached for real */
#define DICTF_TRAVERSED	0x80

static void recover_top(char *ptr);
static void recover_elm(hammer_btree_leaf_elm_t leaf);
static struct recover_dict *get_dict(int64_t obj_id, uint16_t llid);
static char *recover_path(struct recover_dict *dict);
static void sanitize_string(char *str);

static const char *TargetDir;
static int CachedFd = -1;
static char *CachedPath;

void
hammer_cmd_recover(const char *target_dir)
{
	struct buffer_info *data_buffer;
	struct volume_info *scan;
	struct volume_info *volume;
	hammer_off_t off;
	hammer_off_t off_end;
	char *ptr;

	AssertOnFailure = 0;
	TargetDir = target_dir;

	printf("Running raw scan of HAMMER image, recovering to %s\n",
		TargetDir);
	mkdir(TargetDir, 0777);

	data_buffer = NULL;
	TAILQ_FOREACH(scan, &VolList, entry) {
		volume = get_volume(scan->vol_no);

		off = HAMMER_ZONE_RAW_BUFFER + 0;
		off |= HAMMER_VOL_ENCODE(volume->vol_no);
		off_end = off + (volume->ondisk->vol_buf_end - volume->ondisk->vol_buf_beg);
		while (off < off_end) {
			ptr = get_buffer_data(off, &data_buffer, 0);
			if (ptr) {
				recover_top(ptr);
				off += HAMMER_BUFSIZE;
			}
		}
	}
	rel_buffer(data_buffer);

	if (CachedPath) {
		free(CachedPath);
		close(CachedFd);
		CachedPath = NULL;
		CachedFd = -1;
	}

	AssertOnFailure = 1;
}

/*
 * Top level recovery processor.  Assume the data is a B-Tree node.
 * If the CRC is good we attempt to process the node, building the
 * object space and creating the dictionary as we go.
 */
static void
recover_top(char *ptr)
{
	struct hammer_node_ondisk *node;
	hammer_btree_elm_t elm;
	int maxcount;
	int i;

	for (node = (void *)ptr; (char *)node < ptr + HAMMER_BUFSIZE; ++node) {
		if (crc32(&node->crc + 1, HAMMER_BTREE_CRCSIZE) ==
		    node->crc &&
		    node->type == HAMMER_BTREE_TYPE_LEAF) {
			/*
			 * Scan elements
			 */
			maxcount = HAMMER_BTREE_LEAF_ELMS;
			for (i = 0; i < node->count && i < maxcount; ++i) {
				elm = &node->elms[i];
				if (elm->base.btype != 'R')
					continue;
				recover_elm(&elm->leaf);
			}
		}
	}
}

static void
recover_elm(hammer_btree_leaf_elm_t leaf)
{
	struct buffer_info *data_buffer = NULL;
	struct recover_dict *dict;
	struct recover_dict *dict2;
	hammer_data_ondisk_t ondisk;
	hammer_off_t data_offset;
	struct stat st;
	int chunk;
	int len;
	int zfill;
	int64_t file_offset;
	uint16_t llid;
	size_t nlen;
	int fd;
	char *name;
	char *path1;
	char *path2;

	/*
	 * Ignore deleted records
	 */
	if (leaf->delete_ts)
		return;
	if ((data_offset = leaf->data_offset) != 0)
		ondisk = get_buffer_data(data_offset, &data_buffer, 0);
	else
		ondisk = NULL;
	if (ondisk == NULL)
		goto done;

	len = leaf->data_len;
	chunk = HAMMER_BUFSIZE - ((int)data_offset & HAMMER_BUFMASK);
	if (chunk > len)
		chunk = len;

	if (len < 0 || len > HAMMER_XBUFSIZE || len > chunk)
		goto done;

	llid = leaf->base.localization >> 16;

	dict = get_dict(leaf->base.obj_id, llid);

	switch(leaf->base.rec_type) {
	case HAMMER_RECTYPE_INODE:
		/*
		 * We found an inode which also tells us where the file
		 * or directory is in the directory hierarchy.
		 */
		if (VerboseOpt) {
			printf("file %016jx:%05d inode found\n",
				(uintmax_t)leaf->base.obj_id, llid);
		}
		path1 = recover_path(dict);

		/*
		 * Attach the inode to its parent.  This isn't strictly
		 * necessary because the information is also in the
		 * directory entries, but if we do not find the directory
		 * entry this ensures that the files will still be
		 * reasonably well organized in their proper directories.
		 */
		if ((dict->flags & DICTF_PARENT) == 0 &&
		    dict->obj_id != 1 && ondisk->inode.parent_obj_id != 0) {
			dict->flags |= DICTF_PARENT;
			dict->parent = get_dict(ondisk->inode.parent_obj_id,
						llid);
			if (dict->parent &&
			    (dict->parent->flags & DICTF_MADEDIR) == 0) {
				dict->parent->flags |= DICTF_MADEDIR;
				path2 = recover_path(dict->parent);
				printf("mkdir %s\n", path2);
				mkdir(path2, 0777);
				free(path2);
				path2 = NULL;
			}
		}
		if (dict->obj_type == 0)
			dict->obj_type = ondisk->inode.obj_type;
		dict->size = ondisk->inode.size;
		path2 = recover_path(dict);

		if (lstat(path1, &st) == 0) {
			if (ondisk->inode.obj_type == HAMMER_OBJTYPE_REGFILE) {
				truncate(path1, dict->size);
				/* chmod(path1, 0666); */
			}
			if (strcmp(path1, path2)) {
				printf("Rename %s -> %s\n", path1, path2);
				rename(path1, path2);
			}
		} else if (ondisk->inode.obj_type == HAMMER_OBJTYPE_REGFILE) {
			printf("mkinode (file) %s\n", path2);
			fd = open(path2, O_RDWR|O_CREAT, 0666);
			if (fd > 0)
				close(fd);
		} else if (ondisk->inode.obj_type == HAMMER_OBJTYPE_DIRECTORY) {
			printf("mkinode (dir) %s\n", path2);
			mkdir(path2, 0777);
			dict->flags |= DICTF_MADEDIR;
		}
		free(path1);
		free(path2);
		break;
	case HAMMER_RECTYPE_DATA:
		/*
		 * File record data
		 */
		if (leaf->base.obj_id == 0)
			break;
		if (VerboseOpt) {
			printf("file %016jx:%05d data %016jx,%d\n",
				(uintmax_t)leaf->base.obj_id,
				llid,
				(uintmax_t)leaf->base.key - len,
				len);
		}

		/*
		 * Update the dictionary entry
		 */
		if (dict->obj_type == 0)
			dict->obj_type = HAMMER_OBJTYPE_REGFILE;

		/*
		 * If the parent directory has not been created we
		 * have to create it (typically a PFS%05d)
		 */
		if (dict->parent &&
		    (dict->parent->flags & DICTF_MADEDIR) == 0) {
			dict->parent->flags |= DICTF_MADEDIR;
			path2 = recover_path(dict->parent);
			printf("mkdir %s\n", path2);
			mkdir(path2, 0777);
			free(path2);
			path2 = NULL;
		}

		/*
		 * Create the file if necessary, report file creations
		 */
		path1 = recover_path(dict);
		if (CachedPath && strcmp(CachedPath, path1) == 0) {
			fd = CachedFd;
		} else {
			fd = open(path1, O_CREAT|O_RDWR, 0666);
		}
		if (fd < 0) {
			printf("Unable to create %s: %s\n",
				path1, strerror(errno));
			free(path1);
			break;
		}
		if ((dict->flags & DICTF_MADEFILE) == 0) {
			dict->flags |= DICTF_MADEFILE;
			printf("mkfile %s\n", path1);
		}

		/*
		 * And write the record.  A HAMMER data block is aligned
		 * and may contain trailing zeros after the file EOF.  The
		 * inode record is required to get the actual file size.
		 *
		 * However, when the inode record is not available
		 * we can do a sparse write and that will get it right
		 * most of the time even if the inode record is never
		 * found.
		 */
		file_offset = (int64_t)leaf->base.key - len;
		lseek(fd, (off_t)file_offset, SEEK_SET);
		while (len) {
			if (dict->size == -1) {
				for (zfill = chunk - 1; zfill >= 0; --zfill) {
					if (((char *)ondisk)[zfill])
						break;
				}
				++zfill;
			} else {
				zfill = chunk;
			}

			if (zfill)
				write(fd, ondisk, zfill);
			if (zfill < chunk)
				lseek(fd, chunk - zfill, SEEK_CUR);

			len -= chunk;
			data_offset += chunk;
			file_offset += chunk;
			ondisk = get_buffer_data(data_offset, &data_buffer, 0);
			if (ondisk == NULL)
				break;
			chunk = HAMMER_BUFSIZE -
				((int)data_offset & HAMMER_BUFMASK);
			if (chunk > len)
				chunk = len;
		}
		if (dict->size >= 0 && file_offset > dict->size) {
			ftruncate(fd, dict->size);
			/* fchmod(fd, 0666); */
		}

		if (fd == CachedFd) {
			free(path1);
		} else if (CachedPath) {
			free(CachedPath);
			close(CachedFd);
			CachedPath = path1;
			CachedFd = fd;
		} else {
			CachedPath = path1;
			CachedFd = fd;
		}
		break;
	case HAMMER_RECTYPE_DIRENTRY:
		nlen = len - offsetof(struct hammer_entry_data, name[0]);
		if ((int)nlen < 0)	/* illegal length */
			break;
		if (ondisk->entry.obj_id == 0 || ondisk->entry.obj_id == 1)
			break;
		name = malloc(nlen + 1);
		bcopy(ondisk->entry.name, name, nlen);
		name[nlen] = 0;
		sanitize_string(name);

		/*
		 * We can't deal with hardlinks so if the object already
		 * has a name assigned to it we just keep using that name.
		 */
		dict2 = get_dict(ondisk->entry.obj_id, llid);
		path1 = recover_path(dict2);

		if (dict2->name == NULL)
			dict2->name = name;
		else
			free(name);

		/*
		 * Attach dict2 to its directory (dict), create the
		 * directory (dict) if necessary.  We must ensure
		 * that the directory entry exists in order to be
		 * able to properly rename() the file without creating
		 * a namespace conflict.
		 */
		if ((dict2->flags & DICTF_PARENT) == 0) {
			dict2->flags |= DICTF_PARENT;
			dict2->parent = dict;
			if ((dict->flags & DICTF_MADEDIR) == 0) {
				dict->flags |= DICTF_MADEDIR;
				path2 = recover_path(dict);
				printf("mkdir %s\n", path2);
				mkdir(path2, 0777);
				free(path2);
				path2 = NULL;
			}
		}
		path2 = recover_path(dict2);
		if (strcmp(path1, path2) != 0 && lstat(path1, &st) == 0) {
			printf("Rename %s -> %s\n", path1, path2);
			rename(path1, path2);
		}
		free(path1);
		free(path2);

		printf("dir  %016jx:%05d entry %016jx \"%s\"\n",
			(uintmax_t)leaf->base.obj_id,
			llid,
			(uintmax_t)ondisk->entry.obj_id,
			name);
		break;
	default:
		/*
		 * Ignore any other record types
		 */
		break;
	}
done:
	rel_buffer(data_buffer);
}

#define RD_HSIZE	32768
#define RD_HMASK	(RD_HSIZE - 1)

struct recover_dict *RDHash[RD_HSIZE];

static
struct recover_dict *
get_dict(int64_t obj_id, uint16_t llid)
{
	struct recover_dict *dict;
	int i;

	if (obj_id == 0)
		return(NULL);

	i = crc32(&obj_id, sizeof(obj_id)) & RD_HMASK;
	for (dict = RDHash[i]; dict; dict = dict->next) {
		if (dict->obj_id == obj_id &&
		    dict->llid == llid) {
			break;
		}
	}
	if (dict == NULL) {
		dict = malloc(sizeof(*dict));
		bzero(dict, sizeof(*dict));
		dict->obj_id = obj_id;
		dict->llid = llid;
		dict->next = RDHash[i];
		dict->size = -1;
		RDHash[i] = dict;

		/*
		 * Always connect dangling dictionary entries to object 1
		 * (the root of the PFS).
		 *
		 * DICTF_PARENT will not be set until we know what the
		 * real parent directory object is.
		 */
		if (dict->obj_id != 1)
			dict->parent = get_dict(1, llid);
	}
	return(dict);
}

struct path_info {
	enum { PI_FIGURE, PI_LOAD } state;
	uint16_t llid;
	char *base;
	char *next;
	int len;
};

static void recover_path_helper(struct recover_dict *, struct path_info *);

static
char *
recover_path(struct recover_dict *dict)
{
	struct path_info info;

	bzero(&info, sizeof(info));
	info.llid = dict->llid;
	info.state = PI_FIGURE;
	recover_path_helper(dict, &info);
	info.base = malloc(info.len);
	info.next = info.base;
	info.state = PI_LOAD;
	recover_path_helper(dict, &info);

	return(info.base);
}

static
void
recover_path_helper(struct recover_dict *dict, struct path_info *info)
{
	/*
	 * Calculate path element length
	 */
	dict->flags |= DICTF_TRAVERSED;

	switch(info->state) {
	case PI_FIGURE:
		if (dict->obj_id == 1)
			info->len += 8;
		else if (dict->name)
			info->len += strlen(dict->name);
		else
			info->len += 6 + 16;
		++info->len;

		if (dict->parent &&
		    (dict->parent->flags & DICTF_TRAVERSED) == 0) {
			recover_path_helper(dict->parent, info);
		} else {
			info->len += strlen(TargetDir) + 1;
		}
		break;
	case PI_LOAD:
		if (dict->parent &&
		    (dict->parent->flags & DICTF_TRAVERSED) == 0) {
			recover_path_helper(dict->parent, info);
		} else {
			strcpy(info->next, TargetDir);
			info->next += strlen(info->next);
		}

		*info->next++ = '/';
		if (dict->obj_id == 1) {
			snprintf(info->next, 8+1, "PFS%05d", info->llid);
		} else if (dict->name) {
			strcpy(info->next, dict->name);
		} else {
			snprintf(info->next, 6+16+1, "obj_0x%016jx",
				(uintmax_t)dict->obj_id);
		}
		info->next += strlen(info->next);
		break;
	}
	dict->flags &= ~DICTF_TRAVERSED;
}

static
void
sanitize_string(char *str)
{
	while (*str) {
		if (!isprint(*str))
			*str = 'x';
		++str;
	}
}
