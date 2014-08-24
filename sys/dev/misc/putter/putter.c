/*	$NetBSD: putter.c,v 1.31 2011/02/06 14:29:25 haad Exp $	*/

/*
 * Copyright (c) 2006, 2007  Antti Kantee.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Ulla Tuominen Foundation and the Finnish Cultural Foundation and the
 * Research Foundation of Helsinki University of Technology
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Pass-to-Userspace TransporTER: generic kernel-user request-response
 * transport interface.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/devfs.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include <dev/misc/putter/putter_sys.h>

static MALLOC_DEFINE(M_PUTTER, "putter", "Putter device data");

/*
 * Device routines.  These are for when /dev/putter is initially
 * opened before it has been cloned.
 */

static d_open_t puttercdopen;
static d_read_t putter_fop_read;
static d_write_t putter_fop_write;
static d_ioctl_t putter_fop_ioctl;
static d_close_t putter_fop_close;
static d_kqfilter_t putter_fop_kqfilter;

DEVFS_DECLARE_CLONE_BITMAP(putter);

/* dev */
static struct dev_ops putter_ops = {
	{ "putter", 0, 0 },
	.d_open =	puttercdopen,
	.d_close =	putter_fop_close,
	.d_read =	putter_fop_read,
	.d_write =	putter_fop_write,
	.d_ioctl =	putter_fop_ioctl,
	.d_kqfilter =	putter_fop_kqfilter,
};

#if 0
/*
 * Configuration data.
 *
 * This is static-size for now.  Will be redone for devfs.
 */

#define PUTTER_CONFSIZE 16

static struct putter_config {
	int	pc_minor;
	int	(*pc_config)(int, int, int);
} putterconf[PUTTER_CONFSIZE];

static int
putter_configure(dev_t dev, int flags, int fmt, int fd)
{
	struct putter_config *pc;

	/* are we the catch-all node? */
	if (minor(dev) == PUTTER_MINOR_WILDCARD
	    || minor(dev) == PUTTER_MINOR_COMPAT)
		return 0;

	/* nopes?  try to configure us */
	for (pc = putterconf; pc->pc_config; pc++)
		if (minor(dev) == pc->pc_minor)
			return pc->pc_config(fd, flags, fmt);
	return ENXIO;
}

int
putter_register(putter_config_fn pcfn, int minor)
{
	int i;

	for (i = 0; i < PUTTER_CONFSIZE; i++)
		if (putterconf[i].pc_config == NULL)
			break;
	if (i == PUTTER_CONFSIZE)
		return EBUSY;

	putterconf[i].pc_minor = minor;
	putterconf[i].pc_config = pcfn;
	return 0;
}
#endif

/*
 * putter instance structures.  these are always allocated and freed
 * from the context of the transport user.
 */
struct putter_instance {
	pid_t			pi_pid;
	int			pi_idx;
	struct kqinfo		pi_kq;

	void			*pi_private;
	struct putter_ops	*pi_pop;

	uint8_t			*pi_curput;
	size_t			pi_curres;
	void			*pi_curopaq;

	TAILQ_ENTRY(putter_instance) pi_entries;
};
#define PUTTER_EMBRYO ((void *)-1)	/* before attach	*/
#define PUTTER_DEAD ((void *)-2)	/* after detach		*/

static TAILQ_HEAD(, putter_instance) putter_ilist
    = TAILQ_HEAD_INITIALIZER(putter_ilist);

#ifdef DEBUG
#ifndef PUTTERDEBUG
#define PUTTERDEBUG
#endif
#endif

#ifdef PUTTERDEBUG
int putterdebug = 0;
#define DPRINTF(x) if (putterdebug > 0) kprintf x
#define DPRINTF_VERBOSE(x) if (putterdebug > 1) kprintf x
#else
#define DPRINTF(x)
#define DPRINTF_VERBOSE(x)
#endif

/*
 * public init / deinit
 */

/* protects both the list and the contents of the list elements */
static struct spinlock pi_mtx = SPINLOCK_INITIALIZER(&pi_mtx, "pi_mtx");

/*
 * fd routines, for cloner
 */

