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

#include <sys/signalvar.h>
#include <sys/kern_syscall.h>

static MALLOC_DEFINE(M_FUSE_BUF, "fuse_buf", "FUSE buf");
static MALLOC_DEFINE(M_FUSE_IPC, "fuse_ipc", "FUSE ipc");

static struct objcache *fuse_ipc_objcache = NULL;
static struct objcache_malloc_args fuse_ipc_args = {
	sizeof(struct fuse_ipc), M_FUSE_IPC,
};

static int
fuse_block_sigs(sigset_t *oldset)
{
	if (curproc) {
		sigset_t newset;
		int error;

		SIGFILLSET(newset);
		SIGDELSET(newset, SIGKILL);

		error = kern_sigprocmask(SIG_BLOCK, &newset, oldset);
		KKASSERT(!error);
		return error;
	}

	return -1;
}

static int
fuse_restore_sigs(sigset_t *oldset)
{
	if (curproc) {
		int error = kern_sigprocmask(SIG_SETMASK, oldset, NULL);
		KKASSERT(!error);
		return error;
	}

	return -1;
}

void
fuse_buf_alloc(struct fuse_buf *fbp, size_t len)
{
	fbp->buf = kmalloc(len, M_FUSE_BUF, M_WAITOK | M_ZERO);
	KKASSERT(fbp->buf);
	fbp->len = len;
}

void
fuse_buf_free(struct fuse_buf *fbp)
{
	if (fbp->buf) {
		kfree(fbp->buf, M_FUSE_BUF);
		fbp->buf = NULL;
	}
	fbp->len = 0;
}

struct fuse_ipc*
fuse_ipc_get(struct fuse_mount *fmp, size_t len)
{
	struct fuse_ipc *fip;

	fip = objcache_get(fuse_ipc_objcache, M_WAITOK);
	refcount_init(&fip->refcnt, 1);
	fip->fmp = fmp;
	fip->unique = atomic_fetchadd_long(&fmp->unique, 1);
	fip->done = 0;

	fuse_buf_alloc(&fip->request, sizeof(struct fuse_in_header) + len);
	fip->reply.buf = NULL;

	return fip;
}

void
fuse_ipc_put(struct fuse_ipc *fip)
{
	if (refcount_release(&fip->refcnt)) {
		fuse_buf_free(&fip->request);
		fuse_buf_free(&fip->reply);
		objcache_put(fuse_ipc_objcache, fip);
	}
}

static void
fuse_ipc_remove(struct fuse_ipc *fip)
{
	struct fuse_mount *fmp = fip->fmp;
	struct fuse_ipc *p, *tmp;

	mtx_lock(&fmp->ipc_lock);
	TAILQ_FOREACH_MUTABLE(p, &fmp->request_head, request_entry, tmp) {
		if (fip == p) {
			TAILQ_REMOVE(&fmp->request_head, p, request_entry);
			break;
		}
	}
	TAILQ_FOREACH_MUTABLE(p, &fmp->reply_head, reply_entry, tmp) {
		if (fip == p) {
			TAILQ_REMOVE(&fmp->reply_head, p, reply_entry);
			break;
		}
	}
	mtx_unlock(&fmp->ipc_lock);
}

void*
fuse_ipc_fill(struct fuse_ipc *fip, int op, uint64_t ino, struct ucred *cred)
{
	if (!cred)
		cred = curthread->td_ucred;

	fuse_fill_in_header(fuse_in(fip), fuse_in_size(fip), op, fip->unique,
	    ino, cred->cr_uid, cred->cr_rgid,
	    curthread->td_proc ? curthread->td_proc->p_pid : 0);

	fuse_dbgipc(fip, 0, "");

	return fuse_in_data(fip);
}

static int
fuse_ipc_wait(struct fuse_ipc *fip)
{
	sigset_t oldset;
	int error, retry = 0;

	if (fuse_test_dead(fip->fmp)) {
		KKASSERT(!fuse_ipc_test_replied(fip));
		fuse_ipc_set_replied(fip);
		return ENOTCONN;
	}

	if (fuse_ipc_test_replied(fip))
		return 0;
again:
	fuse_block_sigs(&oldset);
	error = tsleep(fip, PCATCH, "ftxp", 5 * hz);
	fuse_restore_sigs(&oldset);
	if (!error)
		KKASSERT(fuse_ipc_test_replied(fip)); /* XXX */

	if (error == EWOULDBLOCK) {
		if (!fuse_ipc_test_replied(fip)) {
			if (!retry)
				fuse_print("timeout/retry\n");
			if (retry++ < 6)
				goto again;
			fuse_print("timeout\n");
			fuse_ipc_remove(fip);
			fuse_ipc_set_replied(fip);
			return ETIMEDOUT;
		} else
			fuse_dbg("EWOULDBLOCK lost race\n");
	} else if (error) {
		fuse_print("error=%d\n", error);
		fuse_ipc_remove(fip);
		fuse_ipc_set_replied(fip);
		return error;
	}

	if (fuse_test_dead(fip->fmp)) {
		KKASSERT(fuse_ipc_test_replied(fip));
		return ENOTCONN;
	}

	return 0;
}

int
fuse_ipc_tx(struct fuse_ipc *fip)
{
	struct fuse_mount *fmp = fip->fmp;
	struct fuse_out_header *ohd;
	int error;

	if (fuse_test_dead(fmp)) {
		fuse_ipc_put(fip);
		return ENOTCONN;
	}

	mtx_lock(&fmp->mnt_lock);

	mtx_lock(&fmp->ipc_lock);
	TAILQ_INSERT_TAIL(&fmp->reply_head, fip, reply_entry);
	TAILQ_INSERT_TAIL(&fmp->request_head, fip, request_entry);
	mtx_unlock(&fmp->ipc_lock);

	wakeup(fmp);
	KNOTE(&fmp->kq.ki_note, 0);
	mtx_unlock(&fmp->mnt_lock);

	error = fuse_ipc_wait(fip);
	KKASSERT(fuse_ipc_test_replied(fip));
	if (error) {
		fuse_dbgipc(fip, error, "ipc_wait");
		fuse_ipc_put(fip);
		return error;
	}

	ohd = fuse_out(fip);
	KKASSERT(ohd);
	error = ohd->error;
	if (error) {
		fuse_dbgipc(fip, error, "ipc_error");
		fuse_ipc_put(fip);
		if (error < 0)
			error = -error;
		return error;
	}
	fuse_dbgipc(fip, 0, "done");

	return 0;
}

void
fuse_ipc_init(void)
{
	fuse_ipc_objcache = objcache_create("fuse_ipc", 0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc_zero, objcache_malloc_free, &fuse_ipc_args);
}

void
fuse_ipc_cleanup(void)
{
	objcache_destroy(fuse_ipc_objcache);
}
