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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/tree.h>
#include <sys/syslink_rpc.h>
#include <sys/proc.h>
#include <machine/stdarg.h>
#include <sys/devfs.h>
#include <sys/dsched.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

static int mpsafe_writes;
static int mplock_writes;
static int mpsafe_reads;
static int mplock_reads;
static int mpsafe_strategies;
static int mplock_strategies;

SYSCTL_INT(_kern, OID_AUTO, mpsafe_writes, CTLFLAG_RD, &mpsafe_writes,
	   0, "mpsafe writes");
SYSCTL_INT(_kern, OID_AUTO, mplock_writes, CTLFLAG_RD, &mplock_writes,
	   0, "non-mpsafe writes");
SYSCTL_INT(_kern, OID_AUTO, mpsafe_reads, CTLFLAG_RD, &mpsafe_reads,
	   0, "mpsafe reads");
SYSCTL_INT(_kern, OID_AUTO, mplock_reads, CTLFLAG_RD, &mplock_reads,
	   0, "non-mpsafe reads");
SYSCTL_INT(_kern, OID_AUTO, mpsafe_strategies, CTLFLAG_RD, &mpsafe_strategies,
	   0, "mpsafe strategies");
SYSCTL_INT(_kern, OID_AUTO, mplock_strategies, CTLFLAG_RD, &mplock_strategies,
	   0, "non-mpsafe strategies");

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
DEVOP_DESC_INIT(mmap);
DEVOP_DESC_INIT(mmap_single);
DEVOP_DESC_INIT(strategy);
DEVOP_DESC_INIT(kqfilter);
DEVOP_DESC_INIT(revoke);
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
	.d_mmap = nommap,
	.d_mmap_single = nommap_single,
	.d_strategy = nostrategy,
	.d_dump = nodump,
	.d_psize = nopsize,
	.d_kqfilter = nokqfilter,
	.d_revoke = norevoke,
	.d_clone = noclone
};

static __inline
int
dev_needmplock(cdev_t dev)
{
    return((dev->si_ops->head.flags & D_MPSAFE) == 0);
}
    
/************************************************************************
 *			GENERAL DEVICE API FUNCTIONS			*
 ************************************************************************
 *
 * The MPSAFEness of these depends on dev->si_ops->head.flags
 */
int
dev_dopen(cdev_t dev, int oflags, int devtype, struct ucred *cred)
{
	struct dev_open_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_open_desc;
	ap.a_head.a_dev = dev;
	ap.a_oflags = oflags;
	ap.a_devtype = devtype;
	ap.a_cred = cred;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_open(&ap);
	if (needmplock)
		rel_mplock();
	return (error);
}

int
dev_dclose(cdev_t dev, int fflag, int devtype)
{
	struct dev_close_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_close_desc;
	ap.a_head.a_dev = dev;
	ap.a_fflag = fflag;
	ap.a_devtype = devtype;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_close(&ap);
	if (needmplock)
		rel_mplock();
	return (error);
}

int
dev_dread(cdev_t dev, struct uio *uio, int ioflag)
{
	struct dev_read_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_read_desc;
	ap.a_head.a_dev = dev;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;

	if (needmplock) {
		get_mplock();
		++mplock_reads;
	} else {
		++mpsafe_reads;
	}
	error = dev->si_ops->d_read(&ap);
	if (needmplock)
		rel_mplock();
	if (error == 0)
		dev->si_lastread = time_second;
	return (error);
}

int
dev_dwrite(cdev_t dev, struct uio *uio, int ioflag)
{
	struct dev_write_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	dev->si_lastwrite = time_second;
	ap.a_head.a_desc = &dev_write_desc;
	ap.a_head.a_dev = dev;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;

	if (needmplock) {
		get_mplock();
		++mplock_writes;
	} else {
		++mpsafe_writes;
	}
	error = dev->si_ops->d_write(&ap);
	if (needmplock)
		rel_mplock();
	return (error);
}

