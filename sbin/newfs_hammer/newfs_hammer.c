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

#include <sys/sysctl.h>
#include <sys/ioctl_compat.h>

#include "hammer_util.h"

static int64_t getsize(const char *str, int pw);
static int trim_volume(struct volume_info *vol);
static void format_volume(struct volume_info *vol, int nvols,const char *label);
static hammer_off_t format_root_directory(const char *label);
static uint64_t nowtime(void);
static void print_volume(const struct volume_info *vol);
static void usage(int exit_code);
static void test_header_junk_size(int64_t size);
static void test_boot_area_size(int64_t size);
static void test_memory_log_size(int64_t size);
static void test_undo_buffer_size(int64_t size);

static int ForceOpt;
static int64_t HeaderJunkSize = -1;
static int64_t BootAreaSize = -1;
static int64_t MemoryLogSize = -1;
static int64_t UndoBufferSize;
static int HammerVersion = -1;

#define GIG	(1024LL*1024*1024)

int
main(int ac, char **av)
{
	uint32_t status;
	off_t total;
	off_t avg_vol_size;
	int ch;
	int i;
	int nvols;
	int eflag = 0;
	const char *label = NULL;
	struct volume_info *vol;

	/*
	 * Sanity check basic filesystem structures.  No cookies for us
	 * if it gets broken!
	 */
	assert(sizeof(struct hammer_volume_ondisk) <= HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_volume_ondisk) <= HAMMER_MIN_VOL_JUNK);
	assert(sizeof(struct hammer_blockmap_layer1) == 32);
	assert(sizeof(struct hammer_blockmap_layer2) == 16);

	/*
	 * Parse arguments
	 */
	while ((ch = getopt(ac, av, "dfEL:j:b:m:u:hC:V:")) != -1) {
		switch(ch) {
		case 'd':
			++DebugOpt;
			break;
		case 'f':
			ForceOpt = 1;
			break;
		case 'E':
			eflag = 1;
			break;
		case 'L':
			label = optarg;
			break;
		case 'j': /* Not mentioned in newfs_hammer(8) */
			HeaderJunkSize = getsize(optarg, 2);
			test_header_junk_size(HeaderJunkSize);
			break;
		case 'b':
			BootAreaSize = getsize(optarg, 2);
			test_boot_area_size(BootAreaSize);
			break;
		case 'm':
			MemoryLogSize = getsize(optarg, 2);
			test_memory_log_size(MemoryLogSize);
			break;
		case 'u':
			UndoBufferSize = getsize(optarg, 2);
			test_undo_buffer_size(UndoBufferSize);
			break;
		case 'h':
			usage(0);
			break;
		case 'C':
			if (hammer_parse_cache_size(optarg) == -1)
				usage(1);
			break;
		case 'V':
			HammerVersion = strtol(optarg, NULL, 0);
			if (HammerVersion < HAMMER_VOL_VERSION_MIN ||
			    HammerVersion >= HAMMER_VOL_VERSION_WIP) {
				errx(1,
				     "I don't understand how to format "
				     "HAMMER version %d",
				     HammerVersion);
			}
			break;
		default:
			usage(1);
			break;
		}
	}
	ac -= optind;
	av += optind;
	nvols = ac;

	if (HammerVersion < 0) {
		size_t olen = sizeof(HammerVersion);
		HammerVersion = HAMMER_VOL_VERSION_DEFAULT;

		if (sysctlbyname("vfs.hammer.supported_version",
				 &HammerVersion, &olen, NULL, 0)) {
			hwarn("HAMMER VFS not loaded, cannot get version info, "
				"using version %d",
				HammerVersion);
		} else if (HammerVersion >= HAMMER_VOL_VERSION_WIP) {
			HammerVersion = HAMMER_VOL_VERSION_WIP - 1;
			hwarn("HAMMER VFS supports higher version than "
				"I understand, using version %d",
				HammerVersion);
		}
	}

	if (nvols == 0)
		errx(1, "You must specify at least one special file (volume)");
	if (nvols > HAMMER_MAX_VOLUMES)
		errx(1, "The maximum number of volumes is %d",
			HAMMER_MAX_VOLUMES);

	if (label == NULL) {
		hwarnx("A filesystem label must be specified");
		usage(1);
	}

	/*
	 * Generate a filesystem id and lookup the filesystem type
	 */
	uuidgen(&Hammer_FSId, 1);
	uuid_name_lookup(&Hammer_FSType, HAMMER_FSTYPE_STRING, &status);
	if (status != uuid_s_ok) {
		errx(1, "uuids file does not have the DragonFly "
			"HAMMER filesystem type");
	}

	total = 0;
	for (i = 0; i < nvols; ++i) {
		vol = init_volume(av[i], O_RDWR, i);
		printf("Volume %d %s %-15s size %s\n",
			vol->vol_no, vol->type, vol->name,
			sizetostr(vol->size));

		if (eflag) {
			if (trim_volume(vol) == -1 && ForceOpt == 0)
				errx(1, "Use -f option to proceed");
		}
		total += vol->size;
	}

	/*
	 * Reserve space for (future) header junk, setup our poor-man's
	 * big-block allocator.  Note that the header junk space includes
	 * volume header which is 1928 bytes.
	 */
	if (HeaderJunkSize == -1)
		HeaderJunkSize = HAMMER_VOL_JUNK_SIZE;
	else if (HeaderJunkSize < (int64_t)sizeof(struct hammer_volume_ondisk))
		HeaderJunkSize = sizeof(struct hammer_volume_ondisk);
	HeaderJunkSize = HAMMER_BUFSIZE_DOALIGN(HeaderJunkSize);

	/*
	 * Calculate defaults for the boot area and memory log sizes,
	 * only if not specified by -b or -m option.
	 */
	avg_vol_size = total / nvols;
	if (BootAreaSize == -1)
		BootAreaSize = init_boot_area_size(BootAreaSize, avg_vol_size);
	if (MemoryLogSize == -1)
		MemoryLogSize = init_memory_log_size(MemoryLogSize, avg_vol_size);

	/*
	 * Format the volumes.  Format the root volume first so we can
	 * bootstrap the freemap.
	 */
	format_volume(get_root_volume(), nvols, label);
	for (i = 0; i < nvols; ++i) {
		if (i != HAMMER_ROOT_VOLNO)
			format_volume(get_volume(i), nvols, label);
	}

	print_volume(get_root_volume());

	flush_all_volumes();
	return(0);
}

