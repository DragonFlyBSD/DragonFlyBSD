/*
 * Copyright (c) 2023 The DragonFly Project.  All rights reserved.
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
#include "hammer2.h"

#define INUM_HSIZE	(1024*1024)
#define INUM_HMASK	(INUM_HSIZE - 1)

#include <openssl/sha.h>

typedef struct bref_entry {
	struct bref_entry *next;
	hammer2_off_t	data_off;
} bref_entry_t;

typedef struct recover_entry {
	struct recover_entry *next;
	bref_entry_t *brefs;
	hammer2_key_t inum;
} recover_entry_t;

static recover_entry_t **RecoverHash;

static void enter_inum(hammer2_key_t inum);
static bref_entry_t **check_inum(hammer2_key_t inum);
static int dump_inum(bref_entry_t **bree, hammer2_blockref_t *bref,
			const char *destdir, const char *filename,
			long *name_iterator);
static int dump_file_data(int wfd, hammer2_off_t fsize,
			hammer2_blockref_t *bref, int count);
static int validate_crc(hammer2_blockref_t *bref, void *data, size_t bytes);
static uint32_t hammer2_to_unix_xid(const uuid_t *uuid);

/*
 * Recover the specified file.
 *
 * Basically do a raw scan of the drive image looking for directory entries
 * and inodes.  Index all inodes found, including copies, and filter
 * directory entries for the requested filename to locate inode numbers.
 *
 * All copies that are located are written to destdir with a suffix .00001,
 * .00002, etc.
 */
int
cmd_recover(const char *devpath, const char *filename,
	    const char *destdir)
{
	hammer2_media_data_t data;
	hammer2_volume_t *vol;
	int fd;
	hammer2_off_t loff;
	hammer2_off_t poff;
	size_t flen = strlen(filename);
	size_t i;
	long name_iterator = 1;

	if (strchr(filename, '/')) {
		fprintf(stderr, "specify last component of path only\n");
		return 1;
	}

	hammer2_init_volumes(devpath, 1);
	RecoverHash = calloc(INUM_HSIZE, sizeof(recover_entry_t));

	printf("PASS 1\n");

	loff = 0;
	while ((vol = hammer2_get_volume(loff)) != NULL) {
		fd = vol->fd;
		poff = loff - vol->offset;
		while (poff < vol->size) {
			if (pread(fd, &data, sizeof(data), poff) !=
			    sizeof(data))
			{
				/* try to skip possible I/O error */
				poff += sizeof(data);
				continue;
			}
			for (i = 0; i < HAMMER2_IND_COUNT_MAX; ++i) {
				hammer2_blockref_t *bref;

				bref = &data.npdata[i];
				if (bref->type == HAMMER2_BREF_TYPE_DIRENT &&
				    flen == bref->embed.dirent.namlen &&
				    bcmp(filename, bref->check.buf, flen) == 0)
				{
					printf("found %s inum %ld\n",
						filename,
						bref->embed.dirent.inum);
					enter_inum(bref->embed.dirent.inum);
				}
			}
			poff += sizeof(data);
		}
		loff = vol->offset + vol->size;
	}

	printf("PASS 2\n");

	loff = 0;
	while ((vol = hammer2_get_volume(loff)) != NULL) {
		fd = vol->fd;
		poff = loff - vol->offset;
		while (poff < vol->size) {
			pread(fd, &data, sizeof(data), poff);
			for (i = 0; i < HAMMER2_IND_COUNT_MAX; ++i) {
				hammer2_blockref_t *bref;
				bref_entry_t **bree;

				/*
				 * If it looks like an inode, try to dump
				 * it.  dump_inum() validates the check code
				 * and does other sanity checks, since our
				 * scan can very easily misinterpret user
				 * data for meta-data.
				 *
				 * check_inum() determines if the inode
				 * number is one that we are looking for.
				 */
				bref = &data.npdata[i];
				if (bref->type == HAMMER2_BREF_TYPE_INODE &&
				    (bree = check_inum(bref->key)) != NULL)
				{
					dump_inum(bree, bref, destdir,
						  filename, &name_iterator);
				}
			}
			poff += sizeof(data);
		}
		loff = vol->offset + vol->size;
	}

	/*
	 * Cleanup
	 */
	hammer2_cleanup_volumes();

	for (i = 0; i < INUM_HSIZE; ++i) {
		recover_entry_t *entry;
		bref_entry_t *bscan;

		if ((entry = RecoverHash[i]) != NULL) {
			RecoverHash[i] = entry->next;

			while ((bscan = entry->brefs) != NULL) {
				entry->brefs = bscan->next;
				free(bscan);
			}
			free(entry);
		}
	}
	free(RecoverHash);

	return 0;
}