static int
putter_fop_read(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct putter_instance *pi = dev->si_drv1;
	struct uio *uio = ap->a_uio;
	size_t origres, moved;
	int error;

	if (pi->pi_private == PUTTER_EMBRYO || pi->pi_private == PUTTER_DEAD) {
		kprintf("putter_fop_read: private %d not inited\n", pi->pi_idx);
		return ENOENT;
	}

	if (pi->pi_curput == NULL) {
		error = pi->pi_pop->pop_getout(pi->pi_private, uio->uio_resid,
		    ap->a_ioflag & IO_NDELAY, &pi->pi_curput,
		    &pi->pi_curres, &pi->pi_curopaq);
		if (error) {
			return error;
		}
	}

	origres = uio->uio_resid;
	error = uiomove(pi->pi_curput, pi->pi_curres, uio);
	moved = origres - uio->uio_resid;
	DPRINTF(("putter_fop_read (%p): moved %zu bytes from %p, error %d\n",
	    pi, moved, pi->pi_curput, error));

	KKASSERT(pi->pi_curres >= moved);
	pi->pi_curres -= moved;
	pi->pi_curput += moved;

	if (pi->pi_curres == 0) {
		pi->pi_pop->pop_releaseout(pi->pi_private,
		    pi->pi_curopaq, error);
		pi->pi_curput = NULL;
	}

	return error;
}

static int
putter_fop_write(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct putter_instance *pi = dev->si_drv1;
	struct uio *uio = ap->a_uio;
	struct putter_hdr pth;
	uint8_t *buf;
	size_t frsize;
	int error;

	DPRINTF(("putter_fop_write (%p): writing response, resid %zu\n",
	    pi->pi_private, uio->uio_resid));

	if (pi->pi_private == PUTTER_EMBRYO || pi->pi_private == PUTTER_DEAD) {
		kprintf("putter_fop_write: putter %d not inited\n", pi->pi_idx);
		return ENOENT;
	}

	error = uiomove((char *)&pth, sizeof(struct putter_hdr), uio);
	if (error) {
		return error;
	}

	/* Sorry mate, the kernel doesn't buffer. */
	frsize = pth.pth_framelen - sizeof(struct putter_hdr);
	if (uio->uio_resid < frsize) {
		return EINVAL;
	}

	buf = kmalloc(frsize + sizeof(struct putter_hdr), M_PUTTER, M_WAITOK);
	memcpy(buf, &pth, sizeof(pth));
	error = uiomove(buf+sizeof(struct putter_hdr), frsize, uio);
	if (error == 0) {
		pi->pi_pop->pop_dispatch(pi->pi_private,
		    (struct putter_hdr *)buf);
	}
	kfree(buf, M_PUTTER);

	return error;
}

/*
 * device close = forced unmount.
 *
 * unmounting is a frightfully complex operation to avoid races
 */
static int
putter_fop_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct putter_instance *pi = dev->si_drv1;
	int rv;

	DPRINTF(("putter_fop_close: device closed\n"));

 restart:
	spin_lock(&pi_mtx);
	/*
	 * First check if the driver was never born.  In that case
	 * remove the instance from the list.  If mount is attempted later,
	 * it will simply fail.
	 */
	if (pi->pi_private == PUTTER_EMBRYO) {
		TAILQ_REMOVE(&putter_ilist, pi, pi_entries);
		spin_unlock(&pi_mtx);

		DPRINTF(("putter_fop_close: data associated with dev %i was "
		    "embryonic\n", dev->si_uminor));

		goto out;
	}

	/*
	 * Next, analyze if unmount was called and the instance is dead.
	 * In this case we can just free the structure and go home, it
	 * was removed from the list by putter_rmprivate().
	 */
	if (pi->pi_private == PUTTER_DEAD) {
		spin_unlock(&pi_mtx);

		DPRINTF(("putter_fop_close: putter associated with dev %d "
		    "dead, freeing\n", pi->pi_idx));

		goto out;
	}

	/*
	 * So we have a reference.  Proceed to unravel the
	 * underlying driver.
	 */
	spin_unlock(&pi_mtx);

	/* hmm?  suspicious locking? */
	while ((rv = pi->pi_pop->pop_close(pi->pi_private)) == ERESTART)
		goto restart;

 out:
	/*
	 * Finally, release the instance information.  It was already
	 * removed from the list by putter_rmprivate() and we know it's
	 * dead, so no need to lock.
	 */
	kfree(pi, M_PUTTER);

	return 0;
}

static int
putter_fop_ioctl(struct dev_ioctl_args *ap)
{

	/*
	 * work already done in sys_ioctl().  skip sanity checks to enable
	 * setting non-blocking fd on an embryotic driver.
	 */
	if (ap->a_cmd == FIONBIO)
		return 0;

	return EINVAL;
}

/* kqueue stuff */

static void
filt_putterdetach(struct knote *kn)
{
	struct putter_instance *pi = (void *)kn->kn_hook;
	struct klist *klist;

	klist = &pi->pi_kq.ki_note;
	knote_remove(klist, kn);
}