static
void
print_volume(const struct volume_info *vol)
{
	hammer_volume_ondisk_t ondisk;
	hammer_blockmap_t blockmap;
	hammer_off_t total = 0;
	int i, nvols;
	uint32_t status;
	const char *name = NULL;
	char *fsidstr;

	ondisk = vol->ondisk;
	blockmap = &ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];

	nvols = ondisk->vol_count;
	for (i = 0; i < nvols; ++i) {
		struct volume_info *p = get_volume(i);
		total += p->size;
		if (p->vol_no == HAMMER_ROOT_VOLNO) {
			assert(name == NULL);
			name = p->name;
		}
	}

	uuid_to_string(&Hammer_FSId, &fsidstr, &status);

	printf("---------------------------------------------\n");
	printf("HAMMER version %d\n", HammerVersion);
	printf("%d volume%s total size %s\n",
		nvols, (nvols == 1 ? "" : "s"), sizetostr(total));
	printf("root-volume:         %s\n", name);
	if (DebugOpt)
		printf("header-junk-size:    %s\n",
			sizetostr(ondisk->vol_bot_beg));
	printf("boot-area-size:      %s\n",
		sizetostr(ondisk->vol_mem_beg - ondisk->vol_bot_beg));
	printf("memory-log-size:     %s\n",
		sizetostr(ondisk->vol_buf_beg - ondisk->vol_mem_beg));
	printf("undo-buffer-size:    %s\n",
		sizetostr(HAMMER_OFF_LONG_ENCODE(blockmap->alloc_offset)));
	printf("total-pre-allocated: %s\n",
		sizetostr(HAMMER_OFF_SHORT_ENCODE(vol->vol_free_off)));
	printf("fsid:                %s\n", fsidstr);
	printf("\n");
	printf("NOTE: Please remember that you may have to manually set up a\n"
		"cron(8) job to prune and reblock the filesystem regularly.\n"
		"By default, the system automatically runs 'hammer cleanup'\n"
		"on a nightly basis.  The periodic.conf(5) variable\n"
		"'daily_clean_hammer_enable' can be unset to disable this.\n"
		"Also see 'man hammer' and 'man HAMMER' for more information.\n");
	if (total < 10*GIG) {
		printf("\nWARNING: The minimum UNDO/REDO FIFO is %s, "
			"you really should not\n"
			"try to format a HAMMER filesystem this small.\n",
			sizetostr(HAMMER_BIGBLOCK_SIZE *
			       HAMMER_MIN_UNDO_BIGBLOCKS));
	}
	if (total < 50*GIG) {
		printf("\nWARNING: HAMMER filesystems less than 50GB are "
			"not recommended!\n"
			"You may have to run 'hammer prune-everything' and "
			"'hammer reblock'\n"
			"quite often, even if using a nohistory mount.\n");
	}
}

