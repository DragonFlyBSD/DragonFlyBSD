/*-
 * Copyright (c) 1999 Michael Smith
 * All rights reserved.
 * Copyright (c) 1999 Poul-Henning Kamp
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
 *	$FreeBSD: src/sys/kern/vfs_conf.c,v 1.49.2.5 2003/01/07 11:56:53 joerg Exp $
 *	$DragonFly: src/sys/kern/vfs_conf.c,v 1.12 2004/09/30 18:59:48 dillon Exp $
 */

/*
 * Locate and mount the root filesystem.
 *
 * The root filesystem is detailed in the kernel environment variable
 * vfs.root.mountfrom, which is expected to be in the general format
 *
 * <vfsname>:[<path>]
 * vfsname   := the name of a VFS known to the kernel and capable
 *              of being mounted as root
 * path      := disk device name or other data used by the filesystem
 *              to locate its physical store
 *
 */

#include "opt_rootdevname.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/reboot.h>
#include <sys/diskslice.h>
#include <sys/disklabel.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/device.h>
#include <sys/namecache.h>
#include <sys/paths.h>

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#endif

MALLOC_DEFINE(M_MOUNT, "mount", "vfs mount structure");

#define ROOTNAME	"root_device"

struct vnode	*rootvnode;
struct namecache *rootncp;

/* 
 * The root specifiers we will try if RB_CDROM is specified.  Note that
 * the ATA driver will accept acd*a and acd*c, but the SCSI driver
 * will only accept cd*c, so use 'c'.
 */
static char *cdrom_rootdevnames[] = {
	"cd9660:cd0c",
	"cd9660:acd0c",
	"cd9660:cd1c",
	"cd9660:acd1c",
	NULL
};

static void	vfs_mountroot(void *junk);
static int	vfs_mountroot_try(const char *mountfrom);
static int	vfs_mountroot_ask(void);
static void	gets(char *cp);

/* legacy find-root code */
char		*rootdevnames[2] = {NULL, NULL};
static int	setrootbyname(char *name);

SYSINIT(mountroot, SI_SUB_MOUNT_ROOT, SI_ORDER_SECOND, vfs_mountroot, NULL);
	
/*
 * Find and mount the root filesystem
 */
static void
vfs_mountroot(void *junk)
{
	int	i;
	dev_t	save_rootdev = rootdev;
	
	/* 
	 * The root filesystem information is compiled in, and we are
	 * booted with instructions to use it.
	 */
#ifdef ROOTDEVNAME
	if ((boothowto & RB_DFLTROOT) && 
	    !vfs_mountroot_try(ROOTDEVNAME))
		return;
#endif
	/* 
	 * We are booted with instructions to prompt for the root filesystem,
	 * or to use the compiled-in default when it doesn't exist.
	 */
	if (boothowto & (RB_DFLTROOT | RB_ASKNAME)) {
		if (!vfs_mountroot_ask())
			return;
	}

	/*
	 * We've been given the generic "use CDROM as root" flag.  This is
	 * necessary because one media may be used in many different
	 * devices, so we need to search for them.
	 */
	if (boothowto & RB_CDROM) {
		for (i = 0; cdrom_rootdevnames[i] != NULL; i++) {
			if (!vfs_mountroot_try(cdrom_rootdevnames[i]))
				return;
		}
	}

	/*
	 * Try to use the value read by the loader from /etc/fstab, or
	 * supplied via some other means.  This is the preferred 
	 * mechanism.
	 */
	if (!vfs_mountroot_try(getenv("vfs.root.mountfrom")))
		return;

	/*
	 * If a vfs set rootdev, try it (XXX VINUM HACK!)
	 */
	if (save_rootdev != NODEV) {
		rootdev = save_rootdev;
		if (!vfs_mountroot_try(""))
			return;
	}

	/* 
	 * Try values that may have been computed by the machine-dependant
	 * legacy code.
	 */
	if (rootdevnames[0] && !vfs_mountroot_try(rootdevnames[0]))
		return;
	if (rootdevnames[1] && !vfs_mountroot_try(rootdevnames[1]))
		return;

	/*
	 * If we have a compiled-in default, and haven't already tried it, try
	 * it now.
	 */
#ifdef ROOTDEVNAME
	if (!(boothowto & RB_DFLTROOT))
		if (!vfs_mountroot_try(ROOTDEVNAME))
			return;
#endif

	/* 
	 * Everything so far has failed, prompt on the console if we haven't
	 * already tried that.
	 */
	if (!(boothowto & (RB_DFLTROOT | RB_ASKNAME)) && !vfs_mountroot_ask())
		return;
	panic("Root mount failed, startup aborted.");
}