int
dev_dioctl(cdev_t dev, u_long cmd, caddr_t data, int fflag, struct ucred *cred,
	   struct sysmsg *msg)
{
	struct dev_ioctl_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_ioctl_desc;
	ap.a_head.a_dev = dev;
	ap.a_cmd = cmd;
	ap.a_data = data;
	ap.a_fflag = fflag;
	ap.a_cred = cred;
	ap.a_sysmsg = msg;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_ioctl(&ap);
	if (needmplock)
		rel_mplock();
	return (error);
}

int
dev_dmmap(cdev_t dev, vm_offset_t offset, int nprot)
{
	struct dev_mmap_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_mmap_desc;
	ap.a_head.a_dev = dev;
	ap.a_offset = offset;
	ap.a_nprot = nprot;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_mmap(&ap);
	if (needmplock)
		rel_mplock();

	if (error == 0)
		return(ap.a_result);
	return(-1);
}

int
dev_dmmap_single(cdev_t dev, vm_ooffset_t *offset, vm_size_t size,
                 struct vm_object **object, int nprot)
{
	struct dev_mmap_single_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_mmap_single_desc;
	ap.a_head.a_dev = dev;
	ap.a_offset = offset;
	ap.a_size = size;
	ap.a_object = object;
	ap.a_nprot = nprot;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_mmap_single(&ap);
	if (needmplock)
		rel_mplock();

	return(error);
}

int
dev_dclone(cdev_t dev)
{
	struct dev_clone_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_clone_desc;
	ap.a_head.a_dev = dev;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_clone(&ap);
	if (needmplock)
		rel_mplock();
	return (error);
}

int
dev_drevoke(cdev_t dev)
{
	struct dev_revoke_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_revoke_desc;
	ap.a_head.a_dev = dev;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_revoke(&ap);
	if (needmplock)
		rel_mplock();

	return (error);
}

/*
 * Core device strategy call, used to issue I/O on a device.  There are
 * two versions, a non-chained version and a chained version.  The chained
 * version reuses a BIO set up by vn_strategy().  The only difference is
 * that, for now, we do not push a new tracking structure when chaining
 * from vn_strategy.  XXX this will ultimately have to change.
 */
void
dev_dstrategy(cdev_t dev, struct bio *bio)
{
	struct dev_strategy_args ap;
	struct bio_track *track;
	int needmplock = dev_needmplock(dev);

	ap.a_head.a_desc = &dev_strategy_desc;
	ap.a_head.a_dev = dev;
	ap.a_bio = bio;

	KKASSERT(bio->bio_track == NULL);
	KKASSERT(bio->bio_buf->b_cmd != BUF_CMD_DONE);
	if (bio->bio_buf->b_cmd == BUF_CMD_READ)
	    track = &dev->si_track_read;
	else
	    track = &dev->si_track_write;
	bio_track_ref(track);
	bio->bio_track = track;

	if (dsched_is_clear_buf_priv(bio->bio_buf))
		dsched_new_buf(bio->bio_buf);

	KKASSERT((bio->bio_flags & BIO_DONE) == 0);
	if (needmplock) {
		get_mplock();
		++mplock_strategies;
	} else {
		++mpsafe_strategies;
	}
	(void)dev->si_ops->d_strategy(&ap);
	if (needmplock)
		rel_mplock();
}

void
dev_dstrategy_chain(cdev_t dev, struct bio *bio)
{
	struct dev_strategy_args ap;
	int needmplock = dev_needmplock(dev);

	ap.a_head.a_desc = &dev_strategy_desc;
	ap.a_head.a_dev = dev;
	ap.a_bio = bio;

	KKASSERT(bio->bio_track != NULL);
	KKASSERT((bio->bio_flags & BIO_DONE) == 0);
	if (needmplock)
		get_mplock();
	(void)dev->si_ops->d_strategy(&ap);
	if (needmplock)
		rel_mplock();
}

/*
 * note: the disk layer is expected to set count, blkno, and secsize before
 * forwarding the message.
 */
int
dev_ddump(cdev_t dev, void *virtual, vm_offset_t physical, off_t offset,
    size_t length)
{
	struct dev_dump_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_dump_desc;
	ap.a_head.a_dev = dev;
	ap.a_count = 0;
	ap.a_blkno = 0;
	ap.a_secsize = 0;
	ap.a_virtual = virtual;
	ap.a_physical = physical;
	ap.a_offset = offset;
	ap.a_length = length;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_dump(&ap);
	if (needmplock)
		rel_mplock();
	return (error);
}

