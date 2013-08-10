/*
 * Copyright (c) 2006-2007 The DragonFly Project.  All rights reserved.
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
/*
 * This module implements the core syslink() system call and provides
 * glue for kernel syslink frontends and backends, creating a intra-host
 * communications infrastructure and DMA transport abstraction.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/alist.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/lock.h>
#include <sys/uio.h>
#include <sys/objcache.h>
#include <sys/queue.h>
#include <sys/thread.h>
#include <sys/tree.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/sysref.h>
#include <sys/syslink.h>
#include <sys/syslink_msg.h>
#include <netinet/in.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/buf2.h>
#include <sys/mplock2.h>

/*
 * Syslink Connection abstraction
 */
struct slcommon {
	struct spinlock spin;
	int	refs;
};

struct sldesc {
	struct slmsgq	inq;
	struct slmsg_rb_tree reply_rb_root; /* replies to requests */
	struct spinlock	spin;
	struct sldesc	*peer;		/* peer syslink, if any */
	struct file	*xfp;		/* external file pointer */
	struct slcommon	*common;
	int	flags;
	int	rwaiters;		/* number of threads waiting */
	int	wblocked;		/* blocked waiting for us to drain */
	size_t	cmdbytes;		/* unreplied commands pending */
	size_t  repbytes;		/* undrained replies pending */
	int	(*backend_wblocked)(struct sldesc *, int, sl_proto_t);
	int	(*backend_write)(struct sldesc *, struct slmsg *);
	void	(*backend_reply)(struct sldesc *,struct slmsg *,struct slmsg *);
	void	(*backend_dispose)(struct sldesc *, struct slmsg *);
};

#define SLF_RSHUTDOWN	0x0001
#define SLF_WSHUTDOWN	0x0002

static int syslink_cmd_new(struct syslink_info_new *info, int *result);
static struct sldesc *allocsldesc(struct slcommon *common);
static void setsldescfp(struct sldesc *sl, struct file *fp);
static void shutdownsldesc(struct sldesc *sl, int how);
static void shutdownsldesc2(struct sldesc *sl, int how);
static void sldrop(struct sldesc *sl);
static int syslink_validate_msg(struct syslink_msg *msg, int bytes);
static int syslink_validate_elm(struct syslink_elm *elm, sl_reclen_t bytes,
				 int swapit, int depth);

static int sl_local_mmap(struct slmsg *slmsg, char *base, size_t len);
static void sl_local_munmap(struct slmsg *slmsg);

static int backend_wblocked_user(struct sldesc *sl, int nbio, sl_proto_t proto);
static int backend_write_user(struct sldesc *sl, struct slmsg *slmsg);
static void backend_reply_user(struct sldesc *sl, struct slmsg *slcmd,
			       struct slmsg *slrep);
static void backend_dispose_user(struct sldesc *sl, struct slmsg *slmsg);

static int backend_wblocked_kern(struct sldesc *sl, int nbio, sl_proto_t proto);
static int backend_write_kern(struct sldesc *sl, struct slmsg *slmsg);
static void backend_reply_kern(struct sldesc *sl, struct slmsg *slcmd,
			       struct slmsg *slrep);
static void backend_dispose_kern(struct sldesc *sl, struct slmsg *slmsg);
static void slmsg_put(struct slmsg *slmsg);

/*
 * Objcache memory backend
 *
 * All three object caches return slmsg structures but each is optimized
 * for syslink message buffers of varying sizes.  We use the slightly
 * more complex ctor/dtor API in order to provide ready-to-go slmsg's.
 */

static struct objcache *sl_objcache_big;
static struct objcache *sl_objcache_small;
static struct objcache *sl_objcache_none;

MALLOC_DEFINE(M_SYSLINK, "syslink", "syslink manager");

static boolean_t slmsg_ctor(void *data, void *private, int ocflags);
static void slmsg_dtor(void *data, void *private);

static
void
syslinkinit(void *dummy __unused)
{
	size_t n = sizeof(struct slmsg);

	sl_objcache_none = objcache_create_mbacked(M_SYSLINK, n, 0, 64,
						   slmsg_ctor, slmsg_dtor,
						   &sl_objcache_none);
	sl_objcache_small= objcache_create_mbacked(M_SYSLINK, n, 0, 64,
						   slmsg_ctor, slmsg_dtor,
						   &sl_objcache_small);
	sl_objcache_big  = objcache_create_mbacked(M_SYSLINK, n, 0, 16,
						   slmsg_ctor, slmsg_dtor,
						   &sl_objcache_big);
}

static
boolean_t
slmsg_ctor(void *data, void *private, int ocflags)
{
	struct slmsg *slmsg = data;

	bzero(slmsg, sizeof(*slmsg));

	slmsg->oc = *(struct objcache **)private;
	if (slmsg->oc == sl_objcache_none) {
		slmsg->maxsize = 0;
	} else if (slmsg->oc == sl_objcache_small) {
		slmsg->maxsize = SLMSG_SMALL;
	} else if (slmsg->oc == sl_objcache_big) {
		slmsg->maxsize = SLMSG_BIG;
	} else {
		panic("slmsg_ctor: bad objcache?");
	}
	if (slmsg->maxsize) {
		slmsg->msg = kmalloc(slmsg->maxsize,
				     M_SYSLINK, M_WAITOK|M_ZERO);
	}
	xio_init(&slmsg->xio);
	return(TRUE);
}

static
void
slmsg_dtor(void *data, void *private)
{
	struct slmsg *slmsg = data;

	if (slmsg->maxsize && slmsg->msg) {
		kfree(slmsg->msg, M_SYSLINK);
		slmsg->msg = NULL;
	}
	slmsg->oc = NULL;
}

SYSINIT(syslink, SI_BOOT2_MACHDEP, SI_ORDER_ANY, syslinkinit, NULL)

static int rb_slmsg_compare(struct slmsg *msg1, struct slmsg *msg2);
RB_GENERATE2(slmsg_rb_tree, slmsg, rbnode, rb_slmsg_compare,
	     sysid_t, msg->sm_msgid);

/*
 * Sysctl elements
 */
