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
 * $DragonFly: src/sys/kern/kern_device.c,v 1.11 2004/05/19 22:52:58 dillon Exp $
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

static struct cdevlink 	*cdevbase[NUMCDEVSW];

static int cdevsw_putport(lwkt_port_t port, lwkt_msg_t msg);

struct cdevsw dead_cdevsw;

/*
 * Initialize a message port to serve as the default message-handling port
 * for device operations.  This message port provides compatibility with
 * traditional cdevsw dispatch functions by running them synchronously.
 *
 * YYY NOTE: ms_cmd can now hold a function pointer, should this code be
 * converted from an integer op to a function pointer with a flag to
 * indicate legacy operation?
 */
static void
init_default_cdevsw_port(lwkt_port_t port)
{
    lwkt_initport(port, NULL);
    port->mp_putport = cdevsw_putport;
}

static
int
cdevsw_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
    cdevallmsg_t msg = (cdevallmsg_t)lmsg;
    struct cdevsw *devsw = msg->am_msg.dev->si_devsw;
    int error;

    /*
     * Run the device switch function synchronously in the context of the
     * caller and return a synchronous error code (anything not EASYNC).
     */
    switch(msg->am_lmsg.ms_cmd.cm_op) {
    case CDEV_CMD_OPEN:
	error = devsw->old_open(
		    msg->am_open.msg.dev,
		    msg->am_open.oflags,
		    msg->am_open.devtype,
		    msg->am_open.td);
	break;
    case CDEV_CMD_CLOSE:
	error = devsw->old_close(
		    msg->am_close.msg.dev,
		    msg->am_close.fflag,
		    msg->am_close.devtype,
		    msg->am_close.td);
	break;
    case CDEV_CMD_STRATEGY:
	devsw->old_strategy(msg->am_strategy.bp);
	error = 0;
	break;
    case CDEV_CMD_IOCTL:
	error = devsw->old_ioctl(
		    msg->am_ioctl.msg.dev,
		    msg->am_ioctl.cmd,
		    msg->am_ioctl.data,
		    msg->am_ioctl.fflag,
		    msg->am_ioctl.td);
	break;
    case CDEV_CMD_DUMP:
	error = devsw->old_dump(
		    msg->am_dump.msg.dev,
		    msg->am_dump.count,
		    msg->am_dump.blkno,
		    msg->am_dump.secsize);
	break;
    case CDEV_CMD_PSIZE:
	msg->am_psize.result = devsw->old_psize(msg->am_psize.msg.dev);
	error = 0;	/* XXX */
	break;
    case CDEV_CMD_READ:
	error = devsw->old_read(
		    msg->am_read.msg.dev,
		    msg->am_read.uio,
		    msg->am_read.ioflag);
	break;
    case CDEV_CMD_WRITE:
	error = devsw->old_write(
		    msg->am_read.msg.dev,
		    msg->am_read.uio,
		    msg->am_read.ioflag);
	break;
    case CDEV_CMD_POLL:
	msg->am_poll.events = devsw->old_poll(
				msg->am_poll.msg.dev,
				msg->am_poll.events,
				msg->am_poll.td);
	error = 0;
	break;
    case CDEV_CMD_KQFILTER:
	msg->am_kqfilter.result = devsw->old_kqfilter(
				msg->am_kqfilter.msg.dev,
				msg->am_kqfilter.kn);
	error = 0;
	break;
    case CDEV_CMD_MMAP:
	msg->am_mmap.result = devsw->old_mmap(
		    msg->am_mmap.msg.dev,
		    msg->am_mmap.offset,
		    msg->am_mmap.nprot);
	error = 0;	/* XXX */
	break;
    default:
	error = ENOSYS;
	break;
    }
    KKASSERT(error != EASYNC);
    return(error);
}

