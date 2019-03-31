/*-
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "fuse.h"

void
fuse_hexdump(const char *p, size_t len)
{
	int i;

	if (!fuse_debug)
		return;

	for (i = 0; i < (int)len; i++) {
		kprintf("%02X ", p[i] & 0xff);
		if ((i + 1) % 32 == 0)
			kprintf("\n");
	}
	kprintf("\n");
}

void
fuse_fill_in_header(struct fuse_in_header *ihd,
    uint32_t len, uint32_t opcode, uint64_t unique, uint64_t nodeid,
    uint32_t uid, uint32_t gid, uint32_t pid)
{
	ihd->len = len;
	ihd->opcode = opcode;
	ihd->unique = unique;
	ihd->nodeid = nodeid;
	ihd->uid = uid;
	ihd->gid = gid;
	ihd->pid = pid;
}

int
fuse_forget_node(struct fuse_mount *fmp, uint64_t ino, uint64_t nlookup,
    struct ucred *cred)
{
	struct fuse_ipc *fip;
	struct fuse_forget_in *ffi;
	int error;

	KKASSERT(nlookup > 0);

	fip = fuse_ipc_get(fmp, sizeof(*ffi));
	ffi = fuse_ipc_fill(fip, FUSE_FORGET, ino, cred);
	ffi->nlookup = nlookup;

	error = fuse_ipc_tx(fip);
	if (error)
		return error;
	fuse_ipc_put(fip);

	return 0;
}

/*
 * Ignore FUSE_COMPAT_XXX which seem to exist for backward compatibility
 * for ancient versions of FUSE protocol.
 */
int
fuse_audit_length(struct fuse_in_header *ihd, struct fuse_out_header *ohd)
{
	size_t len = ohd->len - sizeof(struct fuse_out_header);
	bool res;

	switch (ihd->opcode) {
	case FUSE_LOOKUP:
		res = (len == sizeof(struct fuse_entry_out));
		break;
	case FUSE_FORGET:
		res = true;
		break;
	case FUSE_GETATTR:
		res = (len == sizeof(struct fuse_attr_out));
		break;
	case FUSE_SETATTR:
		res = (len == sizeof(struct fuse_attr_out));
		break;
	case FUSE_READLINK:
		res = (len <= PAGE_SIZE);
		break;
	case FUSE_SYMLINK:
		res = (len == sizeof(struct fuse_entry_out));
		break;
	case FUSE_MKNOD:
		res = (len == sizeof(struct fuse_entry_out));
		break;
	case FUSE_MKDIR:
		res = (len == sizeof(struct fuse_entry_out));
		break;
	case FUSE_UNLINK:
		res = (len == 0);
		break;
	case FUSE_RMDIR:
		res = (len == 0);
		break;
	case FUSE_RENAME:
		res = (len == 0);
		break;
	case FUSE_LINK:
		res = (len == sizeof(struct fuse_entry_out));
		break;
	case FUSE_OPEN:
		res = (len == sizeof(struct fuse_open_out));
		break;
	case FUSE_READ:
		res = (len <= ((struct fuse_read_in*)(ihd + 1))->size);
		break;
	case FUSE_WRITE:
		res = (len == sizeof(struct fuse_write_out));
		break;
	case FUSE_STATFS:
		res = (len == sizeof(struct fuse_statfs_out));
		break;
	case FUSE_RELEASE:
		res = (len == 0);
		break;
	case FUSE_FSYNC:
		res = (len == 0);
		break;
	case FUSE_SETXATTR:
		res = (len == 0);
		break;
	case FUSE_GETXATTR:
		res = true;
		break;
	case FUSE_LISTXATTR:
		res = true;
		break;
	case FUSE_REMOVEXATTR:
		res = (len == 0);
		break;
	case FUSE_FLUSH:
		res = (len == 0);
		break;
	case FUSE_INIT:
		res = (len == sizeof(struct fuse_init_out));
		break;
	case FUSE_OPENDIR:
		res = (len == sizeof(struct fuse_open_out));
		break;
	case FUSE_READDIR:
		res = (len <= ((struct fuse_read_in*)(ihd + 1))->size);
		break;
	case FUSE_RELEASEDIR:
		res = (len == 0);
		break;
	case FUSE_FSYNCDIR:
		res = (len == 0);
		break;
	case FUSE_GETLK:
		res = false;
		break;
	case FUSE_SETLK:
		res = false;
		break;
	case FUSE_SETLKW:
		res = false;
		break;
	case FUSE_ACCESS:
		res = (len == 0);
		break;
	case FUSE_CREATE:
		res = (len == sizeof(struct fuse_entry_out) +
		    sizeof(struct fuse_open_out));
		break;
	case FUSE_INTERRUPT:
		res = false;
		break;
	case FUSE_BMAP:
		res = false;
		break;
	case FUSE_DESTROY:
		res = (len == 0);
		break;
	case FUSE_IOCTL:
		res = false;
		break;
	case FUSE_POLL:
		res = false;
		break;
	case FUSE_NOTIFY_REPLY:
		res = false;
		break;
	case FUSE_BATCH_FORGET:
		res = false;
		break;
	case FUSE_FALLOCATE:
		res = false;
		break;
	case FUSE_READDIRPLUS:
		res = false;
		break;
	case FUSE_RENAME2:
		res = false;
		break;
	case FUSE_LSEEK:
		res = false;
		break;
	case FUSE_COPY_FILE_RANGE:
		res = false;
		break;
	default:
		fuse_panic("Invalid opcode %d", ihd->opcode);
		break;
	}

	if (!res)
		return -1;
	return 0;
}

