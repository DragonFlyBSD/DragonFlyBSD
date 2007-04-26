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
 *
 * $DragonFly: src/sys/kern/kern_syslink.c,v 1.10 2007/04/26 02:10:59 dillon Exp $
 */
/*
 * This module implements the syslink() system call and protocol which
 * is used to glue clusters together as well as to interface userland
 * devices and filesystems to the kernel.
 *
 * We implement the management node concept in this module.  A management
 * node is basically a router node with additional features that take much
 * of the protocol burden away from connecting terminal nodes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/alist.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/uio.h>
#include <sys/thread.h>
#include <sys/tree.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/syslink.h>
#include <sys/syslink_msg.h>
#include <netinet/in.h>

#include <sys/thread2.h>

#include "opt_syslink.h"

/*
 * Red-Black trees organizing the syslink 'router' nodes and connections
 * to router nodes.
 */
struct slrouter;
struct sldata;

RB_HEAD(slrouter_rb_tree, slrouter);
RB_HEAD(sldata_rb_tree, sldata);
RB_PROTOTYPE2(slrouter_rb_tree, slrouter, rbnode,
              rb_slrouter_compare, sysid_t);
RB_PROTOTYPE2(sldata_rb_tree, sldata, rbnode,
              rb_sldata_compare, int);

/*
 * Fifo used to buffer broadcast packets
 */
struct slbuf {
	char	*buf;
	int	bufsize;	/* must be a power of 2 */
	int	bufmask;	/* (bufsize - 1) */
	int	rindex;		/* tail-chasing FIFO indices */
	int	windex;
};

/*
 * Syslink Router abstraction
 */
struct slrouter {
	RB_ENTRY(slrouter) rbnode;		/* list of routers */
	struct sldata_rb_tree sldata_rb_root;	/* connections to router */
	sysid_t	sysid;				/* logical sysid of router */
	int	flags;				/* flags passed on create */
	int	bits;				/* accomodate connections */
	int	count;				/* number of connections */
	int	refs;
	alist_t		bitmap;
	struct slbuf	bbuf;			/* broadcast buffer */
	char label[SYSLINK_LABEL_SIZE];
};

/*
 * Syslink Connection abstraction
 */
struct sldata {
	RB_ENTRY(sldata) rbnode;
	struct slrouter	*router;	/* organizing router */
	struct file	*xfp;		/* external file pointer */
	struct lock	rlock;		/* synchronizing lock */
	struct lock	wlock;		/* synchronizing lock */
	struct thread	*rthread;	/* helper thread */
	struct thread	*wthread;	/* helper thread */
	struct sockbuf	sior;		/* accumulate incoming mbufs */
	struct sockbuf	siow;		/* accumulate outgoing mbufs */
	struct sockaddr	sa;		/* used w/SLIF_SUBNET mode */
	int	bindex;			/* broadcast index */
	int	flags;			/* connection flags */
	int	linkid;
	int	bits;
	int	refs;
	char	label[SYSLINK_LABEL_SIZE];
};

#define SYSLINK_BBUFSIZE	(32*1024)
#define SYSLINK_SIOBUFSIZE	(128*1024)

static int rb_slrouter_compare(struct slrouter *r1, struct slrouter *r2);
static int rb_sldata_compare(struct sldata *d1, struct sldata *d2);

static int syslink_destroy(struct slrouter *slrouter);
static int syslink_add(struct slrouter *slrouter,
			struct syslink_info *info, int *result);
static int syslink_rem(struct slrouter *slrouter, struct sldata *sldata,
			struct syslink_info *info);

static int syslink_read(struct file *fp, struct uio *uio,
			struct ucred *cred, int flags);
static int syslink_write(struct file *fp, struct uio *uio,
			 struct ucred *cred, int flags);
static int syslink_close(struct file *fp);
static int syslink_stat(struct file *fp, struct stat *sb, struct ucred *cred);
static int syslink_shutdown(struct file *fp, int how);
static int syslink_ioctl(struct file *fp, u_long cmd, caddr_t data,
			 struct ucred *cred);
static int syslink_poll(struct file *fp, int events, struct ucred *cred);
static int syslink_kqfilter(struct file *fp, struct knote *kn);

