/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <vfs/hammer2/hammer2_xxhash.h>
#include <vfs/hammer2/hammer2_disk.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <err.h>
#include <uuid.h>

#include "hammer2_subs.h"

#define MAXLABELS	HAMMER2_SET_COUNT

static hammer2_off_t check_volume(const char *path, int *fdp);
static int64_t getsize(const char *str, int64_t minval, int64_t maxval, int pw);
static uint64_t nowtime(void);
static int blkrefary_cmp(const void *b1, const void *b2);
static void usage(void);

static void format_hammer2(int fd, hammer2_off_t total_space,
				hammer2_off_t free_space);
static void alloc_direct(hammer2_off_t *basep, hammer2_blockref_t *bref,
				size_t bytes);

static int Hammer2Version = -1;
static uuid_t Hammer2_FSType;	/* static filesystem type id for HAMMER2 */
static uuid_t Hammer2_VolFSID;	/* unique filesystem id in volu header */
static uuid_t Hammer2_SupCLID;	/* PFS cluster id in super-root inode */
static uuid_t Hammer2_SupFSID;	/* PFS unique id in super-root inode */
static uuid_t Hammer2_PfsCLID[MAXLABELS];
static uuid_t Hammer2_PfsFSID[MAXLABELS];
static const char *Label[MAXLABELS];
static hammer2_off_t BootAreaSize;
static hammer2_off_t AuxAreaSize;
static int NLabels;

