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
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/namecache.h>
#include <sys/paths.h>
#include <sys/thread2.h>
#include <sys/nlookup.h>
#include <sys/devfs.h>
#include <sys/sysctl.h>

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#endif

MALLOC_DEFINE(M_MOUNT, "mount", "vfs mount structure");

#define ROOTNAME	"root_device"

struct vnode	*rootvnode;
struct nchandle rootnch;

/* 
 * The root specifiers we will try if RB_CDROM is specified.  Note that
 * with DEVFS we do not use the compatibility slice's whole-disk 'c'
 * partition.  Instead we just use the whole disk, e.g. cd0 or cd0s0.
 */
static char *cdrom_rootdevnames[] = {
	"cd9660:cd0",	/* SCSI (including AHCI and SILI) */
	"cd9660:acd0",	/* NATA */
	"cd9660:cd1",	/* SCSI (including AHCI and SILI) */
	"cd9660:acd1",	/* NATA */
	"cd9660:cd8",	/* USB */
	"cd9660:cd9",	/* USB */
	NULL
};

int vfs_mountroot_devfs(void);
static void	vfs_mountroot(void *junk);
static int	vfs_mountroot_try(const char *mountfrom);
static int	vfs_mountroot_ask(void);
static int	getline(char *cp, int limit);

/* legacy find-root code */
char		*rootdevnames[2] = {NULL, NULL};
static int	setrootbyname(char *name);

SYSINIT(mountroot, SI_SUB_MOUNT_ROOT, SI_ORDER_SECOND, vfs_mountroot, NULL);

static int wakedelay = 2;	/* delay before mounting root in seconds */
TUNABLE_INT("vfs.root.wakedelay", &wakedelay);

/*
 * Find and mount the root filesystem
 */