static int
filt_putter_rd(struct knote *kn, long hint)
{
	struct putter_instance *pi = (void *)kn->kn_hook;
	int error, rv;

	error = 0;
	spin_lock(&pi_mtx);
	if (pi->pi_private == PUTTER_EMBRYO || pi->pi_private == PUTTER_DEAD)
		error = 1;
	spin_unlock(&pi_mtx);
	if (error) {
		return 0;
	}

	kn->kn_data = pi->pi_pop->pop_waitcount(pi->pi_private);
	rv = kn->kn_data != 0;
	return rv;
}

static int
filt_putter_wr(struct knote *kn, long hint)
{
	/* Writing is always OK */
	kn->kn_data = 0;
	return 1;
}

static struct filterops putter_filtops_rd =
        { FILTEROP_ISFD, NULL, filt_putterdetach, filt_putter_rd };
static struct filterops putter_filtops_wr =
        { FILTEROP_ISFD, NULL, filt_putterdetach, filt_putter_wr };

static int
putter_fop_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct putter_instance *pi = dev->si_drv1;
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &putter_filtops_rd;
		kn->kn_hook = (char *)pi;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &putter_filtops_wr;
		kn->kn_hook = (char *)pi;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return 0;
	}

	klist = &pi->pi_kq.ki_note;
	knote_insert(klist, kn);

	return 0;
}

int
puttercdopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct putter_instance *pi;

	pi = kmalloc(sizeof(struct putter_instance), M_PUTTER,
	    M_WAITOK | M_ZERO);
	dev->si_drv1 = pi;

	pi->pi_pid = curproc->p_pid;
	pi->pi_idx = dev->si_uminor;
	pi->pi_curput = NULL;
	pi->pi_curres = 0;
	pi->pi_curopaq = NULL;
	pi->pi_private = PUTTER_EMBRYO;

	spin_lock(&pi_mtx);
	TAILQ_INSERT_TAIL(&putter_ilist, pi, pi_entries);
	spin_unlock(&pi_mtx);

	DPRINTF(("puttercdopen: registered embryonic pmp for pid: %d\n",
	    pi->pi_pid));
	return 0;
}


/*
 * Set the private structure for the file descriptor.  This is
 * typically done immediately when the counterpart has knowledge
 * about the private structure's address and the file descriptor
 * (e.g. vfs mount routine).
 *
 * We only want to make sure that the caller had the right to open the
 * device, we don't so much care about which context it gets in case
 * the same process opened multiple (since they are equal at this point).
 */
struct putter_instance *
putter_attach(pid_t pid, int minor, void *ppriv, struct putter_ops *pop)
{
	struct putter_instance *pi = NULL;

	spin_lock(&pi_mtx);
	TAILQ_FOREACH(pi, &putter_ilist, pi_entries) {
		if (pi->pi_pid == pid && pi->pi_idx == minor &&
		    pi->pi_private == PUTTER_EMBRYO) {
			pi->pi_private = ppriv;
			pi->pi_pop = pop;
			break;
		    }
	}
	spin_unlock(&pi_mtx);

	DPRINTF(("putter_setprivate: pi at %p (%d/%d)\n", pi,
	    pi ? pi->pi_pid : 0, pi ? pi->pi_idx : 0));

	return pi;
}

/*
 * Remove fp <-> private mapping.
 */
void
putter_detach(struct putter_instance *pi)
{

	spin_lock(&pi_mtx);
	TAILQ_REMOVE(&putter_ilist, pi, pi_entries);
	pi->pi_private = PUTTER_DEAD;
	spin_unlock(&pi_mtx);

	DPRINTF(("putter_nukebypmp: nuked %p\n", pi));
}

void
putter_notify(struct putter_instance *pi)
{

	KNOTE(&pi->pi_kq.ki_note, 0);
}

static int
putter_clone(struct dev_clone_args *ap)
{
	int minor;

	minor = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(putter), 0);
	ap->a_dev = make_only_dev(&putter_ops, minor, UID_ROOT, GID_WHEEL, 0600,
	    "putter%d", minor);
	return 0;
}

static int
putter_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		make_autoclone_dev(&putter_ops, &DEVFS_CLONE_BITMAP(putter),
			putter_clone, UID_ROOT, GID_WHEEL, 0600, "putter");
		break;
	case MOD_UNLOAD:
		devfs_clone_handler_del("putter");
		dev_ops_remove_all(&putter_ops);
		devfs_clone_bitmap_uninit(&DEVFS_CLONE_BITMAP(putter));
		break;
	default:
		break;
	}
	return (0);
}

static moduledata_t putter_mod = {
        "putter",
        putter_modevent,
        NULL
};
DECLARE_MODULE(putter, putter_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(putter, 1);
