/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#include "newfs_hammer.h"

static int64_t getsize(const char *str, int64_t minval, int64_t maxval, int pw);
static const char *sizetostr(off_t size);
static void trim_volume(struct volume_info *vol);
static void check_volume(struct volume_info *vol);
static void format_volume(struct volume_info *vol, int nvols,const char *label,
			off_t total_size);
static hammer_off_t format_root(const char *label);
static u_int64_t nowtime(void);
static void usage(void);

static int ForceOpt = 0;
static int HammerVersion = -1;
static int Eflag = 0;

#define GIG	(1024LL*1024*1024)

int
main(int ac, char **av)
{
	u_int32_t status;
	off_t total;
	int ch;
	int i;
	const char *label = NULL;
	struct volume_info *vol;
	char *fsidstr;

	/*
	 * Sanity check basic filesystem structures.  No cookies for us
	 * if it gets broken!
	 */
	assert(sizeof(struct hammer_volume_ondisk) <= HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_blockmap_layer1) == 32);
	assert(sizeof(struct hammer_blockmap_layer2) == 16);

	/*
	 * Generate a filesystem id and lookup the filesystem type
	 */
	uuidgen(&Hammer_FSId, 1);
	uuid_name_lookup(&Hammer_FSType, "DragonFly HAMMER", &status);
	if (status != uuid_s_ok) {
		errx(1, "uuids file does not have the DragonFly "
			"HAMMER filesystem type");
	}

	/*
	 * Parse arguments
	 */
	while ((ch = getopt(ac, av, "fEL:b:m:u:V:")) != -1) {
		switch(ch) {
		case 'f':
			ForceOpt = 1;
			break;
		case 'E':
			Eflag = 1;
			break;
		case 'L':
			label = optarg;
			break;
		case 'b':
			BootAreaSize = getsize(optarg,
					 HAMMER_BUFSIZE,
					 HAMMER_BOOT_MAXBYTES, 2);
			break;
		case 'm':
			MemAreaSize = getsize(optarg,
					 HAMMER_BUFSIZE,
					 HAMMER_MEM_MAXBYTES, 2);
			break;
		case 'u':
			UndoBufferSize = getsize(optarg,
					 HAMMER_LARGEBLOCK_SIZE,
					 HAMMER_LARGEBLOCK_SIZE *
					 HAMMER_UNDO_LAYER2, 2);
			if (UndoBufferSize < 500*1024*1024 && ForceOpt == 0)
				errx(1, "The minimum UNDO/REDO FIFO size is "
					"500MB\n");
			if (UndoBufferSize < 500*1024*1024) {
				fprintf(stderr,
					"WARNING: you have specified an "
					"UNDO/REDO FIFO size less than 500MB,\n"
					"which may lead to VFS panics.\n");
			}
			break;
		case 'V':
			HammerVersion = strtol(optarg, NULL, 0);
			if (HammerVersion < HAMMER_VOL_VERSION_MIN ||
			    HammerVersion >= HAMMER_VOL_VERSION_WIP) {
				errx(1,
				     "I don't understand how to format "
				     "HAMMER version %d\n",
				     HammerVersion);
			}
			break;
		default:
			usage();
			break;
		}
	}

	if (label == NULL) {
		fprintf(stderr,
			"newfs_hammer: A filesystem label must be specified\n");
		usage();
	}

	if (HammerVersion < 0) {
		size_t olen = sizeof(HammerVersion);
		HammerVersion = HAMMER_VOL_VERSION_DEFAULT;
		if (sysctlbyname("vfs.hammer.supported_version",
				 &HammerVersion, &olen, NULL, 0) == 0) {
			if (HammerVersion >= HAMMER_VOL_VERSION_WIP) {
				HammerVersion = HAMMER_VOL_VERSION_WIP - 1;
				fprintf(stderr,
					"newfs_hammer: WARNING: HAMMER VFS "
					"supports higher version than I "
					"understand,\n"
					"using version %d\n",
					HammerVersion);
			}
		} else {
			fprintf(stderr,
				"newfs_hammer: WARNING: HAMMER VFS not "
				"loaded, cannot get version info.\n"
				"Using version %d\n",
				HAMMER_VOL_VERSION_DEFAULT);
		}
	}

	/*
	 * Collect volume information
	 */
	ac -= optind;
	av += optind;
	NumVolumes = ac;
	RootVolNo = 0;

        if (NumVolumes == 0) {
                fprintf(stderr,
                        "newfs_hammer: You must specify at least one special file (volume)\n");
                exit(1);
        }

	if (NumVolumes > HAMMER_MAX_VOLUMES) {
                fprintf(stderr,
                        "newfs_hammer: The maximum number of volumes is %d\n", HAMMER_MAX_VOLUMES);
		exit(1);
	}

	total = 0;
	for (i = 0; i < NumVolumes; ++i) {
		vol = setup_volume(i, av[i], 1, O_RDWR);

		/*
		 * Load up information on the volume and initialize
		 * its remaining fields.
		 */
		check_volume(vol);
		if (Eflag) {
			char sysctl_name[64];
			int trim_enabled = 0;
			size_t olen = sizeof(trim_enabled);
			char *dev_name = strdup(vol->name);
			dev_name = strtok(dev_name + strlen("/dev/da"),"s");

			sprintf(sysctl_name, "kern.cam.da.%s.trim_enabled",
			    dev_name);
			errno=0;
			sysctlbyname(sysctl_name, &trim_enabled, &olen, NULL, 0);
			if(errno == ENOENT) {
				printf("Device:%s (%s) does not support the "
				    "TRIM command\n", vol->name,sysctl_name);
				usage();
			}
			if(!trim_enabled) {
				printf("Erase device option selected, but "
				    "sysctl (%s) is not enabled\n", sysctl_name);
				usage();

			}
			trim_volume(vol);
		}
		total += vol->size;
	}

	/*
	 * Calculate defaults for the boot and memory area sizes.
	 */
	if (BootAreaSize == 0) {
		BootAreaSize = HAMMER_BOOT_NOMBYTES;
		while (BootAreaSize > total / NumVolumes / HAMMER_MAX_VOLUMES)
			BootAreaSize >>= 1;
		if (BootAreaSize < HAMMER_BOOT_MINBYTES)
			BootAreaSize = 0;
	} else if (BootAreaSize < HAMMER_BOOT_MINBYTES) {
		BootAreaSize = HAMMER_BOOT_MINBYTES;
	}
	if (MemAreaSize == 0) {
		MemAreaSize = HAMMER_MEM_NOMBYTES;
		while (MemAreaSize > total / NumVolumes / HAMMER_MAX_VOLUMES)
			MemAreaSize >>= 1;
		if (MemAreaSize < HAMMER_MEM_MINBYTES)
			MemAreaSize = 0;
	} else if (MemAreaSize < HAMMER_MEM_MINBYTES) {
		MemAreaSize = HAMMER_MEM_MINBYTES;
	}

	/*
	 * Format the volumes.  Format the root volume first so we can
	 * bootstrap the freemap.
	 */
	format_volume(get_volume(RootVolNo), NumVolumes, label, total);
	for (i = 0; i < NumVolumes; ++i) {
		if (i != RootVolNo)
			format_volume(get_volume(i), NumVolumes, label, total);
	}

	/*
	 * Pre-size the blockmap layer1/layer2 infrastructure to the zone
	 * limit.  If we do this the filesystem does not have to allocate
	 * new layer2 blocks which reduces the chances of the reblocker
	 * having to fallback to an extremely inefficient algorithm.
	 */
	vol = get_volume(RootVolNo);
	vol->ondisk->vol0_stat_bigblocks = vol->ondisk->vol0_stat_freebigblocks;
	vol->cache.modified = 1;
	uuid_to_string(&Hammer_FSId, &fsidstr, &status);

	printf("---------------------------------------------\n");
	printf("%d volume%s total size %s version %d\n",
		NumVolumes, (NumVolumes == 1 ? "" : "s"),
		sizetostr(total), HammerVersion);
	printf("boot-area-size:      %s\n", sizetostr(BootAreaSize));
	printf("memory-log-size:     %s\n", sizetostr(MemAreaSize));
	printf("undo-buffer-size:    %s\n", sizetostr(UndoBufferSize));
	printf("total-pre-allocated: %s\n",
		sizetostr(vol->vol_free_off & HAMMER_OFF_SHORT_MASK));
	printf("fsid:                %s\n", fsidstr);
	printf("\n");
	printf("NOTE: Please remember that you may have to manually set up a\n"
		"cron(8) job to prune and reblock the filesystem regularly.\n"
		"By default, the system automatically runs 'hammer cleanup'\n"
		"on a nightly basis.  The periodic.conf(5) variable\n"
		"'daily_clean_hammer_enable' can be unset to disable this.\n"
		"Also see 'man hammer' and 'man HAMMER' for more information.\n");
	if (total < 10*GIG) {
		printf("\nWARNING: The minimum UNDO/REDO FIFO is 500MB, you "
		       "really should not\n"
		       "try to format a HAMMER filesystem this small.\n");
	}
	if (total < 50*GIG) {
		printf("\nWARNING: HAMMER filesystems less than 50GB are "
			"not recommended!\n"
			"You may have to run 'hammer prune-everything' and "
			"'hammer reblock'\n"
			"quite often, even if using a nohistory mount.\n");
	}
	flush_all_volumes();
	return(0);
}