static int syslink_enabled;
SYSCTL_NODE(_kern, OID_AUTO, syslink, CTLFLAG_RW, 0, "Pipe operation");
SYSCTL_INT(_kern_syslink, OID_AUTO, enabled,
	    CTLFLAG_RW, &syslink_enabled, 0, "Enable SYSLINK");
static size_t syslink_bufsize = 65536;
SYSCTL_UINT(_kern_syslink, OID_AUTO, bufsize,
	    CTLFLAG_RW, &syslink_bufsize, 0, "Maximum buffer size");

/*
 * Fileops API - typically used to glue a userland frontend with a
 *		 kernel backend.
 */

static int slfileop_read(struct file *fp, struct uio *uio,
			struct ucred *cred, int flags);
static int slfileop_write(struct file *fp, struct uio *uio,
			 struct ucred *cred, int flags);
static int slfileop_close(struct file *fp);
static int slfileop_stat(struct file *fp, struct stat *sb, struct ucred *cred);
static int slfileop_shutdown(struct file *fp, int how);
static int slfileop_ioctl(struct file *fp, u_long cmd, caddr_t data,
			 struct ucred *cred, struct sysmsg *msg);
static int slfileop_kqfilter(struct file *fp, struct knote *kn);

static struct fileops syslinkops = {
    .fo_read =		slfileop_read,
    .fo_write =		slfileop_write,
    .fo_ioctl =		slfileop_ioctl,
    .fo_kqfilter =	slfileop_kqfilter,
    .fo_stat =		slfileop_stat,
    .fo_close =		slfileop_close,
    .fo_shutdown =	slfileop_shutdown
};

/************************************************************************
 *			PRIMARY SYSTEM CALL INTERFACE			*
 ************************************************************************
 *
 * syslink(int cmd, struct syslink_info *info, size_t bytes)
 *
 * MPALMOSTSAFE
 */
int
sys_syslink(struct syslink_args *uap)
{
	union syslink_info_all info;
	int error;

	/*
	 * System call is under construction and disabled by default. 
	 * Superuser access is also required for now, but eventually
	 * will not be needed.
	 */
	if (syslink_enabled == 0)
		return (EAUTH);
	error = priv_check(curthread, PRIV_ROOT);
	if (error)
		return (error);

	/*
	 * Load and validate the info structure.  Unloaded bytes are zerod
	 * out.  The label field must always be 0-filled, even if not used
	 * for a command.
	 */
	bzero(&info, sizeof(info));
	if ((unsigned)uap->bytes <= sizeof(info)) {
		if (uap->bytes)
			error = copyin(uap->info, &info, uap->bytes);
	} else {
		error = EINVAL;
	}
	if (error)
		return (error);
	get_mplock();

	/*
	 * Process the command
	 */
	switch(uap->cmd) {
	case SYSLINK_CMD_NEW:
		error = syslink_cmd_new(&info.cmd_new, &uap->sysmsg_result);
		break;
	default:
		error = EINVAL;
		break;
	}

	rel_mplock();
	if (error == 0 && info.head.wbflag)
		copyout(&info, uap->info, uap->bytes);
	return (error);
}

/*
 * Create a linked pair of descriptors, like a pipe.
 */
static
int
syslink_cmd_new(struct syslink_info_new *info, int *result)
{
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct file *fp1;
	struct file *fp2;
	struct sldesc *sl;
	struct sldesc *slpeer;
	int error;
	int fd1, fd2;

	error = falloc(td->td_lwp, &fp1, &fd1);
	if (error)
		return(error);
	error = falloc(td->td_lwp, &fp2, &fd2);
	if (error) {
		fsetfd(fdp, NULL, fd1);
		fdrop(fp1);
		return(error);
	}
	slpeer = allocsldesc(NULL);
	slpeer->backend_wblocked = backend_wblocked_user;
	slpeer->backend_write = backend_write_user;
	slpeer->backend_reply = backend_reply_user;
	slpeer->backend_dispose = backend_dispose_user;
	sl = allocsldesc(slpeer->common);
	sl->peer = slpeer;
	sl->backend_wblocked = backend_wblocked_user;
	sl->backend_write = backend_write_user;
	sl->backend_reply = backend_reply_user;
	sl->backend_dispose = backend_dispose_user;
	slpeer->peer = sl;

	setsldescfp(sl, fp1);
	setsldescfp(slpeer, fp2);

	fsetfd(fdp, fp1, fd1);
	fdrop(fp1);
	fsetfd(fdp, fp2, fd2);
	fdrop(fp2);

	info->head.wbflag = 1;	/* write back */
	info->fds[0] = fd1;
	info->fds[1] = fd2;

	return(0);
}

/************************************************************************
 *			LOW LEVEL SLDESC SUPPORT			*
 ************************************************************************
 *
 */

static
struct sldesc *
allocsldesc(struct slcommon *common)
{
	struct sldesc *sl;

	sl = kmalloc(sizeof(struct sldesc), M_SYSLINK, M_WAITOK|M_ZERO);
	if (common == NULL)
		common = kmalloc(sizeof(*common), M_SYSLINK, M_WAITOK|M_ZERO);
	TAILQ_INIT(&sl->inq);		/* incoming requests */
	RB_INIT(&sl->reply_rb_root);	/* match incoming replies */
	spin_init(&sl->spin);
	sl->common = common;
	++common->refs;
	return(sl);
}

static
void
setsldescfp(struct sldesc *sl, struct file *fp)
{
	sl->xfp = fp;
	fp->f_type = DTYPE_SYSLINK;
	fp->f_flag = FREAD | FWRITE;
	fp->f_ops = &syslinkops;
	fp->f_data = sl;
}

/*
 * Red-black tree compare function
 */
static
int
rb_slmsg_compare(struct slmsg *msg1, struct slmsg *msg2)
{
	if (msg1->msg->sm_msgid < msg2->msg->sm_msgid)
		return(-1);
	if (msg1->msg->sm_msgid == msg2->msg->sm_msgid)
		return(0);
	return(1);
}

