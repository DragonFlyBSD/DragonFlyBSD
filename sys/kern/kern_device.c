/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com> All rights reserved.
 * cdevsw from kern/kern_conf.c Copyright (c) 1995 Terrence R. Lambert
 * cdevsw from kern/kern_conf.c Copyright (c) 1995 Julian R. Elishcer,
 *							All rights reserved.
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
 *
 * $DragonFly: src/sys/kern/kern_device.c,v 1.1 2003/07/22 17:03:33 dillon Exp $
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/queue.h>
#include <sys/msgport.h>
#include <sys/device.h>
#include <machine/stdarg.h>
#include <sys/proc.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>

static struct cdevsw 	*cdevsw[NUMCDEVSW];
static struct lwkt_port	*cdevport[NUMCDEVSW];

static int cdevsw_putport(lwkt_port_t port, lwkt_msg_t msg);

/*
 * Initialize a message port to serve as the default message-handling port
 * for device operations.  This message port provides compatibility with
 * traditional cdevsw dispatch functions.  There are two primary modes:
 *
 * mp_td is NULL:  The d_autoq mask is ignored and all messages are translated
 * 		   into directly, synchronous cdevsw calls.
 *
 * mp_td not NULL: The d_autoq mask is used to determine which messages should
 *		   be queued and which should be handled synchronously.
 *
 * Don't worry too much about optimizing this code, the critical devices
 * will implement their own port messaging functions directly.
 */
static void
init_default_cdevsw_port(lwkt_port_t port)
{
    lwkt_init_port(port, NULL);
    port->mp_beginmsg = cdevsw_putport;
}

static
int
cdevsw_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
    cdevallmsg_t msg = (cdevallmsg_t)lmsg;
    struct cdevsw *csw = msg->am_msg.csw;
    int error;

    /*
     * If queueable then officially queue the message
     */
    if (port->mp_td) {
	int mask = (1 << (msg->am_lmsg.ms_cmd & MSG_SUBCMD_MASK));
	if (csw->d_autoq & mask) 
	    return(lwkt_putport(port, &msg->am_lmsg));
    }

    /*
     * Run the device switch function synchronously in the context of the
     * caller and return a synchronous error code (anything not EINPROGRESS).
     */
    switch(msg->am_lmsg.ms_cmd) {
    case CDEV_CMD_OPEN:
	error = csw->old_open(
		    msg->am_open.msg.dev,
		    msg->am_open.oflags,
		    msg->am_open.devtype,
		    msg->am_open.td);
	break;
    case CDEV_CMD_CLOSE:
	error = csw->old_close(
		    msg->am_close.msg.dev,
		    msg->am_close.fflag,
		    msg->am_close.devtype,
		    msg->am_close.td);
	break;
    case CDEV_CMD_STRATEGY:
	csw->old_strategy(msg->am_strategy.bp);
	error = 0;
	break;
    case CDEV_CMD_IOCTL:
	error = csw->old_ioctl(
		    msg->am_ioctl.msg.dev,
		    msg->am_ioctl.cmd,
		    msg->am_ioctl.data,
		    msg->am_ioctl.fflag,
		    msg->am_ioctl.td);
	break;
    case CDEV_CMD_DUMP:
	error = csw->old_dump(msg->am_ioctl.msg.dev);
	break;
    case CDEV_CMD_PSIZE:
	msg->am_psize.result = csw->old_psize(msg->am_psize.msg.dev);
	error = 0;	/* XXX */
	break;
    case CDEV_CMD_READ:
	error = csw->old_read(
		    msg->am_read.msg.dev,
		    msg->am_read.uio,
		    msg->am_read.ioflag);
	break;
    case CDEV_CMD_WRITE:
	error = csw->old_write(
		    msg->am_read.msg.dev,
		    msg->am_read.uio,
		    msg->am_read.ioflag);
	break;
    case CDEV_CMD_POLL:
	msg->am_poll.events = csw->old_poll(
				msg->am_poll.msg.dev,
				msg->am_poll.events,
				msg->am_poll.td);
	error = 0;
	break;
    case CDEV_CMD_KQFILTER:
	msg->am_kqfilter.result = csw->old_kqfilter(
				msg->am_kqfilter.msg.dev,
				msg->am_kqfilter.kn);
	error = 0;
	break;
    case CDEV_CMD_MMAP:
	msg->am_mmap.result = csw->old_mmap(
		    msg->am_mmap.msg.dev,
		    msg->am_mmap.offset,
		    msg->am_mmap.nprot);
	error = 0;	/* XXX */
	break;
    default:
	error = ENOSYS;
	break;
    }
    KKASSERT(error != EINPROGRESS);
    return(error);
}

