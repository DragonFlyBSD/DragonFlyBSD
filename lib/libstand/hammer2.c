/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

#if !defined(BOOT2) && !defined(TESTING)
#define LIBSTAND        1
#endif

#ifdef BOOT2
#include "boot2.h"
#endif

#ifdef TESTING
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#endif

#ifdef LIBSTAND
#include "stand.h"
#endif

#include <vfs/hammer2/hammer2_disk.h>

uint32_t iscsi_crc32(const void *buf, size_t size);
uint32_t iscsi_crc32_ext(const void *buf, size_t size, uint32_t ocrc);

static hammer2_media_data_t media;
static hammer2_blockref_t saved_base;

#define hammer2_icrc32(buf, size)	iscsi_crc32(buf, size)

struct hammer2_fs {
	hammer2_blockref_t		sroot;
	hammer2_blockset_t		sroot_blockset;
#if defined(TESTING)
	int 				fd;
#elif defined(LIBSTAND)
	struct open_file		*f;
#elif defined(BOOT2)
	/* BOOT2 doesn't use a descriptor */
#else
#error "hammer2: unknown library API"
#endif
};

struct hammer2_inode {
	struct hammer2_inode_data	ino;	/* raw inode data */
	off_t				doff;	/* disk inode offset */
};

#ifdef BOOT2

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

#if 0
static size_t
strlen(const char *s)
{
	size_t l = 0;
	for (; *s != 0; s++)
		l++;
	return (l);
}
#endif

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

static
off_t
blockoff(hammer2_blockref_t *bref)
{
	return(bref->data_off & ~HAMMER2_OFF_MASK_RADIX);
}

static
size_t
blocksize(hammer2_blockref_t *bref)
{
	return(1 << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX));
}

static
hammer2_key_t
hammer2_dirhash(const unsigned char *name, size_t len)
{
	const unsigned char *aname = name;
	uint32_t crcx;
	uint64_t key;
	size_t i;
	size_t j;

	key = 0;

	/*
	 * m32
	 */
	crcx = 0;
	for (i = j = 0; i < len; ++i) {
		if (aname[i] == '.' ||
		    aname[i] == '-' ||
		    aname[i] == '_' ||
		    aname[i] == '~') {
			if (i != j)
				crcx += hammer2_icrc32(aname + j, i - j);
			j = i + 1;
		}
	}
	if (i != j)
		crcx += hammer2_icrc32(aname + j, i - j);

	/*
	 * The directory hash utilizes the top 32 bits of the 64-bit key.
	 * Bit 63 must be set to 1.
	 */
	crcx |= 0x80000000U;
	key |= (uint64_t)crcx << 32;

	/*
	 * l16 - crc of entire filename
	 *
	 * This crc reduces degenerate hash collision conditions
	 */
	crcx = hammer2_icrc32(aname, len);
	crcx = crcx ^ (crcx << 16);
	key |= crcx & 0xFFFF0000U;

	/*
	 * Set bit 15.  This allows readdir to strip bit 63 so a positive
	 * 64-bit cookie/offset can always be returned, and still guarantee
	 * that the values 0x0000-0x7FFF are available for artificial entries.
	 * ('.' and '..').
	 */
	key |= 0x8000U;

	return (key);
}

/*
 * Low level read
 */
static
int
h2read(struct hammer2_fs *hfs, void *buf, size_t nbytes, off_t off)
{
#if defined(LIBSTAND)
	size_t rlen;
#endif
	int rc;

#if defined(TESTING)
	rc = pread(hfs->fd, &media, nbytes, off);
	if (rc == (int)nbytes)
		rc = 0;
	else
		rc = -1;
#elif defined(LIBSTAND)
	rc = hfs->f->f_dev->dv_strategy(hfs->f->f_devdata, F_READ,
					off >> DEV_BSHIFT, nbytes,
					buf, &rlen);
	if (rc || rlen != nbytes)
		rc = -1;
#elif defined(BOOT2)
	rc = dskread(buf, off >> DEV_BSHIFT, nbytes >> DEV_BSHIFT);
	if (rc)
		rc = -1;
#else
#error "hammer2: unknown library API"
#endif
	return rc;
}

/*
 * Common code
 *
 * Initialize for HAMMER2 filesystem access given a hammer2_fs with
 * its device file descriptor initialized.
 */