static
void
shutdownsldesc(struct sldesc *sl, int how)
{
	struct slmsg *slmsg;
	int rhow;

	shutdownsldesc2(sl, how);

	/*
	 * Return unread and unreplied messages
	 */
	spin_lock(&sl->spin);
	while ((slmsg = TAILQ_FIRST(&sl->inq)) != NULL) {
		TAILQ_REMOVE(&sl->inq, slmsg, tqnode);
		spin_unlock(&sl->spin);
		if (slmsg->msg->sm_proto & SM_PROTO_REPLY) {
			sl->repbytes -= slmsg->maxsize;
			slmsg->flags &= ~SLMSGF_ONINQ;
			sl->peer->backend_dispose(sl->peer, slmsg);
		}
		/* leave ONINQ set for commands, it will cleared below */
		spin_lock(&sl->spin);
	}
	while ((slmsg = RB_ROOT(&sl->reply_rb_root)) != NULL) {
		RB_REMOVE(slmsg_rb_tree, &sl->reply_rb_root, slmsg);
		sl->cmdbytes -= slmsg->maxsize;
		spin_unlock(&sl->spin);
		slmsg->flags &= ~SLMSGF_ONINQ;
		sl->peer->backend_reply(sl->peer, slmsg, NULL);
		spin_lock(&sl->spin);
	}
	spin_unlock(&sl->spin);

	/*
	 * Call shutdown on the peer with the opposite flags
	 */
	rhow = 0;
	switch(how) {
	case SHUT_RD:
		rhow = SHUT_WR;
		break;
	case SHUT_WR:
		rhow = SHUT_WR;
		break;
	case SHUT_RDWR:
		rhow = SHUT_RDWR;
		break;
	}
	shutdownsldesc2(sl->peer, rhow);
}

static
void
shutdownsldesc2(struct sldesc *sl, int how)
{
	spin_lock(&sl->spin);
	switch(how) {
	case SHUT_RD:
		sl->flags |= SLF_RSHUTDOWN;
		break;
	case SHUT_WR:
		sl->flags |= SLF_WSHUTDOWN;
		break;
	case SHUT_RDWR:
		sl->flags |= SLF_RSHUTDOWN | SLF_WSHUTDOWN;
		break;
	}
	spin_unlock(&sl->spin);

	/*
	 * Handle signaling on the user side
	 */
	if (how & SHUT_RD) {
		if (sl->rwaiters)
			wakeup(&sl->rwaiters);
	}
	if (how & SHUT_WR) {
		if (sl->wblocked) {
			sl->wblocked = 0;	/* race ok */
			wakeup(&sl->wblocked);
		}
	}
}

static
void
sldrop(struct sldesc *sl)
{
	struct sldesc *slpeer;

	spin_lock(&sl->common->spin);
	if (--sl->common->refs == 0) {
		spin_unlock(&sl->common->spin);
		if ((slpeer = sl->peer) != NULL) {
			sl->peer = NULL;
			slpeer->peer = NULL;
			slpeer->common = NULL;
			KKASSERT(slpeer->xfp == NULL);
			KKASSERT(TAILQ_EMPTY(&slpeer->inq));
			KKASSERT(RB_EMPTY(&slpeer->reply_rb_root));
			kfree(slpeer, M_SYSLINK);
		}
		KKASSERT(sl->xfp == NULL);
		KKASSERT(TAILQ_EMPTY(&sl->inq));
		KKASSERT(RB_EMPTY(&sl->reply_rb_root));
		kfree(sl->common, M_SYSLINK);
		sl->common = NULL;
		kfree(sl, M_SYSLINK);
	} else {
		spin_unlock(&sl->common->spin);
	}
}

static
void
slmsg_put(struct slmsg *slmsg)
{
	if (slmsg->flags & SLMSGF_HASXIO) {
		slmsg->flags &= ~SLMSGF_HASXIO;
		get_mplock();
		xio_release(&slmsg->xio);
		rel_mplock();
	}
	slmsg->flags &= ~SLMSGF_LINMAP;
	objcache_put(slmsg->oc, slmsg);
}

/************************************************************************
 *				FILEOPS API				*
 ************************************************************************
 *
 * Implement userland fileops.
 *
 * MPSAFE ops
 */