/*
 * These device dispatch functions provide convenient entry points for
 * any code wishing to make a dev call.
 *
 * YYY we ought to be able to optimize the port lookup by caching it in
 * the dev_t structure itself.
 */
static __inline
struct cdevsw *
_devsw(dev_t dev)
{
    if (dev->si_devsw)
	return (dev->si_devsw);
    return(cdevsw[major(dev)]);
}

static __inline
lwkt_port_t
_init_cdevmsg(dev_t dev, cdevmsg_t msg, int cmd)
{
    struct cdevsw *csw;

    lwkt_initmsg(&msg->msg, cmd);
    msg->dev = dev;
    msg->csw = csw = _devsw(dev);
    if (csw != NULL) {			/* YYY too hackish */
	KKASSERT(csw->d_port);		/* YYY too hackish */
	if (cdevport[major(dev)])	/* YYY too hackish */
	    return(cdevport[major(dev)]);
	return(csw->d_port);
    }
    return(NULL);
}

int
dev_dopen(dev_t dev, int oflags, int devtype, thread_t td)
{
    struct cdevmsg_open	msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_OPEN);
    if (port == NULL)
	return(ENXIO);
    msg.oflags = oflags;
    msg.devtype = devtype;
    msg.td = td;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_dclose(dev_t dev, int fflag, int devtype, thread_t td)
{
    struct cdevmsg_close msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_CLOSE);
    if (port == NULL)
	return(ENXIO);
    msg.fflag = fflag;
    msg.devtype = devtype;
    msg.td = td;
    return(lwkt_domsg(port, &msg.msg.msg));
}

void
dev_dstrategy(dev_t dev, struct buf *bp)
{
    struct cdevmsg_strategy msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_STRATEGY);
    KKASSERT(port);	/* 'nostrategy' function is NULL YYY */
    msg.bp = bp;
    lwkt_domsg(port, &msg.msg.msg);
}

int
dev_dioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, thread_t td)
{
    struct cdevmsg_ioctl msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_IOCTL);
    if (port == NULL)
	return(ENXIO);
    msg.cmd = cmd;
    msg.data = data;
    msg.fflag = fflag;
    msg.td = td;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_ddump(dev_t dev)
{
    struct cdevmsg_dump	msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_DUMP);
    if (port == NULL)
	return(ENXIO);
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_dpsize(dev_t dev)
{
    struct cdevmsg_psize msg;
    lwkt_port_t port;
    int error;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_PSIZE);
    if (port == NULL)
	return(-1);
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.result);
    return(-1);
}

int
dev_dread(dev_t dev, struct uio *uio, int ioflag)
{
    struct cdevmsg_read msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_READ);
    if (port == NULL)
	return(ENXIO);
    msg.uio = uio;
    msg.ioflag = ioflag;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_dwrite(dev_t dev, struct uio *uio, int ioflag)
{
    struct cdevmsg_write msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_WRITE);
    if (port == NULL)
	return(ENXIO);
    msg.uio = uio;
    msg.ioflag = ioflag;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_dpoll(dev_t dev, int events, thread_t td)
{
    struct cdevmsg_poll msg;
    lwkt_port_t port;
    int error;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_POLL);
    if (port == NULL)
	return(ENXIO);
    msg.events = events;
    msg.td = td;
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.events);
    return(seltrue(dev, msg.events, td));
}

int
dev_dkqfilter(dev_t dev, struct knote *kn)
{
    struct cdevmsg_kqfilter msg;
    lwkt_port_t port;
    int error;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_KQFILTER);
    if (port == NULL)
	return(ENXIO);
    msg.kn = kn;
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.result);
    return(ENODEV);
}