static void syslink_rthread_so(void *arg);
static void syslink_rthread_fp(void *arg);
static void syslink_wthread_so(void *arg);
static void syslink_wthread_fp(void *arg);
static int syslink_getsubnet(struct sockaddr *sa);
static struct mbuf *syslink_parse_stream(struct sockbuf *sio);
static void syslink_route(struct slrouter *slrouter, int linkid, struct mbuf *m);
static void slbuf_alloc(struct slbuf *buf, int bytes);
static void slbuf_free(struct slbuf *buf);
static void sldata_rels(struct sldata *sldata);
static void slrouter_rels(struct slrouter *slrouter);
static int process_syslink_msg(struct sldata *sldata, struct syslink_msg *head);
static int syslink_validate(struct syslink_msg *head, int bytes);

RB_GENERATE2(slrouter_rb_tree, slrouter, rbnode,
             rb_slrouter_compare, sysid_t, sysid);
RB_GENERATE2(sldata_rb_tree, sldata, rbnode,
             rb_sldata_compare, int, linkid);

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
 * Support declarations and compare function for our RB trees
 */
static struct slrouter_rb_tree slrouter_rb_root;

static int
rb_slrouter_compare(struct slrouter *r1, struct slrouter *r2)
{
	if (r1->sysid < r2->sysid)
		return(-1);
	if (r1->sysid > r2->sysid)
		return(1);
	return(0);
}

static int
rb_sldata_compare(struct sldata *d1, struct sldata *d2)
{
	if (d1->linkid < d2->linkid)
		return(-1);
	if (d1->linkid > d2->linkid)
		return(1);
	return(0);
}

/*
 * Compare and callback functions for first-sysid and first-linkid searches.
 */
static int
syslink_cmd_locate_cmp(struct slrouter *slrouter, void *data)
{
	struct syslink_info *info = data;

	if (slrouter->sysid < info->sysid)
	    return(-1);
	if (slrouter->sysid > info->sysid)
	    return(1);
	return(0);
}

static int
syslink_cmd_locate_callback(struct slrouter *slrouter, void *data)
{
	struct syslink_info *info = data;

	info->flags = slrouter->flags;	/* also clears SLIF_ERROR */
	bcopy(slrouter->label, info->label, SYSLINK_LABEL_SIZE);

	return(-1);
}

static int
syslink_cmd_find_cmp(struct sldata *sldata, void *data)
{
	struct syslink_info *info = data;

	if (sldata->linkid < info->linkid)
	    return(-1);
	if (sldata->linkid > info->linkid)
	    return(1);
	return(0);
}

static int
syslink_cmd_find_callback(struct sldata *sldata, void *data)
{
	struct syslink_info *info = data;

	info->linkid = sldata->linkid;
	info->flags = sldata->flags;	/* also clears SLIF_ERROR */
	bcopy(sldata->label, info->label, SYSLINK_LABEL_SIZE);

	return(-1);
}

/*
 * Primary system call interface - associate a full-duplex stream
 * (typically a pipe or a connected socket) with a sysid namespace,
 * or create a direct link.
 *
 * syslink(int cmd, struct syslink_info *info, size_t bytes)
 */