static
int
slfileop_read(struct file *fp, struct uio *uio, struct ucred *cred, int flags)
{
	struct sldesc *sl = fp->f_data;		/* fp refed on call */
	struct slmsg *slmsg;
	struct iovec *iov0;
	struct iovec *iov1;
	struct syslink_msg *wmsg;
	int error;
	int nbio;

	/*
	 * Kinda messy.  Figure out the non-blocking state
	 */
	if (flags & O_FBLOCKING)
		nbio = 0;
	else if (flags & O_FNONBLOCKING)
		nbio = 1;
	else if (fp->f_flag & O_NONBLOCK)
		nbio = 1;
	else
		nbio = 0;

	/*
	 * Validate the uio.
	 *
	 * iov0 - message buffer
	 * iov1 - DMA buffer or backup buffer
	 */
	if (uio->uio_iovcnt < 1) {
		error = 0;
		goto done2;
	}
	iov0 = &uio->uio_iov[0];
	if (uio->uio_iovcnt > 2) {
		error = EINVAL;
		goto done2;
	}

	/*
	 * Get a message, blocking if necessary.
	 */
	spin_lock(&sl->spin);
	while ((slmsg = TAILQ_FIRST(&sl->inq)) == NULL) {
		if (sl->flags & SLF_RSHUTDOWN) {
			error = 0;
			goto done1;
		}
		if (nbio) {
			error = EAGAIN;
			goto done1;
		}
		++sl->rwaiters;
		error = ssleep(&sl->rwaiters, &sl->spin, PCATCH, "slrmsg", 0);
		--sl->rwaiters;
		if (error)
			goto done1;
	}
	wmsg = slmsg->msg;

	/*
	 * We have a message and still hold the spinlock.  Make sure the
	 * uio has enough room to hold the message.
	 *
	 * Note that replies do not have XIOs.
	 */
	if (slmsg->msgsize > iov0->iov_len) {
		error = ENOSPC;
		goto done1;
	}
	if (slmsg->xio.xio_bytes) {
		if (uio->uio_iovcnt != 2) {
			error = ENOSPC;
			goto done1;
		}
		iov1 = &uio->uio_iov[1];
		if (slmsg->xio.xio_bytes > iov1->iov_len) {
			error = ENOSPC;
			goto done1;
		}
	} else {
		iov1 = NULL;
	}

	/*
	 * Dequeue the message.  Adjust repbytes immediately.  cmdbytes
	 * are adjusted when the command is replied to, not here.
	 */
	TAILQ_REMOVE(&sl->inq, slmsg, tqnode);
	if (slmsg->msg->sm_proto & SM_PROTO_REPLY)
		sl->repbytes -= slmsg->maxsize;
	spin_unlock(&sl->spin);

	/*
	 * Load the message data into the user buffer.
	 *
	 * If receiving a command an XIO may exist specifying a DMA buffer.
	 * For commands, if DMAW is set we have to copy or map the buffer
	 * so the caller can access the data being written.  If DMAR is set
	 * we do not have to copy but we still must map the buffer so the
	 * caller can directly fill in the data being requested.
	 */
	error = uiomove((void *)slmsg->msg, slmsg->msgsize, uio);
	if (error == 0 && slmsg->xio.xio_bytes &&
	    (wmsg->sm_head.se_cmd & SE_CMDF_REPLY) == 0) {
		if (wmsg->sm_head.se_cmd & SE_CMDF_DMAW) {
			/*
			 * Data being passed to caller or being passed in both
			 * directions, copy or map.
			 */
			get_mplock();
			if ((flags & O_MAPONREAD) &&
			    (slmsg->xio.xio_flags & XIOF_VMLINEAR)) {
				error = sl_local_mmap(slmsg,
						      iov1->iov_base,
						      iov1->iov_len);
				if (error)
				error = xio_copy_xtou(&slmsg->xio, 0,
						      iov1->iov_base,
						      slmsg->xio.xio_bytes);
			} else {
				error = xio_copy_xtou(&slmsg->xio, 0,
						      iov1->iov_base,
						      slmsg->xio.xio_bytes);
			}
			rel_mplock();
		} else if (wmsg->sm_head.se_cmd & SE_CMDF_DMAR) {
			/*
			 * Data will be passed back to originator, map
			 * the buffer if we can, else use the backup
			 * buffer at the same VA supplied by the caller.
			 */
			get_mplock();
			if ((flags & O_MAPONREAD) &&
			    (slmsg->xio.xio_flags & XIOF_VMLINEAR)) {
				error = sl_local_mmap(slmsg,
						      iov1->iov_base,
						      iov1->iov_len);
				error = 0; /* ignore errors */
			}
			rel_mplock();
		}
	}

	/*
	 * Clean up.
	 */
	if (error) {
		/*
		 * Requeue the message if we could not read it successfully
		 */
		spin_lock(&sl->spin);
		TAILQ_INSERT_HEAD(&sl->inq, slmsg, tqnode);
		slmsg->flags |= SLMSGF_ONINQ;
		spin_unlock(&sl->spin);
	} else if (slmsg->msg->sm_proto & SM_PROTO_REPLY) {
		/*
		 * Dispose of any received reply after we've copied it
		 * to userland.  We don't need the slmsg any more.
		 */
		slmsg->flags &= ~SLMSGF_ONINQ;
		sl->peer->backend_dispose(sl->peer, slmsg);
		if (sl->wblocked && sl->repbytes < syslink_bufsize) {
			sl->wblocked = 0;	/* MP race ok here */
			wakeup(&sl->wblocked);
		}
	} else {
		/*
		 * Leave the command in the RB tree but clear ONINQ now
		 * that we have returned it to userland so userland can
		 * reply to it.
		 */
		slmsg->flags &= ~SLMSGF_ONINQ;
	}
	return(error);
done1:
	spin_unlock(&sl->spin);
done2:
	return(error);
}

/*
 * Userland writes syslink message (optionally with DMA buffer in iov[1]).
 */