const char*
fuse_get_ops(int op)
{
	switch (op) {
	case FUSE_LOOKUP:
		return "FUSE_LOOKUP";
	case FUSE_FORGET:
		return "FUSE_FORGET";
	case FUSE_GETATTR:
		return "FUSE_GETATTR";
	case FUSE_SETATTR:
		return "FUSE_SETATTR";
	case FUSE_READLINK:
		return "FUSE_READLINK";
	case FUSE_SYMLINK:
		return "FUSE_SYMLINK";
	case FUSE_MKNOD:
		return "FUSE_MKNOD";
	case FUSE_MKDIR:
		return "FUSE_MKDIR";
	case FUSE_UNLINK:
		return "FUSE_UNLINK";
	case FUSE_RMDIR:
		return "FUSE_RMDIR";
	case FUSE_RENAME:
		return "FUSE_RENAME";
	case FUSE_LINK:
		return "FUSE_LINK";
	case FUSE_OPEN:
		return "FUSE_OPEN";
	case FUSE_READ:
		return "FUSE_READ";
	case FUSE_WRITE:
		return "FUSE_WRITE";
	case FUSE_STATFS:
		return "FUSE_STATFS";
	case FUSE_RELEASE:
		return "FUSE_RELEASE";
	case FUSE_FSYNC:
		return "FUSE_FSYNC";
	case FUSE_SETXATTR:
		return "FUSE_SETXATTR";
	case FUSE_GETXATTR:
		return "FUSE_GETXATTR";
	case FUSE_LISTXATTR:
		return "FUSE_LISTXATTR";
	case FUSE_REMOVEXATTR:
		return "FUSE_REMOVEXATTR";
	case FUSE_FLUSH:
		return "FUSE_FLUSH";
	case FUSE_INIT:
		return "FUSE_INIT";
	case FUSE_OPENDIR:
		return "FUSE_OPENDIR";
	case FUSE_READDIR:
		return "FUSE_READDIR";
	case FUSE_RELEASEDIR:
		return "FUSE_RELEASEDIR";
	case FUSE_FSYNCDIR:
		return "FUSE_FSYNCDIR";
	case FUSE_GETLK:
		return "FUSE_GETLK";
	case FUSE_SETLK:
		return "FUSE_SETLK";
	case FUSE_SETLKW:
		return "FUSE_SETLKW";
	case FUSE_ACCESS:
		return "FUSE_ACCESS";
	case FUSE_CREATE:
		return "FUSE_CREATE";
	case FUSE_INTERRUPT:
		return "FUSE_INTERRUPT";
	case FUSE_BMAP:
		return "FUSE_BMAP";
	case FUSE_DESTROY:
		return "FUSE_DESTROY";
	case FUSE_IOCTL:
		return "FUSE_IOCTL";
	case FUSE_POLL:
		return "FUSE_POLL";
	case FUSE_NOTIFY_REPLY:
		return "FUSE_NOTIFY_REPLY";
	case FUSE_BATCH_FORGET:
		return "FUSE_BATCH_FORGET";
	case FUSE_FALLOCATE:
		return "FUSE_FALLOCATE";
	case FUSE_READDIRPLUS:
		return "FUSE_READDIRPLUS";
	case FUSE_RENAME2:
		return "FUSE_RENAME2";
	case FUSE_LSEEK:
		return "FUSE_LSEEK";
	case FUSE_COPY_FILE_RANGE:
		return "FUSE_COPY_FILE_RANGE";
	default:
		fuse_panic("Invalid opcode %d", op);
		break;
	}

	return NULL;
}
