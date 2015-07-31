/*-
 * Copyright (c) 1997, 1998, 1999
 *	Nan Yang Computer Services Limited.  All rights reserved.
 *
 *  Parts copyright (c) 1997, 1998 Cybernet Corporation, NetMAX project.
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
 *	Services Limited.
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
 */

#ifdef _KERNEL
#include "opt_vinum.h"
#endif

#include <sys/time.h>
#include "vinumstate.h"

/*
 * A disk block number or offset
 */
typedef int64_t         vinum_off_t;

/*
 * Some configuration maxima.  They're an enum because
 * we can't define global constants.  Sorry about that.
 *
 * These aren't as bad as they look: most of them are soft limits.
 */

#define VINUMROOT
enum constants {
	VINUM_HEADER = 512,		/* size of header on disk */
	MAXCONFIGLINE = 1024,		/* maximum size of one config line */
	MINVINUMSLICE = 1048576,	/* minimum size of a slice */

	ROUND_ROBIN_READPOL = -1,	/* round robin read policy */

	/*
	 * type field in minor number
	 */
	VINUM_VOLUME_TYPE = 0,
	VINUM_PLEX_TYPE = 1,
	VINUM_SD_TYPE = 2,
	VINUM_DRIVE_TYPE = 3,
	VINUM_SUPERDEV_TYPE = 4,	/* super device. */
	VINUM_RAWPLEX_TYPE = 5,		/* anonymous plex */
	VINUM_RAWSD_TYPE = 6,		/* anonymous subdisk */

	/*
	 * Shifts for the individual fields in the device
	 */
	VINUM_TYPE_SHIFT = 28,
	VINUM_VOL_SHIFT = 0,
	VINUM_PLEX_SHIFT = 16,
	VINUM_SD_SHIFT = 20,
	VINUM_VOL_WIDTH = 8,
	VINUM_PLEX_WIDTH = 3,
	VINUM_SD_WIDTH = 8,

	/*
	 * Shifts for the second half of raw plex and
	 * subdisk numbers
	 */
	VINUM_RAWPLEX_SHIFT = 8,	/* shift the second half this much */
	VINUM_RAWPLEX_WIDTH = 12,	/* width of second half */

	MAJORDEV_SHIFT = 8,

	MAXPLEX = 8,			/* max number of plexes in a volume */
	MAXSD = 256,			/* max number of subdisks in a plex */
	MAXDRIVENAME = 32,		/* max length of a device name */
	MAXSDNAME = 64,			/* max length of a subdisk name */
	MAXPLEXNAME = 64,		/* max length of a plex name */
	MAXVOLNAME = 64,		/* max length of a volume name */
	MAXNAME = 64,			/* max length of any name */


	/*
	 * Define a minor device number.
	 * This is not used directly; instead, it's
	 * called by the other macros.
	 */
#define VINUMMINOR(v,p,s,t)  (  (v << VINUM_VOL_SHIFT)		\
			      | (p << VINUM_PLEX_SHIFT)		\
			      | (s << VINUM_SD_SHIFT)		\
			      | (t << VINUM_TYPE_SHIFT) )

/* Create device minor numbers */

#ifdef _KERNEL

#define VINUMDEV(v,p,s,t)  	\
		VINUMMINOR (v, p, s, t)

#define VINUM_PLEX(p)		\
		 ((VINUM_RAWPLEX_TYPE << VINUM_TYPE_SHIFT) 	\
		 | (p & 0xff)					\
		 | ((p & ~0xff) << 8))

#define VINUM_SD(s)		\
		 ((VINUM_RAWSD_TYPE << VINUM_TYPE_SHIFT) 	\
		 | (s & 0xff)					\
		 | ((s & ~0xff) << 8))

#endif

    /* Create a bit mask for x bits */
#define MASK(x)	 ((1 << (x)) - 1)

    /* Create a raw block device minor number */
#define VINUMRMINOR(d,t)	\
		( ((d & MASK(VINUM_VOL_WIDTH)) << VINUM_VOL_SHIFT)	\
		 | ((d & ~MASK(VINUM_VOL_WIDTH)) <<			\
			(VINUM_PLEX_SHIFT + VINUM_VOL_WIDTH))		\
		 | (t << VINUM_TYPE_SHIFT) )

    /* extract device type */