int64_t
dev_dpsize(cdev_t dev)
{
	struct dev_psize_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_psize_desc;
	ap.a_head.a_dev = dev;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_psize(&ap);
	if (needmplock)
		rel_mplock();

	if (error == 0)
		return (ap.a_result);
	return(-1);
}

/*
 * Pass-thru to the device kqfilter.
 *
 * NOTE: We explicitly preset a_result to 0 so d_kqfilter() functions
 *	 which return 0 do not have to bother setting a_result.
 */
int
dev_dkqfilter(cdev_t dev, struct knote *kn)
{
	struct dev_kqfilter_args ap;
	int needmplock = dev_needmplock(dev);
	int error;

	ap.a_head.a_desc = &dev_kqfilter_desc;
	ap.a_head.a_dev = dev;
	ap.a_kn = kn;
	ap.a_result = 0;

	if (needmplock)
		get_mplock();
	error = dev->si_ops->d_kqfilter(&ap);
	if (needmplock)
		rel_mplock();

	if (error == 0) 
		return(ap.a_result);
	return(ENODEV);
}

/************************************************************************
 *			DEVICE HELPER FUNCTIONS				*
 ************************************************************************/

/*
 * MPSAFE
 */
int
dev_drefs(cdev_t dev)
{
    return(dev->si_sysref.refcnt);
}

/*
 * MPSAFE
 */
const char *
dev_dname(cdev_t dev)
{
    return(dev->si_ops->head.name);
}

/*
 * MPSAFE
 */
int
dev_dflags(cdev_t dev)
{
    return(dev->si_ops->head.flags);
}

/*
 * MPSAFE
 */
int
dev_dmaj(cdev_t dev)
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
    int needmplock = dev_needmplock(ap->a_dev);
    int error;

    func = *(void **)((char *)ap->a_dev->si_ops + ap->a_desc->sd_offset);

    if (needmplock)
	    get_mplock();
    error = func(ap);
    if (needmplock)
	    rel_mplock();

    return (error);
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
    int needmplock = ((ops->head.flags & D_MPSAFE) == 0);
    int error;

    func = *(void **)((char *)ops + ap->a_desc->sd_offset);

    if (needmplock)
	    get_mplock();
    error = func(ap);
    if (needmplock)
	    rel_mplock();

    return (error);
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

static
int
rb_dev_ops_compare(struct dev_ops_maj *a, struct dev_ops_maj *b)
{
    if (a->maj < b->maj)
	return(-1);
    else if (a->maj > b->maj)
	return(1);
    return(0);
}

RB_GENERATE2(dev_ops_rb_tree, dev_ops_maj, rbnode, rb_dev_ops_compare, int, maj);

struct dev_ops_rb_tree dev_ops_rbhead = RB_INITIALIZER(dev_ops_rbhead);

int
dev_ops_remove_all(struct dev_ops *ops)
{
	return devfs_destroy_dev_by_ops(ops, -1);
}

int
dev_ops_remove_minor(struct dev_ops *ops, int minor)
{
	return devfs_destroy_dev_by_ops(ops, minor);
}

struct dev_ops *
dev_ops_intercept(cdev_t dev, struct dev_ops *iops)
{
	struct dev_ops *oops = dev->si_ops;

	compile_dev_ops(iops);
	iops->head.maj = oops->head.maj;
	iops->head.data = oops->head.data;
	iops->head.flags = oops->head.flags;
	dev->si_ops = iops;
	dev->si_flags |= SI_INTERCEPTED;

	return (oops);
}

void
dev_ops_restore(cdev_t dev, struct dev_ops *oops)
{
	struct dev_ops *iops = dev->si_ops;

	dev->si_ops = oops;
	dev->si_flags &= ~SI_INTERCEPTED;
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
norevoke(struct dev_revoke_args *ap)
{
	/* take no action */
	return(0);
}

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
nommap_single(struct dev_mmap_single_args *ap)
{
	return (ENODEV);
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