static
int
slfileop_write(struct file *fp, struct uio *uio, struct ucred *cred, int flags)
{
	struct sldesc *sl = fp->f_data;
	struct slmsg *slmsg;
	struct slmsg *slcmd;
	struct syslink_msg sltmp;
	struct syslink_msg *wmsg;	/* wire message */
	struct iovec *iov0;
	struct iovec *iov1;
	sl_proto_t proto;
	int nbio;
	int error;
	int xflags;

	/*
	 * Kinda messy.  Figure out the non-blocking state
	 */
	if (flags & O_FBLOCKING)
		nbio = 0;
	else if (flags & O_FNONBLOCKING)
		nbio = 1;
	else if (fp->f_flag & O_NONBLOCK)
		nbio = 1;
	else
		nbio = 0;

	/*
	 * Validate the uio
	 */
	if (uio->uio_iovcnt < 1) {
		error = 0;
		goto done2;
	}
	iov0 = &uio->uio_iov[0];
	if (iov0->iov_len > SLMSG_BIG) {
		error = EFBIG;
		goto done2;
	}
	if (uio->uio_iovcnt > 2) {
		error = EFBIG;
		goto done2;
	}
	if (uio->uio_iovcnt > 1) {
		iov1 = &uio->uio_iov[1];
		if (iov1->iov_len > XIO_INTERNAL_SIZE) {
			error = EFBIG;
			goto done2;
		}
		if ((intptr_t)iov1->iov_base & PAGE_MASK) {
			error = EINVAL;
			goto done2;
		}
	} else {
		iov1 = NULL;
	}

	/*
	 * Handle the buffer-full case.  slpeer cmdbytes is managed
	 * by the backend function, not us so if the callback just
	 * directly implements the message and never adjusts cmdbytes,
	 * we will never sleep here.
	 */
	if (sl->flags & SLF_WSHUTDOWN) {
		error = EPIPE;
		goto done2;
	}

	/*
	 * Only commands can block the pipe, not replies.  Otherwise a
	 * deadlock is possible.
	 */
	error = copyin(iov0->iov_base, &sltmp, sizeof(sltmp));
	if (error)
		goto done2;
	if ((proto = sltmp.sm_proto) & SM_PROTO_ENDIAN_REV)
		proto = bswap16(proto);
	error = sl->peer->backend_wblocked(sl->peer, nbio, proto);
	if (error)
		goto done2;

	/*
	 * Allocate a slmsg and load the message.  Note that the bytes
	 * returned to userland only reflects the primary syslink message
	 * and does not include any DMA buffers.
	 */
	if (iov0->iov_len <= SLMSG_SMALL)
		slmsg = objcache_get(sl_objcache_small, M_WAITOK);
	else
		slmsg = objcache_get(sl_objcache_big, M_WAITOK);
	slmsg->msgsize = iov0->iov_len;
	wmsg = slmsg->msg;

	error = uiomove((void *)wmsg, iov0->iov_len, uio);
	if (error)
		goto done1;
	error = syslink_validate_msg(wmsg, slmsg->msgsize);
	if (error)
		goto done1;

	if ((wmsg->sm_head.se_cmd & SE_CMDF_REPLY) == 0) {
		/*
		 * Install the XIO for commands if any DMA flags are set.
		 *
		 * XIOF_VMLINEAR requires that the XIO represent a
		 * contiguous set of pages associated with a single VM
		 * object (so the reader side can mmap it easily).
		 *
		 * XIOF_VMLINEAR might not be set when the kernel sends
		 * commands to userland so the reader side backs off to
		 * a backup buffer if it isn't set, but we require it
		 * for userland writes.
		 */
		xflags = XIOF_VMLINEAR;
		if (wmsg->sm_head.se_cmd & SE_CMDF_DMAR)
			xflags |= XIOF_READ | XIOF_WRITE;
		else if (wmsg->sm_head.se_cmd & SE_CMDF_DMAW)
			xflags |= XIOF_READ;
		if (xflags && iov1) {
			get_mplock();
			error = xio_init_ubuf(&slmsg->xio, iov1->iov_base,
					      iov1->iov_len, xflags);
			rel_mplock();
			if (error)
				goto done1;
			slmsg->flags |= SLMSGF_HASXIO;
		}
		error = sl->peer->backend_write(sl->peer, slmsg);
	} else {
		/*
		 * Replies have to be matched up against received commands.
		 */
		spin_lock(&sl->spin);
		slcmd = slmsg_rb_tree_RB_LOOKUP(&sl->reply_rb_root,
						slmsg->msg->sm_msgid);
		if (slcmd == NULL || (slcmd->flags & SLMSGF_ONINQ)) {
			error = ENOENT;
			spin_unlock(&sl->spin);
			goto done1;
		}
		RB_REMOVE(slmsg_rb_tree, &sl->reply_rb_root, slcmd);
		sl->cmdbytes -= slcmd->maxsize;
		spin_unlock(&sl->spin);

		/*
		 * If the original command specified DMAR, has an xio, and
		 * our write specifies a DMA buffer, then we can do a
		 * copyback.  But if we are linearly mapped and the caller
		 * is using the map base address, then the caller filled in
		 * the data via the direct memory map and no copyback is
		 * needed.
		 */
		if ((slcmd->msg->sm_head.se_cmd & SE_CMDF_DMAR) && iov1 &&
		    (slcmd->flags & SLMSGF_HASXIO) &&
		    ((slcmd->flags & SLMSGF_LINMAP) == 0 ||
		     iov1->iov_base != slcmd->vmbase)
		) {
			size_t count;
			if (iov1->iov_len > slcmd->xio.xio_bytes)
				count = slcmd->xio.xio_bytes;
			else
				count = iov1->iov_len;
			get_mplock();
			error = xio_copy_utox(&slcmd->xio, 0, iov1->iov_base,
					      count);
			rel_mplock();
		}

		/*
		 * If we had mapped a DMA buffer, remove it
		 */
		if (slcmd->flags & SLMSGF_LINMAP) {
			get_mplock();
			sl_local_munmap(slcmd);
			rel_mplock();
		}

		/*
		 * Reply and handle unblocking
		 */
		sl->peer->backend_reply(sl->peer, slcmd, slmsg);
		if (sl->wblocked && sl->cmdbytes < syslink_bufsize) {
			sl->wblocked = 0;	/* MP race ok here */
			wakeup(&sl->wblocked);
		}

		/*
		 * slmsg has already been dealt with, make sure error is
		 * 0 so we do not double-free it.
		 */
		error = 0;
	}
	/* fall through */
done1:
	if (error)
		slmsg_put(slmsg);
	/* fall through */
done2:
	return(error);
}

/*
 * Close a syslink descriptor.
 *
 * Disassociate the syslink from the file descriptor and disconnect from
 * any peer.
 */
static
int
slfileop_close(struct file *fp)
{
	struct sldesc *sl;

	/*
	 * Disassociate the file pointer.  Take ownership of the ref on the
	 * sldesc.
	 */
	sl = fp->f_data;
	fp->f_data = NULL;
	fp->f_ops = &badfileops;
	sl->xfp = NULL;

	/*
	 * Shutdown both directions.  The other side will not issue API
	 * calls to us after we've shutdown both directions.
	 */
	shutdownsldesc(sl, SHUT_RDWR);

	/*
	 * Cleanup
	 */
	KKASSERT(sl->cmdbytes == 0);
	KKASSERT(sl->repbytes == 0);
	sldrop(sl);
	return(0);
}

/*
 * MPSAFE
 */
static
int
slfileop_stat (struct file *fp, struct stat *sb, struct ucred *cred)
{
	return(EINVAL);
}

static
int
slfileop_shutdown (struct file *fp, int how)
{
	shutdownsldesc((struct sldesc *)fp->f_data, how);
	return(0);
}

static
int
slfileop_ioctl (struct file *fp, u_long cmd, caddr_t data,
		struct ucred *cred, struct sysmsg *msg)
{
	return(EINVAL);
}

static
int
slfileop_kqfilter(struct file *fp, struct knote *kn)
{
	return(0);
}

/************************************************************************
 *			    LOCAL MEMORY MAPPING 			*
 ************************************************************************
 *
 * This feature is currently not implemented
 *
 */

static
int
sl_local_mmap(struct slmsg *slmsg, char *base, size_t len)
{
	return (EOPNOTSUPP);
}

static
void
sl_local_munmap(struct slmsg *slmsg)
{
	/* empty */
}

