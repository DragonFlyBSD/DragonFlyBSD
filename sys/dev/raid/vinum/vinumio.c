/*-
 * Copyright (c) 1997, 1998
 *	Nan Yang Computer Services Limited.  All rights reserved.
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
 * $Id: vinumio.c,v 1.30 2000/05/10 23:23:30 grog Exp grog $
 * $FreeBSD: src/sys/dev/vinum/vinumio.c,v 1.52.2.6 2002/05/02 08:43:44 grog Exp $
 */

#include "vinumhdr.h"
#include "request.h"
#include <vm/vm_zone.h>
#include <sys/nlookup.h>

static char *sappend(char *txt, char *s);
static int drivecmp(const void *va, const void *vb);

/*
 * Open the device associated with the drive, and set drive's vp.
 * Return an error number
 */
int
open_drive(struct drive *drive, struct proc *p, int verbose)
{
    struct nlookupdata nd;
    int error;

    /*
     * Fail if already open
     */
    if (drive->flags & VF_OPEN)
	return EBUSY;

    if (rootdev) {
	/*
	 * Open via filesystem (future)
	 */
	error = nlookup_init(&nd, drive->devicename, UIO_SYSSPACE, NLC_FOLLOW);
	if (error)
	    return error;
	error = vn_open(&nd, NULL, FREAD|FWRITE, 0);
	drive->vp = nd.nl_open_vp;
	nd.nl_open_vp = NULL;
	nlookup_done(&nd);
    } else {
	/*
	 * Open via synthesized vnode backed by disk device
	 */
	error = vn_opendisk(drive->devicename, FREAD|FWRITE, &drive->vp);
	if (error)
	    return error;
    }

    if (error == 0 && drive->vp == NULL)
	error = ENODEV;

    /*
     * A huge amount of pollution all over vinum requires that our low
     * level drive be a device.
     */
    if (error == 0 && drive->vp->v_type != VCHR) {
        vn_close(drive->vp, FREAD|FWRITE, NULL);
	drive->vp = NULL;
	error = ENODEV;
    }
    if (error) {
	drive->state = drive_down;
	if (verbose) {
	    log(LOG_WARNING,
		"vinum open_drive %s: failed with error %d\n",
		drive->devicename, error);
	}
    } else {
	drive->dev = drive->vp->v_rdev;
	drive->flags |= VF_OPEN;
    }
    drive->lasterror = error;
    return error;
}

/*
 * Set some variables in the drive struct
 * in more convenient form.  Return error indication
 */
int
set_drive_parms(struct drive *drive)
{
    drive->blocksize = BLKDEV_IOSIZE;			    /* do we need this? */
    drive->secsperblock = drive->blocksize		    /* number of sectors per block */
	/ drive->partinfo.media_blksize;

    /* Now update the label part */
    bcopy(hostname, drive->label.sysname, VINUMHOSTNAMELEN); /* put in host name */
    getmicrotime(&drive->label.date_of_birth);		    /* and current time */
    drive->label.drive_size = drive->partinfo.media_size;
#if VINUMDEBUG
    if (debug & DEBUG_BIGDRIVE)				    /* pretend we're 100 times as big */
	drive->label.drive_size *= 100;
#endif

    /* number of sectors available for subdisks */
    drive->sectors_available = drive->label.drive_size / DEV_BSIZE - DATASTART;

    /*
     * Bug in 3.0 as of January 1998: you can open
     * non-existent slices.  They have a length of 0.
     */
    if (drive->label.drive_size < MINVINUMSLICE) {	    /* too small to worry about */
	set_drive_state(drive->driveno, drive_down, setstate_force);
	drive->lasterror = ENOSPC;
	return ENOSPC;
    }
    drive->freelist_size = INITIAL_DRIVE_FREELIST;	    /* initial number of entries */
    drive->freelist = (struct drive_freelist *)
	Malloc(INITIAL_DRIVE_FREELIST * sizeof(struct drive_freelist));
    if (drive->freelist == NULL)			    /* can't malloc, dammit */
	return ENOSPC;
    drive->freelist_entries = 1;			    /* just (almost) the complete drive */
    drive->freelist[0].offset = DATASTART;		    /* starts here */
    drive->freelist[0].sectors = (drive->label.drive_size >> DEV_BSHIFT) - DATASTART; /* and it's this long */
    if (drive->label.name[0] != '\0')			    /* got a name */
	set_drive_state(drive->driveno, drive_up, setstate_force); /* our drive is accessible */
    else						    /* we know about it, but that's all */
	drive->state = drive_referenced;
    return 0;
}