static
void
usage(int exit_code)
{
	fprintf(stderr,
		"usage: newfs_hammer -L label [-Efh] [-b bootsize] [-m savesize] [-u undosize]\n"
		"                    [-C cachesize[:readahead]] [-V version] special ...\n"
	);
	exit(exit_code);
}

static void
test_header_junk_size(int64_t size)
{
	if (size < HAMMER_MIN_VOL_JUNK) {
		if (ForceOpt == 0) {
			errx(1, "The minimum header junk size is %s",
				sizetostr(HAMMER_MIN_VOL_JUNK));
		} else {
			hwarnx("You have specified header junk size less than %s",
				sizetostr(HAMMER_MIN_VOL_JUNK));
		}
	} else if (size > HAMMER_MAX_VOL_JUNK) {
		errx(1, "The maximum header junk size is %s",
			sizetostr(HAMMER_MAX_VOL_JUNK));
	}
}

static void
test_boot_area_size(int64_t size)
{
	if (size < HAMMER_BOOT_MINBYTES) {
		if (ForceOpt == 0) {
			errx(1, "The minimum boot area size is %s",
				sizetostr(HAMMER_BOOT_MINBYTES));
		} else {
			hwarnx("You have specified boot area size less than %s",
				sizetostr(HAMMER_BOOT_MINBYTES));
		}
	} else if (size > HAMMER_BOOT_MAXBYTES) {
		errx(1, "The maximum boot area size is %s",
			sizetostr(HAMMER_BOOT_MAXBYTES));
	}
}

static void
test_memory_log_size(int64_t size)
{
	if (size < HAMMER_MEM_MINBYTES) {
		if (ForceOpt == 0) {
			errx(1, "The minimum memory log size is %s",
				sizetostr(HAMMER_MEM_MINBYTES));
		} else {
			hwarnx("You have specified memory log size less than %s",
				sizetostr(HAMMER_MEM_MINBYTES));
		}
	} else if (size > HAMMER_MEM_MAXBYTES) {
		errx(1, "The maximum memory log size is %s",
			sizetostr(HAMMER_MEM_MAXBYTES));
	}
}

static void
test_undo_buffer_size(int64_t size)
{
	int64_t minbuf, maxbuf;

	minbuf = HAMMER_BIGBLOCK_SIZE * HAMMER_MIN_UNDO_BIGBLOCKS;
	maxbuf = HAMMER_BIGBLOCK_SIZE * HAMMER_MAX_UNDO_BIGBLOCKS;

	if (size < minbuf) {
		if (ForceOpt == 0) {
			errx(1, "The minimum UNDO/REDO FIFO size is %s",
				sizetostr(minbuf));
		} else {
			hwarnx("You have specified an UNDO/REDO FIFO size less "
				"than %s, which may lead to VFS panics",
				sizetostr(minbuf));
		}
	} else if (size > maxbuf) {
		errx(1, "The maximum UNDO/REDO FIFO size is %s",
			sizetostr(maxbuf));
	}
}

/*
 * Convert a string to a 64 bit signed integer with various requirements.
 */