#if 0

static
int
sl_local_mmap(struct slmsg *slmsg, char *base, size_t len)
{
	struct vmspace *vms = curproc->p_vmspace;
	vm_offset_t addr = (vm_offset_t)base;

	/* XXX  check user address range */
	error = vm_map_replace(
			&vma->vm_map,
			(vm_offset_t)base, (vm_offset_t)base + len,
			slmsg->xio.xio_pages[0]->object,
			slmsg->xio.xio_pages[0]->pindex << PAGE_SHIFT,
			VM_PROT_READ|VM_PROT_WRITE,
			VM_PROT_READ|VM_PROT_WRITE,
			MAP_DISABLE_SYNCER);
	}
	if (error == 0) {
		slmsg->flags |= SLMSGF_LINMAP;
		slmsg->vmbase = base;
		slmsg->vmsize = len;
	}
	return (error);
}

static
void
sl_local_munmap(struct slmsg *slmsg)
{
	if (slmsg->flags & SLMSGF_LINMAP) {
		vm_map_remove(&curproc->p_vmspace->vm_map,
			      slmsg->vmbase,
			      slmsg->vmbase + slcmd->vmsize);
		slmsg->flags &= ~SLMSGF_LINMAP;
	}
}

#endif

/************************************************************************
 *			    MESSAGE VALIDATION 				*
 ************************************************************************
 *
 * Validate that the syslink message.  Check that all headers and elements
 * conform.  Correct the endian if necessary.
 *
 * NOTE: If reverse endian needs to be corrected, SE_CMDF_UNTRANSLATED
 * is recursively flipped on all syslink_elm's in the message.  As the
 * message traverses the mesh, multiple flips may occur.  It is
 * up to the RPC protocol layer to correct opaque data payloads and
 * SE_CMDF_UNTRANSLATED prevents the protocol layer from misinterpreting
 * a command or reply element which has not been endian-corrected.
 */
static
int
syslink_validate_msg(struct syslink_msg *msg, int bytes)
{
	int aligned_reclen;
	int swapit;
	int error;

	/*
	 * The raw message must be properly-aligned.
	 */
	if (bytes & SL_ALIGNMASK)
		return (EINVAL);

	while (bytes) {
		/*
		 * The message must at least contain the msgid, bytes, and
		 * protoid.
		 */
		if (bytes < SL_MIN_PAD_SIZE)
			return (EINVAL);

		/*
		 * Fix the endian if it is reversed.
		 */
		if (msg->sm_proto & SM_PROTO_ENDIAN_REV) {
			msg->sm_msgid = bswap64(msg->sm_msgid);
			msg->sm_sessid = bswap64(msg->sm_sessid);
			msg->sm_bytes = bswap16(msg->sm_bytes);
			msg->sm_proto = bswap16(msg->sm_proto);
			msg->sm_rlabel = bswap32(msg->sm_rlabel);
			if (msg->sm_proto & SM_PROTO_ENDIAN_REV)
				return (EINVAL);
			swapit = 1;
		} else {
			swapit = 0;
		}

		/*
		 * Validate the contents.  For PADs, the entire payload is
		 * ignored and the minimum message size can be as small as
		 * 8 bytes.
		 */
		if (msg->sm_proto == SMPROTO_PAD) {
			if (msg->sm_bytes < SL_MIN_PAD_SIZE ||
			    msg->sm_bytes > bytes) {
				return (EINVAL);
			}
			/* ignore the entire payload, it can be garbage */
		} else {
			if (msg->sm_bytes < SL_MIN_MSG_SIZE ||
			    msg->sm_bytes > bytes) {
				return (EINVAL);
			}
			error = syslink_validate_elm(
				    &msg->sm_head,
				    msg->sm_bytes - 
					offsetof(struct syslink_msg,
						 sm_head),
				    swapit, SL_MAXDEPTH);
			if (error)
				return (error);
		}

		/*
		 * The aligned payload size must be used to locate the
		 * next syslink_msg in the buffer.
		 */
		aligned_reclen = SL_MSG_ALIGN(msg->sm_bytes);
		bytes -= aligned_reclen;
		msg = (void *)((char *)msg + aligned_reclen);
	}
	return(0);
}

static
int
syslink_validate_elm(struct syslink_elm *elm, sl_reclen_t bytes,
		     int swapit, int depth)
{
	int aligned_reclen;

	/*
	 * If the buffer isn't big enough to fit the header, stop now!
	 */
	if (bytes < SL_MIN_ELM_SIZE)
		return (EINVAL);
	/*
	 * All syslink_elm headers are recursively endian-adjusted.  Opaque
	 * data payloads are not.
	 */
	if (swapit) {
		elm->se_cmd = bswap16(elm->se_cmd) ^ SE_CMDF_UNTRANSLATED;
		elm->se_bytes = bswap16(elm->se_bytes);
		elm->se_aux = bswap32(elm->se_aux);
	}

	/*
	 * Check element size requirements.
	 */
	if (elm->se_bytes < SL_MIN_ELM_SIZE || elm->se_bytes > bytes)
		return (EINVAL);

	/*
	 * Recursively check structured payloads.  A structured payload may
	 * contain as few as 0 recursive elements.
	 */
	if (elm->se_cmd & SE_CMDF_STRUCTURED) {
		if (depth == 0)
			return (EINVAL);
		bytes -= SL_MIN_ELM_SIZE;
		++elm;
		while (bytes > 0) {
			if (syslink_validate_elm(elm, bytes, swapit, depth - 1))
				return (EINVAL);
			aligned_reclen = SL_MSG_ALIGN(elm->se_bytes);
			elm = (void *)((char *)elm + aligned_reclen);
			bytes -= aligned_reclen;
		}
	}
	return(0);
}

/************************************************************************
 *		    BACKEND FUNCTIONS - USER DESCRIPTOR			*
 ************************************************************************
 *
 * Peer backend links are primarily used when userland creates a pair
 * of linked descriptors.
 */

/*
 * Do any required blocking / nbio handling for attempts to write to
 * a sldesc associated with a user descriptor.
 */