/*
 * Initialize a drive: open the device and add device
 * information
 */
int
init_drive(struct drive *drive, int verbose)
{
    if (drive->devicename[0] != '/') {
	drive->lasterror = EINVAL;
	log(LOG_ERR, "vinum: Can't open drive without drive name (%s)\n",
	    drive->devicename);
	return EINVAL;
    }
    drive->lasterror = open_drive(drive, curproc, verbose); /* open the drive */
    if (drive->lasterror)
	return drive->lasterror;

    drive->lasterror = VOP_IOCTL(drive->vp, DIOCGPART,
				 (caddr_t)&drive->partinfo, FREAD|FWRITE,
				 proc0.p_ucred, NULL);
    if (drive->lasterror) {
	if (verbose)
	    log(LOG_WARNING,
		"vinum open_drive %s: Can't get partition information, drive->lasterror %d\n",
		drive->devicename,
		drive->lasterror);
	close_drive(drive);
	return drive->lasterror;
    }
    if (drive->partinfo.fstype != FS_VINUM &&
	!kuuid_is_vinum(&drive->partinfo.fstype_uuid)
    ) {	 
	drive->lasterror = EFTYPE;
	if (verbose)
	    log(LOG_WARNING,
		"vinum open_drive %s: Wrong partition type for vinum\n",
		drive->devicename);
	close_drive(drive);
	return EFTYPE;
    }
    return set_drive_parms(drive);			    /* set various odds and ends */
}

/* Close a drive if it's open. */
void
close_drive(struct drive *drive)
{
    LOCKDRIVE(drive);					    /* keep the daemon out */
    if (drive->flags & VF_OPEN)
	close_locked_drive(drive);			    /* and close it */
    if (drive->state > drive_down)			    /* if it's up */
	drive->state = drive_down;			    /* make sure it's down */
    unlockdrive(drive);
}

/*
 * Real drive close code, called with drive already locked.
 * We have also checked that the drive is open.  No errors.
 */
void
close_locked_drive(struct drive *drive)
{
    /*
     * If we can't access the drive, we can't flush
     * the queues, which spec_close() will try to
     * do.  Get rid of them here first.
     */
    if (drive->vp) {
	drive->lasterror = vn_close(drive->vp, FREAD|FWRITE, NULL);
	drive->vp = NULL;
    }
    drive->flags &= ~VF_OPEN;
}

/*
 * Remove drive from the configuration.
 * Caller must ensure that it isn't active.
 */
void
remove_drive(int driveno)
{
    struct drive *drive = &vinum_conf.drive[driveno];
    struct vinum_hdr *vhdr;				    /* buffer for header */
    int error;

    if (drive->state > drive_referenced) {		    /* real drive */
	if (drive->state == drive_up) {
	    vhdr = (struct vinum_hdr *) Malloc(VINUMHEADERLEN);	/* allocate buffer */
	    CHECKALLOC(vhdr, "Can't allocate memory");
	    error = read_drive(drive, (void *) vhdr, VINUMHEADERLEN, VINUM_LABEL_OFFSET);
	    if (error)
		drive->lasterror = error;
	    else {
		vhdr->magic = VINUM_NOMAGIC;		    /* obliterate the magic, but leave the rest */
		write_drive(drive, (void *) vhdr, VINUMHEADERLEN, VINUM_LABEL_OFFSET);
	    }
	    Free(vhdr);
	}
	free_drive(drive);				    /* close it and free resources */
	save_config();					    /* and save the updated configuration */
    }
}

/*
 * Transfer drive data.  Usually called from one of these defines;
 * #define read_drive(a, b, c, d) driveio (a, b, c, d, BUF_CMD_READ)
 * #define write_drive(a, b, c, d) driveio (a, b, c, d, BUF_CMD_WRITE)
 *
 * length and offset are in bytes, but must be multiples of sector
 * size.  The function *does not check* for this condition, and
 * truncates ruthlessly.
 * Return error number
 */