static int64_t
getsize(const char *str, int powerof2)
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
		errx(1, "Unknown suffix in number '%s'", str);
		/* not reached */
	}

	if (ptr[1]) {
		errx(1, "Unknown suffix in number '%s'", str);
		/* not reached */
	}
	if ((powerof2 & 1) && (val ^ (val - 1)) != ((val << 1) - 1)) {
		errx(1, "Value not power of 2: %s", str);
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
 * TRIM the volume, but only if the backing store is a DEVICE
 */
static
int
trim_volume(struct volume_info *vol)
{
	size_t olen;
	char *dev_name, *p;
	char sysctl_name[64];
	int trim_enabled;
	off_t ioarg[2];

	if (strcmp(vol->type, "DEVICE")) {
		hwarnx("Cannot TRIM regular file %s", vol->name);
		return(-1);
	}
	if (strncmp(vol->name, "/dev/da", 7)) {
		hwarnx("%s does not support the TRIM command", vol->name);
		return(-1);
	}

	/* Extract a number from /dev/da?s? */
	dev_name = strdup(vol->name);
	p = strtok(dev_name + strlen("/dev/da"), "s");
	sprintf(sysctl_name, "kern.cam.da.%s.trim_enabled", p);
	free(dev_name);

	trim_enabled = 0;
	olen = sizeof(trim_enabled);

	if (sysctlbyname(sysctl_name, &trim_enabled, &olen, NULL, 0) == -1) {
		hwarnx("%s (%s) does not support the TRIM command",
			vol->name, sysctl_name);
		return(-1);
	}
	if (!trim_enabled) {
		hwarnx("Erase device option selected, but sysctl (%s) "
			"is not enabled", sysctl_name);
		return(-1);
	}

	ioarg[0] = vol->device_offset;
	ioarg[1] = vol->size;

	printf("Trimming %s %s, sectors %llu-%llu\n",
		vol->type, vol->name,
		(unsigned long long)ioarg[0] / 512,
		(unsigned long long)ioarg[1] / 512);

	if (ioctl(vol->fd, IOCTLTRIM, ioarg) == -1)
		err(1, "Trimming %s failed", vol->name);

	return(0);
}

/*
 * Format a HAMMER volume.
 */
static
void
format_volume(struct volume_info *vol, int nvols, const char *label)
{
	struct volume_info *root_vol;
	hammer_volume_ondisk_t ondisk;
	int64_t freeblks;
	int64_t freebytes;
	int64_t vol_buf_size;
	hammer_off_t vol_alloc;
	int i;

	/*
	 * Initialize basic information in the on-disk volume structure.
	 */
	ondisk = vol->ondisk;

	ondisk->vol_fsid = Hammer_FSId;
	ondisk->vol_fstype = Hammer_FSType;
	snprintf(ondisk->vol_label, sizeof(ondisk->vol_label), "%s", label);
	ondisk->vol_no = vol->vol_no;
	ondisk->vol_count = nvols;
	ondisk->vol_version = HammerVersion;

	vol_alloc = HeaderJunkSize;
	ondisk->vol_bot_beg = vol_alloc;
	vol_alloc += BootAreaSize;
	ondisk->vol_mem_beg = vol_alloc;
	vol_alloc += MemoryLogSize;

	/*
	 * The remaining area is the zone 2 buffer allocation area.
	 */
	ondisk->vol_buf_beg = vol_alloc;
	ondisk->vol_buf_end = vol->size & ~(int64_t)HAMMER_BUFMASK;
	vol_buf_size = HAMMER_VOL_BUF_SIZE(ondisk);

	if (vol_buf_size < (int64_t)sizeof(*ondisk)) {
		errx(1, "volume %d %s is too small to hold the volume header",
		     vol->vol_no, vol->name);
	}
	if ((vol_buf_size & ~HAMMER_OFF_SHORT_MASK) != 0) {
		errx(1, "volume %d %s is too large", vol->vol_no, vol->name);
	}

	ondisk->vol_rootvol = HAMMER_ROOT_VOLNO;
	ondisk->vol_signature = HAMMER_FSBUF_VOLUME;

	vol->vol_free_off = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	vol->vol_free_end = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no,
				vol_buf_size & ~HAMMER_BIGBLOCK_MASK64);

	/*
	 * Format the root volume.
	 */
	if (vol->vol_no == HAMMER_ROOT_VOLNO) {
		/*
		 * Check freemap counts before formatting
		 */
		freeblks = count_freemap(vol);
		freebytes = freeblks * HAMMER_BIGBLOCK_SIZE64;
		if (freebytes < 10*GIG && ForceOpt == 0) {
			errx(1, "Cannot create a HAMMER filesystem less than 10GB "
				"unless you use -f\n(for the size of Volume %d).  "
				"HAMMER filesystems less than 50GB are not "
				"recommended.", HAMMER_ROOT_VOLNO);
		}

		/*
		 * Starting TID
		 */
		ondisk->vol0_next_tid = createtid();

		/*
		 * Format freemap.  vol0_stat_freebigblocks is
		 * the number of big-blocks available for anything
		 * other than freemap zone at this point.
		 */
		format_freemap(vol);
		assert(ondisk->vol0_stat_freebigblocks == 0);
		ondisk->vol0_stat_freebigblocks = initialize_freemap(vol);

		/*
		 * Format zones that are mapped to zone-2.
		 */
		for (i = 0; i < HAMMER_MAX_ZONES; ++i) {
			if (hammer_is_index_record(i))
				format_blockmap(vol, i, 0);
		}

		/*
		 * Format undo zone.  Formatting decrements
		 * vol0_stat_freebigblocks whenever a new big-block
		 * is allocated for undo zone.
		 */
		format_undomap(vol, &UndoBufferSize);
		assert(ondisk->vol0_stat_bigblocks == 0);
		ondisk->vol0_stat_bigblocks = ondisk->vol0_stat_freebigblocks;

		/*
		 * Format the root directory.  Formatting decrements
		 * vol0_stat_freebigblocks whenever a new big-block
		 * is allocated for required zones.
		 */
		ondisk->vol0_btree_root = format_root_directory(label);
		++ondisk->vol0_stat_inodes;	/* root inode */
	} else {
		freeblks = initialize_freemap(vol);
		root_vol = get_root_volume();
		root_vol->ondisk->vol0_stat_freebigblocks += freeblks;
		root_vol->ondisk->vol0_stat_bigblocks += freeblks;
	}
}