int
main(int ac, char **av)
{
	uint32_t status;
	hammer2_off_t total_space;
	hammer2_off_t free_space;
	hammer2_off_t reserved_space;
	int ch;
	int fd = -1;
	int i;
	int defaultlabels = 1;
	char *vol_fsid = NULL;
	char *sup_clid_name = NULL;
	char *sup_fsid_name = NULL;
	char *pfs_clid_name = NULL;
	char *pfs_fsid_name = NULL;

	Label[NLabels++] = "LOCAL";

	/*
	 * Sanity check basic filesystem structures.  No cookies for us
	 * if it gets broken!
	 */
	assert(sizeof(hammer2_volume_data_t) == HAMMER2_VOLUME_BYTES);
	assert(sizeof(hammer2_inode_data_t) == HAMMER2_INODE_BYTES);
	assert(sizeof(hammer2_blockref_t) == HAMMER2_BLOCKREF_BYTES);

	/*
	 * Generate a filesystem id and lookup the filesystem type
	 */
	srandomdev();
	uuidgen(&Hammer2_VolFSID, 1);
	uuidgen(&Hammer2_SupCLID, 1);
	uuidgen(&Hammer2_SupFSID, 1);
	uuid_from_string(HAMMER2_UUID_STRING, &Hammer2_FSType, &status);
	/*uuid_name_lookup(&Hammer2_FSType, "DragonFly HAMMER2", &status);*/
	if (status != uuid_s_ok) {
		errx(1, "uuids file does not have the DragonFly "
			"HAMMER2 filesystem type");
	}

	/*
	 * Parse arguments
	 */
	while ((ch = getopt(ac, av, "L:b:r:V:")) != -1) {
		switch(ch) {
		case 'L':
			defaultlabels = 0;
			if (strcasecmp(optarg, "none") == 0) {
				break;
			}
			if (NLabels >= MAXLABELS) {
				errx(1, "Limit of %d local labels",
				     MAXLABELS - 1);
			}
			Label[NLabels++] = optarg;
			if (strlen(Label[NLabels-1]) > HAMMER2_INODE_MAXNAME) {
				errx(1, "Volume label '%s' is too long "
					"(64 chars max)\n", optarg);
			}
			break;
		case 'b':
			BootAreaSize = getsize(optarg,
					 HAMMER2_NEWFS_ALIGN,
					 HAMMER2_BOOT_MAX_BYTES, 2);
			break;
		case 'r':
			AuxAreaSize = getsize(optarg,
					 HAMMER2_NEWFS_ALIGN,
					 HAMMER2_AUX_MAX_BYTES, 2);
			break;
		case 'V':
			Hammer2Version = strtol(optarg, NULL, 0);
			if (Hammer2Version < HAMMER2_VOL_VERSION_MIN ||
			    Hammer2Version >= HAMMER2_VOL_VERSION_WIP) {
				errx(1,
				     "I don't understand how to format "
				     "HAMMER2 version %d\n",
				     Hammer2Version);
			}
			break;
		default:
			usage();
			break;
		}
	}

	/*
	 * Check Hammer2 version
	 */
	if (Hammer2Version < 0) {
		size_t olen = sizeof(Hammer2Version);
		Hammer2Version = HAMMER2_VOL_VERSION_DEFAULT;
		if (sysctlbyname("vfs.hammer2.supported_version",
				 &Hammer2Version, &olen, NULL, 0) == 0) {
			if (Hammer2Version >= HAMMER2_VOL_VERSION_WIP) {
				Hammer2Version = HAMMER2_VOL_VERSION_WIP - 1;
				fprintf(stderr,
					"newfs_hammer2: WARNING: HAMMER2 VFS "
					"supports higher version than I "
					"understand,\n"
					"using version %d\n",
					Hammer2Version);
			}
		} else {
			fprintf(stderr,
				"newfs_hammer2: WARNING: HAMMER2 VFS not "
				"loaded, cannot get version info.\n"
				"Using version %d\n",
				HAMMER2_VOL_VERSION_DEFAULT);
		}
	}

	ac -= optind;
	av += optind;

	if (ac != 1 || av[0][0] == 0) {
		fprintf(stderr, "Exactly one disk device must be specified\n");
		exit(1);
	}

	/*
	 * Adjust Label[] and NLabels.
	 */
	if (defaultlabels) {
		char c = av[0][strlen(av[0]) - 1];
		if (c == 'a')
			Label[NLabels++] = "BOOT";
		else if (c == 'd')
			Label[NLabels++] = "ROOT";
		else
			Label[NLabels++] = "DATA";
	}

	/*
	 * Collect volume information.
	 */
	total_space = check_volume(av[0], &fd);

	/*
	 * ~typically 8MB alignment to avoid edge cases for reserved blocks
	 * and so raid stripes (if any) operate efficiently.
	 */
	total_space &= ~HAMMER2_VOLUME_ALIGNMASK64;

	/*
	 * Calculate defaults for the boot area size and round to the
	 * volume alignment boundary.
	 *
	 * NOTE: These areas are currently not used for booting but are
	 *	 reserved for future filesystem expansion.
	 */
	if (BootAreaSize == 0) {
		BootAreaSize = HAMMER2_BOOT_NOM_BYTES;
		while (BootAreaSize > total_space / 20)
			BootAreaSize >>= 1;
		if (BootAreaSize < HAMMER2_BOOT_MIN_BYTES)
			BootAreaSize = HAMMER2_BOOT_MIN_BYTES;
	} else if (BootAreaSize < HAMMER2_BOOT_MIN_BYTES) {
		BootAreaSize = HAMMER2_BOOT_MIN_BYTES;
	}
	BootAreaSize = (BootAreaSize + HAMMER2_VOLUME_ALIGNMASK64) &
		        ~HAMMER2_VOLUME_ALIGNMASK64;

	/*
	 * Calculate defaults for the aux area size and round to the
	 * volume alignment boundary.
	 *
	 * NOTE: These areas are currently not used for logging but are
	 *	 reserved for future filesystem expansion.
	 */
	if (AuxAreaSize == 0) {
		AuxAreaSize = HAMMER2_AUX_NOM_BYTES;
		while (AuxAreaSize > total_space / 20)
			AuxAreaSize >>= 1;
		if (AuxAreaSize < HAMMER2_AUX_MIN_BYTES)
			AuxAreaSize = HAMMER2_AUX_MIN_BYTES;
	} else if (AuxAreaSize < HAMMER2_AUX_MIN_BYTES) {
		AuxAreaSize = HAMMER2_AUX_MIN_BYTES;
	}
	AuxAreaSize = (AuxAreaSize + HAMMER2_VOLUME_ALIGNMASK64) &
		       ~HAMMER2_VOLUME_ALIGNMASK64;

	/*
	 * We'll need to stuff this in the volume header soon.
	 */
	hammer2_uuid_to_str(&Hammer2_VolFSID, &vol_fsid);
	hammer2_uuid_to_str(&Hammer2_SupCLID, &sup_clid_name);
	hammer2_uuid_to_str(&Hammer2_SupFSID, &sup_fsid_name);

	/*
	 * Calculate the amount of reserved space.  HAMMER2_ZONE_SEG (4MB)
	 * is reserved at the beginning of every 1GB of storage, rounded up.
	 * Thus a 200MB filesystem will still have a 4MB reserve area.
	 *
	 * We also include the boot and aux areas in the reserve.  The
	 * reserve is used to help 'df' calculate the amount of available
	 * space.
	 *
	 * XXX I kinda screwed up and made the reserved area on the LEVEL1
	 *     boundary rather than the ZONE boundary.  LEVEL1 is on 1GB
	 *     boundaries rather than 2GB boundaries.  Stick with the LEVEL1
	 *     boundary.
	 */
	reserved_space = ((total_space + HAMMER2_FREEMAP_LEVEL1_MASK) /
			  HAMMER2_FREEMAP_LEVEL1_SIZE) * HAMMER2_ZONE_SEG64;

	free_space = total_space - reserved_space - BootAreaSize - AuxAreaSize;
	if ((int64_t)free_space < 0) {
		fprintf(stderr, "Not enough free space\n");
		exit(1);
	}

	format_hammer2(fd, total_space, free_space);
	fsync(fd);
	close(fd);

	printf("---------------------------------------------\n");
	printf("version:          %d\n", Hammer2Version);
	printf("total-size:       %s (%jd bytes)\n",
	       sizetostr(total_space),
	       (intmax_t)total_space);
	printf("boot-area-size:   %s\n", sizetostr(BootAreaSize));
	printf("aux-area-size:    %s\n", sizetostr(AuxAreaSize));
	printf("topo-reserved:    %s\n", sizetostr(reserved_space));
	printf("free-space:       %s\n", sizetostr(free_space));
	printf("vol-fsid:         %s\n", vol_fsid);
	printf("sup-clid:         %s\n", sup_clid_name);
	printf("sup-fsid:         %s\n", sup_fsid_name);
	for (i = 0; i < NLabels; ++i) {
		printf("PFS \"%s\"\n", Label[i]);
		hammer2_uuid_to_str(&Hammer2_PfsCLID[i], &pfs_clid_name);
		hammer2_uuid_to_str(&Hammer2_PfsFSID[i], &pfs_fsid_name);
		printf("    clid %s\n", pfs_clid_name);
		printf("    fsid %s\n", pfs_fsid_name);
	}

	free(vol_fsid);
	free(sup_clid_name);
	free(sup_fsid_name);
	free(pfs_clid_name);
	free(pfs_fsid_name);

	return(0);
}

