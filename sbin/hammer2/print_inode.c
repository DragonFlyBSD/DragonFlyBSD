/*
 * Copyright (c) 2013-2019 The DragonFly Project.  All rights reserved.
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

#include "hammer2.h"

static void
hexdump_inode(const void *data, size_t len)
{
	const unsigned char *p = data;
	size_t i;

	if (VerboseOpt <= 0)
		return;

	for (i = 0; i < len; i++) {
		printf("%02X", *p);
		if (i != len - 1)
			printf(" ");
		p++;
	}
	printf("\n");
}

void
print_inode(const char *path)
{
	hammer2_ioc_inode_t inode;
	hammer2_inode_data_t *ipdata;
	hammer2_inode_meta_t *meta;
	char *str = NULL;
	int i, fd;

	fd = hammer2_ioctl_handle(path);
	if (fd == -1)
		return;

	if (ioctl(fd, HAMMER2IOC_INODE_GET, &inode) == -1) {
		printf("ioctl(HAMMER2IOC_INODE_GET) failed\n");
		return;
	}
	ipdata = &inode.ip_data;
	meta = &ipdata->meta;

	hexdump_inode(meta, sizeof(*meta));
	printf("version = %u\n", meta->version);
	printf("pfs_subtype = %u\n", meta->pfs_subtype);
	printf("uflags = 0x%x\n", (unsigned int)meta->uflags);
	printf("rmajor = %u\n", meta->rmajor);
	printf("rminor = %u\n", meta->rminor);
	printf("ctime = %s\n", hammer2_time64_to_str(meta->ctime, &str));
	printf("mtime = %s\n", hammer2_time64_to_str(meta->mtime, &str));
	printf("atime = %s\n", hammer2_time64_to_str(meta->atime, &str));
	printf("btime = %s\n", hammer2_time64_to_str(meta->btime, &str));
	printf("uid = %s\n", hammer2_uuid_to_str(&meta->uid, &str));
	printf("gid = %s\n", hammer2_uuid_to_str(&meta->gid, &str));
	printf("type = %u\n", meta->type);
	printf("op_flags = 0x%x\n", meta->op_flags);
	printf("cap_flags = 0x%x\n", meta->cap_flags);
	printf("mode = 0%o\n", meta->mode);
	printf("inum = 0x%jx\n", (uintmax_t)meta->inum);
	printf("size = %ju\n", (uintmax_t)meta->size);
	printf("nlinks = %ju\n", (uintmax_t)meta->nlinks);
	printf("iparent = 0x%jx\n", (uintmax_t)meta->iparent);
	printf("name_key = 0x%jx\n", (uintmax_t)meta->name_key);
	printf("name_len = %u\n", meta->name_len);
	printf("ncopies = %u\n", meta->ncopies);
	printf("comp_algo = %u\n", meta->comp_algo);
	printf("target_type = %u\n", meta->target_type);
	printf("check_algo = %u\n", meta->check_algo);
	printf("pfs_nmasters = %u\n", meta->pfs_nmasters);
	printf("pfs_type = %u\n", meta->pfs_type);
	printf("pfs_inum = 0x%jx\n", (uintmax_t)meta->pfs_inum);
	printf("pfs_clid = %s\n",
	    hammer2_uuid_to_str(&meta->pfs_clid, &str));
	printf("pfs_fsid = %s\n",
	    hammer2_uuid_to_str(&meta->pfs_fsid, &str));
	printf("data_quota = 0x%jx\n", (uintmax_t)meta->data_quota);
	printf("inode_quota = 0x%jx\n", (uintmax_t)meta->inode_quota);
	printf("pfs_lsnap_tid = 0x%jx\n", (uintmax_t)meta->pfs_lsnap_tid);
	printf("decrypt_check = 0x%jx\n", (uintmax_t)meta->decrypt_check);

	free(str);
	/* XXX HAMMER2IOC_INODE_GET only supports meta part */
	return;
	printf("\n");

	hexdump_inode(ipdata->filename, sizeof(ipdata->filename));
	printf("filename = \"%s\"\n", ipdata->filename);
	printf("\n");

	if (!(meta->op_flags & HAMMER2_OPFLAG_DIRECTDATA)) {
		for (i = 0; i < HAMMER2_SET_COUNT; ++i) {
			hammer2_blockref_t *bref =
			    &ipdata->u.blockset.blockref[i];
			hexdump_inode(bref, sizeof(*bref));

			if (bref->type == HAMMER2_BREF_TYPE_EMPTY) {
				printf("blockref[%d] is empty\n", i);
				continue;
			}
			printf("blockref[%d] type = %u\n", i, bref->type);
			printf("blockref[%d] methods = %u\n", i, bref->methods);
			printf("blockref[%d] copyid = %u\n", i, bref->copyid);
			printf("blockref[%d] keybits = %u\n", i, bref->keybits);
			printf("blockref[%d] vradix = %u\n", i, bref->vradix);
			printf("blockref[%d] flags = 0x%x\n", i, bref->flags);
			printf("blockref[%d] leaf_count = %u\n", i,
			    bref->leaf_count);
			printf("blockref[%d] key = 0x%jx\n", i,
			    (uintmax_t)bref->key);
			printf("blockref[%d] mirror_tid = 0x%jx\n", i,
			    (uintmax_t)bref->mirror_tid);
			printf("blockref[%d] modify_tid = 0x%jx\n", i,
			    (uintmax_t)bref->modify_tid);
			printf("blockref[%d] data_off = 0x%jx\n", i,
			    (uintmax_t)bref->data_off);
			printf("blockref[%d] update_tid = 0x%jx\n", i,
			    (uintmax_t)bref->update_tid);
			if (i != HAMMER2_SET_COUNT - 1)
				printf("\n");
		}
	} else {
		hexdump_inode(ipdata->u.data, sizeof(ipdata->u.data));
		printf("embedded data\n");
	}
}
