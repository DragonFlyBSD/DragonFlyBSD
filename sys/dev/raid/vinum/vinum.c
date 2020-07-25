/*-
 * Copyright (c) 1997, 1998
 *	Nan Yang Computer Services Limited.  All rights reserved.
 *
 *  Written by Greg Lehey
 *
 *  This software is distributed under the so-called ``Berkeley
 *  License'':
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
 *	This product includes software developed by Nan Yang Computer
 *      Services Limited.
 * 4. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even if
 * advised of the possibility of such damage.
 *
 * $Id: vinum.c,v 1.33 2001/01/09 06:19:15 grog Exp grog $
 * $FreeBSD: src/sys/dev/vinum/vinum.c,v 1.38.2.3 2003/01/07 12:14:16 joerg Exp $
 */

#define STATIC static					    /* nothing while we're testing XXX */

#include "vinumhdr.h"
#include <sys/sysmsg.h>					    /* for sync(2) */
#include <sys/poll.h>					    /* XXX: poll ops used in kq filters */
#include <sys/event.h>
#include <sys/udev.h>
#ifdef VINUMDEBUG
#include <sys/reboot.h>
int debug = 0;
extern int total_malloced;
extern int malloccount;
extern struct mc malloced[];
#endif
#include "request.h"

struct dev_ops vinum_ops =
{
	{ "vinum", 0, D_DISK },
	.d_open =	vinumopen,
	.d_close =	vinumclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	vinumioctl,
	.d_kqfilter =	vinumkqfilter,
	.d_strategy =	vinumstrategy,
	.d_dump =	vinumdump,
	.d_psize =	vinumsize,
};

/* Called by main() during pseudo-device attachment. */
STATIC void vinumattach(void *);

STATIC int vinum_modevent(module_t mod, modeventtype_t type, void *unused);
STATIC void vinum_initconf(void);

struct _vinum_conf vinum_conf;				    /* configuration information */
cdev_t	vinum_super_dev;
cdev_t	vinum_wsuper_dev;
cdev_t	vinum_daemon_dev;

/*
 * Called by main() during pseudo-device attachment.  All we need
 * to do is allocate enough space for devices to be configured later, and
 * add devsw entries.
 */
static void
vinumattach(void *dummy)
{
    char *cp, *cp1, *cp2, **drives;
    int i, rv;
    struct volume *vol;

    /* modload should prevent multiple loads, so this is worth a panic */
    if ((vinum_conf.flags & VF_LOADED) != 0)
	panic("vinum: already loaded");

    log(LOG_INFO, "vinum: loaded\n");
    vinum_conf.flags |= VF_LOADED;			    /* we're loaded now */

    daemonq = NULL;					    /* initialize daemon's work queue */
    dqend = NULL;

#if 0
    dev_ops_add(&vinum_ops, 0, 0);
#endif

    vinum_initconf();

    /*
     * Create superdev, wrongsuperdev, and controld devices.
     */
    vinum_super_dev =	make_dev(&vinum_ops, VINUM_SUPERDEV,
				 UID_ROOT, GID_WHEEL, 0600,
				 VINUM_SUPERDEV_BASE);
    vinum_wsuper_dev =	make_dev(&vinum_ops, VINUM_WRONGSUPERDEV,
				 UID_ROOT, GID_WHEEL, 0600,
				 VINUM_WRONGSUPERDEV_BASE);
    vinum_daemon_dev = 	make_dev(&vinum_ops, VINUM_DAEMON_DEV,
				 UID_ROOT, GID_WHEEL, 0600,
				 VINUM_DAEMON_DEV_BASE);

    /*
     * See if the loader has passed us a disk to
     * read the initial configuration from.
     */
    if ((cp = kgetenv("vinum.drives")) != NULL) {
	for (cp1 = cp, i = 0, drives = NULL; *cp1 != '\0'; i++) {
	    cp2 = cp1;
	    while (*cp1 != '\0' && *cp1 != ',' && *cp1 != ' ')
		cp1++;
	    if (*cp1 != '\0')
		*cp1++ = '\0';
	    drives = krealloc(drives, (unsigned long)((i + 1) * sizeof(char *)),
			     M_TEMP, M_WAITOK);
	    drives[i] = cp2;
	}
	if (i == 0)
	    goto bailout;
	rv = vinum_scandisk(drives, i);
	if (rv)
	    log(LOG_NOTICE, "vinum_scandisk() returned %d", rv);
    bailout:
	kfree(drives, M_TEMP);
    }
    if ((cp = kgetenv("vinum.root")) != NULL) {
	for (i = 0; i < vinum_conf.volumes_used; i++) {
	    vol = &vinum_conf.volume[i];
	    if ((vol->state == volume_up)
		&& (strcmp (vol->name, cp) == 0) 
	    ) {
		rootdev = make_dev(&vinum_ops, i, UID_ROOT, GID_OPERATOR,
				0640, VINUM_BASE "vinumroot");
		udev_dict_set_cstr(rootdev, "subsystem", "raid");
		udev_dict_set_cstr(rootdev, "disk-type", "raid");
		log(LOG_INFO, "vinum: using volume %s for root device\n", cp);
		break;
	    }
	}
    }
}

