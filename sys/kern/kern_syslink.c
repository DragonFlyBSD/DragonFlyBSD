/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 *
 * $DragonFly: src/sys/kern/kern_syslink.c,v 1.1 2006/08/06 18:56:44 dillon Exp $
 */
/*
 * This module implements the syslink() system call and protocol which
 * is used to glue clusters together as well as to interface userland
 * devices and filesystems to the kernel.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/uio.h>
#include <sys/thread.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/syslink.h>
#include <sys/syslink_msg.h>

#include <sys/thread2.h>

/*
 * fileops interface.  slbuf and sldata are also used in conjunction with a
 * normal file descriptor.
 */

struct slbuf {
    char	*buf;
    int		bufsize;	/* must be a power of 2 */
    int		bufmask;	/* (bufsize - 1) */
    int		rindex;		/* tail-chasing FIFO indices */
    int		windex;
};

struct sldata {
    struct slbuf rbuf;
    struct slbuf wbuf;
    struct file	*xfp;		/* external file pointer */
    struct lock rlock;		/* synchronizing lock */
    struct lock wlock;		/* synchronizing lock */
    struct thread *rthread;	/* xfp -> rbuf & process */
    struct thread *wthread;	/* wbuf -> xfp */
    int flags;
    int refs;
};

#define SLF_RQUIT	0x0001
#define SLF_WQUIT	0x0002
#define SLF_RDONE	0x0004
#define SLF_WDONE	0x0008

#define SYSLINK_BUFSIZE	(128*1024)

static int syslink_read (struct file *fp, struct uio *uio,
			 struct ucred *cred, int flags);
static int syslink_write (struct file *fp, struct uio *uio,
			 struct ucred *cred, int flags);
static int syslink_close (struct file *fp);
static int syslink_stat (struct file *fp, struct stat *sb, struct ucred *cred);
static int syslink_shutdown (struct file *fp, int how);
static int syslink_ioctl (struct file *fp, u_long cmd, caddr_t data,
			 struct ucred *cred);
static int syslink_poll (struct file *fp, int events, struct ucred *cred);
static int syslink_kqfilter(struct file *fp, struct knote *kn);

static void syslink_rthread(void *arg);
static void syslink_wthread(void *arg);
static void slbuf_alloc(struct slbuf *buf, int bytes);
static void slbuf_free(struct slbuf *buf);
static void sldata_rels(struct sldata *sldata);
static int process_syslink_msg(struct sldata *sldata, struct syslink_msg *head);
static int syslink_validate(struct syslink_msg *head, int bytes);

static struct fileops syslinkops = {
    .fo_read =		syslink_read,
    .fo_write =		syslink_write,
    .fo_ioctl =		syslink_ioctl,
    .fo_poll =		syslink_poll,
    .fo_kqfilter =	syslink_kqfilter,
    .fo_stat =		syslink_stat,
    .fo_close =		syslink_close,
    .fo_shutdown =	syslink_shutdown
};

MALLOC_DEFINE(M_SYSLINK, "syslink", "syslink manager");

static int syslink_enabled;
SYSCTL_INT(_kern, OID_AUTO, syslink_enabled,
	    CTLFLAG_RW, &syslink_enabled, 0, "Enable SYSLINK");

/*
 * Kernel mask and match bits.  These may be modified during early boot,
 * before any syslink services have been established, but must remain fixed
 * after that.  Note that the match value is only used if a message leaves
 * the machine's domain.  '0' is used for unmasked match bits to indicate
 * transport within the machine.
 */
static sysid_t sl_mask  = 0x00000000FFFFFFFFLL;
static sysid_t sl_match = 0x0000000100000000LL;

/*
 * Primary system call interface - associate a full-duplex stream
 * (typically a pipe or a connected socket) with a sysid namespace,
 * or create a direct link.
 *
 * syslink(int fd, int cmd, sysid_t *mask, sysid_t *match)
 */