int
sys_syslink(struct syslink_args *uap)
{
	struct syslink_info info;
	struct slrouter *slrouter = NULL;
	struct sldata *sldata = NULL;
	int error;
	int n;

	/*
	 * System call is under construction and disabled by default. 
	 * Superuser access is also required.
	 */
	if (syslink_enabled == 0)
		return (EAUTH);
	error = suser(curthread);
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

	if (info.label[sizeof(info.label)-1] != 0)
		return (EINVAL);

	/*
	 * Process command
	 */
	switch(uap->cmd) {
	case SYSLINK_CMD_CREATE:
		/*
		 * Create a new syslink router node.  Set refs to prevent the
		 * router node from being destroyed.  One ref is our temporary
		 * reference while the other is the SLIF_DESTROYED-interlocked
		 * reference.
		 */
		if (info.bits < 2 || info.bits > SYSLINK_ROUTER_MAXBITS)
			return (EINVAL);
		slrouter = kmalloc(sizeof(struct slrouter), M_SYSLINK,
				    M_WAITOK|M_ZERO);
		if (slrouter_rb_tree_RB_LOOKUP(&slrouter_rb_root, info.sysid)) {
			kfree(slrouter, M_SYSLINK);
			slrouter = NULL;
			return (EINVAL);
		}
		slrouter->sysid = info.sysid;
		slrouter->refs = 2;
		slrouter->bits = info.bits;
		slrouter->flags = info.flags & SLIF_USERFLAGS;
		slrouter->bitmap = alist_create(1 << info.bits, M_SYSLINK);
		slbuf_alloc(&slrouter->bbuf, SYSLINK_BBUFSIZE);
		RB_INIT(&slrouter->sldata_rb_root);
		RB_INSERT(slrouter_rb_tree, &slrouter_rb_root, slrouter);
		break;
	case SYSLINK_CMD_DESTROY:
		/*
		 * Destroy a syslink router node.  The physical node is
		 * not freed until our temporary reference is removed.
		 */
		slrouter = slrouter_rb_tree_RB_LOOKUP(&slrouter_rb_root,
						      info.sysid);
		if (slrouter) {
			++slrouter->refs;
			if ((slrouter->flags & SLIF_DESTROYED) == 0) {
				slrouter->flags |= SLIF_DESTROYED;
				/* SLIF_DESTROYED interlock */
				slrouter_rels(slrouter);
				error = syslink_destroy(slrouter);
				/* still holding our private interlock */
			}
		}
		break;
	case SYSLINK_CMD_LOCATE:
		/*
		 * Locate the first syslink router node >= info.sysid
		 */
		info.flags |= SLIF_ERROR;
		n = slrouter_rb_tree_RB_SCAN(
			    &slrouter_rb_root,
			    syslink_cmd_locate_cmp, syslink_cmd_locate_callback,
			    &info);
		if (info.flags & SLIF_ERROR)
			error = ENOENT;
		break;
	case SYSLINK_CMD_ADD:
		slrouter = slrouter_rb_tree_RB_LOOKUP(&slrouter_rb_root, info.sysid);
		if (info.bits &&
		    (info.bits < 2 || info.bits > SYSLINK_ROUTER_MAXBITS)) {
			error = EINVAL;
		} else if (slrouter && (slrouter->flags & SLIF_DESTROYED)) {
			/*
			 * Someone is trying to destroy this route node,
			 * no new adds please!
			 */
			error = EIO;
		} else if (slrouter) {
			++slrouter->refs;
			error = syslink_add(slrouter, &info,
					    &uap->sysmsg_result);
		} else {
			error = EINVAL;
		}
		break;
	case SYSLINK_CMD_REM:
		slrouter = slrouter_rb_tree_RB_LOOKUP(&slrouter_rb_root,
						      info.sysid);
		if (slrouter) {
			++slrouter->refs;
			sldata = sldata_rb_tree_RB_LOOKUP(&slrouter->sldata_rb_root, info.linkid);
			if (sldata) {
				++sldata->refs;
				error = syslink_rem(slrouter, sldata, &info);
			} else {
				error = ENOENT;
			}
		} else {
			error = EINVAL;
		}
		break;
	case SYSLINK_CMD_FIND:
		slrouter = slrouter_rb_tree_RB_LOOKUP(&slrouter_rb_root, info.sysid);
		info.flags |= SLIF_ERROR;
		if (slrouter) {
			++slrouter->refs;
			n = sldata_rb_tree_RB_SCAN(
				&slrouter->sldata_rb_root,
				syslink_cmd_find_cmp, syslink_cmd_find_callback,
				&info);
			if (info.flags & SLIF_ERROR)
				error = ENOENT;
		} else {
			error = EINVAL;
		}
		break;
	default:
		error = EINVAL;
		break;
	}

	/*
	 * Cleanup
	 */
	if (sldata)
		sldata_rels(sldata);
	if (slrouter)
		slrouter_rels(slrouter);
	return (error);
}

