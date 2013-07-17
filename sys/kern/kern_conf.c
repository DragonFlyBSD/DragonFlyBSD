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
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <machine/stdarg.h>

#include <sys/sysref2.h>

#include <sys/devfs.h>

int dev_ref_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, dev_refs, CTLFLAG_RW, &dev_ref_debug, 0,
    "Toggle device reference debug output");

/*
 * cdev_t and u_dev_t primitives.  Note that the major number is always
 * extracted from si_umajor, not from si_devsw, because si_devsw is replaced
 * when a device is destroyed.
 */
int
major(cdev_t dev)
{
	if (dev == NULL)
		return NOUDEV;
	return(dev->si_umajor);
}

int
minor(cdev_t dev)
{
	if (dev == NULL)
		return NOUDEV;
	return(dev->si_uminor);
}

/*
 * Compatibility function with old udev_t format to convert the
 * non-consecutive minor space into a consecutive minor space.
 */
int
lminor(cdev_t dev)
{
	int y;

	if (dev == NULL)
		return NOUDEV;
	y = dev->si_uminor;
	if (y & 0x0000ff00)
		return NOUDEV;
	return ((y & 0xff) | (y >> 8));
}

/*
 * Convert a device pointer to an old style device number.  Return NOUDEV
 * if the device is invalid or if the device (maj,min) cannot be converted
 * to an old style udev_t.
 */
udev_t
dev2udev(cdev_t dev)
{
	if (dev == NULL)
		return NOUDEV;

	return (udev_t)dev->si_inode;
}

/*
 * Convert a device number to a device pointer.  The device is referenced
 * ad-hoc, meaning that the caller should call reference_dev() if it wishes
 * to keep ahold of the returned structure long term.
 *
 * The returned device is associated with the currently installed cdevsw
 * for the requested major number.  NULL is returned if the major number
 * has not been registered.
 */
cdev_t
udev2dev(udev_t x, int b)
{
	if (x == NOUDEV || b != 0)
		return(NULL);

	return devfs_find_device_by_udev(x);
}

int
dev_is_good(cdev_t dev)
{
	if (dev != NULL && dev->si_ops != &dead_dev_ops)
		return(1);
	return(0);
}

/*
 * Various user device number extraction and conversion routines
 */
int
uminor(udev_t dev)
{
	if (dev == NOUDEV)
		return(-1);
	return(dev & 0xffff00ff);
}

int
umajor(udev_t dev)
{
	if (dev == NOUDEV)
		return(-1);
	return((dev & 0xff00) >> 8);
}

udev_t
makeudev(int x, int y)
{
	if ((x & 0xffffff00) || (y & 0x0000ff00))
		return NOUDEV;
	return ((x << 8) | y);
}

/*
 * Create an internal or external device.
 *
 * This routine creates and returns an unreferenced ad-hoc entry for the
 * device which will remain intact until the device is destroyed.  If the
 * caller intends to store the device pointer it must call reference_dev()
 * to retain a real reference to the device.
 *
 * If an entry already exists, this function will set (or override)
 * its cred requirements and name (XXX DEVFS interface).
 */
cdev_t
make_dev(struct dev_ops *ops, int minor, uid_t uid, gid_t gid,
	int perms, const char *fmt, ...)
{
	cdev_t	devfs_dev;
	__va_list ap;

	/*
	 * compile the cdevsw and install the device
	 */
	compile_dev_ops(ops);

	devfs_dev = devfs_new_cdev(ops, minor, NULL);
	__va_start(ap, fmt);
	kvsnrprintf(devfs_dev->si_name, sizeof(devfs_dev->si_name),
		    32, fmt, ap);
	__va_end(ap);

	devfs_debug(DEVFS_DEBUG_INFO,
		    "make_dev called for %s\n",
		    devfs_dev->si_name);
	devfs_create_dev(devfs_dev, uid, gid, perms);

	return (devfs_dev);
}

/*
 * make_dev_covering has equivalent functionality to make_dev, except that it
 * also takes the cdev of the underlying device. Hence this function should
 * only be used by systems and drivers which create devices covering others
 */