static
void
usage(void)
{
	fprintf(stderr,
		"usage: newfs_hammer2 [-b bootsize] [-r auxsize] "
		"[-V version] [-L label ...] special\n"
	);
	exit(1);
}

/*
 * Convert a string to a 64 bit signed integer with various requirements.
 */
static int64_t
getsize(const char *str, int64_t minval, int64_t maxval, int powerof2)
{
	int64_t val;
	char *ptr;

	val = strtoll(str, &ptr, 0);
	switch(*ptr) {
	case 't':
	case 'T':
		val *= 1024;
		/* fall through */
	case 'g':
	case 'G':
		val *= 1024;
		/* fall through */
	case 'm':
	case 'M':
		val *= 1024;
		/* fall through */
	case 'k':
	case 'K':
		val *= 1024;
		break;
	default:
		errx(1, "Unknown suffix in number '%s'\n", str);
		/* not reached */
	}
	if (ptr[1]) {
		errx(1, "Unknown suffix in number '%s'\n", str);
		/* not reached */
	}
	if (val < minval) {
		errx(1, "Value too small: %s, min is %s\n",
		     str, sizetostr(minval));
		/* not reached */
	}
	if (val > maxval) {
		errx(1, "Value too large: %s, max is %s\n",
		     str, sizetostr(maxval));
		/* not reached */
	}
	if ((powerof2 & 1) && (val ^ (val - 1)) != ((val << 1) - 1)) {
		errx(1, "Value not power of 2: %s\n", str);
		/* not reached */
	}
	if ((powerof2 & 2) && (val & HAMMER2_NEWFS_ALIGNMASK)) {
		errx(1, "Value not an integral multiple of %dK: %s",
		     HAMMER2_NEWFS_ALIGN / 1024, str);
		/* not reached */
	}
	return(val);
}