/*
 * Mount (mountfrom) as the root filesystem.
 */
static int
vfs_mountroot_try(const char *mountfrom)
{
	struct thread	*td = curthread;
        struct mount	*mp;
	char		*vfsname, *devname;
	int		error;
	char		patt[32];
	lwkt_tokref	ilock;
	int		s;

	vfsname = NULL;
	devname = NULL;
	mp      = NULL;
	error   = EINVAL;

	if (mountfrom == NULL)
		return(error);		/* don't complain */

	s = splcam();			/* Overkill, but annoying without it */
	printf("Mounting root from %s\n", mountfrom);
	splx(s);

	/* parse vfs name and devname */
	vfsname = malloc(MFSNAMELEN, M_MOUNT, M_WAITOK);
	devname = malloc(MNAMELEN, M_MOUNT, M_WAITOK);
	vfsname[0] = devname[0] = 0;
	sprintf(patt, "%%%d[a-z0-9]:%%%ds", MFSNAMELEN, MNAMELEN);
	if (sscanf(mountfrom, patt, vfsname, devname) < 1)
		goto done;

	/* allocate a root mount */
	error = vfs_rootmountalloc(vfsname, 
			devname[0] != 0 ? devname : ROOTNAME, &mp);
	if (error != 0) {
		printf("Can't allocate root mount for filesystem '%s': %d\n",
		       vfsname, error);
		goto done;
	}
	mp->mnt_flag |= MNT_ROOTFS;

	/* do our best to set rootdev */
	if ((devname[0] != 0) && setrootbyname(devname))
		printf("setrootbyname failed\n");

	/* If the root device is a type "memory disk", mount RW */
	if (rootdev != NODEV && dev_is_good(rootdev) &&
	    (dev_dflags(rootdev) & D_MEMDISK)) {
		mp->mnt_flag &= ~MNT_RDONLY;
	}

	error = VFS_MOUNT(mp, NULL, NULL, td);

done:
	if (vfsname != NULL)
		free(vfsname, M_MOUNT);
	if (devname != NULL)
		free(devname, M_MOUNT);
	if (error != 0) {
		if (mp != NULL) {
			vfs_unbusy(mp, td);
			free(mp, M_MOUNT);
		}
		printf("Root mount failed: %d\n", error);
	} else {

		/* register with list of mounted filesystems */
		lwkt_gettoken(&ilock, &mountlist_token);
		TAILQ_INSERT_HEAD(&mountlist, mp, mnt_list);
		lwkt_reltoken(&ilock);

		/* sanity check system clock against root fs timestamp */
		inittodr(mp->mnt_time);
		vfs_unbusy(mp, td);
	}
	return(error);
}

/*
 * Spin prompting on the console for a suitable root filesystem
 */
static int
vfs_mountroot_ask(void)
{
	char name[128];
	int i;
	dev_t dev;

	for(;;) {
		printf("\nManual root filesystem specification:\n");
		printf("  <fstype>:<device>  Mount <device> using filesystem <fstype>\n");
		printf("                       eg. ufs:da0s1a\n");
		printf("  ?                  List valid disk boot devices\n");
		printf("  <empty line>       Abort manual input\n");
		printf("\nmountroot> ");
		gets(name);
		if (name[0] == 0)
			return(1);
		if (name[0] == '?') {
			printf("Possibly valid devices for 'ufs' root:\n");
			for (i = 0; i < NUMCDEVSW; i++) {
				dev = udev2dev(makeudev(i, 0), 0);
				if (dev_is_good(dev))
					printf(" \"%s\"", dev_dname(dev));
			}
			printf("\n");
			continue;
		}
		if (!vfs_mountroot_try(name))
			return(0);
	}
}

