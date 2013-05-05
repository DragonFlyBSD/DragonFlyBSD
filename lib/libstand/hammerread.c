/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon Schubert <corecode@fs.ei.tum.de>
 * and Matthew Dillon <dillon@backplane.com>
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

/*
 * This file is being used by boot2 and libstand (loader).
 * Compile with -DTESTING to obtain a binary.
 */


#if !defined(BOOT2) && !defined(TESTING)
#define	LIBSTAND	1
#endif

#ifdef BOOT2
#include "boot2.h"
#else
#include <sys/param.h>
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef TESTING
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#endif

#ifdef LIBSTAND
#include "stand.h"
#endif

#include <vfs/hammer/hammer_disk.h>

#ifndef BOOT2
struct blockentry {
	hammer_off_t	off;
	int		use;
	char		*data;
};

#ifdef TESTING
#define NUMCACHE	16
#else
#define	NUMCACHE	6
#endif

struct hfs {
#ifdef TESTING
	int		fd;
#else	// libstand
	struct open_file *f;
#endif
	hammer_off_t	root;
	int64_t		buf_beg;
	int64_t		last_dir_ino;
	u_int8_t	last_dir_cap_flags;
	int		lru;
	struct blockentry cache[NUMCACHE];
};

static void *
hread(struct hfs *hfs, hammer_off_t off)
{
	hammer_off_t boff = off & ~HAMMER_BUFMASK64;

	boff &= HAMMER_OFF_LONG_MASK;

	if (HAMMER_ZONE_DECODE(off) != HAMMER_ZONE_RAW_VOLUME_INDEX)
		boff += hfs->buf_beg;

	struct blockentry *be = NULL;
	for (int i = 0; i < NUMCACHE; i++) {
		if (be == NULL || be->use > hfs->cache[i].use)
			be = &hfs->cache[i];
		if (hfs->cache[i].off == boff) {
			be = &hfs->cache[i];
			break;
		}
	}
	if (be->off != boff) {
		// Didn't find any match
		be->off = boff;
#ifdef TESTING
		ssize_t res = pread(hfs->fd, be->data, HAMMER_BUFSIZE,
				    boff & HAMMER_OFF_SHORT_MASK);
		if (res != HAMMER_BUFSIZE)
			err(1, "short read on off %llx", boff);
#else	// libstand
		size_t rlen;
		int rv = hfs->f->f_dev->dv_strategy(hfs->f->f_devdata, F_READ,
			boff >> DEV_BSHIFT, HAMMER_BUFSIZE,
			be->data, &rlen);
		if (rv || rlen != HAMMER_BUFSIZE)
			return (NULL);
#endif
	}

	be->use = ++hfs->lru;
	return &be->data[off & HAMMER_BUFMASK];
}

#else	/* BOOT2 */

struct hammer_dmadat {
	struct boot2_dmadat boot2;
	char		buf[HAMMER_BUFSIZE];
};

#define fsdmadat	((struct hammer_dmadat *)boot2_dmadat)

struct hfs {
	hammer_off_t	root;
	int64_t		last_dir_ino;
	u_int8_t	last_dir_cap_flags;
	int64_t		buf_beg;
};

static void *
hread(struct hfs *hfs, hammer_off_t off)
{
	char *buf = fsdmadat->buf;

	hammer_off_t boff = off & ~HAMMER_BUFMASK64;
	boff &= HAMMER_OFF_LONG_MASK;
	if (HAMMER_ZONE_DECODE(off) != HAMMER_ZONE_RAW_VOLUME_INDEX)
		boff += hfs->buf_beg;
	boff &= HAMMER_OFF_SHORT_MASK;
	boff >>= DEV_BSHIFT;
	if (dskread(buf, boff, HAMMER_BUFSIZE >> DEV_BSHIFT))
		return (NULL);
	return (&buf[off & HAMMER_BUFMASK]);
}

static void
bzero(void *buf, size_t size)
{
	for (size_t i = 0; i < size; i++)
		((char *)buf)[i] = 0;
}