int
driveio(struct drive *drive, char *buf, size_t length, off_t offset, buf_cmd_t cmd)
{
    int error;
    struct buf *bp;
    caddr_t saveaddr;

    error = 0;						    /* to keep the compiler happy */
    while (length) {					    /* divide into small enough blocks */
	int len = umin(length, MAXBSIZE);		    /* maximum block device transfer is MAXBSIZE */

	bp = geteblk(len);				    /* get a buffer header */
	bp->b_cmd = cmd;
	bp->b_bio1.bio_offset = offset;			    /* disk offset */
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	saveaddr = bp->b_data;
	bp->b_data = buf;
	bp->b_bcount = len;
	vn_strategy(drive->vp, &bp->b_bio1);
	error = biowait(&bp->b_bio1, (cmd == BUF_CMD_READ ? "drvrd" : "drvwr"));
	bp->b_data = saveaddr;
	bp->b_flags |= B_INVAL | B_AGE;
	bp->b_flags &= ~B_ERROR;
	brelse(bp);
	if (error)
	    break;
	length -= len;					    /* update pointers */
	buf += len;
	offset += len;
    }
    return error;
}

/*
 * Check a drive for a vinum header.  If found,
 * update the drive information.  We come here
 * with a partially populated drive structure
 * which includes the device name.
 *
 * Return information on what we found.
 *
 * This function is called from two places: check_drive,
 * which wants to find out whether the drive is a
 * Vinum drive, and config_drive, which asserts that
 * it is a vinum drive.  In the first case, we don't
 * print error messages (verbose==0), in the second
 * we do (verbose==1).
 */
enum drive_label_info
read_drive_label(struct drive *drive, int verbose)
{
    int error;
    int result;
    struct vinum_hdr *vhdr;

    error = init_drive(drive, verbose);			    /* find the drive */
    if (error)						    /* find the drive */
	return DL_CANT_OPEN;				    /* not ours */

    vhdr = (struct vinum_hdr *) Malloc(VINUMHEADERLEN);	    /* allocate buffers */
    CHECKALLOC(vhdr, "Can't allocate memory");

    drive->state = drive_up;				    /* be optimistic */
    error = read_drive(drive, (void *) vhdr, VINUMHEADERLEN, VINUM_LABEL_OFFSET);
    if (vhdr->magic == VINUM_MAGIC) {			    /* ours! */
	if (drive->label.name[0]			    /* we have a name for this drive */
	&&(strcmp(drive->label.name, vhdr->label.name))) {  /* but it doesn't match the real name */
	    drive->lasterror = EINVAL;
	    result = DL_WRONG_DRIVE;			    /* it's the wrong drive */
	    drive->state = drive_unallocated;		    /* put it back, it's not ours */
	} else
	    result = DL_OURS;
	/*
	 * We copy the drive anyway so that we have
	 * the correct name in the drive info.  This
	 * may not be the name specified
	 */
	drive->label = vhdr->label;			    /* put in the label information */
    } else if (vhdr->magic == VINUM_NOMAGIC)		    /* was ours, but we gave it away */
	result = DL_DELETED_LABEL;			    /* and return the info */
    else
	result = DL_NOT_OURS;				    /* we could have it, but we don't yet */
    Free(vhdr);						    /* that's all. */
    return result;
}

/*
 * Check a drive for a vinum header.  If found,
 * read configuration information from the drive and
 * incorporate the data into the configuration.
 *
 * Return drive number.
 */
struct drive *
check_drive(char *devicename)
{
    int driveno;
    int i;
    struct drive *drive;

    driveno = find_drive_by_dev(devicename, 1);		    /* if entry doesn't exist, create it */
    drive = &vinum_conf.drive[driveno];			    /* and get a pointer */

    if (read_drive_label(drive, 0) == DL_OURS) {	    /* one of ours */
	for (i = 0; i < vinum_conf.drives_allocated; i++) { /* see if the name already exists */
	    if ((i != driveno)				    /* not this drive */
	    &&(DRIVE[i].state != drive_unallocated)	    /* and it's allocated */
	    &&(strcmp(DRIVE[i].label.name,
			DRIVE[driveno].label.name) == 0)) { /* and it has the same name */
		struct drive *mydrive = &DRIVE[i];

		if (mydrive->devicename[0] == '/') {	    /* we know a device name for it */
		    /*
		     * set an error, but don't take the
		     * drive down: that would cause unneeded
		     * error messages.
		     */
		    drive->lasterror = EEXIST;
		    break;
		} else {				    /* it's just a place holder, */
		    int sdno;

		    for (sdno = 0; sdno < vinum_conf.subdisks_allocated; sdno++) { /* look at each subdisk */
			if ((SD[sdno].driveno == i)	    /* it's pointing to this one, */
			&&(SD[sdno].state != sd_unallocated)) {	/* and it's a real subdisk */
			    SD[sdno].driveno = drive->driveno; /* point to the one we found */
			    update_sd_state(sdno);	    /* and update its state */
			}
		    }
		    bzero(mydrive, sizeof(struct drive));   /* don't deallocate it, just remove it */
		}
	    }
	}
    } else {
	if (drive->lasterror == 0)
	    drive->lasterror = ENODEV;
	close_drive(drive);
	drive->state = drive_down;
    }
    return drive;
}