cdev_t
make_dev_covering(struct dev_ops *ops, struct dev_ops *bops, int minor,
	    uid_t uid, gid_t gid, int perms, const char *fmt, ...)
{
	cdev_t	devfs_dev;
	__va_list ap;

	/*
	 * compile the cdevsw and install the device
	 */
	compile_dev_ops(ops);

	devfs_dev = devfs_new_cdev(ops, minor, bops);
	__va_start(ap, fmt);
	kvsnrprintf(devfs_dev->si_name, sizeof(devfs_dev->si_name),
		    32, fmt, ap);
	__va_end(ap);

	devfs_debug(DEVFS_DEBUG_INFO,
		    "make_dev called for %s\n",
		    devfs_dev->si_name);
	devfs_create_dev(devfs_dev, uid, gid, perms);

	return (devfs_dev);
}



cdev_t
make_only_devfs_dev(struct dev_ops *ops, int minor, uid_t uid, gid_t gid,
	int perms, const char *fmt, ...)
{
	cdev_t	devfs_dev;
	__va_list ap;

	/*
	 * compile the cdevsw and install the device
	 */
	compile_dev_ops(ops);
	devfs_dev = devfs_new_cdev(ops, minor, NULL);

	/*
	 * Set additional fields (XXX DEVFS interface goes here)
	 */
	__va_start(ap, fmt);
	kvsnrprintf(devfs_dev->si_name, sizeof(devfs_dev->si_name),
		    32, fmt, ap);
	__va_end(ap);

	devfs_create_dev(devfs_dev, uid, gid, perms);

	return (devfs_dev);
}

cdev_t
make_only_dev(struct dev_ops *ops, int minor, uid_t uid, gid_t gid,
	int perms, const char *fmt, ...)
{
	cdev_t	devfs_dev;
	__va_list ap;

	/*
	 * compile the cdevsw and install the device
	 */
	compile_dev_ops(ops);
	devfs_dev = devfs_new_cdev(ops, minor, NULL);
	devfs_dev->si_perms = perms;
	devfs_dev->si_uid = uid;
	devfs_dev->si_gid = gid;

	/*
	 * Set additional fields (XXX DEVFS interface goes here)
	 */
	__va_start(ap, fmt);
	kvsnrprintf(devfs_dev->si_name, sizeof(devfs_dev->si_name),
		    32, fmt, ap);
	__va_end(ap);

	reference_dev(devfs_dev);

	return (devfs_dev);
}

cdev_t
make_only_dev_covering(struct dev_ops *ops, struct dev_ops *bops, int minor,
	uid_t uid, gid_t gid, int perms, const char *fmt, ...)
{
	cdev_t	devfs_dev;
	__va_list ap;

	/*
	 * compile the cdevsw and install the device
	 */
	compile_dev_ops(ops);
	devfs_dev = devfs_new_cdev(ops, minor, bops);
	devfs_dev->si_perms = perms;
	devfs_dev->si_uid = uid;
	devfs_dev->si_gid = gid;

	/*
	 * Set additional fields (XXX DEVFS interface goes here)
	 */
	__va_start(ap, fmt);
	kvsnrprintf(devfs_dev->si_name, sizeof(devfs_dev->si_name),
		    32, fmt, ap);
	__va_end(ap);

	reference_dev(devfs_dev);

	return (devfs_dev);
}

void
destroy_only_dev(cdev_t dev)
{
	release_dev(dev);
	release_dev(dev);
	release_dev(dev);
}

/*
 * destroy_dev() removes the adhoc association for a device and revectors
 * its ops to &dead_dev_ops.
 *
 * This routine releases the reference count associated with the ADHOC
 * entry, plus releases the reference count held by the caller.  What this
 * means is that you should not call destroy_dev(make_dev(...)), because
 * make_dev() does not bump the reference count (beyond what it needs to
 * create the ad-hoc association).  Any procedure that intends to destroy
 * a device must have its own reference to it first.
 */