static void
bcopy(void *src, void *dst, size_t size)
{
	memcpy(dst, src, size);
}

static size_t
strlen(const char *s)
{
	size_t l = 0;
	for (; *s != 0; s++)
		l++;
	return (l);
}

static int
memcmp(const void *a, const void *b, size_t len)
{
	for (size_t p = 0; p < len; p++) {
		int r = ((const char *)a)[p] - ((const char *)b)[p];
		if (r != 0)
			return (r);
	}

	return (0);
}

#endif

/*
 * (from hammer_btree.c)
 *
 * Compare two B-Tree elements, return -N, 0, or +N (e.g. similar to strcmp).
 *
 * Note that for this particular function a return value of -1, 0, or +1
 * can denote a match if create_tid is otherwise discounted.  A create_tid
 * of zero is considered to be 'infinity' in comparisons.
 *
 * See also hammer_rec_rb_compare() and hammer_rec_cmp() in hammer_object.c.
 */
static int
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

	/*
	 * A create_tid of zero indicates a record which is undeletable
	 * and must be considered to have a value of positive infinity.
	 */
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

/*
 * Heuristical search for the first element whos comparison is <= 1.  May
 * return an index whos compare result is > 1 but may only return an index
 * whos compare result is <= 1 if it is the first element with that result.
 */
static int
hammer_btree_search_node(hammer_base_elm_t elm, hammer_node_ondisk_t node)
{
	int b;
	int s;
	int i;
	int r;

	/*
	 * Don't bother if the node does not have very many elements
	 */
	b = 0;
	s = node->count;
	while (s - b > 4) {
		i = b + (s - b) / 2;
		r = hammer_btree_cmp(elm, &node->elms[i].leaf.base);
		if (r <= 1) {
			s = i;
		} else {
			b = i;
		}
	}
	return(b);
}

#if 0
/*
 * (from hammer_subs.c)
 *
 * Return a namekey hash.   The 64 bit namekey hash consists of a 32 bit
 * crc in the MSB and 0 in the LSB.  The caller will use the low bits to
 * generate a unique key and will scan all entries with the same upper
 * 32 bits when issuing a lookup.
 *
 * We strip bit 63 in order to provide a positive key, this way a seek
 * offset of 0 will represent the base of the directory.
 *
 * This function can never return 0.  We use the MSB-0 space to synthesize
 * artificial directory entries such as "." and "..".
 */
static int64_t
hammer_directory_namekey(const void *name, int len)
{
	int64_t key;

	key = (int64_t)(crc32(name, len) & 0x7FFFFFFF) << 32;
	if (key == 0)
		key |= 0x100000000LL;
	return(key);
}
#else
static int64_t
hammer_directory_namekey(const void *name __unused, int len __unused)
{
	return (0);
}
#endif


#ifndef BOOT2
/*
 * Misc
 */
static u_int32_t
hammer_to_unix_xid(uuid_t *uuid)
{
	return(*(u_int32_t *)&uuid->node[2]);
}

static int
hammer_get_dtype(u_int8_t obj_type)
{
	switch(obj_type) {
	case HAMMER_OBJTYPE_DIRECTORY:
		return(DT_DIR);
	case HAMMER_OBJTYPE_REGFILE:
		return(DT_REG);
	case HAMMER_OBJTYPE_DBFILE:
		return(DT_DBF);
	case HAMMER_OBJTYPE_FIFO:
		return(DT_FIFO);
	case HAMMER_OBJTYPE_SOCKET:
		return(DT_SOCK);
	case HAMMER_OBJTYPE_CDEV:
		return(DT_CHR);
	case HAMMER_OBJTYPE_BDEV:
		return(DT_BLK);
	case HAMMER_OBJTYPE_SOFTLINK:
		return(DT_LNK);
	default:
		return(DT_UNKNOWN);
	}
	/* not reached */
}