/*
 * Check if we have anything open.  If confopen is != 0,
 * that goes for the super device as well, otherwise
 * only for volumes.
 *
 * Return 0 if not inactive, 1 if inactive.
 */
int
vinum_inactive(int confopen)
{
    int i;
    int can_do = 1;					    /* assume we can do it */

    if (confopen && (vinum_conf.flags & VF_OPEN))	    /* open by vinum(8)? */
	return 0;					    /* can't do it while we're open */
    lock_config();
    for (i = 0; i < vinum_conf.volumes_allocated; i++) {
	if ((VOL[i].state > volume_down)
	    && (VOL[i].flags & VF_OPEN)) {		    /* volume is open */
	    can_do = 0;
	    break;
	}
    }
    unlock_config();
    return can_do;
}

/*
 * Free all structures.
 * If cleardrive is 0, save the configuration; otherwise
 * remove the configuration from the drive.
 *
 * Before coming here, ensure that no volumes are open.
 */
void
free_vinum(int cleardrive)
{
    union daemoninfo di = { .nothing = 0 };
    int i;
    int drives_allocated = vinum_conf.drives_allocated;

    if (DRIVE != NULL) {
	if (cleardrive) {				    /* remove the vinum config */
	    for (i = 0; i < drives_allocated; i++)
		remove_drive(i);			    /* remove the drive */
	} else {					    /* keep the config */
	    for (i = 0; i < drives_allocated; i++)
		free_drive(&DRIVE[i]);			    /* close files and things */
	}
	Free(DRIVE);
    }
    while ((vinum_conf.flags & (VF_STOPPING | VF_DAEMONOPEN))
	== (VF_STOPPING | VF_DAEMONOPEN)) {		    /* at least one daemon open, we're stopping */
	queue_daemon_request(daemonrq_return, di);	    /* stop the daemon */
	tsleep(&vinumclose, 0, "vstop", 1);		    /* and wait for it */
    }
    if (SD != NULL) {
	for (i = 0; i < vinum_conf.subdisks_allocated; i++) {
	    struct sd *sd = &vinum_conf.sd[i];
	    if (sd->sd_dev) {
		destroy_dev(sd->sd_dev);
		sd->sd_dev = NULL;
	    }
	}
	Free(SD);
    }
    if (PLEX != NULL) {
	for (i = 0; i < vinum_conf.plexes_allocated; i++) {
	    struct plex *plex = &vinum_conf.plex[i];

	    if (plex->plex_dev) {
		destroy_dev(plex->plex_dev);
		plex->plex_dev = NULL;
	    }

	    if (plex->state != plex_unallocated) {	    /* we have real data there */
		if (plex->sdnos)
		    Free(plex->sdnos);
	    }
	}
	Free(PLEX);
    }
    if (VOL != NULL) {
	for (i = 0; i < vinum_conf.volumes_allocated; i++) {
	    struct volume *vol = &vinum_conf.volume[i];

	    if (vol->vol_dev) {
		destroy_dev(vol->vol_dev);
		vol->vol_dev = NULL;
	    }
	}
	Free(VOL);
    }
    bzero(&vinum_conf, sizeof(vinum_conf));
    vinum_initconf();
}

