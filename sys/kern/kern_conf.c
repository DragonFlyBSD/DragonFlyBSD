/*-
 * Parts Copyright (c) 1995 Terrence R. Lambert
 * Copyright (c) 1995 Julian R. Elischer
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
 *      This product includes software developed by Terrence R. Lambert.
 * 4. The name Terrence R. Lambert may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Julian R. Elischer ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE TERRENCE R. LAMBERT BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/kern_conf.c,v 1.73.2.3 2003/03/10 02:18:25 imp Exp $
 * $DragonFly: src/sys/kern/kern_conf.c,v 1.8 2004/05/19 22:52:58 dillon Exp $
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
#include <sys/device.h>
#include <machine/stdarg.h>

#define cdevsw_ALLOCSTART	(NUMCDEVSW/2)

MALLOC_DEFINE(M_DEVT, "dev_t", "dev_t storage");

/*
 * This is the number of hash-buckets.  Experiements with 'real-life'
 * udev_t's show that a prime halfway between two powers of two works
 * best.
 */
#define DEVT_HASH 83

/* The number of dev_t's we can create before malloc(9) kick in.  */
#define DEVT_STASH 50

static struct specinfo devt_stash[DEVT_STASH];
static LIST_HEAD(, specinfo) dev_hash[DEVT_HASH];
static LIST_HEAD(, specinfo) dev_free_list;

static int free_devt;
SYSCTL_INT(_debug, OID_AUTO, free_devt, CTLFLAG_RW, &free_devt, 0, "");
int dev_ref_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, dev_refs, CTLFLAG_RW, &dev_ref_debug, 0, "");

/*
 * dev_t and u_dev_t primitives.  Note that the major number is always
 * extracted from si_udev, not from si_devsw, because si_devsw is replaced
 * when a device is destroyed.
 */
int
major(dev_t x)
{
	if (x == NODEV)
		return NOUDEV;
	return((x->si_udev >> 8) & 0xff);
}

int
minor(dev_t x)
{
	if (x == NODEV)
		return NOUDEV;
	return(x->si_udev & 0xffff00ff);
}

int
lminor(dev_t x)
{
	int i;

	if (x == NODEV)
		return NOUDEV;
	i = minor(x);
	return ((i & 0xff) | (i >> 8));
}

/*
 * This is a bit complex because devices are always created relative to
 * a particular cdevsw, including 'hidden' cdevsw's (such as the raw device
 * backing a disk subsystem overlay), so we have to compare both the
 * devsw and udev fields to locate the correct device.
 *
 * The device is created if it does not already exist.  If SI_ADHOC is not
 * set the device will be referenced (once) and SI_ADHOC will be set.
 * The caller must explicitly add additional references to the device if
 * the caller wishes to track additional references.
 */
static
dev_t
hashdev(struct cdevsw *devsw, int x, int y)
{
	struct specinfo *si;
	udev_t	udev;
	int hash;
	static int stashed;

	udev = makeudev(x, y);
	hash = udev % DEVT_HASH;
	LIST_FOREACH(si, &dev_hash[hash], si_hash) {
		if (si->si_devsw == devsw && si->si_udev == udev)
			return (si);
	}
	if (stashed >= DEVT_STASH) {
		MALLOC(si, struct specinfo *, sizeof(*si), M_DEVT,
		    M_WAITOK|M_USE_RESERVE|M_ZERO);
	} else if (LIST_FIRST(&dev_free_list)) {
		si = LIST_FIRST(&dev_free_list);
		LIST_REMOVE(si, si_hash);
	} else {
		si = devt_stash + stashed++;
		si->si_flags |= SI_STASHED;
	}
	si->si_devsw = devsw;
	si->si_flags |= SI_HASHED | SI_ADHOC;
	si->si_udev = udev;
	si->si_refs = 1;
	LIST_INSERT_HEAD(&dev_hash[hash], si, si_hash);
	si->si_port = devsw->d_port;
	devsw->d_clone(si);
	if (devsw != &dead_cdevsw)
		++devsw->d_refs;
	if (dev_ref_debug) {
		printf("create    dev %p %s(minor=%08x) refs=%d\n", 
			si, devtoname(si), uminor(si->si_udev),
			si->si_refs);
	}
        return (si);
}

/*
 * Convert a device pointer to a device number
 */
udev_t
dev2udev(dev_t x)
{
	if (x == NODEV)
		return NOUDEV;
	return (x->si_udev);
}

/*
 * Convert a device number to a device pointer.  The device is referenced
 * ad-hoc, meaning that the caller should call reference_dev() if it wishes
 * to keep ahold of the returned structure long term.
 *
 * The returned device is associated with the currently installed cdevsw
 * for the requested major number.  NODEV is returned if the major number
 * has not been registered.
 */