int
dev_dmmap(dev_t dev, vm_offset_t offset, int nprot)
{
    struct cdevmsg_mmap msg;
    lwkt_port_t port;
    int error;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_MMAP);
    if (port == NULL)
	return(-1);
    msg.offset = offset;
    msg.nprot = nprot;
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.result);
    return(-1);
}

int
dev_port_dopen(lwkt_port_t port, dev_t dev, int oflags, int devtype, thread_t td)
{
    struct cdevmsg_open	msg;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_OPEN);
    if (port == NULL)
	return(ENXIO);
    msg.oflags = oflags;
    msg.devtype = devtype;
    msg.td = td;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_port_dclose(lwkt_port_t port, dev_t dev, int fflag, int devtype, thread_t td)
{
    struct cdevmsg_close msg;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_CLOSE);
    if (port == NULL)
	return(ENXIO);
    msg.fflag = fflag;
    msg.devtype = devtype;
    msg.td = td;
    return(lwkt_domsg(port, &msg.msg.msg));
}

void
dev_port_dstrategy(lwkt_port_t port, dev_t dev, struct buf *bp)
{
    struct cdevmsg_strategy msg;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_STRATEGY);
    KKASSERT(port);	/* 'nostrategy' function is NULL YYY */
    msg.bp = bp;
    lwkt_domsg(port, &msg.msg.msg);
}

int
dev_port_dioctl(lwkt_port_t port, dev_t dev, u_long cmd, caddr_t data, int fflag, thread_t td)
{
    struct cdevmsg_ioctl msg;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_IOCTL);
    if (port == NULL)
	return(ENXIO);
    msg.cmd = cmd;
    msg.data = data;
    msg.fflag = fflag;
    msg.td = td;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_port_ddump(lwkt_port_t port, dev_t dev)
{
    struct cdevmsg_dump	msg;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_DUMP);
    if (port == NULL)
	return(ENXIO);
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_port_dpsize(lwkt_port_t port, dev_t dev)
{
    struct cdevmsg_psize msg;
    int error;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_PSIZE);
    if (port == NULL)
	return(-1);
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.result);
    return(-1);
}

int
dev_port_dread(lwkt_port_t port, dev_t dev, struct uio *uio, int ioflag)
{
    struct cdevmsg_read msg;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_READ);
    if (port == NULL)
	return(ENXIO);
    msg.uio = uio;
    msg.ioflag = ioflag;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_port_dwrite(lwkt_port_t port, dev_t dev, struct uio *uio, int ioflag)
{
    struct cdevmsg_write msg;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_WRITE);
    if (port == NULL)
	return(ENXIO);
    msg.uio = uio;
    msg.ioflag = ioflag;
    return(lwkt_domsg(port, &msg.msg.msg));
}

int
dev_port_dpoll(lwkt_port_t port, dev_t dev, int events, thread_t td)
{
    struct cdevmsg_poll msg;
    int error;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_POLL);
    if (port == NULL)
	return(ENXIO);
    msg.events = events;
    msg.td = td;
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.events);
    return(seltrue(dev, msg.events, td));
}

int
dev_port_dkqfilter(lwkt_port_t port, dev_t dev, struct knote *kn)
{
    struct cdevmsg_kqfilter msg;
    int error;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_KQFILTER);
    if (port == NULL)
	return(ENXIO);
    msg.kn = kn;
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.result);
    return(ENODEV);
}

int
dev_port_dmmap(lwkt_port_t port, dev_t dev, vm_offset_t offset, int nprot)
{
    struct cdevmsg_mmap msg;
    int error;

    _init_cdevmsg(dev, &msg.msg, CDEV_CMD_MMAP);
    if (port == NULL)
	return(-1);
    msg.offset = offset;
    msg.nprot = nprot;
    error = lwkt_domsg(port, &msg.msg.msg);
    if (error == 0)
	return(msg.result);
    return(-1);
}

const char *
dev_dname(dev_t dev)
{
    struct cdevsw *csw;

    if ((csw = _devsw(dev)) != NULL)
	return(csw->d_name);
    return(NULL);
}