int
sys_syslink(struct syslink_args *uap)
{
    int error;
    struct file *fp;
    struct sldata *sldata;

    /*
     * System call is under construction and disabled by default
     */
    if (syslink_enabled == 0)
	return (EAUTH);

    switch(uap->cmd) {
    case SYSLINK_ESTABLISH:
	error = suser(curthread);
	if (error)
	    break;
	sldata = malloc(sizeof(struct sldata), M_SYSLINK, M_WAITOK|M_ZERO);
	lockinit(&sldata->rlock, "slread", 0, 0);
	lockinit(&sldata->wlock, "slwrite", 0, 0);

	if (uap->fd < 0) {
	    /*
	     * We create a direct syslink descriptor.  Only the reader thread
	     * is needed.
	     */
	    error = falloc(curproc, &fp, &uap->fd);
	    if (error == 0) {
		fp->f_type = DTYPE_SYSLINK;
		fp->f_flag = FREAD | FWRITE;
		fp->f_ops = &syslinkops;
		fp->f_data = sldata;
		slbuf_alloc(&sldata->rbuf, SYSLINK_BUFSIZE);
		slbuf_alloc(&sldata->wbuf, SYSLINK_BUFSIZE);
		sldata->refs = 2;
		sldata->flags = SLF_WQUIT | SLF_WDONE;
		lwkt_create(syslink_rthread, sldata,
			    &sldata->rthread, NULL,
			    0, -1, "syslink_r");
	    }
	} else {
	    sldata->xfp = holdfp(curproc->p_fd, uap->fd, -1);
	    if (sldata->xfp != NULL) {
		slbuf_alloc(&sldata->rbuf, SYSLINK_BUFSIZE);
		slbuf_alloc(&sldata->wbuf, SYSLINK_BUFSIZE);
		sldata->refs = 2;
		lwkt_create(syslink_rthread, sldata,
			    &sldata->rthread, NULL,
			    0, -1, "syslink_r");
		lwkt_create(syslink_wthread, sldata,
			    &sldata->wthread, NULL,
			    0, -1, "syslink_w");
	    } else {
		error = EBADF;
	    }
	}
	if (error)
		free(sldata, M_SYSLINK);
	break;
    case SYSLINK_GETSYSMASK:
	error = copyout(&sl_mask, uap->mask, sizeof(sl_mask));
	if (error == 0)
		error = copyout(&sl_match, uap->match, sizeof(sl_match));
	break;
    default:
	error = ENOTSUP;
	break;
    }
    return(error);
}

/*
 * This thread reads from an external descriptor into rbuf, then parses and
 * dispatches syslink messages from rbuf.
 */
static
void
syslink_rthread(void *arg)
{
    struct sldata *sldata = arg;
    struct slbuf *slbuf = &sldata->rbuf;
    struct syslink_msg *head;
    const int min_msg_size = offsetof(struct syslink_msg, src_sysid);

    while ((sldata->flags & SLF_RQUIT) == 0) {
	int count;
	int used;
	int error;

	/*
	 * Calculate contiguous space available to read and read as much
	 * as possible.
	 *
	 * If the entire buffer is used there's probably a format error
	 * of some sort and we terminate the link.
	 */
	used = slbuf->windex - slbuf->rindex;
	error = 0;

	/*
	 * Read some data, terminate the link if an error occurs or if EOF
	 * is encountered.  xfp can be NULL, indicating that the data was
	 * injected by other means.
	 */
	if (sldata->xfp) {
		count = slbuf->bufsize - (slbuf->windex & slbuf->bufmask);
		if (count > slbuf->bufsize - used)
		    count = slbuf->bufsize - used;
		if (count == 0)
		    break;
		error = fp_read(sldata->xfp,
				slbuf->buf + (slbuf->windex & slbuf->bufmask), count,
				&count, 0);
		if (error)
		    break;
		if (count == 0)
		    break;
		slbuf->windex += count;
		used += count;
	} else {
		tsleep(slbuf, 0, "fiford", 0);
	}

	/*
	 * Process as many syslink messages as we can.  The record length
	 * must be at least a minimal PAD record (8 bytes).  A msgid of 0
	 * is PAD.
	 */
	while (slbuf->windex - slbuf->rindex >= min_msg_size) {
	    int aligned_reclen;

	    head = (void *)(slbuf->buf + (slbuf->rindex & slbuf->bufmask));
	    if (head->reclen < min_msg_size) {
		error = EINVAL;
		break;
	    }
	    aligned_reclen = SLMSG_ALIGN(head->reclen);

	    /*
	     * Disallow wraps
	     */
	    if ((slbuf->rindex & slbuf->bufmask) >
		((slbuf->rindex + aligned_reclen) & slbuf->bufmask)
	    ) {
		error = EINVAL;
		break;
	    }

	    /*
	     * Insufficient data read
	     */
	    if (slbuf->windex - slbuf->rindex < aligned_reclen)
		break;

	    /*
	     * Process non-pad messages.  Non-pad messages have to be at
	     * least the size of the syslink_msg structure.
	     */
	    if (head->msgid) {
		if (head->reclen < sizeof(struct syslink_msg)) {
		    error = EINVAL;
		    break;
		}
		error = process_syslink_msg(sldata, head);
		if (error)
		    break;
	    }
	    cpu_sfence();
	    slbuf->rindex += aligned_reclen;
	}
	if (error)
	    break;
    }

    /*
     * Mark us as done and deref sldata.  Tell the writer to terminate as
     * well.
     */
    sldata->flags |= SLF_RDONE;
    if ((sldata->flags & SLF_WDONE) == 0) {
	    sldata->flags |= SLF_WQUIT;
	    wakeup(&sldata->wbuf);
    }
    sldata_rels(sldata);
}