static uint64_t
nowtime(void)
{
	struct timeval tv;
	uint64_t xtime;

	gettimeofday(&tv, NULL);
	xtime = tv.tv_sec * 1000000LL + tv.tv_usec;
	return(xtime);
}

/*
 * Figure out how big the volume is.
 */
static
hammer2_off_t
check_volume(const char *path, int *fdp)
{
	struct partinfo pinfo;
	struct stat st;
	hammer2_off_t size;

	/*
	 * Get basic information about the volume
	 */
	*fdp = open(path, O_RDWR);
	if (*fdp < 0)
		err(1, "Unable to open %s R+W", path);
	if (ioctl(*fdp, DIOCGPART, &pinfo) < 0) {
		/*
		 * Allow the formatting of regular files as HAMMER2 volumes
		 */
		if (fstat(*fdp, &st) < 0)
			err(1, "Unable to stat %s", path);
		if (!S_ISREG(st.st_mode))
			errx(1, "Unsupported file type for %s", path);
		size = st.st_size;
	} else {
		/*
		 * When formatting a block device as a HAMMER2 volume the
		 * sector size must be compatible.  HAMMER2 uses 64K
		 * filesystem buffers but logical buffers for direct I/O
		 * can be as small as HAMMER2_LOGSIZE (16KB).
		 */
		if (pinfo.reserved_blocks) {
			errx(1, "HAMMER2 cannot be placed in a partition "
				"which overlaps the disklabel or MBR");
		}
		if (pinfo.media_blksize > HAMMER2_PBUFSIZE ||
		    HAMMER2_PBUFSIZE % pinfo.media_blksize) {
			errx(1, "A media sector size of %d is not supported",
			     pinfo.media_blksize);
		}
		size = pinfo.media_size;
	}
	printf("Volume %-15s size %s\n", path, sizetostr(size));
	return (size);
}

/*
 * Create the volume header, the super-root directory inode, and
 * the writable snapshot subdirectory (named via the label) which
 * is to be the initial mount point, or at least the first mount point.
 * newfs_hammer2 doesn't format the freemap bitmaps for these.
 *
 * 0                      4MB
 * [----reserved_area----][boot_area][aux_area]
 * [[vol_hdr][freemap]...]                     [sroot][root][root]...
 *     \                                        ^\     ^     ^
 *      \--------------------------------------/  \---/-----/---...
 *
 * NOTE: The passed total_space is 8MB-aligned to avoid edge cases.
 */