void
destroy_dev(cdev_t dev)
{
	if (dev) {
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "destroy_dev called for %s\n",
			    dev->si_name);
		devfs_destroy_dev(dev);
	}
}

/*
 * Make sure all asynchronous disk and devfs related operations have
 * completed.
 *
 * Typically called prior to mountroot to ensure that all disks have
 * been completely probed and on module unload to ensure that ops
 * structures have been dereferenced.
 */
void
sync_devs(void)
{
	disk_config(NULL);
	devfs_config();
	disk_config(NULL);
	devfs_config();
}

int
make_dev_alias(cdev_t target, const char *fmt, ...)
{
	__va_list ap;
	char *name;

	__va_start(ap, fmt);
	kvasnrprintf(&name, PATH_MAX, 32, fmt, ap);
	__va_end(ap);

	devfs_make_alias(name, target);
	kvasfree(&name);

	return 0;
}

int
destroy_dev_alias(cdev_t target, const char *fmt, ...)
{
	__va_list ap;
	char *name;

	__va_start(ap, fmt);
	kvasnrprintf(&name, PATH_MAX, 32, fmt, ap);
	__va_end(ap);

	devfs_destroy_alias(name, target);
	kvasfree(&name);

	return 0;
}

extern struct dev_ops default_dev_ops;

cdev_t
make_autoclone_dev(struct dev_ops *ops, struct devfs_bitmap *bitmap,
		d_clone_t *nhandler, uid_t uid, gid_t gid, int perms, const char *fmt, ...)
{
	__va_list ap;
	cdev_t dev;
	char *name;

	__va_start(ap, fmt);
	kvasnrprintf(&name, PATH_MAX, 32, fmt, ap);
	__va_end(ap);

	if (bitmap != NULL)
		devfs_clone_bitmap_init(bitmap);

	devfs_clone_handler_add(name, nhandler);
	dev = make_dev_covering(&default_dev_ops, ops, 0xffff00ff,
		       uid, gid, perms, "%s", name);
	kvasfree(&name);
	return dev;
}

void
destroy_autoclone_dev(cdev_t dev, struct devfs_bitmap *bitmap)
{
	if (dev == NULL)
		return;

	devfs_clone_handler_del(dev->si_name);

	if (bitmap != NULL)
		devfs_clone_bitmap_uninit(bitmap);

	destroy_dev(dev);
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
cdev_t
reference_dev(cdev_t dev)
{
	//kprintf("reference_dev\n");

	if (dev != NULL) {
		sysref_get(&dev->si_sysref);
		if (dev_ref_debug & 2) {
			kprintf("reference dev %p %s(minor=%08x) refs=%d\n",
			    dev, devtoname(dev), dev->si_uminor,
			    dev->si_sysref.refcnt);
		}
	}
	return(dev);
}

/*
 * release a reference on a device.  The device will be terminated when the
 * last reference has been released.
 *
 * NOTE: we must use si_umajor to figure out the original major number,
 * because si_ops could already be pointing at dead_dev_ops.
 */
void
release_dev(cdev_t dev)
{
	//kprintf("release_dev\n");

	if (dev == NULL)
		return;
	sysref_put(&dev->si_sysref);
}

const char *
devtoname(cdev_t dev)
{
	int mynor;
	int len;
	char *p;
	const char *dname;

	if (dev == NULL)
		return("#nodev");
	if (dev->si_name[0] == '#' || dev->si_name[0] == '\0') {
		p = dev->si_name;
		len = sizeof(dev->si_name);
		if ((dname = dev_dname(dev)) != NULL)
			ksnprintf(p, len, "#%s/", dname);
		else
			ksnprintf(p, len, "#%d/", major(dev));
		len -= strlen(p);
		p += strlen(p);
		mynor = minor(dev);
		if (mynor < 0 || mynor > 255)
			ksnprintf(p, len, "%#x", (u_int)mynor);
		else
			ksnprintf(p, len, "%d", mynor);
	}
	return (dev->si_name);
}