#define DEVTYPE(x) ((minor (x) >> VINUM_TYPE_SHIFT) & 7)

    /*
     * This mess is used to catch people who compile
     * a debug vinum(8) and non-debug kernel module,
     * or the other way round.
     */

#ifdef VINUMDEBUG

/* superdevice number */
#define	VINUM_SUPERDEV		VINUMMINOR(1, 0, 0, VINUM_SUPERDEV_TYPE)

/* non-debug superdevice number */
#define	VINUM_WRONGSUPERDEV	VINUMMINOR(2, 0, 0, VINUM_SUPERDEV_TYPE)

#else

/* superdevice number */
#define	VINUM_SUPERDEV		VINUMMINOR(2, 0, 0, VINUM_SUPERDEV_TYPE)

/* debug superdevice number */
#define	VINUM_WRONGSUPERDEV	VINUMMINOR(1, 0, 0, VINUM_SUPERDEV_TYPE)

#endif

/* daemon superdevice number */
#define	VINUM_DAEMON_DEV	VINUMMINOR(0, 0, 0, VINUM_SUPERDEV_TYPE)

	/*
	 * the number of object entries to cater for initially, and also the
	 * value by which they are incremented.  It doesn't take long
	 * to extend them, so theoretically we could start with 1 of each, but
	 * it's untidy to allocate such small areas.  These values are
	 * probably too small.
	 */

	INITIAL_DRIVES = 4,
	INITIAL_VOLUMES = 4,
	INITIAL_PLEXES = 8,
	INITIAL_SUBDISKS = 16,
	INITIAL_SUBDISKS_IN_PLEX = 4,	/* num subdisks to alloc to a plex */
	INITIAL_SUBDISKS_IN_DRIVE = 4,	/* num subdisks to alloc to a drive */
	INITIAL_DRIVE_FREELIST = 16,	/* num entries in drive freelist */
	PLEX_REGION_TABLE_SIZE = 8,	/* num entries in plex region tables */
	PLEX_LOCKS = 256,		/* num locks to alloc to a plex */
	MAX_REVIVE_BLOCKSIZE = MAXPHYS,	/* maximum revive block size */
	DEFAULT_REVIVE_BLOCKSIZE = 65536,/* default revive block size */
	VINUMHOSTNAMELEN = 32,		/* host name field in label */
};

/* device numbers */

/*
 *  31 30    28 27                   20 19 18    16 15         8 7      0
 * |---------------------------------------------------------------------|
 * |X |  Type  |    Subdisk number     | X| Plex   |      Major |  volno |
 * |---------------------------------------------------------------------|
 *
 *    0x2                 03                 1           19	    06
 *
 * The fields in the minor number are interpreted as follows:
 *
 * Volume:              Only type and volume number are relevant
 * Plex in volume:      type, plex number in volume and volume number
 *			are relevant
 * raw plex:            type, plex number is made of bits 27-16 and 7-0
 * raw subdisk:         type, subdisk number is made of bits 27-16 and 7-0
 */

#if 0

/* This doesn't get used.  Consider removing it. */
struct devcode {
	/*
	 * CARE.  These fields assume a big-endian word.  On a
	 * little-endian system, they're the wrong way around
	 */
	unsigned volume:8;		/* up to 256 volumes */
	unsigned major:8;		/* major number fits */
	unsigned plex:3;		/* up to 8 plexes per volume */
	unsigned unused:1;		/* up for grabs */
	unsigned sd:8;			/* up to 256 subdisks per plex */
	unsigned type:3;		/* type of object */
	/*
	 * type field
	 VINUM_VOLUME = 0,
	 VINUM_PLEX = 1,
	 VINUM_SUBDISK = 2,
	 VINUM_DRIVE = 3,
	 VINUM_SUPERDEV = 4,
	 VINUM_RAWPLEX = 5,
	 VINUM_RAWSD = 6 */
	unsigned signbit:1;		/* to make 32 bits */
};

#endif

#define VINUM_BASE  "vinum/"
#define VINUM_DIR   "/dev/vinum"

/*
 * These definitions help catch
 * userland/kernel mismatches.
 */
#if VINUMDEBUG

/* normal super device */
#define VINUM_WRONGSUPERDEV_NAME	VINUM_DIR "/control"
#define VINUM_WRONGSUPERDEV_BASE	VINUM_BASE "control"

/* debug super device */
#define VINUM_SUPERDEV_NAME		VINUM_DIR "/Control"
#define VINUM_SUPERDEV_BASE		VINUM_BASE "Control"