static
void
format_hammer2(int fd, hammer2_off_t total_space, hammer2_off_t free_space)
{
	char *buf = malloc(HAMMER2_PBUFSIZE);
	hammer2_volume_data_t *vol;
	hammer2_inode_data_t *rawip;
	hammer2_blockref_t sroot_blockref;
	hammer2_blockref_t root_blockref[MAXLABELS];
	uint64_t now;
	hammer2_off_t volu_base = 0;
	hammer2_off_t boot_base = HAMMER2_ZONE_SEG;
	hammer2_off_t aux_base = boot_base + BootAreaSize;
	hammer2_off_t alloc_base = aux_base + AuxAreaSize;
	hammer2_off_t tmp_base;
	size_t n;
	int i;

	/*
	 * Clear the entire reserve for the first 2G segment and
	 * make sure we can write to the last block.
	 */
	bzero(buf, HAMMER2_PBUFSIZE);
	tmp_base = volu_base;
	for (i = 0; i < HAMMER2_ZONE_BLOCKS_SEG; ++i) {
		n = pwrite(fd, buf, HAMMER2_PBUFSIZE, tmp_base);
		if (n != HAMMER2_PBUFSIZE) {
			perror("write");
			exit(1);
		}
		tmp_base += HAMMER2_PBUFSIZE;
	}

	n = pwrite(fd, buf, HAMMER2_PBUFSIZE,
		   volu_base + total_space - HAMMER2_PBUFSIZE);
	if (n != HAMMER2_PBUFSIZE) {
		perror("write (at-end-of-volume)");
		exit(1);
	}

	/*
	 * Make sure alloc_base won't cross the reserved area at the
	 * beginning of each 1GB.
	 *
	 * Reserve space for the super-root inode and the root inode.
	 * Make sure they are in the same 64K block to simplify our code.
	 */
	assert((alloc_base & HAMMER2_PBUFMASK) == 0);
	assert(alloc_base < HAMMER2_FREEMAP_LEVEL1_SIZE);

	/*
	 * Clear the boot/aux area.
	 */
	for (tmp_base = boot_base; tmp_base < alloc_base;
	     tmp_base += HAMMER2_PBUFSIZE) {
		n = pwrite(fd, buf, HAMMER2_PBUFSIZE, tmp_base);
		if (n != HAMMER2_PBUFSIZE) {
			perror("write (boot/aux)");
			exit(1);
		}
	}

	now = nowtime();
	alloc_base &= ~HAMMER2_PBUFMASK64;
	alloc_direct(&alloc_base, &sroot_blockref, HAMMER2_INODE_BYTES);

	for (i = 0; i < NLabels; ++i) {
		uuidgen(&Hammer2_PfsCLID[i], 1);
		uuidgen(&Hammer2_PfsFSID[i], 1);

		alloc_direct(&alloc_base, &root_blockref[i],
			     HAMMER2_INODE_BYTES);
		assert(((sroot_blockref.data_off ^ root_blockref[i].data_off) &
			~HAMMER2_PBUFMASK64) == 0);

		/*
		 * Format the root directory inode, which is left empty.
		 */
		rawip = (void *)(buf + (HAMMER2_OFF_MASK_LO &
					root_blockref[i].data_off));
		rawip->meta.version = HAMMER2_INODE_VERSION_ONE;
		rawip->meta.ctime = now;
		rawip->meta.mtime = now;
		/* rawip->atime = now; NOT IMPL MUST BE ZERO */
		rawip->meta.btime = now;
		rawip->meta.type = HAMMER2_OBJTYPE_DIRECTORY;
		rawip->meta.mode = 0755;
		rawip->meta.inum = 1;	/* root inode, inumber 1 */
		rawip->meta.nlinks = 1;	/* directory link count compat */

		rawip->meta.name_len = strlen(Label[i]);
		bcopy(Label[i], rawip->filename, rawip->meta.name_len);
		rawip->meta.name_key =
				dirhash(rawip->filename, rawip->meta.name_len);

		/*
		 * Compression mode and supported copyids.
		 *
		 * Do not allow compression when creating any "BOOT" label
		 * (pfs-create also does the same if the pfs is named "BOOT")
		 */
		if (strcasecmp(Label[i], "BOOT") == 0) {
			rawip->meta.comp_algo = HAMMER2_ENC_ALGO(
						    HAMMER2_COMP_AUTOZERO);
			rawip->meta.check_algo = HAMMER2_ENC_ALGO(
						    HAMMER2_CHECK_XXHASH64);
		} else {
			rawip->meta.comp_algo = HAMMER2_ENC_ALGO(
						    HAMMER2_COMP_NEWFS_DEFAULT);
			rawip->meta.check_algo = HAMMER2_ENC_ALGO(
						    HAMMER2_CHECK_XXHASH64);
		}

		/*
		 * NOTE: We leave nmasters set to 0, which means that we
		 *	 don't know how many masters there are.  The quorum
		 *	 calculation will effectively be 1 ( 0 / 2 + 1 ).
		 */
		rawip->meta.pfs_clid = Hammer2_PfsCLID[i];
		rawip->meta.pfs_fsid = Hammer2_PfsFSID[i];
		rawip->meta.pfs_type = HAMMER2_PFSTYPE_MASTER;
		rawip->meta.op_flags |= HAMMER2_OPFLAG_PFSROOT;

		/* first allocatable inode number */
		rawip->meta.pfs_inum = 16;

		/* rawip->u.blockset is left empty */

		/*
		 * The root blockref will be stored in the super-root inode as
		 * the only directory entry.  The copyid here is the actual
		 * copyid of the storage ref.
		 *
		 * The key field for a directory entry's blockref is
		 * essentially the name key for the entry.
		 */
		root_blockref[i].key = rawip->meta.name_key;
		root_blockref[i].copyid = HAMMER2_COPYID_LOCAL;
		root_blockref[i].keybits = 0;
		root_blockref[i].check.xxhash64.value =
				XXH64(rawip, sizeof(*rawip), XXH_HAMMER2_SEED);
		root_blockref[i].type = HAMMER2_BREF_TYPE_INODE;
		root_blockref[i].methods =
				HAMMER2_ENC_CHECK(HAMMER2_CHECK_XXHASH64) |
				HAMMER2_ENC_COMP(HAMMER2_COMP_NONE);
		root_blockref[i].mirror_tid = 16;
		root_blockref[i].flags = HAMMER2_BREF_FLAG_PFSROOT;
	}

	/*
	 * Format the super-root directory inode, giving it one directory
	 * entry (root_blockref) and fixup the icrc method.
	 *
	 * The superroot contains one directory entry pointing at the root
	 * inode (named via the label).  Inodes contain one blockset which
	 * is fully associative so we can put the entry anywhere without
	 * having to worry about the hash.  Use index 0.
	 */
	rawip = (void *)(buf + (HAMMER2_OFF_MASK_LO & sroot_blockref.data_off));
	rawip->meta.version = HAMMER2_INODE_VERSION_ONE;
	rawip->meta.ctime = now;
	rawip->meta.mtime = now;
	/* rawip->meta.atime = now; NOT IMPL MUST BE ZERO */
	rawip->meta.btime = now;
	rawip->meta.type = HAMMER2_OBJTYPE_DIRECTORY;
	rawip->meta.mode = 0700;	/* super-root - root only */
	rawip->meta.inum = 0;		/* super root inode, inumber 0 */
	rawip->meta.nlinks = 2;		/* directory link count compat */

	rawip->meta.name_len = 0;	/* super-root is unnamed */
	rawip->meta.name_key = 0;

	rawip->meta.comp_algo = HAMMER2_ENC_ALGO(HAMMER2_COMP_AUTOZERO);
	rawip->meta.check_algo = HAMMER2_ENC_ALGO(HAMMER2_CHECK_XXHASH64);

	/*
	 * The super-root is flagged as a PFS and typically given its own
	 * random FSID, making it possible to mirror an entire HAMMER2 disk
	 * snapshots and all if desired.  PFS ids are used to match up
	 * mirror sources and targets and cluster copy sources and targets.
	 *
	 * (XXX whole-disk logical mirroring is not really supported in
	 *  the first attempt because each PFS is in its own modify/mirror
	 *  transaction id domain, so normal mechanics cannot cross a PFS
	 *  boundary).
	 */
	rawip->meta.pfs_clid = Hammer2_SupCLID;
	rawip->meta.pfs_fsid = Hammer2_SupFSID;
	rawip->meta.pfs_type = HAMMER2_PFSTYPE_SUPROOT;
	snprintf((char*)rawip->filename, sizeof(rawip->filename), "SUPROOT");
	rawip->meta.name_key = 0;
	rawip->meta.name_len = strlen((char*)rawip->filename);

	/* The super-root has an inode number of 0 */
	rawip->meta.pfs_inum = 0;

	/*
	 * Currently newfs_hammer2 just throws the PFS inodes into the
	 * top-level block table at the volume root and doesn't try to
	 * create an indirect block, so we are limited to ~4 at filesystem
	 * creation time.  More can be added after mounting.
	 */
	qsort(root_blockref, NLabels, sizeof(root_blockref[0]), blkrefary_cmp);
	for (i = 0; i < NLabels; ++i)
		rawip->u.blockset.blockref[i] = root_blockref[i];

	/*
	 * The sroot blockref will be stored in the volume header.
	 */
	sroot_blockref.copyid = HAMMER2_COPYID_LOCAL;
	sroot_blockref.keybits = 0;
	sroot_blockref.check.xxhash64.value =
				XXH64(rawip, sizeof(*rawip), XXH_HAMMER2_SEED);
	sroot_blockref.type = HAMMER2_BREF_TYPE_INODE;
	sroot_blockref.methods = HAMMER2_ENC_CHECK(HAMMER2_CHECK_XXHASH64) |
			         HAMMER2_ENC_COMP(HAMMER2_COMP_AUTOZERO);
	sroot_blockref.mirror_tid = 16;
	rawip = NULL;

	/*
	 * Write out the 64K HAMMER2 block containing the root and sroot.
	 */
	assert((sroot_blockref.data_off & ~HAMMER2_PBUFMASK64) ==
		((alloc_base - 1) & ~HAMMER2_PBUFMASK64));
	n = pwrite(fd, buf, HAMMER2_PBUFSIZE,
		   sroot_blockref.data_off & ~HAMMER2_PBUFMASK64);
	if (n != HAMMER2_PBUFSIZE) {
		perror("write");
		exit(1);
	}

	/*
	 * Format the volume header.
	 *
	 * The volume header points to sroot_blockref.  Also be absolutely
	 * sure that allocator_beg is set.
	 */
	assert(HAMMER2_VOLUME_BYTES <= HAMMER2_PBUFSIZE);
	bzero(buf, HAMMER2_PBUFSIZE);
	vol = (void *)buf;

	vol->magic = HAMMER2_VOLUME_ID_HBO;
	vol->boot_beg = boot_base;
	vol->boot_end = boot_base + BootAreaSize;
	vol->aux_beg = aux_base;
	vol->aux_end = aux_base + AuxAreaSize;
	vol->volu_size = total_space;
	vol->version = Hammer2Version;
	vol->flags = 0;

	vol->fsid = Hammer2_VolFSID;
	vol->fstype = Hammer2_FSType;

	vol->peer_type = DMSG_PEER_HAMMER2;	/* LNK_CONN identification */

	vol->allocator_size = free_space;
	vol->allocator_free = free_space;
	vol->allocator_beg = alloc_base;

	vol->sroot_blockset.blockref[0] = sroot_blockref;
	vol->mirror_tid = 16;	/* all blockref mirror TIDs set to 16 */
	vol->freemap_tid = 16;	/* all blockref mirror TIDs set to 16 */
	vol->icrc_sects[HAMMER2_VOL_ICRC_SECT1] =
			hammer2_icrc32((char *)vol + HAMMER2_VOLUME_ICRC1_OFF,
				       HAMMER2_VOLUME_ICRC1_SIZE);

	/*
	 * Set ICRC_SECT0 after all remaining elements of sect0 have been
	 * populated in the volume header.  Note hat ICRC_SECT* (except for
	 * SECT0) are part of sect0.
	 */
	vol->icrc_sects[HAMMER2_VOL_ICRC_SECT0] =
			hammer2_icrc32((char *)vol + HAMMER2_VOLUME_ICRC0_OFF,
				       HAMMER2_VOLUME_ICRC0_SIZE);
	vol->icrc_volheader =
			hammer2_icrc32((char *)vol + HAMMER2_VOLUME_ICRCVH_OFF,
				       HAMMER2_VOLUME_ICRCVH_SIZE);

	/*
	 * Write the volume header and all alternates.
	 */
	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		if (i * HAMMER2_ZONE_BYTES64 >= total_space)
			break;
		n = pwrite(fd, buf, HAMMER2_PBUFSIZE,
			   volu_base + i * HAMMER2_ZONE_BYTES64);
		if (n != HAMMER2_PBUFSIZE) {
			perror("write");
			exit(1);
		}
	}

	/*
	 * Cleanup
	 */
	free(buf);
}

static void
alloc_direct(hammer2_off_t *basep, hammer2_blockref_t *bref, size_t bytes)
{
	int radix;

	radix = 0;
	assert(bytes);
	while ((bytes & 1) == 0) {
		bytes >>= 1;
		++radix;
	}
	assert(bytes == 1);
	if (radix < HAMMER2_RADIX_MIN)
		radix = HAMMER2_RADIX_MIN;

	bzero(bref, sizeof(*bref));
	bref->data_off = *basep | radix;
	bref->vradix = radix;

	*basep += 1U << radix;
}

static int
blkrefary_cmp(const void *b1, const void *b2)
{
	const hammer2_blockref_t *bref1 = b1;
	const hammer2_blockref_t *bref2 = b2;

	if (bref1->key < bref2->key)
		return(-1);
	if (bref1->key > bref2->key)
		return(1);
	return 0;
}