STATIC void
vinum_initconf(void)
{
    vinum_conf.physbufs = nswbuf_kva / 2 + 1;

    /* allocate space: drives... */
    DRIVE = (struct drive *) Malloc(sizeof(struct drive) * INITIAL_DRIVES);
    CHECKALLOC(DRIVE, "vinum: no memory\n");
    bzero(DRIVE, sizeof(struct drive) * INITIAL_DRIVES);
    vinum_conf.drives_allocated = INITIAL_DRIVES;
    vinum_conf.drives_used = 0;

    /* volumes, ... */
    VOL = (struct volume *) Malloc(sizeof(struct volume) * INITIAL_VOLUMES);
    CHECKALLOC(VOL, "vinum: no memory\n");
    bzero(VOL, sizeof(struct volume) * INITIAL_VOLUMES);
    vinum_conf.volumes_allocated = INITIAL_VOLUMES;
    vinum_conf.volumes_used = 0;

    /* plexes, ... */
    PLEX = (struct plex *) Malloc(sizeof(struct plex) * INITIAL_PLEXES);
    CHECKALLOC(PLEX, "vinum: no memory\n");
    bzero(PLEX, sizeof(struct plex) * INITIAL_PLEXES);
    vinum_conf.plexes_allocated = INITIAL_PLEXES;
    vinum_conf.plexes_used = 0;

    /* and subdisks */
    SD = (struct sd *) Malloc(sizeof(struct sd) * INITIAL_SUBDISKS);
    CHECKALLOC(SD, "vinum: no memory\n");
    bzero(SD, sizeof(struct sd) * INITIAL_SUBDISKS);
    vinum_conf.subdisks_allocated = INITIAL_SUBDISKS;
    vinum_conf.subdisks_used = 0;
}

STATIC int
vinum_modevent(module_t mod, modeventtype_t type, void *unused)
{
    switch (type) {
    case MOD_LOAD:
	vinumattach(NULL);
	return 0;					    /* OK */
    case MOD_UNLOAD:
	if (!vinum_inactive(1))				    /* is anything open? */
	    return EBUSY;				    /* yes, we can't do it */
	vinum_conf.flags |= VF_STOPPING;		    /* note that we want to stop */
	sys_sync(NULL, NULL);			    /* write out buffers */
	free_vinum(0);					    /* clean up */

	if (vinum_super_dev) {
	    destroy_dev(vinum_super_dev);
	    vinum_super_dev = NULL;
	}
	if (vinum_wsuper_dev) {
	    destroy_dev(vinum_wsuper_dev);
	    vinum_wsuper_dev = NULL;
	}
	if (vinum_daemon_dev) {
	    destroy_dev(vinum_daemon_dev);
	    vinum_daemon_dev = NULL;
	}

	sync_devs();
#ifdef VINUMDEBUG
	if (total_malloced) {
	    int i;
#ifdef INVARIANTS
	    int *poke;
#endif

	    for (i = 0; i < malloccount; i++) {
		if (debug & DEBUG_WARNINGS)		    /* want to hear about them */
		    log(LOG_WARNING,
			"vinum: exiting with %d bytes malloced from %s:%d\n",
			malloced[i].size,
			malloced[i].file,
			malloced[i].line);
#ifdef INVARIANTS
		poke = &((int *) malloced[i].address)
		    [malloced[i].size / (2 * sizeof(int))]; /* middle of the area */
		if (*poke == 0xdeadc0de)		    /* already freed */
		    log(LOG_ERR,
			"vinum: exiting with malloc table inconsistency at %p from %s:%d\n",
			malloced[i].address,
			malloced[i].file,
			malloced[i].line);
#endif
		Free(malloced[i].address);
	    }
	}
#endif
	dev_ops_remove_all(&vinum_ops);
	log(LOG_INFO, "vinum: unloaded\n");		    /* tell the world */
	return 0;
    default:
	break;
    }
    return 0;
}