/*
 * This thread takes outgoing syslink messages queued to wbuf and writes them
 * to the descriptor.  PAD is stripped.  PAD is also added as required to
 * conform to the outgoing descriptor's buffering requirements.
 */
static
void
syslink_wthread(void *arg)
{
    struct sldata *sldata = arg;
    struct slbuf *slbuf = &sldata->wbuf;
    struct syslink_msg *head;
    int error;

    while ((sldata->flags & SLF_WQUIT) == 0) {
	error = 0;
	for (;;) {
	    int aligned_reclen;
	    int used;
	    int count;

	    used = slbuf->windex - slbuf->rindex;
	    if (used < offsetof(struct syslink_msg, src_sysid))
		break;

	    head = (void *)(slbuf->buf + (slbuf->rindex & slbuf->bufmask));
	    if (head->reclen < offsetof(struct syslink_msg, src_sysid)) {
		error = EINVAL;
		break;
	    }
	    aligned_reclen = SLMSG_ALIGN(head->reclen);

	    /*
	     * Disallow wraps
	     */
	    if ((slbuf->rindex & slbuf->bufmask) >
		((slbuf->rindex + aligned_reclen) & slbuf->bufmask)
	    ) {
		error = EINVAL;
		break;
	    }

	    /*
	     * Insufficient data read
	     */
	    if (used < aligned_reclen)
		break;

	    /*
	     * Write it out whether it is PAD or not.   XXX re-PAD for output
	     * here.
	     */
	    error = fp_write(sldata->xfp, head, aligned_reclen, &count);
	    if (error)
		break;
	    if (count != aligned_reclen) {
		error = EIO;
		break;
	    }
	    slbuf->rindex += aligned_reclen;
	}
	if (error)
	    break;
	tsleep(slbuf, 0, "fifowt", 0);
    }
    sldata->flags |= SLF_WDONE;
    sldata_rels(sldata);
}

static
void
slbuf_alloc(struct slbuf *slbuf, int bytes)
{
    bzero(slbuf, sizeof(*slbuf));
    slbuf->buf = malloc(bytes, M_SYSLINK, M_WAITOK);
    slbuf->bufsize = bytes;
    slbuf->bufmask = bytes - 1;
}

static
void
slbuf_free(struct slbuf *slbuf)
{
    free(slbuf->buf, M_SYSLINK);
    slbuf->buf = NULL;
}

static
void
sldata_rels(struct sldata *sldata)
{
    if (--sldata->refs == 0) {
	slbuf_free(&sldata->rbuf);
	slbuf_free(&sldata->wbuf);
	free(sldata, M_SYSLINK);
    }
}

/*
 * fileops for an established syslink when the kernel is asked to create a
 * descriptor (verses one being handed to it).  No threads are created in
 * this case.
 */

/*
 * Transfer zero or more messages from the kernel to userland.  Only complete
 * messages are returned.  If the uio has insufficient space then EMSGSIZE
 * is returned.  The kernel feeds messages to wbuf so we use wlock (structures
 * are relative to the kernel).
 */
static
int
syslink_read(struct file *fp, struct uio *uio, struct ucred *cred, int flags)
{
    struct sldata *sldata = fp->f_data;
    struct slbuf *slbuf = &sldata->wbuf;
    struct syslink_msg *head;
    int bytes;
    int contig;
    int error;
    int nbio;

    if (flags & O_FBLOCKING)
	nbio = 0;
    else if (flags & O_FNONBLOCKING)
	nbio = 1;
    else if (fp->f_flag & O_NONBLOCK)
	nbio = 1;
    else
	nbio = 0;

    lockmgr(&sldata->wlock, LK_EXCLUSIVE | LK_RETRY);

    /*
     * Calculate the number of bytes we can transfer in one shot.  Transfers
     * do not wrap the FIFO.
     */
    contig = slbuf->bufsize - (slbuf->rindex & slbuf->bufmask);
    for (;;) {
	bytes = slbuf->windex - slbuf->rindex;
	if (bytes)
	    break;
	if (nbio) {
	    error = EAGAIN;
	    goto done;
	}
	tsleep(slbuf, 0, "fiford", 0);
    }
    if (bytes > contig)
	bytes = contig;

    /*
     * The uio must be able to accomodate the transfer.
     */
    if (uio->uio_resid < bytes) {
	error = ENOSPC;
	goto done;
    }

    /*
     * Copy the data to userland and update rindex.
     */
    head = (void *)(slbuf->buf + (slbuf->rindex & slbuf->bufmask));
    error = uiomove((caddr_t)head, bytes, uio);
    if (error == 0)
	slbuf->rindex += bytes;

    /*
     * Cleanup
     */
done:
    lockmgr(&sldata->wlock, LK_RELEASE);
    return (error);
}

