/*
 * Copyright (c) 2000-2001 Boris Popov
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * $FreeBSD: src/sys/netsmb/smb_dev.c,v 1.2.2.1 2001/05/22 08:32:33 bp Exp $
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/file.h>		/* Must come after sys/malloc.h */
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/thread2.h>
#include <sys/devfs.h>
#include <net/if.h>

#include "smb.h"
#include "smb_conn.h"
#include "smb_subr.h"
#include "smb_dev.h"

#define SMB_GETDEV(dev)		((struct smb_dev*)(dev)->si_drv1)
#define	SMB_CHECKMINOR(dev)	do { \
				    sdp = SMB_GETDEV(dev); \
				    if (sdp == NULL) return ENXIO; \
				} while(0)

static d_open_t	 nsmb_dev_open;
static d_close_t nsmb_dev_close;
static d_read_t	 nsmb_dev_read;
static d_write_t nsmb_dev_write;
static d_ioctl_t nsmb_dev_ioctl;
static d_clone_t nsmbclone;
DEVFS_DECLARE_CLONE_BITMAP(nsmb);

MODULE_VERSION(netsmb, NSMB_VERSION);

#define	SI_NAMED	0

static int smb_version = NSMB_VERSION;


SYSCTL_DECL(_net_smb);
SYSCTL_INT(_net_smb, OID_AUTO, version, CTLFLAG_RD, &smb_version, 0, "");

static MALLOC_DEFINE(M_NSMBDEV, "NETSMBDEV", "NET/SMB device");


/*
int smb_dev_queue(struct smb_dev *ndp, struct smb_rq *rqp, int prio);
*/

static struct dev_ops nsmb_ops = {
	{ NSMB_NAME, 0, 0 },
	.d_open =	nsmb_dev_open,
	.d_close =	nsmb_dev_close,
	.d_read =	nsmb_dev_read,
	.d_write =	nsmb_dev_write,
	.d_ioctl = 	nsmb_dev_ioctl,
};


static int
nsmb_dev_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct smb_dev *sdp;

	sdp = SMB_GETDEV(dev);
	if (sdp && (sdp->sd_flags & NSMBFL_OPEN))
		return EBUSY;
	if (sdp == NULL) {
		sdp = kmalloc(sizeof(*sdp), M_NSMBDEV, M_WAITOK);
		dev->si_drv1 = (void*)sdp;
	}
	/*
	 * XXX: this is just crazy - make a device for an already passed
	 * device...  someone should take care of it.
	 */
#if 0
	if ((dev->si_flags & SI_NAMED) == 0) {
		make_dev(&nsmb_ops, minor(dev),
			ap->a_cred->cr_uid, ap->a_cred->cr_gid,
			0700, NSMB_NAME"%d", lminor(dev));
	}
#endif
	bzero(sdp, sizeof(*sdp));
/*
	STAILQ_INIT(&sdp->sd_rqlist);
	STAILQ_INIT(&sdp->sd_rplist);
	bzero(&sdp->sd_pollinfo, sizeof(struct kqinfo));
*/
	crit_enter();
	sdp->sd_level = -1;
	sdp->sd_flags |= NSMBFL_OPEN;
	crit_exit();
	return 0;
}

static int
nsmb_dev_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct smb_dev *sdp;
	struct smb_vc *vcp;
	struct smb_share *ssp;
	struct smb_cred scred;

	SMB_CHECKMINOR(dev);
	crit_enter();
	if ((sdp->sd_flags & NSMBFL_OPEN) == 0) {
		crit_exit();
		return EBADF;
	}
	smb_makescred(&scred, curthread, NULL);
	ssp = sdp->sd_share;
	if (ssp != NULL)
		smb_share_rele(ssp, &scred);
	vcp = sdp->sd_vc;
	if (vcp != NULL)
		smb_vc_rele(vcp, &scred);
/*
	smb_flushq(&sdp->sd_rqlist);
	smb_flushq(&sdp->sd_rplist);
*/
	dev->si_drv1 = NULL;
	kfree(sdp, M_NSMBDEV);
	crit_exit();
	devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(nsmb), dev->si_uminor);
	destroy_dev(dev);
	return 0;
}