static void
vfs_mountroot(void *junk)
{
	cdev_t	save_rootdev = rootdev;
	int	i;
	int	dummy;
	
	/*
	 * Make sure all disk devices created so far have also been probed,
	 * and also make sure that the newly created device nodes for
	 * probed disks are ready, too.
	 *
	 * Messages can fly around here so get good synchronization
	 * coverage.
	 *
	 * XXX - Delay some more (default: 2s) to help drivers which pickup
	 *       devices asynchronously and are not caught by CAM's initial
	 *	 probe.
	 */
	sync_devs();
	tsleep(&dummy, 0, "syncer", hz * wakedelay);


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
	if (!vfs_mountroot_try(kgetenv("vfs.root.mountfrom")))
		return;

	/*
	 * If a vfs set rootdev, try it (XXX VINUM HACK!)
	 */
	if (save_rootdev != NULL) {
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


int
vfs_mountroot_devfs(void)
{
	struct vnode *vp;
	struct nchandle nch;
	struct nlookupdata nd;
	struct mount *mp;
	struct vfsconf *vfsp;
	int error;
	struct ucred *cred = proc0.p_ucred;
	const char *devfs_path, *init_chroot;
	char *dev_malloced = NULL;

	if ((init_chroot = kgetenv("init_chroot")) != NULL) {
		size_t l;

		l = strlen(init_chroot) + sizeof("/dev");
		dev_malloced = kmalloc(l, M_MOUNT, M_WAITOK);
		ksnprintf(dev_malloced, l, "%s/dev", init_chroot);
		devfs_path = dev_malloced;
	} else {
		devfs_path = "/dev";
	}
	/*
	 * Lookup the requested path and extract the nch and vnode.
	 */
	error = nlookup_init_raw(&nd,
	     devfs_path, UIO_SYSSPACE, NLC_FOLLOW,
	     cred, &rootnch);

	if (error == 0) {
		devfs_debug(DEVFS_DEBUG_DEBUG, "vfs_mountroot_devfs: nlookup_init is ok...\n");
		if ((error = nlookup(&nd)) == 0) {
			devfs_debug(DEVFS_DEBUG_DEBUG, "vfs_mountroot_devfs: nlookup is ok...\n");
			if (nd.nl_nch.ncp->nc_vp == NULL) {
				devfs_debug(DEVFS_DEBUG_SHOW, "vfs_mountroot_devfs: nlookup: simply not found\n");
				error = ENOENT;
			}
		}
	}
	if (dev_malloced != NULL)
		kfree(dev_malloced, M_MOUNT), dev_malloced = NULL;
	devfs_path = NULL;
	if (error) {
		nlookup_done(&nd);
		devfs_debug(DEVFS_DEBUG_SHOW, "vfs_mountroot_devfs: nlookup failed, error: %d\n", error);
		return (error);
	}

	/*
	 * Extract the locked+refd ncp and cleanup the nd structure
	 */
	nch = nd.nl_nch;
	cache_zero(&nd.nl_nch);
	nlookup_done(&nd);

	/*
	 * now we have the locked ref'd nch and unreferenced vnode.
	 */
	vp = nch.ncp->nc_vp;
	if ((error = vget(vp, LK_EXCLUSIVE)) != 0) {
		cache_put(&nch);
		devfs_debug(DEVFS_DEBUG_SHOW, "vfs_mountroot_devfs: vget failed\n");
		return (error);
	}
	cache_unlock(&nch);

	if ((error = vinvalbuf(vp, V_SAVE, 0, 0)) != 0) {
		cache_drop(&nch);
		vput(vp);
		devfs_debug(DEVFS_DEBUG_SHOW, "vfs_mountroot_devfs: vinvalbuf failed\n");
		return (error);
	}
	if (vp->v_type != VDIR) {
		cache_drop(&nch);
		vput(vp);
		devfs_debug(DEVFS_DEBUG_SHOW, "vfs_mountroot_devfs: vp is not VDIR\n");
		return (ENOTDIR);
	}

	vfsp = vfsconf_find_by_name("devfs");

	/*
	 * Allocate and initialize the filesystem.
	 */
	mp = kmalloc(sizeof(struct mount), M_MOUNT, M_ZERO|M_WAITOK);
	mount_init(mp);
	vfs_busy(mp, LK_NOWAIT);
	mp->mnt_op = vfsp->vfc_vfsops;
	mp->mnt_vfc = vfsp;
	vfsp->vfc_refcount++;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	mp->mnt_stat.f_owner = cred->cr_uid;
	vn_unlock(vp);

	/*
	 * Mount the filesystem.
	 */
	error = VFS_MOUNT(mp, "/dev", NULL, cred);

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/*
	 * Put the new filesystem on the mount list after root.  The mount
	 * point gets its own mnt_ncmountpt (unless the VFS already set one
	 * up) which represents the root of the mount.  The lookup code
	 * detects the mount point going forward and checks the root of
	 * the mount going backwards.
	 *
	 * It is not necessary to invalidate or purge the vnode underneath
	 * because elements under the mount will be given their own glue
	 * namecache record.
	 */
	if (!error) {
		if (mp->mnt_ncmountpt.ncp == NULL) {
			/*
			 * allocate, then unlock, but leave the ref intact
			 */
			cache_allocroot(&mp->mnt_ncmountpt, mp, NULL);
			cache_unlock(&mp->mnt_ncmountpt);
		}
		mp->mnt_ncmounton = nch;		/* inherits ref */
		nch.ncp->nc_flag |= NCF_ISMOUNTPT;

		/* XXX get the root of the fs and cache_setvp(mnt_ncmountpt...) */
		mountlist_insert(mp, MNTINS_LAST);
		vn_unlock(vp);
		//checkdirs(&mp->mnt_ncmounton, &mp->mnt_ncmountpt);
		error = vfs_allocate_syncvnode(mp);
		if (error) {
			devfs_debug(DEVFS_DEBUG_SHOW, "vfs_mountroot_devfs: vfs_allocate_syncvnode failed\n");
		}
		vfs_unbusy(mp);
		error = VFS_START(mp, 0);
		vrele(vp);
	} else {
		vn_syncer_thr_stop(mp);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_coherency_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_journal_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_norm_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_spec_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_fifo_ops);
		mp->mnt_vfc->vfc_refcount--;
		vfs_unbusy(mp);
		kfree(mp, M_MOUNT);
		cache_drop(&nch);
		vput(vp);
		devfs_debug(DEVFS_DEBUG_SHOW, "vfs_mountroot_devfs: mount failed\n");
	}

	devfs_debug(DEVFS_DEBUG_DEBUG, "rootmount_devfs done with error: %d\n", error);
	return (error);
}


/*
 * Mount (mountfrom) as the root filesystem.
 */
static int
vfs_mountroot_try(const char *mountfrom)
{
	struct mount	*mp;
	char		*vfsname, *devname;
	int		error;
	char		patt[32];
	const char	*cp, *ep;
	char		*mf;
	struct proc	*p;
	struct vnode	*vp;

	vfsname = NULL;
	devname = NULL;
	mp      = NULL;
	error   = EINVAL;

	if (mountfrom == NULL)
		return(error);		/* don't complain */

	crit_enter();
	kprintf("Mounting root from %s\n", mountfrom);
	crit_exit();

	cp = mountfrom;
	/* parse vfs name and devname */
	vfsname = kmalloc(MFSNAMELEN, M_MOUNT, M_WAITOK);
	devname = kmalloc(MNAMELEN, M_MOUNT, M_WAITOK);
	mf = kmalloc(MFSNAMELEN+MNAMELEN, M_MOUNT, M_WAITOK);
	for(;;) {
		for (ep = cp; (*ep != 0) && (*ep != ';'); ep++);
		bzero(vfsname, MFSNAMELEN);
		bzero(devname, MNAMELEN);
		bzero(mf, MFSNAMELEN+MNAMELEN);
		strncpy(mf, cp, MFSNAMELEN+MNAMELEN);

		vfsname[0] = devname[0] = 0;
		ksprintf(patt, "%%%d[a-z0-9]:%%%ds", MFSNAMELEN, MNAMELEN);
		if (ksscanf(mf, patt, vfsname, devname) < 1)
			goto end;

		/* allocate a root mount */
		error = vfs_rootmountalloc(vfsname,
				devname[0] != 0 ? devname : ROOTNAME, &mp);
		if (error != 0) {
			kprintf("Can't allocate root mount for filesystem '%s': %d\n",
			       vfsname, error);
			goto end;
		}
		mp->mnt_flag |= MNT_ROOTFS;

		/* do our best to set rootdev */
		if ((strcmp(vfsname, "hammer") != 0) && (devname[0] != 0) &&
		    setrootbyname(devname))
			kprintf("setrootbyname failed\n");

		/* If the root device is a type "memory disk", mount RW */
		if (rootdev != NULL && dev_is_good(rootdev) &&
		    (dev_dflags(rootdev) & D_MEMDISK)) {
			mp->mnt_flag &= ~MNT_RDONLY;
		}

		error = VFS_MOUNT(mp, NULL, NULL, proc0.p_ucred);

		if (!error)
			break;
end:
		if(*ep == 0)
			break;
		cp = ep + 1;
	}

	if (vfsname != NULL)
		kfree(vfsname, M_MOUNT);
	if (devname != NULL)
		kfree(devname, M_MOUNT);
	if (mf != NULL)
		kfree(mf, M_MOUNT);
	if (error == 0) {
		/* register with list of mounted filesystems */
		mountlist_insert(mp, MNTINS_FIRST);

		/* sanity check system clock against root fs timestamp */
		inittodr(mp->mnt_time);

		/* Get the vnode for '/'.  Set p->p_fd->fd_cdir to reference it. */
		mp = mountlist_boot_getfirst();
		if (VFS_ROOT(mp, &vp))
			panic("cannot find root vnode");
		if (mp->mnt_ncmountpt.ncp == NULL) {
			cache_allocroot(&mp->mnt_ncmountpt, mp, vp);
			cache_unlock(&mp->mnt_ncmountpt);	/* leave ref intact */
		}
		p = curproc;
		p->p_fd->fd_cdir = vp;
		vref(p->p_fd->fd_cdir);
		p->p_fd->fd_rdir = vp;
		vref(p->p_fd->fd_rdir);
		vfs_cache_setroot(vp, cache_hold(&mp->mnt_ncmountpt));
		vn_unlock(vp);			/* leave ref intact */
		cache_copy(&mp->mnt_ncmountpt, &p->p_fd->fd_ncdir);
		cache_copy(&mp->mnt_ncmountpt, &p->p_fd->fd_nrdir);

		vfs_unbusy(mp);
		if (mp->mnt_syncer == NULL) {
			error = vfs_allocate_syncvnode(mp);
			if (error)
				kprintf("Warning: no syncer vp for root!\n");
			error = 0;
		}
		VFS_START( mp, 0 );
	} else {
		if (mp != NULL) {
			vn_syncer_thr_stop(mp);
			vfs_unbusy(mp);
			kfree(mp, M_MOUNT);
		}
		kprintf("Root mount failed: %d\n", error);
	}
	return(error);
}


static void
vfs_mountroot_ask_callback(char *name, cdev_t dev, bool is_alias,
    void *arg __unused)
{
	if (!is_alias && dev_is_good(dev) && (dev_dflags(dev) & D_DISK))
		kprintf(" \"%s\" ", name);
}


/*
 * Spin prompting on the console for a suitable root filesystem
 */
static int
vfs_mountroot_ask(void)
{
	char name[128];
	int llimit = 100;

	kprintf("\nManual root filesystem specification:\n");
	kprintf("  <fstype>:<device>  Specify root (e.g. ufs:da0s1a)\n");
	kprintf("  ?                  List valid disk boot devices\n");
	kprintf("  panic              Just panic\n");
	kprintf("  abort              Abort manual input\n");
	while (llimit--) {
		kprintf("\nmountroot> ");

		if (getline(name, 128) < 0)
			break;
		if (name[0] == 0) {
			;
		} else if (name[0] == '?') {
			kprintf("Possibly valid devices for root FS:\n");
			//enumerate all disk devices
			devfs_scan_callback(vfs_mountroot_ask_callback, NULL);
			kprintf("\n");
			continue;
		} else if (strcmp(name, "panic") == 0) {
			panic("panic from console");
		} else if (strcmp(name, "abort") == 0) {
			break;
		} else if (vfs_mountroot_try(name) == 0) {
			return(0);
		}
	}
	return(1);
}


static int
getline(char *cp, int limit)
{
	char *lp;
	int c;

	lp = cp;
	for (;;) {
		c = cngetc();

		switch (c) {
		case -1:
			return(-1);
		case '\n':
		case '\r':
			kprintf("\n");
			*lp++ = '\0';
			return(0);
		case '\b':
		case '\177':
			if (lp > cp) {
				kprintf("\b \b");
				lp--;
			} else {
				kprintf("%c", 7);
			}
			continue;
		case '#':
			kprintf("#");
			lp--;
			if (lp < cp)
				lp = cp;
			continue;
		case '@':
		case 'u' & 037:
			lp = cp;
			kprintf("%c", '\n');
			continue;
		default:
			if (lp - cp >= limit - 1) {
				kprintf("%c", 7);
			} else {
				kprintf("%c", c);
				*lp++ = c;
			}
			continue;
		}
	}
}

/*
 * Convert a given name to the cdev_t of the disk-like device
 * it refers to.
 */
cdev_t
kgetdiskbyname(const char *name) 
{
	cdev_t rdev;

	/*
	 * Get the base name of the device
	 */
	if (strncmp(name, __SYS_PATH_DEV, sizeof(__SYS_PATH_DEV) - 1) == 0)
		name += sizeof(__SYS_PATH_DEV) - 1;

	/*
	 * Locate the device
	 */
	rdev = devfs_find_device_by_name("%s", name);
	if (rdev == NULL) {
		kprintf("no disk named '%s'\n", name);
	}
	/*
	 * FOUND DEVICE
	 */
	return(rdev);
}

/*
 * Set rootdev to match (name), given that we expect it to
 * refer to a disk-like device.
 */
static int
setrootbyname(char *name)
{
	cdev_t diskdev;

	diskdev = kgetdiskbyname(name);
	if (diskdev != NULL) {
		rootdev = diskdev;
		return (0);
	}
	/* set to NULL if kgetdiskbyname() fails so that if the first rootdev is
	 * found by fails to mount and the second one isn't found, mountroot_try
	 * doesn't try again with the first one
	 */
	rootdev = NULL;
	return (1);
}

#ifdef DDB
DB_SHOW_COMMAND(disk, db_getdiskbyname)
{
	cdev_t dev;

	if (modif[0] == '\0') {
		db_error("usage: show disk/devicename");
		return;
	}
	dev = kgetdiskbyname(modif);
	if (dev != NULL)
		db_printf("cdev_t = %p\n", dev);
	else
		db_printf("No disk device matched.\n");
}
#endif

static int
vfs_sysctl_real_root(SYSCTL_HANDLER_ARGS)
{
	char *real_root;
	size_t len;
	int error;

	real_root = kgetenv("vfs.root.realroot");

	if (real_root == NULL)
		real_root = "";

	len = strlen(real_root) + 1;

	error = sysctl_handle_string(oidp, real_root, len, req);

	return error;
}

SYSCTL_PROC(_vfs, OID_AUTO, real_root,
	    CTLTYPE_STRING | CTLFLAG_RD, 0, 0, vfs_sysctl_real_root,
	    "A", "Real root mount string");