#else

/* debug super device */
#define VINUM_WRONGSUPERDEV_NAME	VINUM_DIR "/Control"
#define VINUM_WRONGSUPERDEV_BASE	VINUM_BASE "Control"

/* normal super device */
#define VINUM_SUPERDEV_NAME		VINUM_DIR "/control"
#define VINUM_SUPERDEV_BASE		VINUM_BASE "control"

#endif

/* super device for daemon only */
#define VINUM_DAEMON_DEV_NAME		VINUM_DIR "/controld"
#define VINUM_DAEMON_DEV_BASE		VINUM_BASE "controld"

/*
 * Flags for all objects.  Most of them only apply to
 * specific objects, but we have space for all in any
 * 32 bit flags word.
 */
enum objflags {
	VF_LOCKED = 1,			/* locked access to this object */
	VF_LOCKING = 2,			/* we want access to this object */
	VF_OPEN = 4,			/* object has openers */
	VF_WRITETHROUGH = 8,		/* volume: write through */
	VF_INITED = 0x10,		/* unit has been initialized */

	/* 0x20 unused, was: VF_WLABEL: label area is writable */
	VF_LABELLING = 0x40,		/* unit is currently being labelled */
	VF_WANTED = 0x80,		/* waiting to obtain a lock */
	VF_RAW = 0x100,			/* raw volume (no file system) */
	VF_LOADED = 0x200,		/* module is loaded */
	VF_CONFIGURING = 0x400,		/* someone is changing the config */
	VF_WILL_CONFIGURE = 0x800,	/* someone wants to change the config */
	VF_CONFIG_INCOMPLETE = 0x1000,	/* not finished changing the config */
	VF_CONFIG_SETUPSTATE = 0x2000,	/* set a vol up if all plexes empty */
	VF_READING_CONFIG = 0x4000,	/* reading config database from disk */
	VF_FORCECONFIG = 0x8000,	/* config drives even with diff names */
	VF_NEWBORN = 0x10000,		/* for objects: we've just created it */
	VF_CONFIGURED = 0x20000,	/* for drives: we read the config */
	VF_STOPPING = 0x40000,		/* for vinum_conf: stop on last close */

	VF_DAEMONOPEN = 0x80000,	/* the daemon has us open (only
					 * superdev) */

	VF_CREATED = 0x100000,		/* for vols: freshly created,
					 * more then new */
	VF_HOTSPARE = 0x200000,		/* for drives: use as hot spare */
	VF_RETRYERRORS = 0x400000,	/* don't down subdisks on I/O errors */
};

/* Global configuration information for the vinum subsystem */
struct _vinum_conf {
	/* Pointers to vinum structures */
	struct drive *drive;
	struct sd *sd;
	struct plex *plex;
	struct volume *volume;

	/* the number allocated */
	int drives_allocated;
	int subdisks_allocated;
	int plexes_allocated;
	int volumes_allocated;

	/* and the number currently in use */
	int drives_used;
	int subdisks_used;
	int plexes_used;
	int volumes_used;

	int flags;

#define VINUM_MAXACTIVE  30000	/* max number of active requests */
	int active;		/* current number of requests outstanding */
	int maxactive;		/* max number of requests ever outstanding */
#if VINUMDEBUG
	struct request *lastrq;
	struct bio *lastbio;
#endif
	int physbufs;
};

/* Use these defines to simplify code */
#define DRIVE vinum_conf.drive
#define SD vinum_conf.sd
#define PLEX vinum_conf.plex
#define VOL vinum_conf.volume
#define VFLAGS vinum_conf.flags

/*
 * Slice header
 *
 * Vinum drives start with this structure:
 *
 *\                                            Sector
 * |--------------------------------------|
 * |   PDP-11 memorial boot block         |      0
 * |--------------------------------------|
 * |   Disk label, maybe                  |      1
 * |--------------------------------------|
 * |   Slice definition  (vinum_hdr)      |      8
 * |--------------------------------------|
 * |                                      |
 * |   Configuration info, first copy     |      9
 * |                                      |
 * |--------------------------------------|
 * |                                      |
 * |   Configuration info, second copy    |      9 + size of config
 * |                                      |
 * |--------------------------------------|
 */

/*
 * Sizes and offsets of our information
 */