dev_t
udev2dev(udev_t x, int b)
{
	dev_t dev;
	struct cdevsw *devsw;

	if (x == NOUDEV || b != 0)
		return(NODEV);
	devsw = cdevsw_get(umajor(x), uminor(x));
	if (devsw == NULL)
		return(NODEV);
	dev = hashdev(devsw, umajor(x), uminor(x));
	return(dev);
}

int
dev_is_good(dev_t dev)
{
	if (dev != NODEV && dev->si_devsw != &dead_cdevsw)
		return(1);
	return(0);
}

/*
 * Various user device number extraction and conversion routines
 */
int
uminor(udev_t dev)
{
	return(dev & 0xffff00ff);
}

int
umajor(udev_t dev)
{
	return((dev & 0xff00) >> 8);
}

udev_t
makeudev(int x, int y)
{
        return ((x << 8) | y);
}

/*
 * Create an internal or external device.
 *
 * Device majors can be overloaded and used directly by the kernel without
 * conflict, but userland will only see the particular device major that
 * has been installed with cdevsw_add().
 *
 * This routine creates an ad-hoc entry for the device.  The caller must
 * call reference_dev() to track additional references beyond the ad-hoc
 * entry.  If an entry already exists, this function will set (or override)
 * its cred requirements and name (XXX DEVFS interface).
 */
dev_t
make_dev(struct cdevsw *devsw, int minor, uid_t uid, gid_t gid, 
	int perms, const char *fmt, ...)
{
	dev_t	dev;
	__va_list ap;
	int i;

	/*
	 * compile the cdevsw and install the device
	 */
	compile_devsw(devsw);
	dev = hashdev(devsw, devsw->d_maj, minor);

	/*
	 * Set additional fields (XXX DEVFS interface goes here)
	 */
	__va_start(ap, fmt);
	i = kvprintf(fmt, NULL, dev->si_name, 32, ap);
	dev->si_name[i] = '\0';
	__va_end(ap);

	return (dev);
}

/*
 * This function is similar to make_dev() but no cred information or name
 * need be specified.
 */
dev_t
make_adhoc_dev(struct cdevsw *devsw, int minor)
{
	dev_t dev;

	dev = hashdev(devsw, devsw->d_maj, minor);
	return(dev);
}

/*
 * This function is similar to make_dev() except the new device is created
 * using an old device as a template.
 */
dev_t
make_sub_dev(dev_t odev, int minor)
{
	dev_t	dev;

	dev = hashdev(odev->si_devsw, umajor(odev->si_udev), minor);

	/*
	 * Copy cred requirements and name info XXX DEVFS.
	 */
	if (dev->si_name[0] == 0 && odev->si_name[0])
		bcopy(odev->si_name, dev->si_name, sizeof(dev->si_name));
	return (dev);
}

/*
 * destroy_dev() removes the adhoc association for a device and revectors
 * its devsw to &dead_cdevsw.
 *
 * This routine releases the reference count associated with the ADHOC
 * entry, plus releases the reference count held by the caller.  What this
 * means is that you should not call destroy_dev(make_dev(...)), because
 * make_dev() does not bump the reference count (beyond what it needs to
 * create the ad-hoc association).  Any procedure that intends to destroy
 * a device must have its own reference to it first.
 */
void
destroy_dev(dev_t dev)
{
	int hash;

	if (dev == NODEV)
		return;
	if ((dev->si_flags & SI_ADHOC) == 0) {
		release_dev(dev);
		return;
	}
	if (dev_ref_debug) {
		printf("destroy   dev %p %s(minor=%08x) refs=%d\n", 
			dev, devtoname(dev), uminor(dev->si_udev),
			dev->si_refs);
	}
	if (dev->si_refs < 2) {
		printf("destroy_dev(): too few references on device! "
			"%p %s(minor=%08x) refs=%d\n",
		    dev, devtoname(dev), uminor(dev->si_udev),
		    dev->si_refs);
	}
	dev->si_flags &= ~SI_ADHOC;
	if (dev->si_flags & SI_HASHED) {
		hash = dev->si_udev % DEVT_HASH;
		LIST_REMOVE(dev, si_hash);
		dev->si_flags &= ~SI_HASHED;
	}
	if (dead_cdevsw.d_port == NULL)
		compile_devsw(&dead_cdevsw);
	if (dev->si_devsw && dev->si_devsw != &dead_cdevsw)
		cdevsw_release(dev->si_devsw);
	dev->si_drv1 = 0;
	dev->si_drv2 = 0;
	dev->si_devsw = &dead_cdevsw;
	dev->si_port = dev->si_devsw->d_port;
	--dev->si_refs;		/* release adhoc association reference */
	release_dev(dev);	/* release callers reference */
}