static
int
syslink_destroy_callback(struct sldata *sldata, void *data __unused)
{
	++sldata->refs;
	if ((sldata->flags & SLIF_RQUIT) == 0) {
		sldata->flags |= SLIF_RQUIT;
		wakeup(&sldata->rthread);
	}
	if ((sldata->flags & SLIF_WQUIT) == 0) {
		sldata->flags |= SLIF_WQUIT;
		wakeup(&sldata->wthread);
	}
	sldata_rels(sldata);
	return(0);
}

/*
 * Shutdown all the connections going into this syslink.
 *
 * Try to wait for completion, but return after 1 second 
 * regardless.
 */
static
int
syslink_destroy(struct slrouter *slrouter)
{
	int retries = 10;

	while (!RB_EMPTY(&slrouter->sldata_rb_root) && retries) {
		RB_SCAN(sldata_rb_tree, &slrouter->sldata_rb_root, NULL,
			syslink_destroy_callback, slrouter);
		--retries;
		tsleep(&retries, 0, "syslnk", hz / 10);
	}
	if (RB_EMPTY(&slrouter->sldata_rb_root))
		return(0);
	else
		return(EINPROGRESS);
}

static
int
syslink_add(struct slrouter *slrouter, struct syslink_info *info,
	    int *result)
{
	struct sldata *sldata;
	struct file *fp;
	int maxphys;
	int numphys;
	int linkid;
	int error;

	error = 0;
	maxphys = 1 << slrouter->bits;
	numphys = info->bits ? (1 << info->bits) : 1;

	/*
	 * Create a connection to the route node and allocate a physical ID.
	 * Physical ID 0 is reserved for the route node itself, and an all-1's
	 * ID is reserved as a broadcast address.
	 */
	sldata = kmalloc(sizeof(struct sldata), M_SYSLINK, M_WAITOK|M_ZERO);

	linkid = alist_alloc(slrouter->bitmap, numphys);
	if (linkid == ALIST_BLOCK_NONE) {
		kfree(sldata, M_SYSLINK);
		return (ENOSPC);
	}

	/*
	 * Insert the node, initializing enough fields to prevent things from
	 * being ripped out from under us before we have a chance to complete
	 * the system call.
	 */
	sldata->linkid = linkid;
	sldata->refs = 1;
	++slrouter->count;
	if (sldata_rb_tree_RB_LOOKUP(&slrouter->sldata_rb_root, linkid))
		panic("syslink_add: free linkid wasn't free!");
	RB_INSERT(sldata_rb_tree, &slrouter->sldata_rb_root, sldata);

	/*
	 * Complete initialization of the physical route node.  Setting 
	 * sldata->router activates the node.
	 */
	sbinit(&sldata->sior, SYSLINK_SIOBUFSIZE);
	sbinit(&sldata->siow, SYSLINK_SIOBUFSIZE);
	sldata->bindex = slrouter->bbuf.windex;
	sldata->flags = info->flags & SLIF_USERFLAGS;
	lockinit(&sldata->rlock, "slread", 0, 0);
	lockinit(&sldata->wlock, "slwrite", 0, 0);
	bcopy(&info->u.sa, &sldata->sa, sizeof(sldata->sa));

	if (info->fd < 0) {
		/*
		 * We create a direct syslink descriptor.  No helper threads
		 * are needed.
		 */
		error = falloc(curproc, &fp, &info->fd);
		if (error == 0) {
			fp->f_type = DTYPE_SYSLINK;
			fp->f_flag = FREAD | FWRITE;
			fp->f_ops = &syslinkops;
			fp->f_data = sldata;
			/* one ref: the fp descriptor */
			sldata->refs += 1;
			sldata->flags |= SLIF_WQUIT | SLIF_WDONE;
			sldata->flags |= SLIF_RQUIT | SLIF_RDONE;
			fsetfd(curproc, fp, info->fd);
			fdrop(fp);
			*result = info->fd;
		}
	} else {
		sldata->xfp = holdfp(curproc->p_fd, info->fd, -1);
		if (sldata->xfp != NULL) {
			/* two refs: reader thread and writer thread */
			sldata->refs += 2;
			if (sldata->xfp->f_type == DTYPE_SOCKET) {
				lwkt_create(syslink_rthread_so, sldata,
					    &sldata->rthread, NULL,
					    0, -1, "syslink_r");
				lwkt_create(syslink_wthread_so, sldata,
					    &sldata->wthread, NULL,
					    0, -1, "syslink_w");
			} else {
				lwkt_create(syslink_rthread_fp, sldata,
					    &sldata->rthread, NULL,
					    0, -1, "syslink_r");
				lwkt_create(syslink_wthread_fp, sldata,
					    &sldata->wthread, NULL,
					    0, -1, "syslink_w");
			}
		} else {
			error = EBADF;
		}
	}
	sldata->router = slrouter;
	sldata_rels(sldata);
	return(error);
}