static
int
backend_wblocked_user(struct sldesc *sl, int nbio, sl_proto_t proto)
{
	int error = 0;
	int *bytesp = (proto & SM_PROTO_REPLY) ? &sl->repbytes : &sl->cmdbytes;

	/*
	 * Block until sufficient data is drained by the target.  It is
	 * ok to have a MP race against cmdbytes.
	 */
	if (*bytesp >= syslink_bufsize) {
		spin_lock(&sl->spin);
		while (*bytesp >= syslink_bufsize) {
			if (sl->flags & SLF_WSHUTDOWN) {
				error = EPIPE;
				break;
			}
			if (nbio) {
				error = EAGAIN;
				break;
			}
			++sl->wblocked;
			error = ssleep(&sl->wblocked, &sl->spin,
				       PCATCH, "slwmsg", 0);
			if (error)
				break;
		}
		spin_unlock(&sl->spin);
	}
	return (error);
}

/*
 * Unconditionally write a syslink message to the sldesc associated with
 * a user descriptor.  Command messages are also placed in a red-black
 * tree so their DMA tag (if any) can be accessed and so they can be
 * linked to any reply message.
 */
static
int
backend_write_user(struct sldesc *sl, struct slmsg *slmsg)
{
	int error;

	spin_lock(&sl->spin);
	if (sl->flags & SLF_RSHUTDOWN) {
		/*
		 * Not accepting new messages
		 */
		error = EPIPE;
	} else if (slmsg->msg->sm_proto & SM_PROTO_REPLY) {
		/*
		 * Write a reply
		 */
		TAILQ_INSERT_TAIL(&sl->inq, slmsg, tqnode);
		sl->repbytes += slmsg->maxsize;
		slmsg->flags |= SLMSGF_ONINQ;
		error = 0;
	} else if (RB_INSERT(slmsg_rb_tree, &sl->reply_rb_root, slmsg)) {
		/*
		 * Write a command, but there was a msgid collision when
		 * we tried to insert it into the RB tree.
		 */
		error = EEXIST;
	} else {
		/*
		 * Write a command, successful insertion into the RB tree.
		 */
		TAILQ_INSERT_TAIL(&sl->inq, slmsg, tqnode);
		sl->cmdbytes += slmsg->maxsize;
		slmsg->flags |= SLMSGF_ONINQ;
		error = 0;
	}
	spin_unlock(&sl->spin);
	if (sl->rwaiters)
		wakeup(&sl->rwaiters);
	return(error);
}

/*
 * Our peer is replying a command we previously sent it back to us, along
 * with the reply message (if not NULL).  We just queue the reply to
 * userland and free of the command.
 */
static
void
backend_reply_user(struct sldesc *sl, struct slmsg *slcmd, struct slmsg *slrep)
{
	int error;

	slmsg_put(slcmd);
	if (slrep) {
		spin_lock(&sl->spin);
		if ((sl->flags & SLF_RSHUTDOWN) == 0) {
			TAILQ_INSERT_TAIL(&sl->inq, slrep, tqnode);
			sl->repbytes += slrep->maxsize;
			error = 0;
		} else {
			error = EPIPE;
		}
		spin_unlock(&sl->spin);
		if (error)
			sl->peer->backend_dispose(sl->peer, slrep);
		else if (sl->rwaiters)
			wakeup(&sl->rwaiters);
	}
}

static
void
backend_dispose_user(struct sldesc *sl, struct slmsg *slmsg)
{
	slmsg_put(slmsg);
}

/************************************************************************
 *	    		KERNEL DRIVER OR FILESYSTEM API			*
 ************************************************************************
 *
 */

/*
 * Create a user<->kernel link, returning the user descriptor in *fdp
 * and the kernel descriptor in *kslp.  0 is returned on success, and an
 * error code is returned on failure.
 */
int
syslink_ukbackend(int *pfd, struct sldesc **kslp)
{
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct file *fp;
	struct sldesc *usl;
	struct sldesc *ksl;
	int error;
	int fd;

	*pfd = -1;
	*kslp = NULL;

	error = falloc(td->td_lwp, &fp, &fd);
	if (error)
		return(error);
	usl = allocsldesc(NULL);
	usl->backend_wblocked = backend_wblocked_user;
	usl->backend_write = backend_write_user;
	usl->backend_reply = backend_reply_user;
	usl->backend_dispose = backend_dispose_user;

	ksl = allocsldesc(usl->common);
	ksl->peer = usl;
	ksl->backend_wblocked = backend_wblocked_kern;
	ksl->backend_write = backend_write_kern;
	ksl->backend_reply = backend_reply_kern;
	ksl->backend_dispose = backend_dispose_kern;

	usl->peer = ksl;

	setsldescfp(usl, fp);
	fsetfd(fdp, fp, fd);
	fdrop(fp);

	*pfd = fd;
	*kslp = ksl;
	return(0);
}

/*
 * Assign a unique message id, issue a syslink message to userland,
 * and wait for a reply.
 */
int
syslink_kdomsg(struct sldesc *ksl, struct slmsg *slmsg)
{
	struct syslink_msg *msg;
	int error;

	/*
	 * Finish initializing slmsg and post it to the red-black tree for
	 * reply matching.  If the message id is already in use we return
	 * EEXIST, giving the originator the chance to roll a new msgid.
	 */
	msg = slmsg->msg;
	slmsg->msgsize = msg->sm_bytes;
	if ((error = syslink_validate_msg(msg, msg->sm_bytes)) != 0)
		return (error);
	msg->sm_msgid = allocsysid();

	/*
	 * Issue the request and wait for a matching reply or failure,
	 * then remove the message from the matching tree and return.
	 */
	error = ksl->peer->backend_write(ksl->peer, slmsg);
	spin_lock(&ksl->spin);
	if (error == 0) {
		while (slmsg->rep == NULL) {
			error = ssleep(slmsg, &ksl->spin, 0, "kwtmsg", 0);
			/* XXX ignore error for now */
		}
		if (slmsg->rep == (struct slmsg *)-1) {
			error = EIO;
			slmsg->rep = NULL;
		} else {
			error = slmsg->rep->msg->sm_head.se_aux;
		}
	}
	spin_unlock(&ksl->spin);
	return(error);
}