/*
 * Destroy all ad-hoc device associations associated with a domain within a
 * device switch.
 */
void
destroy_all_dev(struct cdevsw *devsw, u_int mask, u_int match)
{
	int i;
	dev_t dev;
	dev_t ndev;

	for (i = 0; i < DEVT_HASH; ++i) {
		ndev = LIST_FIRST(&dev_hash[i]);
		while ((dev = ndev) != NULL) {
		    ndev = LIST_NEXT(dev, si_hash);
		    KKASSERT(dev->si_flags & SI_ADHOC);
		    if (dev->si_devsw == devsw && 
			(dev->si_udev & mask) == match
		    ) {
			++dev->si_refs;
			destroy_dev(dev);
		    }
		}
	}
}

/*
 * Add a reference to a device.  Callers generally add their own references
 * when they are going to store a device node in a variable for long periods
 * of time, to prevent a disassociation from free()ing the node.
 *
 * Also note that a caller that intends to call destroy_dev() must first
 * obtain a reference on the device.  The ad-hoc reference you get with
 * make_dev() and friends is NOT sufficient to be able to call destroy_dev().
 */
dev_t
reference_dev(dev_t dev)
{
	if (dev != NODEV) {
		++dev->si_refs;
		if (dev_ref_debug) {
			printf("reference dev %p %s(minor=%08x) refs=%d\n", 
			    dev, devtoname(dev), uminor(dev->si_udev),
			    dev->si_refs);
		}
	}
	return(dev);
}

/*
 * release a reference on a device.  The device will be freed when the last
 * reference has been released.
 *
 * NOTE: we must use si_udev to figure out the original (major, minor),
 * because si_devsw could already be pointing at dead_cdevsw.
 */
void
release_dev(dev_t dev)
{
	if (dev == NODEV)
		return;
	if (free_devt) {
		KKASSERT(dev->si_refs > 0);
	} else {
		if (dev->si_refs <= 0) {
			printf("Warning: extra release of dev %p(%s)\n",
			    dev, devtoname(dev));
			free_devt = 0;	/* prevent bad things from occuring */
		}
	}
	--dev->si_refs;
	if (dev_ref_debug) {
		printf("release   dev %p %s(minor=%08x) refs=%d\n", 
			dev, devtoname(dev), uminor(dev->si_udev),
			dev->si_refs);
	}
	if (dev->si_refs == 0) {
		if (dev->si_flags & SI_ADHOC) {
			printf("Warning: illegal final release on ADHOC"
				" device %p(%s), the device was never"
				" destroyed!\n",
				dev, devtoname(dev));
		}
		if (dev->si_flags & SI_HASHED) {
			printf("Warning: last release on device, no call"
				" to destroy_dev() was made! dev %p(%s)\n",
				dev, devtoname(dev));
			dev->si_refs = 3;
			destroy_dev(dev);
			dev->si_refs = 0;
		}
		if (SLIST_FIRST(&dev->si_hlist) != NULL) {
			printf("Warning: last release on device, vnode"
				" associations still exist! dev %p(%s)\n",
				dev, devtoname(dev));
			free_devt = 0;	/* prevent bad things from occuring */
		}
		if (dev->si_devsw && dev->si_devsw != &dead_cdevsw) {
			cdevsw_release(dev->si_devsw);
			dev->si_devsw = NULL;
		}
		if (free_devt) {
			if (dev->si_flags & SI_STASHED) {
				bzero(dev, sizeof(*dev));
				LIST_INSERT_HEAD(&dev_free_list, dev, si_hash);
			} else {
				FREE(dev, M_DEVT);
			}
		}
	}
}

const char *
devtoname(dev_t dev)
{
	int mynor;
	int len;
	char *p;
	const char *dname;

	if (dev == NODEV)
		return("#nodev");
	if (dev->si_name[0] == '#' || dev->si_name[0] == '\0') {
		p = dev->si_name;
		len = sizeof(dev->si_name);
		if ((dname = dev_dname(dev)) != NULL)
			snprintf(p, len, "#%s/", dname);
		else
			snprintf(p, len, "#%d/", major(dev));
		len -= strlen(p);
		p += strlen(p);
		mynor = minor(dev);
		if (mynor < 0 || mynor > 255)
			snprintf(p, len, "%#x", (u_int)mynor);
		else
			snprintf(p, len, "%d", mynor);
	}
	return (dev->si_name);
}