enum {
	VINUM_LABEL_OFFSET = 4096,	/* offset of vinum label */
	VINUMHEADERLEN = 512,		/* size of vinum label */
	VINUM_CONFIG_OFFSET = 4608,	/* offset of first config copy */
	MAXCONFIG = 65536,		/* and size of config copy */

	/* this is where the data starts */
	DATASTART = (MAXCONFIG * 2 + VINUM_CONFIG_OFFSET) / DEV_BSIZE
};

/*
 * hostname is 256 bytes long, but we don't need to shlep
 * multiple copies in vinum.  We use the host name just
 * to identify this system, and 32 bytes should be ample
 * for that purpose
 */

struct vinum_label {
	char sysname[VINUMHOSTNAMELEN];	/* system name at time of creation */
	char name[MAXDRIVENAME];	/* our name of the drive */
	struct timeval date_of_birth;	/* the time it was created */
	struct timeval last_update;	/* and the time of last update */
	/*
	 * total size in bytes of the drive.  This value
	 * includes the headers.
	 */
	off_t drive_size;
};

struct vinum_hdr {
	uint64_t magic;			/* we're long on magic numbers */

	/*
	 * Size in bytes of each copy of the
	 * configuration info.  This must be a multiple
	 * of the sector size.
	 */
	int config_length;
	struct vinum_label label;	/* unique label */
};

/* should be this */
#define VINUM_MAGIC    22322600044678729LL

/* becomes this after obliteration */
#define VINUM_NOMAGIC  22322600044678990LL

/* Information returned from read_drive_label */
enum drive_label_info {
	DL_CANT_OPEN,			/* invalid partition */
	DL_NOT_OURS,			/* valid part, but no vinum label */
	DL_DELETED_LABEL,		/* valid part, deleted label found */
	DL_WRONG_DRIVE,			/* drive name doesn't match */
	DL_OURS				/* valid partition and label found */
};

/*** Drive definitions ***/
/*
 * A drive corresponds to a disk slice.  We use a different term to show
 * the difference in usage: it doesn't have to be a slice, and could
 * theoretically be a complete, unpartitioned disk
 */

struct drive {
	char devicename[MAXDRIVENAME];	/* name of the slice it's on */
	enum drivestate state;		/* current state */
	int flags;			/* flags */
	int subdisks_allocated;		/* number of entries in sd */
	int subdisks_used;		/* and the number used */
	int blocksize;			/* size of fs blocks */
	int pid;			/* of locker */
	u_int64_t sectors_available;	/* number of sectors still available */
	int secsperblock;
	int lasterror;			/* last error on drive */
	int driveno;			/* index of drive in vinum_conf */
	int opencount;			/* number of up subdisks */
	u_int64_t reads;		/* number of reads on this drive */
	u_int64_t writes;		/* number of writes on this drive */
	u_int64_t bytes_read;		/* number of bytes read */
	u_int64_t bytes_written;	/* number of bytes written */
	struct vinum_label label;	/* and the label information */
#define DRIVE_MAXACTIVE  30000		/* maximum number of active requests */
	int active;			/* current number of reqs outstanding */
	int maxactive;			/* max num of reqs ever outstanding */
	int freelist_size;		/* entries alloced in free list */
	int freelist_entries;		/* entries used in free list */
	struct drive_freelist {		/* sorted list of free space on drive */
		u_int64_t offset;	/* offset of entry */
		u_int64_t sectors;	/* and length in sectors */
	} *freelist;
	struct partinfo partinfo;	/* partition information */
	/* XXX kludge until we get this struct cleaned up */
#if _KERNEL
	struct vnode *vp;
	struct cdev *dev;
#else
	void	*vp_dummy;
	void	*dev_dummy;
#endif
#ifdef VINUMDEBUG
	char lockfilename[16];		/* locked with file */
	int lockline;			/* and the line number */
#endif
};

/*** Subdisk definitions ***/

struct sd {
	char name[MAXSDNAME];		/* name of subdisk */
	enum sdstate state;		/* state */
	int flags;
	int lasterror;			/* last error occurred */
	/* offsets in blocks */
	int64_t driveoffset;		/* offset on drive */
#ifdef _KERNEL
	cdev_t	sd_dev;
#else
	void	*sd_dev_dummy;
#endif