static int
hammer_get_mode(u_int8_t obj_type)
{
	switch(obj_type) {
	case HAMMER_OBJTYPE_DIRECTORY:
		return(S_IFDIR);
	case HAMMER_OBJTYPE_REGFILE:
		return(S_IFREG);
	case HAMMER_OBJTYPE_DBFILE:
		return(S_IFDB);
	case HAMMER_OBJTYPE_FIFO:
		return(S_IFIFO);
	case HAMMER_OBJTYPE_SOCKET:
		return(S_IFSOCK);
	case HAMMER_OBJTYPE_CDEV:
		return(S_IFCHR);
	case HAMMER_OBJTYPE_BDEV:
		return(S_IFBLK);
	case HAMMER_OBJTYPE_SOFTLINK:
		return(S_IFLNK);
	default:
		return(0);
	}
	/* not reached */
}

#if DEBUG > 1
static void
hprintb(hammer_base_elm_t e)
{
	printf("%d/", e->localization);
	if (e->obj_id >> 32 != 0)
		printf("%lx%08lx",
		       (long)(e->obj_id >> 32),
		       (long)(e->obj_id & 0xffffffff));
	else
		printf("%lx", (long)e->obj_id);
	printf("/%d/", e->rec_type);
	if (e->key >> 32 != 0)
		printf("%lx%08lx",
		       (long)(e->key >> 32),
		       (long)(e->key & 0xffffffff));
	else
		printf("%lx", (long)e->key);
#ifdef TESTING
	printf("/%llx/%llx", e->create_tid, e->delete_tid);
#endif
}
#endif /* DEBUG > 1 */
#endif /* !BOOT2 */

static hammer_btree_leaf_elm_t
hfind(struct hfs *hfs, hammer_base_elm_t key, hammer_base_elm_t end)
{
#if DEBUG > 1
	printf("searching for ");
	hprintb(key);
	printf(" end ");
	hprintb(end);
	printf("\n");
#endif

	int n;
	int r;
	struct hammer_base_elm search = *key;
	struct hammer_base_elm backtrack;
	hammer_off_t nodeoff = hfs->root;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t e = NULL;
	int internal;

loop:
	node = hread(hfs, nodeoff);
	if (node == NULL)
		return (NULL);
	internal = node->type == HAMMER_BTREE_TYPE_INTERNAL;

#if DEBUG > 3
	for (int i = 0; i < node->count; i++) {
		printf("E: ");
		hprintb(&node->elms[i].base);
		printf("\n");
	}
	if (internal) {
		printf("B: ");
		hprintb(&node->elms[node->count].base);
		printf("\n");
	}
#endif

	n = hammer_btree_search_node(&search, node);

	// In internal nodes, we cover the right boundary as well.
	// If we hit it, we'll backtrack.
	for (; n < node->count + internal; n++) {
		e = &node->elms[n];
		r = hammer_btree_cmp(&search, &e->base);

		if (r < 0)
			break;
	}

	// unless we stopped right on the left side, we need to back off a bit
	if (n > 0)
		e = &node->elms[--n];

#if DEBUG > 2
	printf("  found: ");
	hprintb(&e->base);
	printf("\n");
#endif

	if (internal) {
		// If we hit the right boundary, backtrack to
		// the next higher level.
		if (n == node->count)
			goto backtrack;
		nodeoff = e->internal.subtree_offset;
		backtrack = (e+1)->base;
		goto loop;
	}

	r = hammer_btree_cmp(key, &e->base);
	// If we're more off than the createtid, take the next elem
	if (r > 1) {
		e++;
		n++;
	}

	// Skip deleted elements
	while (n < node->count && e->base.delete_tid != 0) {
		e++;
		n++;
	}

	// In the unfortunate event when there is no next
	// element in this node, we repeat the search with
	// a key beyond the right boundary
	if (n == node->count) {
backtrack:
		search = backtrack;
		nodeoff = hfs->root;

#if DEBUG > 2
		printf("hit right boundary (%d), resetting search to ",
		       node->count);
		hprintb(&search);
		printf("\n");
#endif
		goto loop;
	}

#if DEBUG > 1
	printf("  result: ");
	hprintb(&e->base);
	printf("\n");
#endif

	if (end != NULL)
		if (hammer_btree_cmp(end, &e->base) < -1)
			goto fail;

	return (&e->leaf);

fail:
#if DEBUG > 1
	printf("  fail.\n");
#endif
	return (NULL);
}