static
void
usage(void)
{
	fprintf(stderr,
		"usage: newfs_hammer -L label [-Ef] [-b bootsize] [-m savesize] [-u undosize]\n"
		"                    [-V version] special ...\n"
	);
	exit(1);
}

/*
 * Convert the size in bytes to a human readable string.
 */
static
const char *
sizetostr(off_t size)
{
	static char buf[32];

	if (size < 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2f", (double)size);
	} else if (size < 1024 * 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2fKB",
			(double)size / 1024);
	} else if (size < 1024 * 1024 * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fMB",
			(double)size / (1024 * 1024));
	} else if (size < 1024 * 1024 * 1024LL * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fGB",
			(double)size / (1024 * 1024 * 1024LL));
	} else {
		snprintf(buf, sizeof(buf), "%6.2fTB",
			(double)size / (1024 * 1024 * 1024LL * 1024LL));
	}
	return(buf);
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
	if ((powerof2 & 2) && (val & HAMMER_BUFMASK)) {
		errx(1, "Value not an integral multiple of %dK: %s",
		     HAMMER_BUFSIZE / 1024, str);
		/* not reached */
	}
	return(val);
}

/*
 * Generate a transaction id.  Transaction ids are no longer time-based.
 * Put the nail in the coffin by not making the first one time-based.
 *
 * We could start at 1 here but start at 2^32 to reserve a small domain for
 * possible future use.
 */