/*
 * Lookup within the block specified by (*base), loading the block from disk
 * if necessary.  Locate the first key within the requested range and
 * recursively run through indirect blocks as needed.  The caller may loop
 * by setting key_beg to *key_ret.
 *
 * Returns 0 if nothing could be found and the key range has been exhausted.
 * returns -1 if a disk error occured.  Otherwise returns the size of the
 * data block and returns the data block in *pptr and bref in *bref_ret.
 *
 * NOTE! When reading file data, the returned bref's key will be the nearest
 *	 data block we looked up.  The file read procedure must handle any
 *       zero-fill or skip.  However, we will truncate the return value to
 *	 the file size.
 */
static int
h2lookup(struct hammer2_fs *hfs, hammer2_blockref_t *base,
	 hammer2_key_t key_beg, hammer2_key_t key_end,
	 hammer2_blockref_t *bref_ret, void **pptr)
{
	hammer2_blockref_t *bref;
	hammer2_blockref_t best;
	hammer2_key_t scan_beg;
	hammer2_key_t scan_end;
	int i;
	int rc;
	int count;
	int dev_boff;
	int dev_bsize;

	if (base == NULL) {
		saved_base.data_off = (hammer2_off_t)-1;
		return(0);
	}
	if (base->data_off == (hammer2_off_t)-1)
		return(-1);

	/*
	 * Calculate the number of blockrefs to scan
	 */
	switch(base->type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		count = blocksize(base) / sizeof(hammer2_blockref_t);
		break;
	}

	/*
	 * Find the best candidate (the lowest blockref within the specified
	 * range).  The array can be fully set associative so make no ordering
	 * assumptions.
	 */
again:
	best.key = HAMMER2_KEY_MAX;
	best.type = 0;

	for (i = 0; i < count; ++i) {
		/*
		 * [re]load when returning from our recursion
		 */
		if (base->type != HAMMER2_BREF_TYPE_VOLUME &&
		    base->data_off != saved_base.data_off) {
			if (h2read(hfs, &media,
				   blocksize(base), blockoff(base))) {
				return(-1);
			}
			saved_base = *base;
		}

		/*
		 * Special case embedded file data
		 */
		if (base->type == HAMMER2_BREF_TYPE_INODE) {
			if (media.ipdata.meta.op_flags &
			    HAMMER2_OPFLAG_DIRECTDATA) {
				*pptr = media.ipdata.u.data;
				bref_ret->type = HAMMER2_BREF_TYPE_DATA;
				bref_ret->key = 0;
				return HAMMER2_EMBEDDED_BYTES;
			}
		}

		/*
		 * Calculate the bref in our scan.
		 */
		switch(base->type) {
		case HAMMER2_BREF_TYPE_VOLUME:
			bref = &hfs->sroot_blockset.blockref[i];
			break;
		case HAMMER2_BREF_TYPE_INODE:
			bref = &media.ipdata.u.blockset.blockref[i];
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			bref = &media.npdata[i];
			break;
		}
		if (bref->type == 0)
			continue;
		if (bref->key > best.key)
			continue;
		scan_beg = bref->key;
		scan_end = scan_beg + ((hammer2_key_t)1 << bref->keybits) - 1;
		if (scan_end >= key_beg && scan_beg <= key_end) {
			best = *bref;
		}
	}

	/*
	 * Figure out what to do with the results.
	 */
	switch(best.type) {
	case 0:
		/*
		 * Return 0
		 */
		rc = 0;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		/*
		 * Matched an indirect block.  If the block turns out to
		 * contain nothing we continue the iteration, otherwise
		 * we return the data from the recursion.
		 *
		 * Be sure to handle the overflow case when recalculating
		 * key_beg.
		 */
		rc = h2lookup(hfs, &best, key_beg, key_end, bref_ret, pptr);
		if (rc == 0) {
			key_beg = best.key +
				  ((hammer2_key_t)1 << best.keybits);
			if (key_beg > best.key && key_beg <= key_end)
				goto again;
		}
		break;
	case HAMMER2_BREF_TYPE_INODE:
	case HAMMER2_BREF_TYPE_DATA:
		/*
		 * Terminal match.  Leaf elements might not be data-aligned.
		 */
		dev_bsize = blocksize(&best);
		if (dev_bsize < HAMMER2_LBUFSIZE)
			dev_bsize = HAMMER2_LBUFSIZE;
		dev_boff = blockoff(&best) -
			   (blockoff(&best) & ~HAMMER2_LBUFMASK64);
		if (h2read(hfs, &media, dev_bsize, blockoff(&best) - dev_boff))
			return(-1);
		saved_base.data_off = (hammer2_off_t)-1;
		*bref_ret = best;
		*pptr = media.buf + dev_boff;
		rc = blocksize(&best);
		break;
	}
	return(rc);
}