int
dev_dflags(dev_t dev)
{
    struct cdevsw *csw;

    if ((csw = _devsw(dev)) != NULL)
	return(csw->d_flags);
    return(NULL);
}

int
dev_dmaj(dev_t dev)
{
    struct cdevsw *csw;

    if ((csw = _devsw(dev)) != NULL)
	return(csw->d_maj);
    return(NULL);
}

lwkt_port_t
dev_dport(dev_t dev)
{
    struct cdevsw *csw;

    if ((csw = _devsw(dev)) != NULL) {
	if (cdevport[major(dev)])	/* YYY too hackish */
	    return(cdevport[major(dev)]);
	return(csw->d_port);
    }
    return(NULL);
}

#if 0
/*
 * cdevsw[] array functions, moved from kern/kern_conf.c
 */
struct cdevsw *
devsw(dev_t dev)
{
    return(_devsw(dev));
}
#endif

/*
 * Convert a cdevsw template into the real thing, filling in fields the
 * device left empty with appropriate defaults.
 */
void
compile_devsw(struct cdevsw *devsw)
{
    static lwkt_port devsw_compat_port;

    if (devsw_compat_port.mp_beginmsg == NULL)
	init_default_cdevsw_port(&devsw_compat_port);
    
    if (devsw->old_open == NULL)
	devsw->old_open = noopen;
    if (devsw->old_close == NULL)
	devsw->old_close = noclose;
    if (devsw->old_read == NULL)
	devsw->old_read = noread;
    if (devsw->old_write == NULL)
	devsw->old_write = nowrite;
    if (devsw->old_ioctl == NULL)
	devsw->old_ioctl = noioctl;
    if (devsw->old_poll == NULL)
	devsw->old_poll = nopoll;
    if (devsw->old_mmap == NULL)
	devsw->old_mmap = nommap;
    if (devsw->old_strategy == NULL)
	devsw->old_strategy = nostrategy;
    if (devsw->old_dump == NULL)
	devsw->old_dump = nodump;
    if (devsw->old_psize == NULL)
	devsw->old_psize = nopsize;
    if (devsw->old_kqfilter == NULL)
	devsw->old_kqfilter = nokqfilter;

    if (devsw->d_port == NULL)
	devsw->d_port = &devsw_compat_port;
}

/*
 * Add a cdevsw entry
 */
int
cdevsw_add(struct cdevsw *newentry)
{
    compile_devsw(newentry);
    if (newentry->d_maj < 0 || newentry->d_maj >= NUMCDEVSW) {
	printf("%s: ERROR: driver has bogus cdevsw->d_maj = %d\n",
	    newentry->d_name, newentry->d_maj);
	return (EINVAL);
    }
    if (cdevsw[newentry->d_maj]) {
	printf("WARNING: \"%s\" is usurping \"%s\"'s cdevsw[]\n",
	    newentry->d_name, cdevsw[newentry->d_maj]->d_name);
    }
    cdevsw[newentry->d_maj] = newentry;
    return (0);
}

/*
 * Add a cdevsw entry and override the port.
 */
lwkt_port_t
cdevsw_add_override(struct cdevsw *newentry, lwkt_port_t port)
{
    int error;

    if ((error = cdevsw_add(newentry)) == 0)
	cdevport[newentry->d_maj] = port;
    return(newentry->d_port);
}

lwkt_port_t
cdevsw_dev_override(dev_t dev, lwkt_port_t port)
{
    struct cdevsw *csw;

    KKASSERT(major(dev) >= 0 && major(dev) < NUMCDEVSW);
    if ((csw = _devsw(dev)) != NULL) {
	cdevport[major(dev)] = port;
	return(csw->d_port);
    }
    return(NULL);
}

/*
 *  Remove a cdevsw entry
 */
int
cdevsw_remove(struct cdevsw *oldentry)
{
    if (oldentry->d_maj < 0 || oldentry->d_maj >= NUMCDEVSW) {
	printf("%s: ERROR: driver has bogus cdevsw->d_maj = %d\n",
	    oldentry->d_name, oldentry->d_maj);
	return EINVAL;
    }
    cdevsw[oldentry->d_maj] = NULL;
    cdevport[oldentry->d_maj] = NULL;
    return 0;
}