/*
 * Returns the directory entry localization field based on the directory
 * inode's capabilities.
 */
static u_int32_t
hdirlocalization(struct hfs *hfs, ino_t ino)
{
	struct hammer_base_elm key;

	if (ino != hfs->last_dir_ino) {
		bzero(&key, sizeof(key));
		key.obj_id = ino;
		key.localization = HAMMER_LOCALIZE_INODE;
		key.rec_type = HAMMER_RECTYPE_INODE;
		hammer_btree_leaf_elm_t e;
		hammer_data_ondisk_t ed;

		e = hfind(hfs, &key, &key);
		if (e) {
			ed = hread(hfs, e->data_offset);
			if (ed) {
				hfs->last_dir_ino = ino;
				hfs->last_dir_cap_flags = ed->inode.cap_flags;
			} else {
				printf("hdirlocal: no inode data for %llx\n",
					(long long)ino);
			}
		} else {
			printf("hdirlocal: no inode entry for %llx\n",
				(long long)ino);
		}
	}
	if (hfs->last_dir_cap_flags & HAMMER_INODE_CAP_DIR_LOCAL_INO)
		return(HAMMER_LOCALIZE_INODE);
	else
		return(HAMMER_LOCALIZE_MISC);
}

#ifndef BOOT2
static int
hreaddir(struct hfs *hfs, ino_t ino, int64_t *off, struct dirent *de)
{
	struct hammer_base_elm key, end;

#if DEBUG > 2
	printf("%s(%llx, %lld)\n", __func__, (long long)ino, *off);
#endif

	bzero(&key, sizeof(key));
	key.obj_id = ino;
	key.localization = hdirlocalization(hfs, ino);
	key.rec_type = HAMMER_RECTYPE_DIRENTRY;
	key.key = *off;

	end = key;
	end.key = HAMMER_MAX_KEY;

	hammer_btree_leaf_elm_t e;

	e = hfind(hfs, &key, &end);
	if (e == NULL) {
		errno = ENOENT;
		return (-1);
	}

	*off = e->base.key + 1;		// remember next pos

	de->d_namlen = e->data_len - HAMMER_ENTRY_NAME_OFF;
	de->d_type = hammer_get_dtype(e->base.obj_type);
	hammer_data_ondisk_t ed = hread(hfs, e->data_offset);
	if (ed == NULL)
		return (-1);
	de->d_ino = ed->entry.obj_id;
	bcopy(ed->entry.name, de->d_name, de->d_namlen);
	de->d_name[de->d_namlen] = 0;

	return (0);
}
#endif

static ino_t
hresolve(struct hfs *hfs, ino_t dirino, const char *name)
{
	struct hammer_base_elm key, end;
	size_t namel = strlen(name);

#if DEBUG > 2
	printf("%s(%llx, %s)\n", __func__, (long long)dirino, name);
#endif

	bzero(&key, sizeof(key));
	key.obj_id = dirino;
	key.localization = hdirlocalization(hfs, dirino);
	key.key = hammer_directory_namekey(name, namel);
	key.rec_type = HAMMER_RECTYPE_DIRENTRY;
	end = key;
	end.key = HAMMER_MAX_KEY;

	hammer_btree_leaf_elm_t e;
	while ((e = hfind(hfs, &key, &end)) != NULL) {
		key.key = e->base.key + 1;

		size_t elen = e->data_len - HAMMER_ENTRY_NAME_OFF;
		hammer_data_ondisk_t ed = hread(hfs, e->data_offset);
		if (ed == NULL)
			return (-1);
#ifdef BOOT2
		if (ls) {
			for (int i = 0; i < elen; i++)
				putchar(ed->entry.name[i]);
			putchar(' ');
			ls = 2;
			continue;
		}
#endif
		if (elen == namel && memcmp(ed->entry.name, name, MIN(elen, namel)) == 0)
			return (ed->entry.obj_id);
	}

#if BOOT2
	if (ls == 2)
		printf("\n");
#endif

	return -1;
}

