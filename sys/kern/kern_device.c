/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com> All rights reserved.
 * cdevsw from kern/kern_conf.c Copyright (c) 1995 Terrence R. Lambert
 * cdevsw from kern/kern_conf.c Copyright (c) 1995 Julian R. Elishcer,
 *							All rights reserved.
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * $DragonFly: src/sys/kern/kern_device.c,v 1.18 2006/07/28 02:17:40 dillon Exp $
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/syslink.h>
#include <sys/proc.h>
#include <machine/stdarg.h>
#include <sys/thread2.h>

/*
 * system link descriptors identify the command in the
 * arguments structure.
 */
#define DDESCNAME(name) __CONCAT(__CONCAT(dev_,name),_desc)

#define DEVOP_DESC_INIT(name)						\
	    struct syslink_desc DDESCNAME(name) = {			\
		__offsetof(struct dev_ops, __CONCAT(d_, name)),	\
	    #name }

DEVOP_DESC_INIT(default);
DEVOP_DESC_INIT(open);
DEVOP_DESC_INIT(close);
DEVOP_DESC_INIT(read);
DEVOP_DESC_INIT(write);
DEVOP_DESC_INIT(ioctl);
DEVOP_DESC_INIT(dump);
DEVOP_DESC_INIT(psize);
DEVOP_DESC_INIT(poll);
DEVOP_DESC_INIT(mmap);
DEVOP_DESC_INIT(strategy);
DEVOP_DESC_INIT(kqfilter);
DEVOP_DESC_INIT(clone);

/*
 * Misc default ops
 */
struct dev_ops dead_dev_ops;

struct dev_ops default_dev_ops = {
	{ "null" },
	.d_default = NULL,	/* must be NULL */
	.d_open = noopen,
	.d_close = noclose,
	.d_read = noread,
	.d_write = nowrite,
	.d_ioctl = noioctl,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_strategy = nostrategy,
	.d_dump = nodump,
	.d_psize = nopsize,
	.d_kqfilter = nokqfilter,
	.d_clone = noclone
};
    
/*
 * This is used to look-up devices
 */
static struct dev_ops_link *dev_ops_array[NUMCDEVSW];

/************************************************************************
 *			GENERAL DEVICE API FUNCTIONS			*
 ************************************************************************/

int
dev_dopen(dev_t dev, int oflags, int devtype, struct ucred *cred)
{
	struct dev_open_args ap;

	ap.a_head.a_desc = &dev_open_desc;
	ap.a_head.a_dev = dev;
	ap.a_oflags = oflags;
	ap.a_devtype = devtype;
	ap.a_cred = cred;
	return(dev->si_ops->d_open(&ap));
}

int
dev_dclose(dev_t dev, int fflag, int devtype)
{
	struct dev_close_args ap;

	ap.a_head.a_desc = &dev_close_desc;
	ap.a_head.a_dev = dev;
	ap.a_fflag = fflag;
	ap.a_devtype = devtype;
	return(dev->si_ops->d_close(&ap));
}

int
dev_dread(dev_t dev, struct uio *uio, int ioflag)
{
	struct dev_read_args ap;
	int error;

	ap.a_head.a_desc = &dev_read_desc;
	ap.a_head.a_dev = dev;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	error = dev->si_ops->d_read(&ap);
	if (error == 0)
		dev->si_lastread = time_second;
	return (error);
}

int
dev_dwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct dev_write_args ap;
	int error;

	dev->si_lastwrite = time_second;
	ap.a_head.a_desc = &dev_write_desc;
	ap.a_head.a_dev = dev;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	error = dev->si_ops->d_write(&ap);
	return (error);
}

int
dev_dioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct ucred *cred)
{
	struct dev_ioctl_args ap;

	ap.a_head.a_desc = &dev_ioctl_desc;
	ap.a_head.a_dev = dev;
	ap.a_cmd = cmd;
	ap.a_data = data;
	ap.a_fflag = fflag;
	ap.a_cred = cred;
	return(dev->si_ops->d_ioctl(&ap));
}

int
dev_dpoll(dev_t dev, int events)
{
	struct dev_poll_args ap;
	int error;

	ap.a_head.a_desc = &dev_poll_desc;
	ap.a_head.a_dev = dev;
	ap.a_events = events;
	error = dev->si_ops->d_poll(&ap);
	if (error == 0)
		return(ap.a_events);
	return (seltrue(dev, events));
}