moduledata_t vinum_mod =
{
    "vinum",
    (modeventhand_t) vinum_modevent,
    0
};
DECLARE_MODULE(vinum, vinum_mod, SI_SUB_RAID, SI_ORDER_MIDDLE);
MODULE_VERSION(vinum, 1);

/* ARGSUSED */
/* Open a vinum object */
int
vinumopen(struct dev_open_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    int error;
    unsigned int index;
    struct volume *vol;
    struct plex *plex;
    struct sd *sd;
    int devminor;					    /* minor number */

    devminor = minor(dev);
    error = 0;
    /* First, decide what we're looking at */
    switch (DEVTYPE(dev)) {
    case VINUM_VOLUME_TYPE:
	index = Volno(dev);
	if (index >= vinum_conf.volumes_allocated)
	    return ENXIO;				    /* no such device */
	vol = &VOL[index];

	switch (vol->state) {
	case volume_unallocated:
	case volume_uninit:
	    return ENXIO;

	case volume_up:
	    vol->flags |= VF_OPEN;			    /* note we're open */
	    return 0;

	case volume_down:
	    return EIO;

	default:
	    return EINVAL;
	}

    case VINUM_PLEX_TYPE:
	if (Volno(dev) >= vinum_conf.volumes_allocated)
	    return ENXIO;
	/* FALLTHROUGH */

    case VINUM_RAWPLEX_TYPE:
	index = Plexno(dev);				    /* get plex index in vinum_conf */
	if (index >= vinum_conf.plexes_allocated)
	    return ENXIO;				    /* no such device */
	plex = &PLEX[index];

	switch (plex->state) {
	case plex_referenced:
	case plex_unallocated:
	    return EINVAL;

	default:
	    plex->flags |= VF_OPEN;			    /* note we're open */
	    return 0;
	}

    case VINUM_SD_TYPE:
	if ((Volno(dev) >= vinum_conf.volumes_allocated)    /* no such volume */
	||(Plexno(dev) >= vinum_conf.plexes_allocated))	    /* or no such plex */
	    return ENXIO;				    /* no such device */

	/* FALLTHROUGH */

    case VINUM_RAWSD_TYPE:
	index = Sdno(dev);				    /* get the subdisk number */
	if ((index >= vinum_conf.subdisks_allocated)	    /* not a valid SD entry */
	||(SD[index].state < sd_init))			    /* or SD is not real */
	    return ENXIO;				    /* no such device */
	sd = &SD[index];

	/*
	 * Opening a subdisk is always a special operation, so we
	 * ignore the state as long as it represents a real subdisk
	 */
	switch (sd->state) {
	case sd_unallocated:
	case sd_uninit:
	    return EINVAL;

	default:
	    sd->flags |= VF_OPEN;			    /* note we're open */
	    return 0;
	}

    case VINUM_SUPERDEV_TYPE:
	error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);  /* are we root? */
	if (error == 0) {				    /* yes, can do */
	    if (devminor == VINUM_DAEMON_DEV)		    /* daemon device */
		vinum_conf.flags |= VF_DAEMONOPEN;	    /* we're open */
	    else if (devminor == VINUM_SUPERDEV)
		vinum_conf.flags |= VF_OPEN;		    /* we're open */
	    else
		error = ENODEV;				    /* nothing, maybe a debug mismatch */
	}
	return error;

	/* Vinum drives are disks.  We already have a disk
	 * driver, so don't handle them here */
    case VINUM_DRIVE_TYPE:
    default:
	return ENODEV;					    /* don't know what to do with these */
    }
}