static char *
sappend(char *txt, char *s)
{
    while ((*s++ = *txt++) != 0);
    return s - 1;
}

void
format_config(char *config, int len)
{
    int i;
    int j;
    char *s = config;
    char *configend = &config[len];

    bzero(config, len);

    /* First write the volume configuration */
    for (i = 0; i < vinum_conf.volumes_allocated; i++) {
	struct volume *vol;

	vol = &vinum_conf.volume[i];
	if ((vol->state > volume_uninit)
	    && (vol->name[0] != '\0')) {		    /* paranoia */
	    ksnprintf(s,
		configend - s,
		"volume %s state %s",
		vol->name,
		volume_state(vol->state));
	    while (*s)
		s++;					    /* find the end */
	    if (vol->preferred_plex >= 0)		    /* preferences, */
		ksnprintf(s,
		    configend - s,
		    " readpol prefer %s",
		    vinum_conf.plex[vol->preferred_plex].name);
	    while (*s)
		s++;					    /* find the end */
	    s = sappend("\n", s);
	}
    }

    /* Then the plex configuration */
    for (i = 0; i < vinum_conf.plexes_allocated; i++) {
	struct plex *plex;

	plex = &vinum_conf.plex[i];
	if ((plex->state > plex_referenced)
	    && (plex->name[0] != '\0')) {		    /* paranoia */
	    ksnprintf(s,
		configend - s,
		"plex name %s state %s org %s ",
		plex->name,
		plex_state(plex->state),
		plex_org(plex->organization));
	    while (*s)
		s++;					    /* find the end */
	    if (isstriped(plex)) {
		ksnprintf(s,
		    configend - s,
		    "%ds ",
		    (int) plex->stripesize);
		while (*s)
		    s++;				    /* find the end */
	    }
	    if (plex->volno >= 0)			    /* we have a volume */
		ksnprintf(s,
		    configend - s,
		    "vol %s ",
		    vinum_conf.volume[plex->volno].name);
	    while (*s)
		s++;					    /* find the end */
	    for (j = 0; j < plex->subdisks; j++) {
		ksnprintf(s,
		    configend - s,
		    " sd %s",
		    vinum_conf.sd[plex->sdnos[j]].name);
	    }
	    s = sappend("\n", s);
	}
    }

    /* And finally the subdisk configuration */
    for (i = 0; i < vinum_conf.subdisks_allocated; i++) {
	struct sd *sd;
	char *drivename;

	sd = &SD[i];
	if ((sd->state != sd_referenced)
	    && (sd->state != sd_unallocated)
	    && (sd->name[0] != '\0')) {			    /* paranoia */
	    drivename = vinum_conf.drive[sd->driveno].label.name;
	    /*
	     * XXX We've seen cases of dead subdisks
	     * which don't have a drive.  If we let them
	     * through here, the drive name is null, so
	     * they get the drive named 'plex'.
	     *
	     * This is a breakage limiter, not a fix.
	     */
	    if (drivename[0] == '\0')
		drivename = "*invalid*";
	    ksnprintf(s,
		configend - s,
		"sd name %s drive %s plex %s len %llus driveoffset %llus state %s",
		sd->name,
		drivename,
		vinum_conf.plex[sd->plexno].name,
		(unsigned long long) sd->sectors,
		(unsigned long long) sd->driveoffset,
		sd_state(sd->state));
	    while (*s)
		s++;					    /* find the end */
	    if (sd->plexno >= 0)
		ksnprintf(s,
		    configend - s,
		    " plexoffset %llds",
		    (long long) sd->plexoffset);
	    else
		ksnprintf(s, configend - s, " detached");
	    while (*s)
		s++;					    /* find the end */
	    if (sd->flags & VF_RETRYERRORS) {
		ksnprintf(s, configend - s, " retryerrors");
		while (*s)
		    s++;				    /* find the end */
	    }
	    ksnprintf(s, configend - s, " \n");
	    while (*s)
		s++;					    /* find the end */
	}
    }
    if (s > &config[len - 2])
	panic("vinum: configuration data overflow");
}