static
int
syslink_rem(struct slrouter *slrouter, struct sldata *sldata,
	    struct syslink_info *info)
{
	int error = EINPROGRESS;

	if ((sldata->flags & SLIF_RQUIT) == 0) {
		sldata->flags |= SLIF_RQUIT;
		wakeup(&sldata->rthread);
		error = 0;
	}
	if ((sldata->flags & SLIF_WQUIT) == 0) {
		sldata->flags |= SLIF_WQUIT;
		wakeup(&sldata->wthread);
		error = 0;
	}
	return(error);
}

/*
 * Read syslink messages from an external socket and route them.
 */
static
void
syslink_rthread_so(void *arg)
{
	struct sldata *sldata = arg;
	struct socket *so;
	struct sockaddr *sa;
	struct mbuf *m;
	int soflags;
	int linkid;
	int error;
	int needsa;

	so = (void *)sldata->xfp->f_data;
	sa = NULL;

	/*
	 * Calculate whether we need to get the peer address or not.
	 * We need to obtain the peer address for packet-mode sockets
	 * representing subnets (rather then single connections).
	 */
	needsa = (sldata->bits && (sldata->flags & SLIF_PACKET));

	while ((sldata->flags & SLIF_RQUIT) == 0) {
		/*
		 * Read some data.  This is easy if the data is packetized,
		 * otherwise we can still obtain an mbuf chain but we have
		 * to parse out the syslink messages.
		 */
		soflags = 0;
		error = so_pru_soreceive(so,
					 (needsa ? &sa : NULL),
					 NULL, &sldata->sior,
					 NULL, &soflags);

		/*
		 * The target is responsible for adjusting the src address
		 * field in the syslink_msg.  We may need subnet information
		 * from the sockaddr to accomplish this.
		 *
		 * For streams representing subnets the originator is
		 * responsible for tagging its subnet bits in the src
		 * address but we have to renormalize
		 */
		linkid = sldata->linkid;
		if (sldata->flags & SLIF_PACKET) {
			if (sldata->bits) {
				linkid += syslink_getsubnet(sa) &
					  ((1 << sldata->bits) - 1);
			}
			if ((m = sldata->sior.sb_mb) != NULL) {
				sbinit(&sldata->sior, SYSLINK_SIOBUFSIZE);
				syslink_route(sldata->router, linkid, m);
			}
		} else {
			while ((m = syslink_parse_stream(&sldata->sior)) != NULL) {
				syslink_route(sldata->router, linkid, m);
			}
		}
	


		/*
		 * 
		 */
		if ((sldata->flags & SLIF_SUBNET) && sldata->bits && sa) {
			linkid += syslink_getsubnet(sa) &
				  ((1 << sldata->bits) - 1);
			FREE(sa, M_SONAME);
		} 
		if (error)
			break;

		/*
		 * Note: Incoming syslink messages must have their headers
		 * adjusted to reflect the origination address.  This will
		 * be handled by syslink_route.
		 */
		if (sldata->flags & SLIF_PACKET) {
			/*
			 * Packetized data can just be directly routed.
			 */
			if ((m = sldata->sior.sb_mb) != NULL) {
				sbinit(&sldata->sior, SYSLINK_SIOBUFSIZE);
				syslink_route(sldata->router, linkid, m);
			}
		} else {
			/*
			 * Stream data has to be parsed out.
			 */
			while ((m = syslink_parse_stream(&sldata->sior)) != NULL) {
				syslink_route(sldata->router, linkid, m);
			}
		}
	}

	/*
	 * Mark us as done and deref sldata.  Tell the writer to terminate as
	 * well.
	 */
	sldata->flags |= SLIF_RDONE;
	sbflush(&sldata->sior);
	sbflush(&sldata->siow);
	if ((sldata->flags & SLIF_WDONE) == 0) {
		sldata->flags |= SLIF_WQUIT;
		wakeup(&sldata->wthread);
	}
	wakeup(&sldata->rthread);
	wakeup(&sldata->wthread);
	sldata_rels(sldata);
}