	/*
	 * plexoffset is the offset from the beginning
	 * of the plex to the very first part of the
	 * subdisk, in sectors.  For striped, RAID-4 and
	 * RAID-5 plexes, only the first stripe is
	 * located at this offset
	 */
	int64_t plexoffset;		/* offset in plex */
	u_int64_t sectors;		/* and length in sectors */
	int plexno;			/* index of plex, if it belongs */
	int driveno;			/* index of the drive */
	int sdno;			/* our index in vinum_conf */
	int plexsdno;			/* and our number in our plex */
	/* (undefined if no plex) */
	u_int64_t reads;		/* number of reads on this subdisk */
	u_int64_t writes;		/* number of writes on this subdisk */
	u_int64_t bytes_read;		/* number of bytes read */
	u_int64_t bytes_written;	/* number of bytes written */
	/* revive parameters */
	u_int64_t revived;		/* blkno of current revive request */
	int revive_blocksize;		/* revive block size (bytes) */
	int revive_interval;		/* and time to wait between transfers */
	pid_t reviver;			/* PID of reviving process */
	/* init parameters */
	u_int64_t initialized;		/* blkno of current init request */
	int init_blocksize;		/* init block size (bytes) */
	int init_interval;		/* time to wait between transfers */
	struct request *waitlist;	/* list of reqs waiting on revive op */
};

/*** Plex definitions ***/

/* kinds of plex organization */
enum plexorg {
	plex_disorg,			/* disorganized */
	plex_concat,			/* concatenated plex */
	plex_striped,			/* striped plex */
	plex_raid4,			/* RAID4 plex */
	plex_raid5			/* RAID5 plex */
};

/* Recognize plex organizations */
/* RAID 1, 4 or 5 */
#define isstriped(p)	(p->organization >= plex_striped)

/* RAID 4 or 5 */
#define isparity(p)	(p->organization >= plex_raid4)

struct plex {
	char name[MAXPLEXNAME];		/* name of plex */
	enum plexorg organization;	/* Plex organization */
	enum plexstate state;		/* and current state */
#ifdef _KERNEL
	cdev_t	plex_dev;
#else
	void	*plex_dev_dummy;
#endif
	u_int64_t length;		/* total length of plex (sectors) */
	int flags;
	int stripesize;			/* size of stripe or raid band,
					 * in sectors */
	int subdisks;			/* number of associated subdisks */
	int subdisks_allocated;		/* number of subdisks allocated
					 * space for */
	int *sdnos;			/* list of component subdisks */
	int plexno;			/* index of plex in vinum_conf */
	int volno;			/* index of volume */
	int volplexno;			/* number of plex in volume */
	/* Statistics */
	u_int64_t reads;		/* number of reads on this plex */
	u_int64_t writes;		/* number of writes on this plex */
	u_int64_t bytes_read;		/* number of bytes read */
	u_int64_t bytes_written;	/* number of bytes written */
	u_int64_t recovered_reads;	/* number of recovered read
					 * operations */
	u_int64_t degraded_writes;	/* number of degraded writes */
	u_int64_t parityless_writes;	/* number of parityless writes */
	u_int64_t multiblock;		/* requests that needed more than
					 * one block */
	u_int64_t multistripe;		/* requests that needed more than
					 * one stripe */
	int sddowncount;		/* number of subdisks down */

	/* Lock information */
	int usedlocks;			/* number currently in use */
	int lockwaits;			/* and number of waits for locks */
	off_t checkblock;		/* block number for parity op */
	struct rangelock *lock;		/* ranges of locked addresses */
};

/*** Volume definitions ***/

/* Address range definitions, for locking volumes */
struct rangelock {
	vinum_off_t stripe;		/* address + 1 of the range being locked  */
	struct buf *bp;		/* user's buffer pointer */
};

struct volume {
	char name[MAXVOLNAME];		/* name of volume */
	enum volumestate state;		/* current state */
	int plexes;			/* number of plexes */
	int preferred_plex;		/* plex to read from, -1 for
					 * round-robin */
#ifdef _KERNEL
	cdev_t	vol_dev;
#else
	void	*vol_dev_dummy;
#endif