/*
 * Similar to syslink_kdomsg but return immediately instead of
 * waiting for a reply.  The kernel must supply a callback function
 * which will be made in the context of the user process replying
 * to the message.
 */
int
syslink_ksendmsg(struct sldesc *ksl, struct slmsg *slmsg,
		 void (*func)(struct slmsg *, void *, int), void *arg)
{
	struct syslink_msg *msg;
	int error;

	/*
	 * Finish initializing slmsg and post it to the red-black tree for
	 * reply matching.  If the message id is already in use we return
	 * EEXIST, giving the originator the chance to roll a new msgid.
	 */
	msg = slmsg->msg;
	slmsg->msgsize = msg->sm_bytes;
	slmsg->callback_func = func;
	slmsg->callback_data = arg;
	if ((error = syslink_validate_msg(msg, msg->sm_bytes)) != 0)
		return (error);
	msg->sm_msgid = allocsysid();

	/*
	 * Issue the request.  If no error occured the operation will be
	 * in progress, otherwise the operation is considered to have failed
	 * and the caller can deallocate the slmsg.
	 */
	error = ksl->peer->backend_write(ksl->peer, slmsg);
	return (error);
}

int
syslink_kwaitmsg(struct sldesc *ksl, struct slmsg *slmsg)
{
	int error;

	spin_lock(&ksl->spin);
	while (slmsg->rep == NULL) {
		error = ssleep(slmsg, &ksl->spin, 0, "kwtmsg", 0);
		/* XXX ignore error for now */
	}
	if (slmsg->rep == (struct slmsg *)-1) {
		error = EIO;
		slmsg->rep = NULL;
	} else {
		error = slmsg->rep->msg->sm_head.se_aux;
	}
	spin_unlock(&ksl->spin);
	return(error);
}

struct slmsg *
syslink_kallocmsg(void)
{
	return(objcache_get(sl_objcache_small, M_WAITOK));
}

void
syslink_kfreemsg(struct sldesc *ksl, struct slmsg *slmsg)
{
	struct slmsg *rep;

	if ((rep = slmsg->rep) != NULL) {
		slmsg->rep = NULL;
		ksl->peer->backend_dispose(ksl->peer, rep);
	}
	slmsg->callback_func = NULL;
	slmsg_put(slmsg);
}

void
syslink_kshutdown(struct sldesc *ksl, int how)
{
	shutdownsldesc(ksl, how);
}

void
syslink_kclose(struct sldesc *ksl)
{
	shutdownsldesc(ksl, SHUT_RDWR);
	sldrop(ksl);
}

/*
 * Associate a DMA buffer with a kernel syslink message prior to it
 * being sent to userland.  The DMA buffer is set up from the point
 * of view of the target.
 */
int
syslink_kdmabuf_pages(struct slmsg *slmsg, struct vm_page **mbase, int npages)
{
	int xflags;
	int error;

	xflags = XIOF_VMLINEAR;
	if (slmsg->msg->sm_head.se_cmd & SE_CMDF_DMAR)
		xflags |= XIOF_READ | XIOF_WRITE;
	else if (slmsg->msg->sm_head.se_cmd & SE_CMDF_DMAW)
		xflags |= XIOF_READ;
	error = xio_init_pages(&slmsg->xio, mbase, npages, xflags);
	slmsg->flags |= SLMSGF_HASXIO;
	return (error);
}

/*
 * Associate a DMA buffer with a kernel syslink message prior to it
 * being sent to userland.  The DMA buffer is set up from the point
 * of view of the target.
 */
int
syslink_kdmabuf_data(struct slmsg *slmsg, char *base, int bytes)
{
	int xflags;

	xflags = XIOF_VMLINEAR;
	if (slmsg->msg->sm_head.se_cmd & SE_CMDF_DMAR)
		xflags |= XIOF_READ | XIOF_WRITE;
	else if (slmsg->msg->sm_head.se_cmd & SE_CMDF_DMAW)
		xflags |= XIOF_READ;
	xio_init_kbuf(&slmsg->xio, base, bytes);
	slmsg->xio.xio_flags |= xflags;
	slmsg->flags |= SLMSGF_HASXIO;
	return(0);
}

/************************************************************************
 *		    BACKEND FUNCTIONS FOR KERNEL API			*
 ************************************************************************
 *
 * These are the backend functions for a sldesc associated with a kernel
 * API.
 */

/*
 * Our peer wants to write a syslink message to us and is asking us to
 * block if our input queue is full.  We don't implement command reception
 * so don't block right now.
 */
static
int
backend_wblocked_kern(struct sldesc *ksl, int nbio, sl_proto_t proto)
{
	/* never blocks */
	return(0);
}

/*
 * Our peer is writing a request to the kernel.  At the moment we do not
 * accept commands.
 */
static
int
backend_write_kern(struct sldesc *ksl, struct slmsg *slmsg)
{
	return(EOPNOTSUPP);
}

/*
 * Our peer wants to reply to a syslink message we sent it earlier.  The
 * original command (that we passed to our peer), and the peer's reply
 * is specified.  If the peer has failed slrep will be NULL.
 */
static
void
backend_reply_kern(struct sldesc *ksl, struct slmsg *slcmd, struct slmsg *slrep)
{
	int error;

	spin_lock(&ksl->spin);
	if (slrep == NULL) {
		slcmd->rep = (struct slmsg *)-1;
		error = EIO;
	} else {
		slcmd->rep = slrep;
		error = slrep->msg->sm_head.se_aux;
	}
	spin_unlock(&ksl->spin);

	/*
	 * Issue callback or wakeup a synchronous waiter.
	 */
	if (slcmd->callback_func) {
		slcmd->callback_func(slcmd, slcmd->callback_data, error);
	} else {
		wakeup(slcmd);
	}
}

/*
 * Any reply messages we sent to our peer are returned to us for disposal.
 * Since we do not currently accept commands from our peer, there will not
 * be any replies returned to the peer to dispose of.
 */
static
void
backend_dispose_kern(struct sldesc *ksl, struct slmsg *slmsg)
{
	panic("backend_dispose_kern: kernel can't accept commands so it "
	      "certainly did not reply to one!");
}