/* ARGSUSED */
int
vinumclose(struct dev_close_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    unsigned int index;
    struct volume *vol;
    int devminor;

    devminor = minor(dev);
    index = Volno(dev);
    /* First, decide what we're looking at */
    switch (DEVTYPE(dev)) {
    case VINUM_VOLUME_TYPE:
	if (index >= vinum_conf.volumes_allocated)
	    return ENXIO;				    /* no such device */
	vol = &VOL[index];

	switch (vol->state) {
	case volume_unallocated:
	case volume_uninit:
	    return ENXIO;

	case volume_up:
	    vol->flags &= ~VF_OPEN;			    /* reset our flags */
	    return 0;

	case volume_down:
	    return EIO;

	default:
	    return EINVAL;
	}

    case VINUM_PLEX_TYPE:
	if (Volno(dev) >= vinum_conf.volumes_allocated)
	    return ENXIO;
	/* FALLTHROUGH */

    case VINUM_RAWPLEX_TYPE:
	index = Plexno(dev);				    /* get plex index in vinum_conf */
	if (index >= vinum_conf.plexes_allocated)
	    return ENXIO;				    /* no such device */
	PLEX[index].flags &= ~VF_OPEN;			    /* reset our flags */
	return 0;

    case VINUM_SD_TYPE:
	if ((Volno(dev) >= vinum_conf.volumes_allocated) || /* no such volume */
	    (Plexno(dev) >= vinum_conf.plexes_allocated))   /* or no such plex */
	    return ENXIO;				    /* no such device */
	/* FALLTHROUGH */

    case VINUM_RAWSD_TYPE:
	index = Sdno(dev);				    /* get the subdisk number */
	if (index >= vinum_conf.subdisks_allocated)
	    return ENXIO;				    /* no such device */
	SD[index].flags &= ~VF_OPEN;			    /* reset our flags */
	return 0;

    case VINUM_SUPERDEV_TYPE:
	/*
	 * don't worry about whether we're root:
	 * nobody else would get this far.
	 */
	if (devminor == VINUM_SUPERDEV)			    /* normal superdev */
	    vinum_conf.flags &= ~VF_OPEN;		    /* no longer open */
	else if (devminor == VINUM_DAEMON_DEV) {	    /* the daemon device */
	    vinum_conf.flags &= ~VF_DAEMONOPEN;		    /* no longer open */
	    if (vinum_conf.flags & VF_STOPPING)		    /* we're stopping, */
		wakeup(&vinumclose);			    /* we can continue stopping now */
	}
	return 0;

    case VINUM_DRIVE_TYPE:
    default:
	return ENODEV;					    /* don't know what to do with these */
    }
}

/* size routine */
int
vinumsize(struct dev_psize_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct volume *vol;

    vol = &VOL[Volno(dev)];

    if (vol->state == volume_up) {
	ap->a_result = (int64_t)vol->size;
	return(0);
    } else {
	return(ENXIO);
    }
}

int
vinumdump(struct dev_dump_args *ap)
{
    /* Not implemented. */
    return ENXIO;
}

void
vinumfilt_detach(struct knote *kn) {}

int
vinumfilt_rd(struct knote *kn, long hint)
{
    cdev_t dev = (cdev_t)kn->kn_hook;

    if (seltrue(dev, POLLIN | POLLRDNORM))
        return (1);

    return (0);
}

int
vinumfilt_wr(struct knote *kn, long hint)
{
    /* Writing is always OK */
    return (1);
}

struct filterops vinumfiltops_rd =
    { FILTEROP_ISFD, NULL, vinumfilt_detach, vinumfilt_rd };
struct filterops vinumfiltops_wr =
    { FILTEROP_ISFD, NULL, vinumfilt_detach, vinumfilt_wr };

int
vinumkqfilter(struct dev_kqfilter_args *ap)
{
    if (ap->a_kn->kn_filter == EVFILT_READ) {
        ap->a_kn->kn_fop = &vinumfiltops_rd;
        ap->a_kn->kn_hook = (caddr_t)ap->a_head.a_dev;
        ap->a_result = 0;
    } else if (ap->a_kn->kn_filter == EVFILT_WRITE) {
	ap->a_kn->kn_fop = &vinumfiltops_wr;
        ap->a_result = 0;
    } else {
        ap->a_result = EOPNOTSUPP;
    }

    return (0);
}
