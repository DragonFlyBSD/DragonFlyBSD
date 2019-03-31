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

#include <sys/conf.h>
#include <sys/device.h>
#include <sys/devfs.h>
#include <sys/uio.h>

static int fuse_cdevpriv_close(struct fuse_mount*);
static struct cdev *fuse_dev;

static void
fuse_cdevpriv_dtor(void *data)
{
	struct fuse_mount *fmp = data;

	if (!fuse_cdevpriv_close(fmp))
		fuse_mount_free(fmp);
}

static int
fuse_device_open(struct dev_open_args *ap)
{
	struct fuse_mount *fmp;

	fmp = kmalloc(sizeof(*fmp), M_TEMP, M_WAITOK | M_ZERO);
	KKASSERT(fmp);

	refcount_init(&fmp->refcnt, 1);
	devfs_set_cdevpriv(ap->a_fp, fmp, fuse_cdevpriv_dtor);
	fuse_dbg("open %s\n", ap->a_head.a_dev->si_name);

	return 0;
}

static int
fuse_device_close(struct dev_close_args *ap)
{
	struct fuse_mount *fmp;
	int error;

	error = devfs_get_cdevpriv(ap->a_fp, (void**)&fmp);
	if (error)
		return error;
	KKASSERT(fmp);

	/* XXX Can't call this on device close due to devfs bug... */
	//fuse_cdevpriv_close(fmp);
	fuse_dbg("close %s\n", ap->a_head.a_dev->si_name);

	return 0;
}

static int
fuse_cdevpriv_close(struct fuse_mount *fmp)
{
	if (!fmp->devvp) {
		fuse_print("/dev/%s not associated with FUSE mount\n",
		    fuse_dev->si_name);
		return ENODEV;
	}

	mtx_lock(&fmp->mnt_lock);
	if (fuse_mount_kill(fmp) == -1)
		KNOTE(&fmp->kq.ki_note, 0);
	KKASSERT(fmp->devvp);
	mtx_unlock(&fmp->mnt_lock);

	return 0;
}

/* Call with ->ipc_lock held. */
static void
fuse_device_clear(struct fuse_mount *fmp)
{
	struct fuse_ipc *fip;

	while ((fip = TAILQ_FIRST(&fmp->request_head)))
		TAILQ_REMOVE(&fmp->request_head, fip, request_entry);

	while ((fip = TAILQ_FIRST(&fmp->reply_head))) {
		TAILQ_REMOVE(&fmp->reply_head, fip, reply_entry);
		if (fuse_ipc_test_and_set_replied(fip))
			wakeup(fip);
	}
}

static int
fuse_device_read(struct dev_read_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct fuse_mount *fmp;
	struct fuse_ipc *fip;
	int error;

	error = devfs_get_cdevpriv(ap->a_fp, (void**)&fmp);
	if (error)
		return error;

	if (fuse_test_dead(fmp))
		return ENOTCONN;

	mtx_lock(&fmp->ipc_lock);
	while (!(fip = TAILQ_FIRST(&fmp->request_head))) {
		error = mtxsleep(fmp, &fmp->ipc_lock, PCATCH, "ftxc", 0);
		if (fuse_test_dead(fmp)) {
			fuse_device_clear(fmp);
			mtx_unlock(&fmp->ipc_lock);
			fuse_dbg("error=%d dead\n", error);
			return ENOTCONN;
		}
		if (error) {
			mtx_unlock(&fmp->ipc_lock);
			fuse_dbg("error=%d\n", error);
			return error;
		}
	}
	TAILQ_REMOVE(&fmp->request_head, fip, request_entry);
	mtx_unlock(&fmp->ipc_lock);

	fuse_dbgipc(fip, 0, "");

	if (uio->uio_resid < fuse_in_size(fip))
		return EILSEQ;

	return uiomove(fuse_in(fip), fuse_in_size(fip), uio);
}