/*
 * Read syslink messages from an external descriptor and route them.  Used
 * when no socket interface is available.
 */
static
void
syslink_rthread_fp(void *arg)
{
	struct sldata *sldata = arg;

#if 0
	/*
	 * Loop until told otherwise
	 */
	while ((sldata->flags & SLIF_RQUIT) == 0) {
		error = fp_read(slink->xfp,
				slbuf->buf +
				(slbuf->windex & slbuf->bufmask
				),
				count, &count, 0, UIO_SYSSPACE);
	}
#endif

	/*
	 * Mark us as done and deref sldata.  Tell the writer to terminate as
	 * well.
	 */
	sldata->flags |= SLIF_RDONE;
	sbflush(&sldata->sior);
	sbflush(&sldata->siow);
	if ((sldata->flags & SLIF_WDONE) == 0) {
		sldata->flags |= SLIF_WQUIT;
		wakeup(&sldata->wthread);
	}
	wakeup(&sldata->rthread);
	wakeup(&sldata->wthread);
	sldata_rels(sldata);
}

static
struct mbuf *
syslink_parse_stream(struct sockbuf *sio)
{
	return(NULL);
}

static
void
syslink_route(struct slrouter *slrouter, int linkid, struct mbuf *m)
{
	m_freem(m);
}