static hammer_tid_t
createtid(void)
{
	static hammer_tid_t lasttid;

	if (lasttid == 0)
		lasttid = 0x0000000100000000ULL;
	return(lasttid++);
}

static u_int64_t
nowtime(void)
{
	struct timeval tv;
	u_int64_t xtime;

	gettimeofday(&tv, NULL);
	xtime = tv.tv_sec * 1000000LL + tv.tv_usec;
	return(xtime);
}

/*
 * TRIM the volume, but only if the backing store is a DEVICE
 */
static
void
trim_volume(struct volume_info *vol)
{
	if (strncmp(vol->type, "DEVICE", sizeof(vol->type)) == 0) {
		off_t ioarg[2];
		
		/* 1MB offset to prevent destroying disk-reserved area */
		ioarg[0] = vol->device_offset;
		ioarg[1] = vol->size;
		printf("Trimming Device:%s, sectors (%llu -%llu)\n",vol->name,
		    (unsigned long long)ioarg[0]/512,
		    (unsigned long long)ioarg[1]/512);
		if (ioctl(vol->fd, IOCTLTRIM, ioarg) < 0) {
			printf("Device trim failed\n");
			usage ();
		}
	}
}

/*
 * Check basic volume characteristics.  HAMMER filesystems use a minimum
 * of a 16KB filesystem buffer size.
 */