static void
enter_inum(hammer2_key_t inum)
{
	recover_entry_t *entry;
	int hv = (inum ^ (inum >> 16)) & INUM_HMASK;

	for (entry = RecoverHash[hv]; entry; entry = entry->next) {
		if (entry->inum == inum)
			return;
	}
	entry = malloc(sizeof(*entry));
	bzero(entry, sizeof(*entry));
	entry->inum = inum;
	entry->next = RecoverHash[hv];
	RecoverHash[hv] = entry;
}

static bref_entry_t **
check_inum(hammer2_key_t inum)
{
	recover_entry_t *entry;
	int hv = (inum ^ (inum >> 16)) & INUM_HMASK;

	for (entry = RecoverHash[hv]; entry; entry = entry->next) {
		if (entry->inum == inum)
			return &entry->brefs;
	}
	return NULL;
}

/*
 * Dump the inode content to a file
 */
static int
dump_inum(bref_entry_t **bree, hammer2_blockref_t *bref,
	  const char *destdir, const char *filename,
	  long *name_iterator)
{
	hammer2_inode_data_t inode;
	hammer2_volume_t *vol;
	hammer2_off_t poff;
	bref_entry_t *bscan;
	char *path1;
	char *path2;
	int vfd;
	int wfd;
	int res;

	/*
	 * First check to see if we have already restored this inode via
	 * its bref->data_off.  We want to restore separate copies of an
	 * inode, but not multiple references to the same copy.
	 *
	 * Return success and ignore duplicate references
	 */
	for (bscan = *bree; bscan; bscan = bscan->next) {
		if (bref->data_off == bscan->data_off)
			return 1;
	}
	bscan = malloc(sizeof(*bscan));
	bzero(bscan, sizeof(*bscan));
	bscan->next = *bree;
	*bree = bscan;
	bscan->data_off = bref->data_off;

	/*
	 * Validate the potential blockref.  Note that this might not be a
	 * real blockref.  Don't trust anything, really.
	 */
	if ((1 << (bref->data_off & 0x1F)) != sizeof(inode))
		return 0;
	if ((bref->data_off & ~0x1F & (sizeof(inode) - 1)) != 0)
		return 0;
	vol = hammer2_get_volume(bref->data_off);
	if (vol == NULL)
		return 0;

	vfd = vol->fd;
	poff = (bref->data_off - vol->offset) & ~0x1FL;
	if (pread(vfd, &inode, sizeof(inode), poff) != sizeof(inode))
		return 0;

	/*
	 * The blockref looks ok but the real test is whether the
	 * inode data it references passes the CRC check.  If it
	 * does, it is highly likely that we have a valid inode.
	 */
	printf(" dump inum %ld @ 0x%016lx ", bref->key, bref->data_off);
	if (validate_crc(bref, &inode, sizeof(inode)) == 0) {
		printf(", invalid crc\n");
		return 0;
	}

	asprintf(&path1, "%s/%s.%05ld", destdir, filename, *name_iterator);
	++*name_iterator;

	/*
	 * [re]create file, initial perms (fixed up after dump is complete)
	 *
	 * If the data block recursion fails the file will be renamed
	 * .corrupted.  If it succeeds, we try to restore ownership,
	 * permissions, and so forth.
	 */
	chflags(path1, 0);
	chmod(path1, 0600);
	wfd = open(path1, O_RDWR|O_CREAT|O_TRUNC, 0600);

	if (inode.meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
		/*
		 * Direct data case
		 */
		if (inode.meta.size > 0 &&
		    inode.meta.size <= sizeof(inode.u.data))
		{
			write(wfd, inode.u.data, inode.meta.size);
		}
		res = 1;
	} else {
		/*
		 * file content, indirect blockrefs
		 */
		res = dump_file_data(wfd, inode.meta.size,
				     &inode.u.blockset.blockref[0],
				     HAMMER2_SET_COUNT);
		if (res == 0)
			printf("(corrupted)");
	}

	/*
	 * On success, set perms, mtime, flags, etc
	 * On failure, rename file to .corrupted
	 */
	if (res) {
		struct timeval tvs[2];

		tvs[0].tv_sec = inode.meta.atime / 1000000;
		tvs[0].tv_usec = inode.meta.atime % 1000000;
		tvs[1].tv_sec = inode.meta.mtime / 1000000;
		tvs[1].tv_usec = inode.meta.mtime % 1000000;

		ftruncate(wfd, inode.meta.size);
		if (futimes(wfd, tvs) < 0)
			perror("futimes");
		fchown(wfd,
		       hammer2_to_unix_xid(&inode.meta.uid),
		       hammer2_to_unix_xid(&inode.meta.gid));

		fchmod(wfd, inode.meta.mode);
		fchflags(wfd, inode.meta.uflags);
		printf(" %s", path1);
	} else {
		struct timeval tvs[2];

		tvs[0].tv_sec = inode.meta.atime / 1000000;
		tvs[0].tv_usec = inode.meta.atime % 1000000;
		tvs[1].tv_sec = inode.meta.mtime / 1000000;
		tvs[1].tv_usec = inode.meta.mtime % 1000000;

		ftruncate(wfd, inode.meta.size);
		if (futimes(wfd, tvs) < 0)
			perror("futimes");
		fchown(wfd,
		       hammer2_to_unix_xid(&inode.meta.uid),
		       hammer2_to_unix_xid(&inode.meta.gid));

		asprintf(&path2, "%s.corrupted", path1);
		rename(path1, path2);
		free(path2);
		printf(" %s", path2);
	}
	close(wfd);

	/*
	 * Cleanup
	 */
	free(path1);
	printf("\n");

	return res;
}