static
void
h2resolve(struct hammer2_fs *hfs, const char *path,
	  hammer2_blockref_t *bref, hammer2_inode_data_t **inop)
{
	hammer2_blockref_t bres;
	hammer2_inode_data_t *ino;
	hammer2_key_t key;
	ssize_t bytes;
	size_t len;

	/*
	 * Start point (superroot)
	 */
	*bref = hfs->sroot;
	if (inop)
		*inop = NULL;

	/*
	 * Iterate path elements
	 */
	while (*path) {
		while (*path == '/')
			++path;
		if (*path == 0)	/* terminal */
			break;

		/*
		 * Calculate path element and look for it in the directory
		 */
		for (len = 0; path[len]; ++len) {
			if (path[len] == '/')
				break;
		}
		key = hammer2_dirhash(path, len);
		for (;;) {
			bytes = h2lookup(hfs, bref,
					 key, key | 0xFFFFU,
					 &bres, (void **)&ino);
			if (bytes == 0)
				break;
			if (len == ino->meta.name_len &&
			    memcmp(path, ino->filename, len) == 0) {
				if (inop)
					*inop = ino;
				break;
			}
			key = bres.key + 1;
		}

		/*
		 * Lookup failure
		 */
		if (bytes == 0) {
			bref->data_off = (hammer2_off_t)-1;
			break;
		}

		/*
		 * Check path continuance, inode must be a directory or
		 * we fail.
		 */
		path += len;
		if (*path && ino->meta.type != HAMMER2_OBJTYPE_DIRECTORY) {
			bref->data_off = (hammer2_off_t)-1;
			break;
		}
		*bref = bres;
	}
}

static
ssize_t
h2readfile(struct hammer2_fs *hfs, hammer2_blockref_t *bref,
	   off_t off, off_t filesize, void *buf, size_t len)
{
	hammer2_blockref_t bres;
	ssize_t total;
	ssize_t bytes;
	ssize_t zfill;
	char *data;

	/*
	 * EOF edge cases
	 */
	if (off >= filesize)
		return (0);
	if (off + len > filesize)
		len = filesize - off;

	/*
	 * Loop until done 
	 */
	total = 0;
	while (len) {
		/*
		 * Find closest bres >= requested offset.
		 */
		bytes = h2lookup(hfs, bref, off, off + len - 1,
				 &bres, (void **)&data);

		if (bytes < 0) {
			if (total == 0)
				total = -1;
			break;
		}

		/*
		 * Load the data into the buffer.  First handle a degenerate
		 * zero-fill case.
		 */
		if (bytes == 0) {
			bzero(buf, len);
			total += len;
			break;
		}

		/*
		 * Returned record overlaps to the left of the requested
		 * position.  It must overlap in this case or h2lookup()
		 * would have returned something else.
		 */
		if (bres.key < off) {
			data += off - bres.key;
			bytes -= off - bres.key;
		}

		/*
		 * Returned record overlaps to the right of the requested
		 * position, handle zero-fill.  Again h2lookup() only returns
		 * this case if there is an actual overlap.
		 */
		if (bres.key > off) {
			zfill = (ssize_t)(bres.key - off);
			bzero(buf, zfill);
			len -= zfill;
			off += zfill;
			total += zfill;
			buf = (char *)buf + zfill;
		}

		/*
		 * Trim returned request before copying.
		 */
		if (bytes > len)
			bytes = len;
		bcopy(data, buf, bytes);
		len -= bytes;
		off += bytes;
		total += bytes;
		buf = (char *)buf + bytes;
	}
	return (total);
}