static __inline
lwkt_port_t
_init_cdevmsg(dev_t dev, cdevmsg_t msg, int cmd)
{
    lwkt_initmsg_simple(&msg->msg, cmd);
    msg->dev = dev;
    return(dev->si_port);
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

/*
 * note: the disk layer is expected to set count, blkno, and secsize before
 * forwarding the message.
 */
int
dev_ddump(dev_t dev)
{
    struct cdevmsg_dump	msg;
    lwkt_port_t port;

    port = _init_cdevmsg(dev, &msg.msg, CDEV_CMD_DUMP);
    if (port == NULL)
	return(ENXIO);
    msg.count = 0;
    msg.blkno = 0;
    msg.secsize = 0;
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

const char *
dev_dname(dev_t dev)
{
    return(dev->si_devsw->d_name);
}

int
dev_dflags(dev_t dev)
{
    return(dev->si_devsw->d_flags);
}

int
dev_dmaj(dev_t dev)
{
    return(dev->si_devsw->d_maj);
}

lwkt_port_t
dev_dport(dev_t dev)
{
    return(dev->si_port);
}

/*
 * Convert a cdevsw template into the real thing, filling in fields the
 * device left empty with appropriate defaults.
 */
void
compile_devsw(struct cdevsw *devsw)
{
    static lwkt_port devsw_compat_port;

    if (devsw_compat_port.mp_putport == NULL)
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
    if (devsw->d_clone == NULL)
	devsw->d_clone = noclone;
}

/*
 * This makes a cdevsw entry visible to userland (e.g /dev/<blah>).
 *
 * The kernel can overload a major number with multiple cdevsw's but only
 * the one installed in cdevbase[] is visible to userland.  make_dev() does
 * not automatically call cdevsw_add() (nor do we want it to, since 
 * partition-managed disk devices are overloaded on top of the raw device).
 */
int
cdevsw_add(struct cdevsw *devsw, u_int mask, u_int match)
{
    int maj;
    struct cdevlink *link;

    compile_devsw(devsw);
    maj = devsw->d_maj;
    if (maj < 0 || maj >= NUMCDEVSW) {
	printf("%s: ERROR: driver has bogus cdevsw->d_maj = %d\n",
	    devsw->d_name, maj);
	return (EINVAL);
    }
    for (link = cdevbase[maj]; link; link = link->next) {
	/*
	 * If we get an exact match we usurp the target, but we only print
	 * a warning message if a different device switch is installed.
	 */
	if (link->mask == mask && link->match == match) {
	    if (link->devsw != devsw) {
		    printf("WARNING: \"%s\" (%p) is usurping \"%s\"'s (%p)"
			" cdevsw[]\n",
			devsw->d_name, devsw, 
			link->devsw->d_name, link->devsw);
		    link->devsw = devsw;
		    ++devsw->d_refs;
	    }
	    return(0);
	}
	/*
	 * XXX add additional warnings for overlaps
	 */
    }

    link = malloc(sizeof(struct cdevlink), M_DEVBUF, M_INTWAIT|M_ZERO);
    link->mask = mask;
    link->match = match;
    link->devsw = devsw;
    link->next = cdevbase[maj];
    cdevbase[maj] = link;
    ++devsw->d_refs;
    return(0);
}

/*
 * Should only be used by udev2dev().
 *
 * If the minor number is -1, we match the first cdevsw we find for this
 * major. 
 *
 * Note that this function will return NULL if the minor number is not within
 * the bounds of the installed mask(s).
 */
struct cdevsw *
cdevsw_get(int x, int y)
{
    struct cdevlink *link;

    if (x < 0 || x >= NUMCDEVSW)
	return(NULL);
    for (link = cdevbase[x]; link; link = link->next) {
	if (y == -1 || (link->mask & y) == link->match)
	    return(link->devsw);
    }
    return(NULL);
}

/*
 * Use the passed cdevsw as a template to create our intercept cdevsw,
 * and install and return ours.
 */
struct cdevsw *
cdevsw_add_override(dev_t backing_dev, u_int mask, u_int match)
{
    struct cdevsw *devsw;
    struct cdevsw *bsw = backing_dev->si_devsw;

    devsw = malloc(sizeof(struct cdevsw), M_DEVBUF, M_INTWAIT|M_ZERO);
    devsw->d_name = bsw->d_name;
    devsw->d_maj = bsw->d_maj;
    devsw->d_flags = bsw->d_flags;
    compile_devsw(devsw);
    cdevsw_add(devsw, mask, match);

    return(devsw);
}

/*
 * Override a device's port, returning the previously installed port.  This
 * is XXX very dangerous.
 */
lwkt_port_t
cdevsw_dev_override(dev_t dev, lwkt_port_t port)
{
    lwkt_port_t oport;

    oport = dev->si_port;
    dev->si_port = port;
    return(oport);
}

/*
 * Remove a cdevsw entry from the cdevbase[] major array so no new user opens
 * can be performed, and destroy all devices installed in the hash table
 * which are associated with this cdevsw.  (see destroy_all_dev()).
 */
int
cdevsw_remove(struct cdevsw *devsw, u_int mask, u_int match)
{
    int maj = devsw->d_maj;
    struct cdevlink *link;
    struct cdevlink **plink;
 
    if (maj < 0 || maj >= NUMCDEVSW) {
	printf("%s: ERROR: driver has bogus cdevsw->d_maj = %d\n",
	    devsw->d_name, maj);
	return EINVAL;
    }
    if (devsw != &dead_cdevsw)
	destroy_all_dev(devsw, mask, match);
    for (plink = &cdevbase[maj]; (link = *plink) != NULL; plink = &link->next) {
	if (link->mask == mask && link->match == match) {
	    if (link->devsw == devsw)
		break;
	    printf("%s: ERROR: cannot remove from cdevsw[], its major"
		    " number %d was stolen by %s\n",
		    devsw->d_name, maj,
		    link->devsw->d_name
	    );
	}
    }
    if (link == NULL) {
	printf("%s(%d): WARNING: cdevsw removed multiple times!\n",
		devsw->d_name, maj);
    } else {
	*plink = link->next;
	--devsw->d_refs; /* XXX cdevsw_release() / record refs */
	free(link, M_DEVBUF);
    }
    if (devsw->d_refs != 0) {
	printf("%s: Warning: cdevsw_remove() called while %d device refs"
		" still exist! (major %d)\n", 
		devsw->d_name,
		devsw->d_refs,
		maj);
    } else {
	printf("%s: cdevsw removed\n", devsw->d_name);
    }
    return 0;
}

/*
 * Release a cdevsw entry.  When the ref count reaches zero, recurse
 * through the stack.
 */
void
cdevsw_release(struct cdevsw *devsw)
{
    --devsw->d_refs;
    if (devsw->d_refs == 0) {
	/* XXX */
    }
}