int
dev_dmmap(dev_t dev, vm_offset_t offset, int nprot)
{
	struct dev_mmap_args ap;
	int error;

	ap.a_head.a_desc = &dev_mmap_desc;
	ap.a_head.a_dev = dev;
	ap.a_offset = offset;
	ap.a_nprot = nprot;
	error = dev->si_ops->d_mmap(&ap);
	if (error == 0)
		return(ap.a_result);
	return(-1);
}

int
dev_dclone(dev_t dev)
{
	struct dev_clone_args ap;

	ap.a_head.a_desc = &dev_clone_desc;
	ap.a_head.a_dev = dev;
	return (dev->si_ops->d_clone(&ap));
}

/*
 * Core device strategy call, used to issue I/O on a device.  There are
 * two versions, a non-chained version and a chained version.  The chained
 * version reuses a BIO set up by vn_strategy().  The only difference is
 * that, for now, we do not push a new tracking structure when chaining
 * from vn_strategy.  XXX this will ultimately have to change.
 */
void
dev_dstrategy(dev_t dev, struct bio *bio)
{
	struct dev_strategy_args ap;
	struct bio_track *track;

	ap.a_head.a_desc = &dev_strategy_desc;
	ap.a_head.a_dev = dev;
	ap.a_bio = bio;

	KKASSERT(bio->bio_track == NULL);
	KKASSERT(bio->bio_buf->b_cmd != BUF_CMD_DONE);
	if (bio->bio_buf->b_cmd == BUF_CMD_READ)
	    track = &dev->si_track_read;
	else
	    track = &dev->si_track_write;
	atomic_add_int(&track->bk_active, 1);
	bio->bio_track = track;
	(void)dev->si_ops->d_strategy(&ap);
}

void
dev_dstrategy_chain(dev_t dev, struct bio *bio)
{
	struct dev_strategy_args ap;

	KKASSERT(bio->bio_track != NULL);
	ap.a_head.a_desc = &dev_strategy_desc;
	ap.a_head.a_dev = dev;
	ap.a_bio = bio;
	(void)dev->si_ops->d_strategy(&ap);
}

/*
 * note: the disk layer is expected to set count, blkno, and secsize before
 * forwarding the message.
 */
int
dev_ddump(dev_t dev)
{
	struct dev_dump_args ap;

	ap.a_head.a_desc = &dev_dump_desc;
	ap.a_head.a_dev = dev;
	ap.a_count = 0;
	ap.a_blkno = 0;
	ap.a_secsize = 0;
	return(dev->si_ops->d_dump(&ap));
}

int
dev_dpsize(dev_t dev)
{
	struct dev_psize_args ap;
	int error;

	ap.a_head.a_desc = &dev_psize_desc;
	ap.a_head.a_dev = dev;
	error = dev->si_ops->d_psize(&ap);
	if (error == 0)
		return (ap.a_result);
	return(-1);
}

int
dev_dkqfilter(dev_t dev, struct knote *kn)
{
	struct dev_kqfilter_args ap;
	int error;

	ap.a_head.a_desc = &dev_kqfilter_desc;
	ap.a_head.a_dev = dev;
	ap.a_kn = kn;
	error = dev->si_ops->d_kqfilter(&ap);
	if (error == 0)
		return(ap.a_result);
	return(ENODEV);
}

/************************************************************************
 *			DEVICE HELPER FUNCTIONS				*
 ************************************************************************/

const char *
dev_dname(dev_t dev)
{
    return(dev->si_ops->head.name);
}

int
dev_dflags(dev_t dev)
{
    return(dev->si_ops->head.flags);
}

int
dev_dmaj(dev_t dev)
{
    return(dev->si_ops->head.maj);
}

/*
 * Used when forwarding a request through layers.  The caller adjusts
 * ap->a_head.a_dev and then calls this function.
 */
int
dev_doperate(struct dev_generic_args *ap)
{
    int (*func)(struct dev_generic_args *);

    func = *(void **)((char *)ap->a_dev->si_ops + ap->a_desc->sd_offset);
    return (func(ap));
}

/*
 * Used by the console intercept code only.  Issue an operation through
 * a foreign ops structure allowing the ops structure associated
 * with the device to remain intact.
 */
int
dev_doperate_ops(struct dev_ops *ops, struct dev_generic_args *ap)
{
    int (*func)(struct dev_generic_args *);

    func = *(void **)((char *)ops + ap->a_desc->sd_offset);
    return (func(ap));
}

/*
 * Convert a template dev_ops into the real thing by filling in 
 * uninitialized fields.
 */