static
int
h2init(struct hammer2_fs *hfs)
{
#if 0
	uint32_t crc0;
#endif
	hammer2_tid_t best_tid = 0;
	void *data;
	off_t off;
	int best;
	int i;

	/*
	 * Find the best volume header.
	 *
	 * WARNING BIOS BUGS: It looks like some BIOSes will implode when
	 * given a disk offset beyond the EOM.  XXX We need to probe the
	 * size of the media and limit our accesses, until then we have
	 * to give up if the first volume header does not have a hammer2
	 * signature.
	 *
	 * XXX Probably still going to be problems w/ HAMMER2 volumes on
	 *     media which is too small w/certain BIOSes.
	 */
	best = -1;
	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		off = i * HAMMER2_ZONE_BYTES64;
		if (i)
			no_io_error = 1;
		if (h2read(hfs, &media, sizeof(media.voldata), off))
			break;
		if (media.voldata.magic != HAMMER2_VOLUME_ID_HBO)
			break;
		if (best < 0 || best_tid < media.voldata.mirror_tid) {
			best = i;
			best_tid = media.voldata.mirror_tid;
		}
	}
	no_io_error = 0;
	if (best < 0)
		return(-1);

	/*
	 * Reload the best volume header and set up the blockref.
	 * We messed with media, clear the cache before continuing.
	 */
	off = best * HAMMER2_ZONE_BYTES64;
	if (h2read(hfs, &media, sizeof(media.voldata), off))
		return(-1);
	hfs->sroot.type = HAMMER2_BREF_TYPE_VOLUME;
	hfs->sroot.data_off = off;
	hfs->sroot_blockset = media.voldata.sroot_blockset;
	h2lookup(hfs, NULL, 0, 0, NULL, NULL);

	/*
	 * Lookup sroot and clear the cache again.
	 */
	h2lookup(hfs, &hfs->sroot, 0, 0, &hfs->sroot, &data);
	h2lookup(hfs, NULL, 0, 0, NULL, NULL);

	return (0);
}

/************************************************************************
 * 				BOOT2 SUPPORT				*
 ************************************************************************
 *
 */
#ifdef BOOT2

static struct hammer2_fs hfs;

static int
boot2_hammer2_init(void)
{
	if (h2init(&hfs))
		return(-1);
	return(0);
}

static boot2_ino_t
boot2_hammer2_lookup(const char *path)
{
	hammer2_blockref_t bref;

	h2resolve(&hfs, path, &bref, NULL);
	return ((boot2_ino_t)bref.data_off);
}

static ssize_t
boot2_hammer2_read(boot2_ino_t ino, void *buf, size_t len)
{
	hammer2_blockref_t bref;
	ssize_t total;

	bzero(&bref, sizeof(bref));
	bref.type = HAMMER2_BREF_TYPE_INODE;
	bref.data_off = ino;

	total = h2readfile(&hfs, &bref, fs_off, 0x7FFFFFFF, buf, len);
	if (total > 0)
		fs_off += total;
	return total;
}

const struct boot2_fsapi boot2_hammer2_api = {
        .fsinit = boot2_hammer2_init,
        .fslookup = boot2_hammer2_lookup,
        .fsread = boot2_hammer2_read
};

#endif

/************************************************************************
 * 				BOOT2 SUPPORT				*
 ************************************************************************
 *
 */
#ifdef LIBSTAND

struct hfile {
	struct hammer2_fs hfs;
	hammer2_blockref_t bref;
	int64_t		fsize;
	uint32_t	mode;
	uint8_t		type;
};

static
int
hammer2_get_dtype(uint8_t type)
{
	switch(type) {
	case HAMMER2_OBJTYPE_DIRECTORY:
		return(DT_DIR);
	case HAMMER2_OBJTYPE_REGFILE:
		return(DT_REG);
	case HAMMER2_OBJTYPE_FIFO:
		return(DT_FIFO);
	case HAMMER2_OBJTYPE_CDEV:
		return(DT_CHR);
	case HAMMER2_OBJTYPE_BDEV:
		return(DT_BLK);
	case HAMMER2_OBJTYPE_SOFTLINK:
		return(DT_LNK);
	case HAMMER2_OBJTYPE_HARDLINK:
		return(DT_UNKNOWN);
	case HAMMER2_OBJTYPE_SOCKET:
		return(DT_SOCK);
	default:
		return(DT_UNKNOWN);
	}
}