/*
 * Format the root directory.
 */
static
hammer_off_t
format_root_directory(const char *label)
{
	hammer_off_t btree_off;
	hammer_off_t idata_off;
	hammer_off_t pfsd_off;
	hammer_tid_t create_tid;
	hammer_node_ondisk_t bnode;
	hammer_inode_data_t idata;
	hammer_pseudofs_data_t pfsd;
	struct buffer_info *data_buffer0 = NULL;
	struct buffer_info *data_buffer1 = NULL;
	struct buffer_info *data_buffer2 = NULL;
	hammer_btree_elm_t elm;
	uint64_t xtime;

	/*
	 * Allocate zero-filled root btree node, inode and pfs
	 */
	bnode = alloc_btree_node(&btree_off, &data_buffer0);
	idata = alloc_meta_element(&idata_off, sizeof(*idata), &data_buffer1);
	pfsd = alloc_meta_element(&pfsd_off, sizeof(*pfsd), &data_buffer2);
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

	/*
	 * Populate the PFS data for the root PFS.
	 */
	pfsd->sync_low_tid = 1;
	pfsd->sync_beg_tid = 0;
	pfsd->sync_end_tid = 0;	/* overriden by vol0_next_tid on root PFS */
	pfsd->shared_uuid = Hammer_FSId;
	pfsd->unique_uuid = Hammer_FSId;
	pfsd->mirror_flags = 0;
	snprintf(pfsd->label, sizeof(pfsd->label), "%s", label);

	/*
	 * Create the root of the B-Tree.  The root is a leaf node so we
	 * do not have to worry about boundary elements.
	 */
	bnode->parent = 0;  /* no parent */
	bnode->count = 2;
	bnode->type = HAMMER_BTREE_TYPE_LEAF;
	bnode->mirror_tid = 0;

	/*
	 * Create the first node element for the inode.
	 */
	elm = &bnode->elms[0];
	elm->leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	elm->leaf.base.localization = HAMMER_DEF_LOCALIZATION |
				      HAMMER_LOCALIZE_INODE;
	elm->leaf.base.obj_id = HAMMER_OBJID_ROOT;
	elm->leaf.base.key = 0;
	elm->leaf.base.create_tid = create_tid;
	elm->leaf.base.delete_tid = 0;
	elm->leaf.base.rec_type = HAMMER_RECTYPE_INODE;
	elm->leaf.base.obj_type = HAMMER_OBJTYPE_DIRECTORY;
	elm->leaf.create_ts = (uint32_t)time(NULL);

	elm->leaf.data_offset = idata_off;
	elm->leaf.data_len = sizeof(*idata);
	hammer_crc_set_leaf(idata, &elm->leaf);

	/*
	 * Create the second node element for the PFS data.
	 * This is supposed to be a record part of the root ip (inode),
	 * so it should have the same obj_type value as above.
	 */
	elm = &bnode->elms[1];
	elm->leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	elm->leaf.base.localization = HAMMER_DEF_LOCALIZATION |
				      HAMMER_LOCALIZE_MISC;
	elm->leaf.base.obj_id = HAMMER_OBJID_ROOT;
	elm->leaf.base.key = 0;
	elm->leaf.base.create_tid = create_tid;
	elm->leaf.base.delete_tid = 0;
	elm->leaf.base.rec_type = HAMMER_RECTYPE_PFS;
	elm->leaf.base.obj_type = HAMMER_OBJTYPE_DIRECTORY;
	elm->leaf.create_ts = (uint32_t)time(NULL);

	elm->leaf.data_offset = pfsd_off;
	elm->leaf.data_len = sizeof(*pfsd);
	hammer_crc_set_leaf(pfsd, &elm->leaf);

	hammer_crc_set_btree(bnode);

	rel_buffer(data_buffer0);
	rel_buffer(data_buffer1);
	rel_buffer(data_buffer2);

	return(btree_off);
}
