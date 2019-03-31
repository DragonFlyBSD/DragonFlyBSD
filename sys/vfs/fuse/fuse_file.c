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

static MALLOC_DEFINE(M_FUSE_FH, "fuse_fh", "FUSE fh");

static struct objcache *fuse_fh_objcache = NULL;
static struct objcache_malloc_args fuse_fh_args = {
	sizeof(uint64_t), M_FUSE_FH,
};

uint64_t fuse_fh(struct file *fp)
{
	uint64_t *fhp = fp->private_data;
	KKASSERT(fhp);

	fuse_dbg("fh=%jx\n", *fhp);
	return *fhp;
}

void fuse_get_fh(struct file *fp, uint64_t fh)
{
	uint64_t *fhp = objcache_get(fuse_fh_objcache, M_WAITOK);
	KKASSERT(fhp);

	*fhp = fh;
	fuse_dbg("fh=%jx\n", *fhp);

	KKASSERT(!fp->private_data);
	fp->private_data = fhp;
}

void fuse_put_fh(struct file *fp)
{
	uint64_t *fhp = fp->private_data;
	KKASSERT(fhp);

	fuse_dbg("fh=%jx\n", *fhp);

	objcache_put(fuse_fh_objcache, fhp);
	fp->private_data = NULL;
}

/*
 * nfh - per node fh (ad-hoc hack)
 *
 * XXX This should be gone, as the concept of nfh is already wrong.
 * This exists due to how BSD VFS is implemented.
 * There are situations where FUSE VOP's can't access fh required by FUSE ops.
 */
uint64_t fuse_nfh(struct fuse_node *fnp)
{
	fuse_dbg("ino=%ju fh=%jx\n", fnp->ino, fnp->fh);
	return fnp->fh;
}

void fuse_get_nfh(struct fuse_node *fnp, uint64_t fh)
{
	fnp->fh = fh;
	fuse_dbg("ino=%ju fh=%jx\n", fnp->ino, fnp->fh);
}

void fuse_put_nfh(struct fuse_node *fnp)
{
	fuse_dbg("ino=%ju fh=%jx\n", fnp->ino, fnp->fh);
}

void
fuse_file_init(void)
{
	fuse_fh_objcache = objcache_create("fuse_fh", 0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc_zero, objcache_malloc_free, &fuse_fh_args);
}

void
fuse_file_cleanup(void)
{
	objcache_destroy(fuse_fh_objcache);
}