static
mode_t
hammer2_get_mode(uint8_t type)
{
	switch(type) {
	case HAMMER2_OBJTYPE_DIRECTORY:
		return(S_IFDIR);
	case HAMMER2_OBJTYPE_REGFILE:
		return(S_IFREG);
	case HAMMER2_OBJTYPE_FIFO:
		return(S_IFIFO);
	case HAMMER2_OBJTYPE_CDEV:
		return(S_IFCHR);
	case HAMMER2_OBJTYPE_BDEV:
		return(S_IFBLK);
	case HAMMER2_OBJTYPE_SOFTLINK:
		return(S_IFLNK);
	case HAMMER2_OBJTYPE_HARDLINK:
		return(0);
	case HAMMER2_OBJTYPE_SOCKET:
		return(S_IFSOCK);
	default:
		return(0);
	}
}

static int
hammer2_open(const char *path, struct open_file *f)
{
	struct hfile *hf = malloc(sizeof(*hf));
	hammer2_inode_data_t *ipdata;

	bzero(hf, sizeof(*hf));
	f->f_offset = 0;
	f->f_fsdata = hf;
	hf->hfs.f = f;

	if (h2init(&hf->hfs)) {
		f->f_fsdata = NULL;
		free(hf);
		errno = ENOENT;
		return(-1);
	}
	h2resolve(&hf->hfs, path, &hf->bref, &ipdata);
	if (hf->bref.data_off == (hammer2_off_t)-1 ||
	    (hf->bref.type != HAMMER2_BREF_TYPE_INODE &&
	    hf->bref.type != HAMMER2_BREF_TYPE_VOLUME)) {
		f->f_fsdata = NULL;
		free(hf);
		errno = ENOENT;
		return(-1);
	}
	if (ipdata) {
		hf->fsize = ipdata->meta.size;
		hf->type = ipdata->meta.type;
		hf->mode = ipdata->meta.mode |
			   hammer2_get_mode(ipdata->meta.type);
	} else {
		hf->fsize = 0;
		hf->type = HAMMER2_OBJTYPE_DIRECTORY;
		hf->mode = 0755 | S_IFDIR;
	}
	return(0);
}

static int
hammer2_close(struct open_file *f)
{
	struct hfile *hf = f->f_fsdata;

	f->f_fsdata = NULL;
	if (hf)
		free(hf);
	return (0);
}

static int
hammer2_read(struct open_file *f, void *buf, size_t len, size_t *resid)
{
	struct hfile *hf = f->f_fsdata;
	ssize_t total;
	int rc = 0;

	total = h2readfile(&hf->hfs, &hf->bref,
			   f->f_offset, hf->fsize, buf, len);
	if (total < 0) {
		rc = EIO;
		total = 0;
	} else {
		f->f_offset += total;
		rc = 0;
	}
	*resid = len - total;
	return rc;
}

static off_t
hammer2_seek(struct open_file *f, off_t offset, int whence)
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
hammer2_stat(struct open_file *f, struct stat *st)
{
	struct hfile *hf = f->f_fsdata;

	st->st_mode = hf->mode;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_size = hf->fsize;

	return (0);
}

static int
hammer2_readdir(struct open_file *f, struct dirent *den)
{
	struct hfile *hf = f->f_fsdata;
	hammer2_blockref_t bres;
	hammer2_inode_data_t *ipdata;
	int bytes;

	for (;;) {
		bytes = h2lookup(&hf->hfs, &hf->bref,
				 f->f_offset | HAMMER2_DIRHASH_VISIBLE, 
				 HAMMER2_KEY_MAX,
				 &bres, (void **)&ipdata);
		if (bytes <= 0)
			break;
		den->d_namlen = ipdata->meta.name_len;
		den->d_type = hammer2_get_dtype(ipdata->meta.type);
		den->d_ino = ipdata->meta.inum;
		bcopy(ipdata->filename, den->d_name, den->d_namlen);
		den->d_name[den->d_namlen] = 0;

		f->f_offset = bres.key + 1;

		return(0);
	}
	return ENOENT;
}

struct fs_ops hammer_fsops = {
	"hammer2",
	hammer2_open,
	hammer2_close,
	hammer2_read,
	null_write,
	hammer2_seek,
	hammer2_stat,
	hammer2_readdir
};

#endif