#if 0


		int count;
		int used;
		int error;

		/*
		 * Calculate contiguous space available to read and read as
		 * much as possible.
		 *
		 * If the entire buffer is used there's probably a format
		 * error of some sort and we terminate the link.
		 */
		used = slbuf->windex - slbuf->rindex;
		error = 0;

		/*
		 * Read some data, terminate the link if an error occurs or
		 * if EOF is encountered.  xfp can be NULL, indicating that
		 * the data was injected by other means.
		 */
		if (sldata->xfp) {
			count = slbuf->bufsize - 
				(slbuf->windex & slbuf->bufmask);
			if (count > slbuf->bufsize - used)
				count = slbuf->bufsize - used;
			if (count == 0)
				break;
			error = fp_read(sldata->xfp,
					slbuf->buf + 
					 (slbuf->windex & slbuf->bufmask),
					count, &count, 0, UIO_SYSSPACE);
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
		 * Process as many syslink messages as we can.  The record
		 * length must be at least a minimal PAD record (8 bytes).
		 */
		while (slbuf->windex - slbuf->rindex >= min_msg_size) {
			int aligned_reclen;

			head = (void *)(slbuf->buf + 
					(slbuf->rindex & slbuf->bufmask));
			if (head->sm_bytes < min_msg_size) {
				error = EINVAL;
				break;
			}
			aligned_reclen = SLMSG_ALIGN(head->sm_bytes);

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
			 * Process non-pad messages.  Non-pad messages have
			 * to be at least the size of the syslink_msg
			 * structure.
			 *
			 * A PAD message's sm_cmd field contains 0.
			 */
			if (head->sm_cmd) {
				if (head->sm_bytes < sizeof(*head)) {
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

}

#endif

/*
 * This thread takes outgoing syslink messages queued to wbuf and writes them
 * to the descriptor.  PAD is stripped.  PAD is also added as required to
 * conform to the outgoing descriptor's buffering requirements.
 */
static
void
syslink_wthread_so(void *arg)
{
	struct sldata *sldata = arg;
	struct slrouter *slrouter;
	struct syslink_msg *head;
	struct sockaddr *sa;
	struct socket *so;
	struct iovec aiov;
	struct uio auio;
	int error;
	int avail;
	int bytes;

#if 0
	so = (void *)sldata->xfp->f_data;
	slrouter = sldata->router;

	while ((sldata->flags & SLIF_WQUIT) == 0) {
		/*
		 * Deal with any broadcast data sitting in the route node's
		 * broadcast buffer.  If we have fallen too far behind the
		 * data may no longer be valid.
		 *
		 * avail -- available data in broadcast buffer and
		 * bytes -- available contiguous data in broadcast buffer
		 */
		if (slrouter->bbuf.rindex - sldata->bindex > 0)
			sldata->bindex = slrouter->bbuf.rindex;
		if ((avail = slrouter->bbuf.windex - sldata->bindex) > 0) {
			bytes = slrouter->bbuf.bufsize -
				(sldata->bindex & slrouter->bbuf.bufmask);
			if (bytes > avail)
				bytes = avail;
			head = (void *)(slrouter->bbuf.buf +
				(sldata->bindex & slrouter->bbuf.bufmask));
			/*
			 * Break into packets if necessary, else just write
			 * it all in one fell swoop.
			 */
			aiov.iov_base = (void *)head;
			aiov.iov_len = bytes;
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			auio.uio_offset = 0;
			auio.uio_resid = bytes;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_WRITE;
			auio.uio_td = curthread;
			if (sldata->flags & SLIF_PACKET) {
				if (head->sm_bytes < SL_MIN_MESSAGE_SIZE) {
					kprintf("syslink_msg too small, terminating\n");
					break;
				}
				if (head->sm_bytes > bytes) {
					kprintf("syslink_msg not FIFO aligned, terminating\n");
					break;
				}
				bytes = SLMSG_ALIGN(head->sm_bytes);
				so_pru_sosend(so, sa, &auio, NULL, NULL, 0, curthread);
			} else {
				so_pru_sosend(so, sa, &auio, NULL, NULL, 0, curthread);
			}
			continue;
		}

		/*
		 * Deal with mbuf records waiting to be output
		 */
		if (sldata->siow.sb_mb != NULL) {
			
		}

		/*
		 * Block waiting for something to do.
		 */
		tsleep(&sldata->wthread, 0, "wait", 0);
	}


		error = 0;
		for (;;) {
			int aligned_reclen;
			int used;
			int count;

			used = slbuf->windex - slbuf->rindex;
			if (used < SL_MIN_MESSAGE_SIZE)
				break;

			head = (void *)(slbuf->buf + 
					(slbuf->rindex & slbuf->bufmask));
			if (head->sm_bytes < SL_MIN_MESSAGE_SIZE) {
				error = EINVAL;
				break;
			}
			aligned_reclen = SLMSG_ALIGN(head->sm_bytes);

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
			 * Write it out whether it is PAD or not.
			 * XXX re-PAD for output here.
			 */
			error = fp_write(sldata->xfp, head,
					 aligned_reclen,
					 &count,
					 UIO_SYSSPACE);
			if (error && error != ENOBUFS)
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
#endif
	sldata->flags |= SLIF_WDONE;
	sldata_rels(sldata);
}

static
void
syslink_wthread_fp(void *arg)
{
	struct sldata *sldata = arg;

	sldata->flags |= SLIF_WDONE;
	sldata_rels(sldata);
}

static
void
slbuf_alloc(struct slbuf *slbuf, int bytes)
{
	bzero(slbuf, sizeof(*slbuf));
	slbuf->buf = kmalloc(bytes, M_SYSLINK, M_WAITOK);
	slbuf->bufsize = bytes;
	slbuf->bufmask = bytes - 1;
}

static
void
slbuf_free(struct slbuf *slbuf)
{
	kfree(slbuf->buf, M_SYSLINK);
	slbuf->buf = NULL;
}

static
void
sldata_rels(struct sldata *sldata)
{
	struct slrouter *slrouter;

	if (--sldata->refs == 0) {
		slrouter = sldata->router;
		KKASSERT(slrouter != NULL);
		++slrouter->refs;
		RB_REMOVE(sldata_rb_tree, 
			  &sldata->router->sldata_rb_root, sldata);
		sldata->router = NULL;
		kfree(sldata, M_SYSLINK);
		slrouter_rels(slrouter);
	}
}

static
void
slrouter_rels(struct slrouter *slrouter)
{
	if (--slrouter->refs == 0 && RB_EMPTY(&slrouter->sldata_rb_root)) {
		KKASSERT(slrouter->flags & SLIF_DESTROYED);
		RB_REMOVE(slrouter_rb_tree, &slrouter_rb_root, slrouter);
		alist_destroy(slrouter->bitmap, M_SYSLINK);
		slrouter->bitmap = NULL;
		slbuf_free(&slrouter->bbuf);
		kfree(slrouter, M_SYSLINK);
	}
}

/*
 * A switched ethernet socket connected to a syslink router node may
 * represent an entire subnet.  We need to generate a subnet id from
 * the originating IP address which the caller can then incorporate into
 * the base linkid assigned to the connection to form the actual linkid
 * originating the message.
 */
static
int
syslink_getsubnet(struct sockaddr *sa)
{
	struct in_addr *i4;
	struct in6_addr *i6;
	int linkid;

	switch(sa->sa_family) {
	case AF_INET:
		i4 = &((struct sockaddr_in *)sa)->sin_addr;
		linkid = (int)ntohl(i4->s_addr);
		break;
	case AF_INET6:
		i6 = &((struct sockaddr_in6 *)sa)->sin6_addr;
		linkid = (int)ntohl(i6->s6_addr32[0]); /* XXX */
		break;
	default:
		linkid = 0;
		break;
	}
	return(linkid);
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
#if 0
	struct syslink_msg *head;
	int bytes;
	int contig;
#endif
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
	error = 0;

#if 0
	/*
	 * Calculate the number of bytes we can transfer in one shot.  Transfers
	 * do not wrap the FIFO.
	 */
	contig = slbuf->bufsize - (slbuf->rindex & slbuf->bufmask);
	for (;;) {
		bytes = slbuf->windex - slbuf->rindex;
		if (bytes)
			break;
		if (sldata->flags & SLIF_RDONE) {
			error = EIO;
			break;
		}
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
#endif
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
#if 0
	struct slbuf *slbuf = &sldata->rbuf;
	struct syslink_msg *head;
	int bytes;
	int contig;
#endif
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
	error = 0;

#if 0
	/* 
	 * Calculate the maximum number of contiguous bytes that may be
	 * available.  Caller is required to not wrap our FIFO.
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
		if (sldata->flags & SLIF_RDONE) {
			error = EIO;
			goto done;
		}
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
	if (error == 0) {
		slbuf->windex += bytes;
		wakeup(slbuf);
	}
done:
#endif
	lockmgr(&sldata->rlock, LK_RELEASE);
	return(error);
}

static
int
syslink_close (struct file *fp)
{
	struct sldata *sldata;

	sldata = fp->f_data;
	if ((sldata->flags & SLIF_RQUIT) == 0) {
		sldata->flags |= SLIF_RQUIT;
		wakeup(&sldata->rthread);
	}
	if ((sldata->flags & SLIF_WQUIT) == 0) {
		sldata->flags |= SLIF_WQUIT;
		wakeup(&sldata->wthread);
	}
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
 * This routine is called from a route node's reader thread to process a
 * syslink message once it has been completely read and its size validated.
 */
static
int
process_syslink_msg(struct sldata *sldata, struct syslink_msg *head)
{
	kprintf("process syslink msg %08x\n", head->sm_cmd);
	return(0);
}

/*
 * Validate that the syslink message header(s) are correctly sized.
 */
static
int
syslink_validate(struct syslink_msg *head, int bytes)
{
	const int min_msg_size = SL_MIN_MESSAGE_SIZE;
	int aligned_reclen;

	while (bytes) {
		/*
		 * Message size and alignment
		 */
		if (bytes < min_msg_size)
			return (EINVAL);
		if (bytes & SL_ALIGNMASK)
			return (EINVAL);
		if (head->sm_cmd && bytes < sizeof(struct syslink_msg))
			return (EINVAL);

		/*
		 * Buffer must contain entire record
		 */
		aligned_reclen = SLMSG_ALIGN(head->sm_bytes);
		if (bytes < aligned_reclen)
			return (EINVAL);
		bytes -= aligned_reclen;
		head = (void *)((char *)head + aligned_reclen);
	}
	return(0);
}