void
compile_dev_ops(struct dev_ops *ops)
{
	int offset;

	for (offset = offsetof(struct dev_ops, dev_ops_first_field);
	     offset <= offsetof(struct dev_ops, dev_ops_last_field);
	     offset += sizeof(void *)
	) {
		void **func_p = (void **)((char *)ops + offset);
		void **def_p = (void **)((char *)&default_dev_ops + offset);
		if (*func_p == NULL) {
			if (ops->d_default)
				*func_p = ops->d_default;
			else
				*func_p = *def_p;
		}
	}
}

/************************************************************************
 *			MAJOR/MINOR SPACE FUNCTION 			*
 ************************************************************************/

/*
 * This makes a dev_ops entry visible to userland (e.g /dev/<blah>).
 *
 * The kernel can overload a major number by making multiple dev_ops_add()
 * calls, but only the most recent one (the first one in the dev_ops_array[]
 * list matching the mask/match) will be visible to userland.  make_dev() does
 * not automatically call dev_ops_add() (nor do we want it to, since 
 * partition-managed disk devices are overloaded on top of the raw device).
 *
 * Disk devices typically register their major, e.g. 'ad0', and then call
 * into the disk label management code which overloads its own onto e.g. 'ad0'
 * to support all the various slice and partition combinations.
 *
 * The mask/match supplied in this call are a full 32 bits and the same
 * mask and match must be specified in a later dev_ops_remove() call to
 * match this add.  However, the match value for the minor number should never
 * have any bits set in the major number's bit range (8-15).  The mask value
 * may be conveniently specified as -1 without creating any major number
 * interference.
 */
int
dev_ops_add(struct dev_ops *ops, u_int mask, u_int match)
{
    int maj;
    struct dev_ops_link *link;

    compile_dev_ops(ops);
    maj = ops->head.maj;
    if (maj < 0 || maj >= NUMCDEVSW) {
	    printf("%s: ERROR: driver has bogus dev_ops->head.maj = %d\n",
		   ops->head.name, maj);
	    return (EINVAL);
    }
    for (link = dev_ops_array[maj]; link; link = link->next) {
	    /*
	     * If we get an exact match we usurp the target, but we only print
	     * a warning message if a different device switch is installed.
	     */
	    if (link->mask == mask && link->match == match) {
		    if (link->ops != ops) {
			    printf("WARNING: \"%s\" (%p) is usurping \"%s\"'s"
				" (%p) dev_ops_array[]\n",
				ops->head.name, ops, 
				link->ops->head.name, link->ops);
			    link->ops = ops;
			    ++ops->head.refs;
		    }
		    return(0);
	    }
	    /*
	     * XXX add additional warnings for overlaps
	     */
    }

    link = malloc(sizeof(struct dev_ops_link), M_DEVBUF, M_INTWAIT|M_ZERO);
    link->mask = mask;
    link->match = match;
    link->ops = ops;
    link->next = dev_ops_array[maj];
    dev_ops_array[maj] = link;
    ++ops->head.refs;
    return(0);
}

/*
 * Should only be used by udev2dev().
 *
 * If the minor number is -1, we match the first ops we find for this
 * major.   If the mask is not -1 then multiple minor numbers can match
 * the same ops.
 *
 * Note that this function will return NULL if the minor number is not within
 * the bounds of the installed mask(s).
 *
 * The specified minor number should NOT include any major bits.
 */
struct dev_ops *
dev_ops_get(int x, int y)
{
	struct dev_ops_link *link;

	if (x < 0 || x >= NUMCDEVSW)
		return(NULL);
	for (link = dev_ops_array[x]; link; link = link->next) {
		if (y == -1 || (link->mask & y) == link->match)
			return(link->ops);
	}
	return(NULL);
}

/*
 * Take a cookie cutter to the major/minor device space for the passed
 * device and generate a new dev_ops visible to userland which the caller
 * can then modify.  The original device is not modified but portions of
 * its major/minor space will no longer be visible to userland.
 */
struct dev_ops *
dev_ops_add_override(dev_t backing_dev, struct dev_ops *template,
		     u_int mask, u_int match)
{
	struct dev_ops *ops;
	struct dev_ops *backing_ops = backing_dev->si_ops;

	ops = malloc(sizeof(struct dev_ops), M_DEVBUF, M_INTWAIT);
	*ops = *template;
	ops->head.name = backing_ops->head.name;
	ops->head.maj = backing_ops->head.maj;
	ops->head.flags = backing_ops->head.flags;
	compile_dev_ops(ops);
	dev_ops_add(ops, mask, match);

	return(ops);
}