	/*
	 * index of plex used for last read, for
	 * round-robin.
	 */
	int last_plex_read;
	int volno;			/* volume number */
	int flags;			/* status and configuration flags */
	int openflags;			/* flags supplied to last open(2) */
	u_int64_t size;			/* size of volume */
	int blocksize;			/* logical block size */
	int active;			/* number of outstanding
					 * requests active */
	int subops;			/* and the number of suboperations */
	/* Statistics */
	u_int64_t bytes_read;		/* number of bytes read */
	u_int64_t bytes_written;	/* number of bytes written */
	u_int64_t reads;		/* number of reads on this volume */
	u_int64_t writes;		/* number of writes on this volume */
	u_int64_t recovered_reads;	/* reads recovered from another plex */

	/*
	 * Unlike subdisks in the plex, space for the
	 * plex pointers is static.
	 */
	int plex[MAXPLEX];		/* index of plexes */
};

/*
 * Table expansion.  Expand table, which contains oldcount
 * entries of type element, by increment entries, and change
 * oldcount accordingly
 */
#define EXPAND(table, element, oldcount, increment)         \
{							    \
      expand_table((void **) &table,			    \
		   oldcount * sizeof (element),		    \
		   (oldcount + increment) * sizeof (element)); \
      oldcount += increment;				    \
}

/*
 * Information on vinum's memory usage
 */
struct meminfo {
	int mallocs;			/* number of malloced blocks */
	int total_malloced;		/* total amount malloced */
	int highwater;			/* maximum number of mallocs */
	struct mc *malloced;		/* pointer to kernel table */
};

#define MCFILENAMELEN	16
struct mc {
	struct timeval time;
	int seq;
	int size;
	short line;
	caddr_t address;
	char file[MCFILENAMELEN];
};

/*
 * These enums are used by the state transition
 * routines.  They're in bit map format:
 *
 * Bit 0: Other plexes in the volume are down
 * Bit 1: Other plexes in the volume are up
 * Bit 2: The current plex is up
 * Maybe they should be local to
 * state.c
 */
enum volplexstate {
	volplex_onlyusdown = 0,		/* 0: we're the only plex,
					 * and we're down */
	volplex_alldown,		/* 1: another plex is down,
					 * and so are we */
	volplex_otherup,		/* 2: another plex is up */
	volplex_otherupdown,		/* 3: other plexes are up and down */
	volplex_onlyus,			/* 4: we're up and alone */
	volplex_onlyusup,		/* 5: only we are up, others are down */
	volplex_allup,			/* 6: all plexes are up */
	volplex_someup			/* 7: some plexes are up,
					 * including us */
};

/* state map for plex */
enum sdstates {
	sd_emptystate = 1,
	sd_downstate = 2,		/* SD is down */
	sd_crashedstate = 4,		/* SD is crashed */
	sd_obsoletestate = 8,		/* SD is obsolete */
	sd_stalestate = 16,		/* SD is stale */
	sd_rebornstate = 32,		/* SD is reborn */
	sd_upstate = 64,		/* SD is up */
	sd_initstate = 128,		/* SD is initializing */
	sd_initializedstate = 256,	/* SD is initialized */
	sd_otherstate = 512,		/* SD is in some other state */
};

/*
 * This is really just a parameter to pass to
 * set_<foo>_state, but since it needs to be known
 * in the external definitions, we need to define
 * it here
 */
enum setstateflags {
	setstate_none = 0,		/* no flags */
	setstate_force = 1,		/* force the state change */
	setstate_configuring = 2,	/* we're currently configuring,
					   don't save */
};

/* Operations for parityops to perform. */
enum parityop {
	checkparity,
	rebuildparity,
	rebuildandcheckparity,		/* rebuildparity with the -v option */
};

#ifdef VINUMDEBUG

/*
 * Debugging stuff
 */
enum debugflags {
	DEBUG_ADDRESSES = 1,		/* show buffer information during
					 * requests */
	DEBUG_NUMOUTPUT = 2,		/* show the value of vp->v_numoutput */
	DEBUG_RESID = 4,		/* go into debugger in complete_rqe */
	DEBUG_LASTREQS = 8,		/* keep a circular buffer of
					 * last requests */
	DEBUG_REVIVECONFLICT = 16,	/* print info about revive conflicts */
	DEBUG_EOFINFO = 32,		/* print info about EOF detection */
	DEBUG_MEMFREE = 64,		/* keep info about Frees */
	DEBUG_BIGDRIVE = 128,		/* pretend our drives are 100 times
					 * the size */
	DEBUG_REMOTEGDB = 256,		/* go into remote gdb */
	DEBUG_WARNINGS = 512,		/* log various relatively
					 * harmless warnings  */
};

#endif