static
void
check_volume(struct volume_info *vol)
{
	struct partinfo pinfo;
	struct stat st;

	/*
	 * Get basic information about the volume
	 */
	vol->fd = open(vol->name, O_RDWR);
	if (vol->fd < 0)
		err(1, "Unable to open %s R+W", vol->name);
	if (ioctl(vol->fd, DIOCGPART, &pinfo) < 0) {
		/*
		 * Allow the formatting of regular files as HAMMER volumes
		 */
		if (fstat(vol->fd, &st) < 0)
			err(1, "Unable to stat %s", vol->name);
		vol->size = st.st_size;
		vol->type = "REGFILE";

		if (Eflag)
			errx(1,"Cannot TRIM regular file %s\n", vol->name);

	} else {
		/*
		 * When formatting a block device as a HAMMER volume the
		 * sector size must be compatible.  HAMMER uses 16384 byte
		 * filesystem buffers.
		 */
		if (pinfo.reserved_blocks) {
			errx(1, "HAMMER cannot be placed in a partition "
				"which overlaps the disklabel or MBR");
		}
		if (pinfo.media_blksize > 16384 ||
		    16384 % pinfo.media_blksize) {
			errx(1, "A media sector size of %d is not supported",
			     pinfo.media_blksize);
		}

		vol->size = pinfo.media_size;
		vol->device_offset = pinfo.media_offset;
		vol->type = "DEVICE";
	}
	printf("Volume %d %s %-15s size %s\n",
	       vol->vol_no, vol->type, vol->name,
	       sizetostr(vol->size));

	/*
	 * Reserve space for (future) header junk, setup our poor-man's
	 * bigblock allocator.
	 */
	vol->vol_alloc = HAMMER_BUFSIZE * 16;
}

/*
 * Format a HAMMER volume.  Cluster 0 will be initially placed in volume 0.
 */
static
void
format_volume(struct volume_info *vol, int nvols, const char *label,
	      off_t total_size __unused)
{
	struct volume_info *root_vol;
	struct hammer_volume_ondisk *ondisk;
	int64_t freeblks;
	int64_t freebytes;
	int i;

	/*
	 * Initialize basic information in the on-disk volume structure.
	 */
	ondisk = vol->ondisk;

	ondisk->vol_fsid = Hammer_FSId;
	ondisk->vol_fstype = Hammer_FSType;
	snprintf(ondisk->vol_name, sizeof(ondisk->vol_name), "%s", label);
	ondisk->vol_no = vol->vol_no;
	ondisk->vol_count = nvols;
	ondisk->vol_version = HammerVersion;

	ondisk->vol_bot_beg = vol->vol_alloc;
	vol->vol_alloc += BootAreaSize;
	ondisk->vol_mem_beg = vol->vol_alloc;
	vol->vol_alloc += MemAreaSize;

	/*
	 * The remaining area is the zone 2 buffer allocation area.
	 */
	ondisk->vol_buf_beg = vol->vol_alloc;
	ondisk->vol_buf_end = vol->size & ~(int64_t)HAMMER_BUFMASK;

	if (ondisk->vol_buf_end < ondisk->vol_buf_beg) {
		errx(1, "volume %d %s is too small to hold the volume header",
		     vol->vol_no, vol->name);
	}

	ondisk->vol_nblocks = (ondisk->vol_buf_end - ondisk->vol_buf_beg) /
			      HAMMER_BUFSIZE;
	ondisk->vol_blocksize = HAMMER_BUFSIZE;

	ondisk->vol_rootvol = RootVolNo;
	ondisk->vol_signature = HAMMER_FSBUF_VOLUME;

	vol->vol_free_off = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	vol->vol_free_end = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no,
				(ondisk->vol_buf_end - ondisk->vol_buf_beg) &
				~HAMMER_LARGEBLOCK_MASK64);

	/*
	 * Format the root volume.
	 */
	if (vol->vol_no == RootVolNo) {
		/*
		 * Starting TID
		 */
		ondisk->vol0_next_tid = createtid();

		format_freemap(vol,
			&ondisk->vol0_blockmap[HAMMER_ZONE_FREEMAP_INDEX]);

		freeblks = initialize_freemap(vol);
		ondisk->vol0_stat_freebigblocks = freeblks;

		freebytes = freeblks * HAMMER_LARGEBLOCK_SIZE64;
		if (freebytes < 10*GIG && ForceOpt == 0) {
			errx(1, "Cannot create a HAMMER filesystem less than "
				"10GB unless you use -f.  HAMMER filesystems\n"
				"less than 50GB are not recommended\n");
		}
			
		for (i = 8; i < HAMMER_MAX_ZONES; ++i) {
			format_blockmap(&ondisk->vol0_blockmap[i],
					HAMMER_ZONE_ENCODE(i, 0));
		}
		format_undomap(ondisk);

		ondisk->vol0_btree_root = format_root(label);
		++ondisk->vol0_stat_inodes;	/* root inode */
	} else {
		freeblks = initialize_freemap(vol);
		root_vol = get_volume(RootVolNo);
		root_vol->cache.modified = 1;
		root_vol->ondisk->vol0_stat_freebigblocks += freeblks;
		root_vol->ondisk->vol0_stat_bigblocks += freeblks;
		rel_volume(root_vol);
	}
}