static ino_t
hlookup(struct hfs *hfs, const char *path)
{
#if DEBUG > 2
	printf("%s(%s)\n", __func__, path);
#endif

#ifdef BOOT2
	ls = 0;
#endif
	ino_t ino = 1;
	do {
		char name[MAXPATHLEN + 1];
		while (*path == '/')
			path++;
		if (*path == 0)
			break;
		for (char *n = name; *path != 0 && *path != '/'; path++, n++) {
			n[0] = *path;
			n[1] = 0;
		}

#ifdef BOOT2
		// A single ? means "list"
		if (name[0] == '?' && name[1] == 0)
			ls = 1;
#endif

		ino = hresolve(hfs, ino, name);
	} while (ino != (ino_t)-1 && *path != 0);

	return (ino);
}


#ifndef BOOT2
static int
hstat(struct hfs *hfs, ino_t ino, struct stat* st)
{
	struct hammer_base_elm key;

#if DEBUG > 2
	printf("%s(%llx)\n", __func__, (long long)ino);
#endif

	bzero(&key, sizeof(key));
	key.obj_id = ino;
	key.localization = HAMMER_LOCALIZE_INODE;
	key.rec_type = HAMMER_RECTYPE_INODE;

	hammer_btree_leaf_elm_t e = hfind(hfs, &key, &key);
	if (e == NULL) {
#ifndef BOOT2
		errno = ENOENT;
#endif
		return -1;
	}

	hammer_data_ondisk_t ed = hread(hfs, e->data_offset);
	if (ed == NULL)
		return (-1);

	st->st_mode = ed->inode.mode | hammer_get_mode(ed->inode.obj_type);
	st->st_uid = hammer_to_unix_xid(&ed->inode.uid);
	st->st_gid = hammer_to_unix_xid(&ed->inode.gid);
	st->st_size = ed->inode.size;

	return (0);
}
#endif

static ssize_t
hreadf(struct hfs *hfs, ino_t ino, int64_t off, int64_t len, char *buf)
{
	int64_t startoff = off;
	struct hammer_base_elm key, end;

	bzero(&key, sizeof(key));
	key.obj_id = ino;
	key.localization = HAMMER_LOCALIZE_MISC;
	key.rec_type = HAMMER_RECTYPE_DATA;
	end = key;
	end.key = HAMMER_MAX_KEY;

	while (len > 0) {
		key.key = off + 1;
		hammer_btree_leaf_elm_t e = hfind(hfs, &key, &end);
		int64_t dlen;

		if (e == NULL || off > e->base.key) {
			bzero(buf, len);
			off += len;
			len = 0;
			break;
		}

		int64_t doff = e->base.key - e->data_len;
		if (off < doff) {
			// sparse file, beginning
			dlen = doff - off;
			dlen = MIN(dlen, len);
			bzero(buf, dlen);
		} else {
			int64_t boff = off - doff;
			hammer_off_t roff = e->data_offset;

			dlen = e->data_len;
			dlen -= boff;
			dlen = MIN(dlen, len);

			while (boff >= HAMMER_BUFSIZE) {
				boff -= HAMMER_BUFSIZE;
				roff += HAMMER_BUFSIZE;
			}

			/*
			 * boff - relative offset in disk buffer (not aligned)
			 * roff - base offset of disk buffer     (not aligned)
			 * dlen - amount of data we think we can copy
			 *
			 * hread only reads 16K aligned buffers, check for
			 * a length overflow and truncate dlen appropriately.
			 */
			if ((roff & ~HAMMER_BUFMASK64) != ((roff + boff + dlen - 1) & ~HAMMER_BUFMASK64))
				dlen = HAMMER_BUFSIZE - ((boff + roff) & HAMMER_BUFMASK);
			char *data = hread(hfs, roff);
			if (data == NULL)
				return (-1);
			bcopy(data + boff, buf, dlen);
		}

		buf += dlen;
		off += dlen;
		len -= dlen;
	}

	return (off - startoff);
}