/*
 * issue a save config request to the dæmon.  The actual work
 * is done in process context by daemon_save_config
 */
void
save_config(void)
{
    union daemoninfo di = { .nothing = 0 };

    queue_daemon_request(daemonrq_saveconfig, di);
}

/*
 * Write the configuration to all vinum slices.  This
 * is performed by the dæmon only
 */
void
daemon_save_config(void)
{
    int error;
    int driveno;
    struct drive *drive;				    /* point to current drive info */
    struct vinum_hdr *vhdr;				    /* and as header */
    char *config;					    /* point to config data */
    int wlabel_on;					    /* to set writing label on/off */

    /* don't save the configuration while we're still working on it */
    if (vinum_conf.flags & VF_CONFIGURING)
	return;
    /* Build a volume header */
    vhdr = (struct vinum_hdr *) Malloc(VINUMHEADERLEN);	    /* get space for the config data */
    CHECKALLOC(vhdr, "Can't allocate config data");
    vhdr->magic = VINUM_MAGIC;				    /* magic number */
    vhdr->config_length = MAXCONFIG;			    /* length of following config info */

    config = Malloc(MAXCONFIG);				    /* get space for the config data */
    CHECKALLOC(config, "Can't allocate config data");

    format_config(config, MAXCONFIG);
    error = 0;						    /* no errors yet */
    for (driveno = 0; driveno < vinum_conf.drives_allocated; driveno++) {
	drive = &vinum_conf.drive[driveno];		    /* point to drive */
	if (drive->state > drive_referenced) {
	    LOCKDRIVE(drive);				    /* don't let it change */

	    /*
	     * First, do some drive consistency checks.  Some
	     * of these are kludges, others require a process
	     * context and couldn't be done before
	     */
	    if ((drive->devicename[0] == '\0')
		|| (drive->label.name[0] == '\0')) {
		unlockdrive(drive);
		free_drive(drive);			    /* get rid of it */
		break;
	    }
	    if (((drive->flags & VF_OPEN) == 0)		    /* drive not open */
	    &&(drive->state > drive_down)) {		    /* and it thinks it's not down */
		unlockdrive(drive);
		set_drive_state(driveno, drive_down, setstate_force); /* tell it what's what */
		continue;
	    }
	    if ((drive->state == drive_down)		    /* it's down */
	    &&(drive->flags & VF_OPEN)) {		    /* but open, */
		unlockdrive(drive);
		close_drive(drive);			    /* close it */
	    } else if (drive->state > drive_down) {
		getmicrotime(&drive->label.last_update);    /* time of last update is now */
		bcopy((char *) &drive->label,		    /* and the label info from the drive structure */
		    (char *) &vhdr->label,
		    sizeof(vhdr->label));
		if ((drive->state != drive_unallocated)
		    && (drive->state != drive_referenced)) { /* and it's a real drive */
		    wlabel_on = 1;			    /* enable writing the label */
		    error = 0;
#if 1
		    error = VOP_IOCTL(drive->vp, DIOCWLABEL,
				      (caddr_t)&wlabel_on, FREAD|FWRITE,
				      proc0.p_ucred, NULL);
#endif
		    if (error == 0)
			error = write_drive(drive, (char *) vhdr, VINUMHEADERLEN, VINUM_LABEL_OFFSET);
		    if (error == 0)
			error = write_drive(drive, config, MAXCONFIG, VINUM_CONFIG_OFFSET); /* first config copy */
		    if (error == 0)
			error = write_drive(drive, config, MAXCONFIG, VINUM_CONFIG_OFFSET + MAXCONFIG);	/* second copy */
		    wlabel_on = 0;			    /* enable writing the label */
#if 1
		    if (error == 0) {
			error = VOP_IOCTL(drive->vp, DIOCWLABEL,
					  (caddr_t)&wlabel_on, FREAD|FWRITE,
					  proc0.p_ucred, NULL);
		    }
#endif
		    unlockdrive(drive);
		    if (error) {
			log(LOG_ERR,
			    "vinum: Can't write config to %s, error %d\n",
			    drive->devicename,
			    error);
			set_drive_state(drive->driveno, drive_down, setstate_force);
		    }
		}
	    } else					    /* not worth looking at, */
		unlockdrive(drive);			    /* just unlock it again */
	}
    }
    Free(vhdr);
    Free(config);
}