/*
 * Remove all matching dev_ops entries from the dev_ops_array[] major
 * array so no new user opens can be performed, and destroy all devices
 * installed in the hash table that are associated with this dev_ops.  (see
 * destroy_all_devs()).
 *
 * The mask and match should match a previous call to dev_ops_add*().
 */
int
dev_ops_remove(struct dev_ops *ops, u_int mask, u_int match)
{
	int maj = ops->head.maj;
	struct dev_ops_link *link;
	struct dev_ops_link **plink;
     
	if (maj < 0 || maj >= NUMCDEVSW) {
		printf("%s: ERROR: driver has bogus ops->d_maj = %d\n",
			ops->head.name, maj);
		return EINVAL;
	}
	if (ops != &dead_dev_ops)
		destroy_all_devs(ops, mask, match);
	for (plink = &dev_ops_array[maj]; (link = *plink) != NULL;
	     plink = &link->next) {
		if (link->mask == mask && link->match == match) {
			if (link->ops == ops)
				break;
			printf("%s: ERROR: cannot remove from dev_ops_array[], "
			       "its major number %d was stolen by %s\n",
				ops->head.name, maj,
				link->ops->head.name
			);
		}
	}
	if (link == NULL) {
		printf("%s(%d)[%08x/%08x]: WARNING: ops removed "
		       "multiple times!\n",
		       ops->head.name, maj, mask, match);
	} else {
		*plink = link->next;
		--ops->head.refs; /* XXX ops_release() / record refs */
		free(link, M_DEVBUF);
	}
	if (dev_ops_array[maj] == NULL && ops->head.refs != 0) {
		printf("%s(%d)[%08x/%08x]: Warning: dev_ops_remove() called "
			"while %d device refs still exist!\n", 
			ops->head.name, maj, mask, match, ops->head.refs);
	} else {
		printf("%s: ops removed\n", ops->head.name);
	}
	return 0;
}

/*
 * Release a ops entry.  When the ref count reaches zero, recurse
 * through the stack.
 */
void
dev_ops_release(struct dev_ops *ops)
{
    --ops->head.refs;
    if (ops->head.refs == 0) {
	/* XXX */
    }
}

struct dev_ops *
dev_ops_intercept(dev_t dev, struct dev_ops *iops)
{
	struct dev_ops *oops = dev->si_ops;

	compile_dev_ops(iops);
	iops->head.maj = oops->head.maj;
	iops->head.data = oops->head.data;
	iops->head.flags = oops->head.flags;
	dev->si_ops = iops;

	return (oops);
}

void
dev_ops_restore(dev_t dev, struct dev_ops *oops)
{
	struct dev_ops *iops = dev->si_ops;

	dev->si_ops = oops;
	iops->head.maj = 0;
	iops->head.data = NULL;
	iops->head.flags = 0;
}

/************************************************************************
 *			DEFAULT DEV OPS FUNCTIONS			*
 ************************************************************************/


/*
 * Unsupported devswitch functions (e.g. for writing to read-only device).
 * XXX may belong elsewhere.
 */

int
noclone(struct dev_clone_args *ap)
{
	/* take no action */
	return (0);	/* allow the clone */
}

int
noopen(struct dev_open_args *ap)
{
	return (ENODEV);
}

int
noclose(struct dev_close_args *ap)
{
	return (ENODEV);
}

int
noread(struct dev_read_args *ap)
{
	return (ENODEV);
}

int
nowrite(struct dev_write_args *ap)
{
	return (ENODEV);
}

int
noioctl(struct dev_ioctl_args *ap)
{
	return (ENODEV);
}

int
nokqfilter(struct dev_kqfilter_args *ap)
{
	return (ENODEV);
}

int
nommap(struct dev_mmap_args *ap)
{
	return (ENODEV);
}

int
nopoll(struct dev_poll_args *ap)
{
	ap->a_events = 0;
	return(0);
}

int
nostrategy(struct dev_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;

	bio->bio_buf->b_flags |= B_ERROR;
	bio->bio_buf->b_error = EOPNOTSUPP;
	biodone(bio);
	return(0);
}

int
nopsize(struct dev_psize_args *ap)
{
	ap->a_result = 0;
	return(0);
}

int
nodump(struct dev_dump_args *ap)
{
	return (ENODEV);
}

/*
 * XXX this is probably bogus.  Any device that uses it isn't checking the
 * minor number.
 */
int
nullopen(struct dev_open_args *ap)
{
	return (0);
}

int
nullclose(struct dev_close_args *ap)
{
	return (0);
}