#ifdef BOOT2
struct hfs hfs;

static int
boot2_hammer_init(void)
{
	hammer_volume_ondisk_t volhead;

	volhead = hread(&hfs, HAMMER_ZONE_ENCODE(1, 0));
	if (volhead == NULL)
		return (-1);
	if (volhead->vol_signature != HAMMER_FSBUF_VOLUME)
		return (-1);
	hfs.root = volhead->vol0_btree_root;
	hfs.buf_beg = volhead->vol_buf_beg;
	return (0);
}

static boot2_ino_t
boot2_hammer_lookup(const char *path)
{
	ino_t ino = hlookup(&hfs, path);

	if (ino == -1)
		ino = 0;

	fs_off = 0;

	return (ino);
}

static ssize_t
boot2_hammer_read(boot2_ino_t ino, void *buf, size_t len)
{
	ssize_t rlen = hreadf(&hfs, ino, fs_off, len, buf);
	if (rlen != -1)
		fs_off += rlen;
	return (rlen);
}

const struct boot2_fsapi boot2_hammer_api = {
	.fsinit = boot2_hammer_init,
	.fslookup = boot2_hammer_lookup,
	.fsread = boot2_hammer_read
};

#endif

#ifndef BOOT2
static int
hinit(struct hfs *hfs)
{
#if DEBUG
	printf("hinit\n");
#endif
	for (int i = 0; i < NUMCACHE; i++) {
		hfs->cache[i].data = malloc(HAMMER_BUFSIZE);
		hfs->cache[i].off = -1;	// invalid
		hfs->cache[i].use = 0;

#if DEBUG
		if (hfs->cache[i].data == NULL)
			printf("malloc failed\n");
#endif
	}
	hfs->lru = 0;
	hfs->last_dir_ino = -1;

	hammer_volume_ondisk_t volhead = hread(hfs, HAMMER_ZONE_ENCODE(1, 0));

#ifdef TESTING
	if (volhead) {
		printf("signature: %svalid\n",
		       volhead->vol_signature != HAMMER_FSBUF_VOLUME ?
				"in" :
				"");
		printf("name: %s\n", volhead->vol_name);
	}
#endif

	if (volhead == NULL || volhead->vol_signature != HAMMER_FSBUF_VOLUME) {
		for (int i = 0; i < NUMCACHE; i++) {
			free(hfs->cache[i].data);
			hfs->cache[i].data = NULL;
		}
		errno = ENODEV;
		return (-1);
	}

	hfs->root = volhead->vol0_btree_root;
	hfs->buf_beg = volhead->vol_buf_beg;

	return (0);
}

static void
hclose(struct hfs *hfs)
{
#if DEBUG
	printf("hclose\n");
#endif
	for (int i = 0; i < NUMCACHE; i++) {
		if (hfs->cache[i].data) {
			free(hfs->cache[i].data);
			hfs->cache[i].data = NULL;
		}
	}
}
#endif

#ifdef LIBSTAND
struct hfile {
	struct hfs	hfs;
	ino_t		ino;
	int64_t		fsize;
};

static int
hammer_open(const char *path, struct open_file *f)
{
	struct hfile *hf = malloc(sizeof(*hf));

	bzero(hf, sizeof(*hf));
	f->f_fsdata = hf;
	hf->hfs.f = f;
	f->f_offset = 0;

	int rv = hinit(&hf->hfs);
	if (rv) {
		f->f_fsdata = NULL;
		free(hf);
		return (rv);
	}

#if DEBUG
	printf("hammer_open %s %p %ld\n", path, f);
#endif

	hf->ino = hlookup(&hf->hfs, path);
	if (hf->ino == -1)
		goto fail;

	struct stat st;
	if (hstat(&hf->hfs, hf->ino, &st) == -1)
		goto fail;
	hf->fsize = st.st_size;

#if DEBUG
	printf("	%ld\n", (long)hf->fsize);
#endif

	return (0);

fail:
#if DEBUG
	printf("hammer_open fail\n");
#endif
	f->f_fsdata = NULL;
	hclose(&hf->hfs);
	free(hf);
	return (ENOENT);
}