static void
gets(char *cp)
{
	char *lp;
	int c;

	lp = cp;
	for (;;) {
		printf("%c", c = cngetc() & 0177);
		switch (c) {
		case -1:
		case '\n':
		case '\r':
			*lp++ = '\0';
			return;
		case '\b':
		case '\177':
			if (lp > cp) {
				printf(" \b");
				lp--;
			}
			continue;
		case '#':
			lp--;
			if (lp < cp)
				lp = cp;
			continue;
		case '@':
		case 'u' & 037:
			lp = cp;
			printf("%c", '\n');
			continue;
		default:
			*lp++ = c;
		}
	}
}

/*
 * Convert a given name to the dev_t of the disk-like device
 * it refers to.
 */
dev_t
getdiskbyname(const char *name) 
{
	char *cp;
	int nlen;
	int cd, unit, slice, part;
	dev_t dev;
	dev_t rdev;

	/*
	 * Get the base name of the device
	 */
	if (strncmp(name, __SYS_PATH_DEV, sizeof(__SYS_PATH_DEV) - 1) == 0)
		name += sizeof(__SYS_PATH_DEV) - 1;
	cp = __DECONST(char *, name);
	while (*cp == '/')
		++cp;
	while (*cp >= 'a' && *cp <= 'z')
		++cp;
	if (cp == name) {
		printf("missing device name\n");
		return (NODEV);
	}
	nlen = cp - name;

	/*
	 * Get the unit.
	 */
	unit = strtol(cp, &cp, 10);
	if (name + nlen == (const char *)cp || unit < 0 || unit > DKMAXUNIT) {
		printf("bad unit: %d\n", unit);
		return (NODEV);
	}

	/*
	 * Get the slice.  Note that if no partition or partition 'a' is
	 * specified, and no slice is specified, we will try both 'ad0a'
	 * (which is what you get when slice is 0), and also 'ad0' (the
	 * whole-disk partition, slice == 1).
	 */
	if (*cp == 's') {
		slice = cp[1] - '0';
		if (slice < 1 || slice > 9) {
			printf("bad slice number\n");
			return(NODEV);
		}
		++slice;	/* slice #1 starts at 2 */
		cp += 2;
	} else {
		slice = 0;
	}

	/*
	 * Get the partition.
	 */
	if (*cp >= 'a' && *cp <= 'p') {
		part = *cp - 'a';
		++cp;
	} else {
		part = 0;
	}

	if (*cp != '\0') {
		printf("junk after name\n");
		return (NODEV);
	}

	/*
	 * Locate the device
	 */
	for (cd = 0; cd < NUMCDEVSW; cd++) {
		const char *dname;

		dev = udev2dev(makeudev(cd, dkmakeminor(unit, slice, part)), 0);
		if (dev_is_good(dev) && (dname = dev_dname(dev)) != NULL) {
			if (strlen(dname) == nlen &&
			    strncmp(dname, name, nlen) == 0) {
				break;
			}
		}
	}
	if (cd == NUMCDEVSW) {
		printf("no such device '%*.*s'\n", nlen, nlen, name);
		return (NODEV);
	}

	/*
	 * FOUND DEVICE
	 */
	rdev = make_sub_dev(dev, dkmakeminor(unit, slice, part));
	return(rdev);
}

/*
 * Set rootdev to match (name), given that we expect it to
 * refer to a disk-like device.
 */
static int
setrootbyname(char *name)
{
	dev_t diskdev;

	diskdev = getdiskbyname(name);
	if (diskdev != NODEV) {
		rootdev = diskdev;
		return (0);
	}

	return (1);
}

#ifdef DDB
DB_SHOW_COMMAND(disk, db_getdiskbyname)
{
	dev_t dev;

	if (modif[0] == '\0') {
		db_error("usage: show disk/devicename");
		return;
	}
	dev = getdiskbyname(modif);
	if (dev != NODEV)
		db_printf("dev_t = %p\n", dev);
	else
		db_printf("No disk device matched.\n");
}
#endif