/*
 * Transfer zero or more messages from userland to the kernel.  Only complete
 * messages may be written.  The kernel processes from rbuf so that is where
 * we have to copy the messages.
 */
static
int
syslink_write (struct file *fp, struct uio *uio, struct ucred *cred, int flags)
{
    struct sldata *sldata = fp->f_data;
    struct slbuf *slbuf = &sldata->rbuf;
    struct syslink_msg *head;
    int bytes;
    int contig;
    int nbio;
    int error;

    if (flags & O_FBLOCKING)
	nbio = 0;
    else if (flags & O_FNONBLOCKING)
	nbio = 1;
    else if (fp->f_flag & O_NONBLOCK)
	nbio = 1;
    else
	nbio = 0;

    lockmgr(&sldata->rlock, LK_EXCLUSIVE | LK_RETRY);

    /* 
     * Calculate the maximum number of contiguous bytes that may be available.
     * Caller is required to not wrap our FIFO.
     */
    contig = slbuf->bufsize - (slbuf->windex & slbuf->bufmask);
    if (uio->uio_resid > contig) {
	error = ENOSPC;
	goto done;
    }

    /*
     * Truncate based on actual unused space available in the FIFO.  If
     * the uio does not fit, block and loop.
     */
    for (;;) {
	bytes = slbuf->bufsize - (slbuf->windex - slbuf->rindex);
	if (bytes > contig)
	    bytes = contig;
	if (uio->uio_resid <= bytes)
	    break;
	if (nbio) {
	    error = EAGAIN;
	    goto done;
	}
	tsleep(slbuf, 0, "fifowr", 0);
    }
    bytes = uio->uio_resid;
    head = (void *)(slbuf->buf + (slbuf->windex & slbuf->bufmask));
    error = uiomove((caddr_t)head, bytes, uio);
    if (error == 0)
	error = syslink_validate(head, bytes);
    if (error == 0)
	slbuf->windex += bytes;
done:
    lockmgr(&sldata->rlock, LK_RELEASE);
    return(error);
}

static
int
syslink_close (struct file *fp)
{
    struct sldata *sldata;

    sldata = fp->f_data;
    fp->f_data = NULL;
    sldata_rels(sldata);
    return(0);
}

static
int
syslink_stat (struct file *fp, struct stat *sb, struct ucred *cred)
{
    return(EINVAL);
}

static
int
syslink_shutdown (struct file *fp, int how)
{
    return(EINVAL);
}

static
int
syslink_ioctl (struct file *fp, u_long cmd, caddr_t data, struct ucred *cred)
{
    return(EINVAL);
}

static
int
syslink_poll (struct file *fp, int events, struct ucred *cred)
{
    return(0);
}

static
int
syslink_kqfilter(struct file *fp, struct knote *kn)
{
    return(0);
}

/*
 * Process a syslink message
 */
static
int
process_syslink_msg(struct sldata *sldata, struct syslink_msg *head)
{
    printf("process syslink msg %08x %04x\n", head->msgid, head->cid);
    return(0);
}

/*
 * Validate that the syslink message header(s) are correctly sized.
 */
static
int
syslink_validate(struct syslink_msg *head, int bytes)
{
    const int min_msg_size = offsetof(struct syslink_msg, src_sysid);
    int aligned_reclen;

    while (bytes) {
	/*
	 * Message size and alignment
	 */
	if (bytes < min_msg_size)
	    return (EINVAL);
	if (bytes & SL_ALIGNMASK)
	    return (EINVAL);
	if (head->msgid && bytes < sizeof(struct syslink_msg))
	    return (EINVAL);

	/*
	 * Buffer must contain entire record
	 */
	aligned_reclen = SLMSG_ALIGN(head->reclen);
	if (bytes < aligned_reclen)
	    return (EINVAL);
	bytes -= aligned_reclen;
	head = (void *)((char *)head + aligned_reclen);
    }
    return(0);
}