static int
hammer_close(struct open_file *f)
{
	struct hfile *hf = f->f_fsdata;

	f->f_fsdata = NULL;
	if (hf) {
	    hclose(&hf->hfs);
	    free(hf);
	}
	return (0);
}

static int
hammer_read(struct open_file *f, void *buf, size_t len, size_t *resid)
{
	struct hfile *hf = f->f_fsdata;

#if DEBUG
	printf("hammer_read %p %ld %ld\n", f, f->f_offset, len);
#endif

	if (f->f_offset >= hf->fsize)
		return (EINVAL);

	size_t maxlen = len;
	if (f->f_offset + len > hf->fsize)
		maxlen = hf->fsize - f->f_offset;

	ssize_t rlen = hreadf(&hf->hfs, hf->ino, f->f_offset, maxlen, buf);
	if (rlen == -1)
		return (EINVAL);

	f->f_offset += rlen;

	*resid = len - rlen;
	return (0);
}

static off_t
hammer_seek(struct open_file *f, off_t offset, int whence)
{
	struct hfile *hf = f->f_fsdata;

	switch (whence) {
	case SEEK_SET:
		f->f_offset = offset;
		break;
	case SEEK_CUR:
		f->f_offset += offset;
		break;
	case SEEK_END:
		f->f_offset = hf->fsize - offset;
		break;
	default:
		return (-1);
	}
	return (f->f_offset);
}

static int
hammer_stat(struct open_file *f, struct stat *st)
{
	struct hfile *hf = f->f_fsdata;

	return (hstat(&hf->hfs, hf->ino, st));
}

static int
hammer_readdir(struct open_file *f, struct dirent *d)
{
	struct hfile *hf = f->f_fsdata;

	int64_t off = f->f_offset;
	int rv = hreaddir(&hf->hfs, hf->ino, &off, d);
	f->f_offset = off;
	return (rv);
}

// libstand
struct fs_ops hammer_fsops = {
	"hammer",
	hammer_open,
	hammer_close,
	hammer_read,
	null_write,
	hammer_seek,
	hammer_stat,
	hammer_readdir
};
#endif	// LIBSTAND

#ifdef TESTING
int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: hammerread <dev>\n");
		return (1);
	}

	struct hfs hfs;
	hfs.fd = open(argv[1], O_RDONLY);
	if (hfs.fd == -1)
		err(1, "unable to open %s", argv[1]);

	if (hinit(&hfs) == -1)
		err(1, "invalid hammerfs");

	for (int i = 2; i < argc; i++) {
		ino_t ino = hlookup(&hfs, argv[i]);
		if (ino == (ino_t)-1) {
			warn("hlookup %s", argv[i]);
			continue;
		}

		struct stat st;
		if (hstat(&hfs, ino, &st)) {
			warn("hstat %s", argv[i]);
			continue;
		}

		printf("%s %d/%d %o %lld\n",
		       argv[i],
		       st.st_uid, st.st_gid,
		       st.st_mode, st.st_size);

		if (S_ISDIR(st.st_mode)) {
			int64_t off = 0;
			struct dirent de;
			while (hreaddir(&hfs, ino, &off, &de) == 0) {
				printf("%s %d %llx\n",
				       de.d_name, de.d_type, de.d_ino);
			}
		} else if (S_ISREG(st.st_mode)) {
			char *buf = malloc(100000);
			int64_t off = 0;
			while (off < st.st_size) {
				int64_t len = MIN(100000, st.st_size - off);
				int64_t rl = hreadf(&hfs, ino, off, len, buf);
				fwrite(buf, rl, 1, stdout);
				off += rl;
			}
			free(buf);
		}
	}

	return 0;
}
#endif