/* Look at all disks on the system for vinum slices */
int
vinum_scandisk(char *devicename[], int drives)
{
    struct drive *volatile drive;
    volatile int driveno;
    volatile int gooddrives;				    /* number of usable drives found */
    int firsttime;					    /* set if we have never configured before */
    int error;
    char *config_text;					    /* read the config info from disk into here */
    char *volatile cptr;				    /* pointer into config information */
    char *eptr;						    /* end pointer into config information */
    char *config_line;					    /* copy the config line to */
    volatile int status;
    int *volatile drivelist;				    /* list of drive indices */
#define DRIVENAMELEN 64
#define DRIVEPARTS   35					    /* max partitions per drive, excluding c */
    char partname[DRIVENAMELEN];			    /* for creating partition names */

    status = 0;						    /* success indication */
    vinum_conf.flags |= VF_READING_CONFIG;		    /* reading config from disk */

    gooddrives = 0;					    /* number of usable drives found */
    firsttime = vinum_conf.drives_used == 0;		    /* are we a virgin? */

    /* allocate a drive pointer list */
    drivelist = (int *) Malloc(drives * DRIVEPARTS * sizeof(int));
    CHECKALLOC(drivelist, "Can't allocate memory");
    error = setjmp(command_fail);			    /* come back here on error */
    if (error) {					    /* longjmped out */
	return error;
    }

    /* Open all drives and find which was modified most recently */
    for (driveno = 0; driveno < drives; driveno++) {
	char part, has_part = 0;			    /* UNIX partition */
	int slice;
	int founddrive;					    /* flag when we find a vinum drive */
	int has_slice = -1;
	char *tmp;

	founddrive = 0;					    /* no vinum drive found yet on this spindle */

	/*
	 * If the device path contains a slice we do not try to tack on
	 * another slice.  If the device path has a partition we only check
	 * that partition.
	 */
	if ((tmp = rindex(devicename[driveno], '/')) == NULL)
	    tmp = devicename[driveno];
	else
		tmp++;
	ksscanf(tmp, "%*[a-z]%*d%*[s]%d%c", &has_slice, &has_part);

	for (slice = 0; slice < MAX_SLICES; slice++) {
	    if (has_slice >= 0 && slice != has_slice)
		continue;

	    for (part = 'a'; part < 'a' + MAXPARTITIONS; part++) {
		if (part == 'c')
		    continue;
		if (has_part && part != has_part)
		    continue;
		if (has_slice >= 0 && has_part)
			strncpy(partname, devicename[driveno], DRIVENAMELEN);
		else if (has_slice >= 0)
			ksnprintf(partname, DRIVENAMELEN,
				"%s%c", devicename[driveno], part);
		else
			ksnprintf(partname, DRIVENAMELEN,
				"%ss%d%c", devicename[driveno], slice, part);
		drive = check_drive(partname);	    /* try to open it */
		if ((drive->lasterror != 0)		    /* didn't work, */
		    ||(drive->state != drive_up))
		    free_drive(drive);		    /* get rid of it */
		else if (drive->flags & VF_CONFIGURED)  /* already read this config, */
		    log(LOG_WARNING,
			"vinum: already read config from %s\n", /* say so */
			drive->label.name);
		else {
		    drivelist[gooddrives] = drive->driveno;	/* keep the drive index */
		    drive->flags &= ~VF_NEWBORN;	    /* which is no longer newly born */
		    gooddrives++;
		    founddrive++;
		}
	    }
	}
    }

    if (gooddrives == 0) {
	if (firsttime)
	    log(LOG_WARNING, "vinum: no drives found\n");
	else
	    log(LOG_INFO, "vinum: no additional drives found\n");
	return ENOENT;
    }
    /*
     * We now have at least one drive
     * open.  Sort them in order of config time
     * and merge the config info with what we
     * have already.
     */
    kqsort(drivelist, gooddrives, sizeof(int), drivecmp);
    config_text = (char *) Malloc(MAXCONFIG * 2);	    /* allocate buffers */
    CHECKALLOC(config_text, "Can't allocate memory");
    config_line = (char *) Malloc(MAXCONFIGLINE * 2);	    /* allocate buffers */
    CHECKALLOC(config_line, "Can't allocate memory");
    for (driveno = 0; driveno < gooddrives; driveno++) {    /* now include the config */
	drive = &DRIVE[drivelist[driveno]];		    /* point to the drive */

	if (firsttime && (driveno == 0))		    /* we've never configured before, */
	    log(LOG_INFO, "vinum: reading configuration from %s\n", drive->devicename);
	else
	    log(LOG_INFO, "vinum: updating configuration from %s\n", drive->devicename);

	if (drive->state == drive_up)
	    /* Read in both copies of the configuration information */
	    error = read_drive(drive, config_text, MAXCONFIG * 2, VINUM_CONFIG_OFFSET);
	else {
	    error = EIO;
	    kprintf("vinum_scandisk: %s is %s\n", drive->devicename, drive_state(drive->state));
	}

	if (error != 0) {
	    log(LOG_ERR, "vinum: Can't read device %s, error %d\n", drive->devicename, error);
	    free_drive(drive);				    /* give it back */
	    status = error;
	}
	/*
	 * At this point, check that the two copies
	 * are the same, and do something useful if
	 * not.  In particular, consider which is
	 * newer, and what this means for the
	 * integrity of the data on the drive.
	 */
	else {
	    vinum_conf.drives_used++;			    /* another drive in use */
	    /* Parse the configuration, and add it to the global configuration */
	    for (cptr = config_text; *cptr != '\0';) {	    /* love this style(9) */
		volatile int parse_status;		    /* return value from parse_config */

		for (eptr = config_line; (*cptr != '\n') && (*cptr != '\0');) /* until the end of the line */
		    *eptr++ = *cptr++;
		*eptr = '\0';				    /* and delimit */
		if (setjmp(command_fail) == 0) {	    /* come back here on error and continue */
		    parse_status = parse_config(config_line, &keyword_set, 1); /* parse the config line */
		    if (parse_status < 0) {		    /* error in config */
			/*
			   * This config should have been parsed in user
			   * space.  If we run into problems here, something
			   * serious is afoot.  Complain and let the user
			   * snarf the config to see what's wrong.
			 */
			log(LOG_ERR,
			    "vinum: Config error on %s, aborting integration\n",
			    drive->devicename);
			free_drive(drive);		    /* give it back */
			status = EINVAL;
		    }
		}
		while (*cptr == '\n')
		    cptr++;				    /* skip to next line */
	    }
	}
	drive->flags |= VF_CONFIGURED;			    /* read this drive's configuration */
    }

    Free(config_line);
    Free(config_text);
    Free(drivelist);
    vinum_conf.flags &= ~VF_READING_CONFIG;		    /* no longer reading from disk */
    if (status != 0)
	kprintf("vinum: couldn't read configuration");
    else
	updateconfig(VF_READING_CONFIG);		    /* update from disk config */
    return status;
}

/*
 * Compare the modification dates of the drives, for qsort.
 * Return 1 if a < b, 0 if a == b, 01 if a > b: in other
 * words, sort backwards.
 */
int
drivecmp(const void *va, const void *vb)
{
    const struct drive *a = &DRIVE[*(const int *) va];
    const struct drive *b = &DRIVE[*(const int *) vb];

    if ((a->label.last_update.tv_sec == b->label.last_update.tv_sec)
	&& (a->label.last_update.tv_usec == b->label.last_update.tv_usec))
	return 0;
    else if ((a->label.last_update.tv_sec > b->label.last_update.tv_sec)
	    || ((a->label.last_update.tv_sec == b->label.last_update.tv_sec)
	    && (a->label.last_update.tv_usec > b->label.last_update.tv_usec)))
	return -1;
    else
	return 1;
}