static int
dump_file_data(int wfd, hammer2_off_t fsize,
	       hammer2_blockref_t *base, int count)
{
	hammer2_media_data_t data;
	hammer2_blockref_t *bref;
	hammer2_volume_t *vol;
	hammer2_off_t poff;
	hammer2_off_t psize;
	int res = 1;
	int rtmp;
	int n;

	for (n = 0; n < count; ++n) {
		bref = &base[n];

		if (bref->type == HAMMER2_BREF_TYPE_EMPTY ||
		    bref->data_off == 0)
		{
			continue;
		}
		vol = hammer2_get_volume(bref->data_off);
		if (vol == NULL)
			continue;
		if (bref->type == HAMMER2_BREF_TYPE_EMPTY ||
		    bref->data_off == 0)
		{
			continue;
		}

		poff = (bref->data_off - vol->offset) & ~0x1FL;
		psize = 1 << (bref->data_off & 0x1F);
		if (psize > sizeof(data)) {
			res = 0;
			continue;
		}

		if (pread(vol->fd, &data, psize, poff) != (ssize_t)psize) {
			res = 0;
			continue;
		}

		if (validate_crc(bref, &data, psize) == 0) {
			res = 0;
			continue;
		}

		switch(bref->type) {
		case HAMMER2_BREF_TYPE_DATA:
			if (bref->key + psize > fsize)
				pwrite(wfd, &data, fsize - bref->key,
				       bref->key);
			else
				pwrite(wfd, &data, psize, bref->key);
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			rtmp = dump_file_data(wfd, fsize,
					  &data.npdata[0],
					  psize / sizeof(hammer2_blockref_t));
			if (res)
				res = rtmp;
			break;
		}
	}
	return res;
}

static int
validate_crc(hammer2_blockref_t *bref, void *data, size_t bytes)
{
	uint32_t cv;
	uint64_t cv64;
	SHA256_CTX hash_ctx;
	int success = 0;
	union {
		uint8_t digest[SHA256_DIGEST_LENGTH];
		uint64_t digest64[SHA256_DIGEST_LENGTH/8];
	} u;

	switch(HAMMER2_DEC_CHECK(bref->methods)) {
	case HAMMER2_CHECK_NONE:
		success = 1;
		break;
	case HAMMER2_CHECK_DISABLED:
		success = 1;
		break;
	case HAMMER2_CHECK_ISCSI32:
		cv = hammer2_icrc32(data, bytes);
		if (bref->check.iscsi32.value != cv) {
			printf("(icrc %02x:%08x/%08x failed) ",
			       bref->methods,
			       bref->check.iscsi32.value,
			       cv);
		} else {
			success = 1;
		}
		break;
	case HAMMER2_CHECK_XXHASH64:
		cv64 = XXH64(data, bytes, XXH_HAMMER2_SEED);
		if (bref->check.xxhash64.value != cv64) {
			printf("(xxhash64 %02x:%016jx/%016jx failed) ",
			       bref->methods,
			       bref->check.xxhash64.value,
			       cv64);
		} else {
			success = 1;
		}
		break;
	case HAMMER2_CHECK_SHA192:
		SHA256_Init(&hash_ctx);
		SHA256_Update(&hash_ctx, data, bytes);
		SHA256_Final(u.digest, &hash_ctx);
		u.digest64[2] ^= u.digest64[3];
		if (memcmp(u.digest, bref->check.sha192.data,
		    sizeof(bref->check.sha192.data))) {
			printf("(sha192 failed) ");
		} else {
			success = 1;
		}
		break;
	case HAMMER2_CHECK_FREEMAP:
		cv = hammer2_icrc32(data, bytes);
		if (bref->check.freemap.icrc32 != cv) {
			printf("(fcrc %02x:%08x/%08x failed) ",
				bref->methods,
				bref->check.freemap.icrc32,
				cv);
		} else {
			success = 1;
		}
		break;
	}
	return success;
}

static uint32_t
hammer2_to_unix_xid(const uuid_t *uuid)
{
        return(*(const uint32_t *)&uuid->node[2]);
}