static int
fuse_device_write(struct dev_write_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct fuse_mount *fmp;
	struct fuse_ipc *fip, *tmp;
	struct fuse_buf fb;
	struct fuse_in_header *ihd;
	struct fuse_out_header *ohd;
	bool found = false;
	int error;

	error = devfs_get_cdevpriv(ap->a_fp, (void**)&fmp);
	if (error)
		return error;

	if (uio->uio_resid < sizeof(*ohd))
		return EILSEQ;

	fuse_buf_alloc(&fb, uio->uio_resid);
	error = uiomove(fb.buf, uio->uio_resid, uio);
	if (error) {
		fuse_buf_free(&fb);
		return error;
	}
	ohd = fb.buf;

	mtx_lock(&fmp->ipc_lock);
	TAILQ_FOREACH_MUTABLE(fip, &fmp->reply_head, reply_entry, tmp) {
		if (fip->unique == ohd->unique) {
			TAILQ_REMOVE(&fmp->reply_head, fip, reply_entry);
			found = true;
			break;
		}
	}
	mtx_unlock(&fmp->ipc_lock);

	if (!found) {
		fuse_dbg("unique=%ju not found\n", ohd->unique);
		fuse_buf_free(&fb);
		return ENOMSG;
	}

	fip->reply = fb;
	ihd = fuse_in(fip);

	/* Non zero ohd->error is not /dev/fuse write error. */
	if (ohd->error == -ENOSYS) {
		fuse_set_nosys(fmp, ihd->opcode);
		fuse_dbgipc(fip, ohd->error, "ENOSYS");
	} else if (!ohd->error && fuse_audit_length(ihd, ohd)) {
		error = EPROTO;
		fuse_dbgipc(fip, error, "audit");
	} else
		fuse_dbgipc(fip, 0, "");

	/* Complete the IPC regardless of above result. */
	if (fuse_ipc_test_and_set_replied(fip))
		wakeup(fip);

	return error;
}

static void filt_fusedevdetach(struct knote*);
static int filt_fusedevread(struct knote*, long);
static int filt_fusedevwrite(struct knote*, long);

static struct filterops fusedevread_filterops =
	{ FILTEROP_ISFD,
	  NULL, filt_fusedevdetach, filt_fusedevread };
static struct filterops fusedevwrite_filterops =
	{ FILTEROP_ISFD,
	  NULL, filt_fusedevdetach, filt_fusedevwrite };

static int
fuse_device_kqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;
	struct klist *klist;
	struct fuse_mount *fmp;
	int error;

	error = devfs_get_cdevpriv(ap->a_fp, (void**)&fmp);
	if (error) {
		ap->a_result = error;
		return 0;
	}

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &fusedevread_filterops;
		kn->kn_hook = (caddr_t)fmp;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &fusedevwrite_filterops;
		kn->kn_hook = (caddr_t)fmp;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return 0;
	}

	klist = &fmp->kq.ki_note;
	knote_insert(klist, kn);

	return 0;
}

static void
filt_fusedevdetach(struct knote *kn)
{
	struct fuse_mount *fmp = (struct fuse_mount*)kn->kn_hook;
	struct klist *klist = &fmp->kq.ki_note;

	knote_remove(klist, kn);
}

static int
filt_fusedevread(struct knote *kn, long hint)
{
	struct fuse_mount *fmp = (struct fuse_mount*)kn->kn_hook;
	int ready = 0;

	mtx_lock(&fmp->ipc_lock);
	if (!TAILQ_EMPTY(&fmp->request_head))
		ready = 1;
	mtx_unlock(&fmp->ipc_lock);

	return ready;
}

static int
filt_fusedevwrite(struct knote *kn, long hint)
{
	return 1;
}

static struct dev_ops fuse_device_cdevsw = {
	{ "fuse", 0, D_MPSAFE, },
	.d_open = fuse_device_open,
	.d_close = fuse_device_close,
	.d_read = fuse_device_read,
	.d_write = fuse_device_write,
	.d_kqfilter = fuse_device_kqfilter,
};

int
fuse_device_init(void)
{
	fuse_dev = make_dev(&fuse_device_cdevsw, 0, UID_ROOT, GID_OPERATOR,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, "fuse");

	if (!fuse_dev)
		return ENOMEM;

	return 0;
}

void
fuse_device_cleanup(void)
{
	KKASSERT(fuse_dev);
	destroy_dev(fuse_dev);
}