static int
nsmb_dev_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	struct smb_dev *sdp;
	struct smb_vc *vcp;
	struct smb_share *ssp;
	struct smb_cred scred;
	int error = 0;

	SMB_CHECKMINOR(dev);
	if ((sdp->sd_flags & NSMBFL_OPEN) == 0)
		return EBADF;

	smb_makescred(&scred, NULL, ap->a_cred);
	switch (ap->a_cmd) {
	    case SMBIOC_OPENSESSION:
		if (sdp->sd_vc)
			return EISCONN;
		error = smb_usr_opensession((struct smbioc_ossn*)data,
		    &scred, &vcp);
		if (error)
			break;
		sdp->sd_vc = vcp;
		smb_vc_unlock(vcp, 0);
		sdp->sd_level = SMBL_VC;
		break;
	    case SMBIOC_OPENSHARE:
		if (sdp->sd_share)
			return EISCONN;
		if (sdp->sd_vc == NULL)
			return ENOTCONN;
		error = smb_usr_openshare(sdp->sd_vc,
		    (struct smbioc_oshare*)data, &scred, &ssp);
		if (error)
			break;
		sdp->sd_share = ssp;
		smb_share_unlock(ssp, 0);
		sdp->sd_level = SMBL_SHARE;
		break;
	    case SMBIOC_REQUEST:
		if (sdp->sd_share == NULL)
			return ENOTCONN;
		error = smb_usr_simplerequest(sdp->sd_share,
		    (struct smbioc_rq*)data, &scred);
		break;
	    case SMBIOC_T2RQ:
		if (sdp->sd_share == NULL)
			return ENOTCONN;
		error = smb_usr_t2request(sdp->sd_share,
		    (struct smbioc_t2rq*)data, &scred);
		break;
	    case SMBIOC_SETFLAGS: {
		struct smbioc_flags *fl = (struct smbioc_flags*)data;
		int on;
	
		if (fl->ioc_level == SMBL_VC) {
			if (fl->ioc_mask & SMBV_PERMANENT) {
				on = fl->ioc_flags & SMBV_PERMANENT;
				if ((vcp = sdp->sd_vc) == NULL)
					return ENOTCONN;
				error = smb_vc_get(vcp, LK_EXCLUSIVE, &scred);
				if (error)
					break;
				if (on && (vcp->obj.co_flags & SMBV_PERMANENT) == 0) {
					vcp->obj.co_flags |= SMBV_PERMANENT;
					smb_vc_ref(vcp);
				} else if (!on && (vcp->obj.co_flags & SMBV_PERMANENT)) {
					vcp->obj.co_flags &= ~SMBV_PERMANENT;
					smb_vc_rele(vcp, &scred);
				}
				smb_vc_put(vcp, &scred);
			} else
				error = EINVAL;
		} else if (fl->ioc_level == SMBL_SHARE) {
			if (fl->ioc_mask & SMBS_PERMANENT) {
				on = fl->ioc_flags & SMBS_PERMANENT;
				if ((ssp = sdp->sd_share) == NULL)
					return ENOTCONN;
				error = smb_share_get(ssp, LK_EXCLUSIVE, &scred);
				if (error)
					break;
				if (on && (ssp->obj.co_flags & SMBS_PERMANENT) == 0) {
					ssp->obj.co_flags |= SMBS_PERMANENT;
					smb_share_ref(ssp);
				} else if (!on && (ssp->obj.co_flags & SMBS_PERMANENT)) {
					ssp->obj.co_flags &= ~SMBS_PERMANENT;
					smb_share_rele(ssp, &scred);
				}
				smb_share_put(ssp, &scred);
			} else
				error = EINVAL;
			break;
		} else
			error = EINVAL;
		break;
	    }
	    case SMBIOC_LOOKUP:
		if (sdp->sd_vc || sdp->sd_share)
			return EISCONN;
		vcp = NULL;
		ssp = NULL;
		error = smb_usr_lookup((struct smbioc_lookup*)data, &scred, &vcp, &ssp);
		if (error)
			break;
		if (vcp) {
			sdp->sd_vc = vcp;
			smb_vc_unlock(vcp, 0);
			sdp->sd_level = SMBL_VC;
		}
		if (ssp) {
			sdp->sd_share = ssp;
			smb_share_unlock(ssp, 0);
			sdp->sd_level = SMBL_SHARE;
		}
		break;
	    case SMBIOC_READ: case SMBIOC_WRITE: {
		struct smbioc_rw *rwrq = (struct smbioc_rw*)data;
		struct uio auio;
		struct iovec iov;
	
		if ((ssp = sdp->sd_share) == NULL)
			return ENOTCONN;
		iov.iov_base = rwrq->ioc_base;
		iov.iov_len = rwrq->ioc_cnt;
		auio.uio_iov = &iov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = rwrq->ioc_offset;
		auio.uio_resid = rwrq->ioc_cnt;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_rw = (ap->a_cmd == SMBIOC_READ) ? UIO_READ : UIO_WRITE;
		auio.uio_td = curthread;
		if (ap->a_cmd == SMBIOC_READ)
			error = smb_read(ssp, rwrq->ioc_fh, &auio, &scred);
		else
			error = smb_write(ssp, rwrq->ioc_fh, &auio, &scred);
		rwrq->ioc_cnt -= auio.uio_resid;
		break;
	    }
	    default:
		error = ENODEV;
	}
	return error;
}