/*
 * Format the root directory.
 */
static
hammer_off_t
format_root(const char *label)
{
	hammer_off_t btree_off;
	hammer_off_t pfsd_off;
	hammer_off_t data_off;
	hammer_tid_t create_tid;
	hammer_node_ondisk_t bnode;
	struct hammer_inode_data *idata;
	hammer_pseudofs_data_t pfsd;
	struct buffer_info *data_buffer1 = NULL;
	struct buffer_info *data_buffer2 = NULL;
	hammer_btree_elm_t elm;
	u_int64_t xtime;

	bnode = alloc_btree_element(&btree_off);
	idata = alloc_data_element(&data_off, sizeof(*idata), &data_buffer1);
	pfsd = alloc_data_element(&pfsd_off, sizeof(*pfsd), &data_buffer2);
	create_tid = createtid();
	xtime = nowtime();

	/*
	 * Populate the inode data and inode record for the root directory.
	 */
	idata->version = HAMMER_INODE_DATA_VERSION;
	idata->mode = 0755;
	idata->ctime = xtime;
	idata->mtime = xtime;
	idata->atime = xtime;
	idata->obj_type = HAMMER_OBJTYPE_DIRECTORY;
	idata->size = 0;
	idata->nlinks = 1;
	if (HammerVersion >= HAMMER_VOL_VERSION_TWO)
		idata->cap_flags |= HAMMER_INODE_CAP_DIR_LOCAL_INO;
	if (HammerVersion >= HAMMER_VOL_VERSION_SIX)
		idata->cap_flags |= HAMMER_INODE_CAP_DIRHASH_ALG1;

	pfsd->sync_low_tid = 1;
	pfsd->sync_beg_tid = 0;
	pfsd->sync_end_tid = 0;	/* overriden by vol0_next_tid on pfs0 */
	pfsd->shared_uuid = Hammer_FSId;
	pfsd->unique_uuid = Hammer_FSId;
	pfsd->reserved01 = 0;
	pfsd->mirror_flags = 0;
	snprintf(pfsd->label, sizeof(pfsd->label), "%s", label);

	/*
	 * Create the root of the B-Tree.  The root is a leaf node so we
	 * do not have to worry about boundary elements.
	 */
	bnode->signature = HAMMER_BTREE_SIGNATURE_GOOD;
	bnode->count = 2;
	bnode->type = HAMMER_BTREE_TYPE_LEAF;

	elm = &bnode->elms[0];
	elm->leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	elm->leaf.base.localization = HAMMER_LOCALIZE_INODE;
	elm->leaf.base.obj_id = HAMMER_OBJID_ROOT;
	elm->leaf.base.key = 0;
	elm->leaf.base.create_tid = create_tid;
	elm->leaf.base.delete_tid = 0;
	elm->leaf.base.rec_type = HAMMER_RECTYPE_INODE;
	elm->leaf.base.obj_type = HAMMER_OBJTYPE_DIRECTORY;
	elm->leaf.create_ts = (u_int32_t)time(NULL);

	elm->leaf.data_offset = data_off;
	elm->leaf.data_len = sizeof(*idata);
	elm->leaf.data_crc = crc32(idata, HAMMER_INODE_CRCSIZE);

	elm = &bnode->elms[1];
	elm->leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	elm->leaf.base.localization = HAMMER_LOCALIZE_MISC;
	elm->leaf.base.obj_id = HAMMER_OBJID_ROOT;
	elm->leaf.base.key = 0;
	elm->leaf.base.create_tid = create_tid;
	elm->leaf.base.delete_tid = 0;
	elm->leaf.base.rec_type = HAMMER_RECTYPE_PFS;
	elm->leaf.base.obj_type = 0;
	elm->leaf.create_ts = (u_int32_t)time(NULL);

	elm->leaf.data_offset = pfsd_off;
	elm->leaf.data_len = sizeof(*pfsd);
	elm->leaf.data_crc = crc32(pfsd, sizeof(*pfsd));

	bnode->crc = crc32(&bnode->crc + 1, HAMMER_BTREE_CRCSIZE);

	return(btree_off);
}