static int
nsmb_dev_read(struct dev_read_args *ap)
{
	return EACCES;
}

static int
nsmb_dev_write(struct dev_write_args *ap)
{
	return EACCES;
}

static int
nsmbclone(struct dev_clone_args *ap)
{
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(nsmb), 0);
	ap->a_dev = make_only_dev(&nsmb_ops, unit, 0, 0, 0600,
				  NSMB_NAME"%d", unit);

	return 0;
}

static int
nsmb_dev_load(module_t mod, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	    case MOD_LOAD:
		error = smb_sm_init();
		if (error)
			break;
		error = smb_iod_init();
		if (error) {
			smb_sm_done();
			break;
		}
		make_autoclone_dev(&nsmb_ops, &DEVFS_CLONE_BITMAP(nsmb),
			nsmbclone, 0, 0, 0700, NSMB_NAME);

		kprintf("netsmb_dev: loaded\n");
		break;
	    case MOD_UNLOAD:
		smb_iod_done();
		error = smb_sm_done();
		error = 0;
		devfs_clone_handler_del(NSMB_NAME);
		dev_ops_remove_all(&nsmb_ops);
		devfs_clone_bitmap_uninit(&DEVFS_CLONE_BITMAP(nsmb));
		kprintf("netsmb_dev: unloaded\n");
		break;
	    default:
		error = EINVAL;
		break;
	}
	return error;
}

DEV_MODULE (dev_netsmb, nsmb_dev_load, 0);

int
smb_dev2share(int fd, int mode, struct smb_cred *scred,
	struct smb_share **sspp)
{
	struct file *fp;
	struct vnode *vp;
	struct smb_dev *sdp;
	struct smb_share *ssp;
	cdev_t dev;
	int error;

	KKASSERT(scred->scr_td->td_proc);

	fp = holdfp(scred->scr_td->td_proc->p_fd, fd, FREAD|FWRITE);
	if (fp == NULL)
		return EBADF;

	vp = (struct vnode*)fp->f_data;
	if (vp == NULL) {
		error = EBADF;
		goto done;
	}
	dev = vn_todev(vp);
	if (dev == NULL) {
		error = EBADF;
		goto done;
	}
	SMB_CHECKMINOR(dev);
	ssp = sdp->sd_share;
	if (ssp == NULL) {
		error = ENOTCONN;
		goto done;
	}
	error = smb_share_get(ssp, LK_EXCLUSIVE, scred);
	if (error == 0)
		*sspp = ssp;
done:
	fdrop(fp);
	return (error);
}
