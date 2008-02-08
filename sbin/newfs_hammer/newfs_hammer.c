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
 * 
 * $DragonFly: src/sbin/newfs_hammer/newfs_hammer.c,v 1.17 2008/02/08 08:30:58 dillon Exp $
 */

#include "newfs_hammer.h"

static int64_t getsize(const char *str, int64_t minval, int64_t maxval, int pw);
static const char *sizetostr(off_t size);
static void check_volume(struct volume_info *vol);
static void format_volume(struct volume_info *vol, int nvols,const char *label);
static hammer_off_t format_root(void);
static void usage(void);

int
main(int ac, char **av)
{
	int i;
	int ch;
	u_int32_t status;
	off_t total;
	const char *label = NULL;

	/*
	 * Sanity check basic filesystem structures.  No cookies for us
	 * if it gets broken!
	 */
	assert(sizeof(struct hammer_volume_ondisk) <= HAMMER_BUFSIZE);

	/*
	 * Generate a filesysem id and lookup the filesystem type
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
	while ((ch = getopt(ac, av, "L:b:m:")) != -1) {
		switch(ch) {
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
		default:
			usage();
			break;
		}
	}

	if (label == NULL) {
		fprintf(stderr,
			"newfs_hammer: A filesystem label must be specified\n");
		exit(1);
	}

	/*
	 * Collect volume information
	 */
	ac -= optind;
	av += optind;
	NumVolumes = ac;
	RootVolNo = 0;

	total = 0;
	for (i = 0; i < NumVolumes; ++i) {
		struct volume_info *vol;

		vol = setup_volume(i, av[i], 1, O_RDWR);

		/*
		 * Load up information on the volume and initialize
		 * its remaining fields.
		 */
		check_volume(vol);
		total += vol->size;
	}

	/*
	 * Calculate defaults for the boot and memory area sizes.
	 */
	if (BootAreaSize == 0) {
		BootAreaSize = HAMMER_BOOT_NOMBYTES;
		while (BootAreaSize > total / NumVolumes / 256)
			BootAreaSize >>= 1;
		if (BootAreaSize < HAMMER_BOOT_MINBYTES)
			BootAreaSize = 0;
	} else if (BootAreaSize < HAMMER_BOOT_MINBYTES) {
		BootAreaSize = HAMMER_BOOT_MINBYTES;
	}
	if (MemAreaSize == 0) {
		MemAreaSize = HAMMER_MEM_NOMBYTES;
		while (MemAreaSize > total / NumVolumes / 256)
			MemAreaSize >>= 1;
		if (MemAreaSize < HAMMER_MEM_MINBYTES)
			MemAreaSize = 0;
	} else if (MemAreaSize < HAMMER_MEM_MINBYTES) {
		MemAreaSize = HAMMER_MEM_MINBYTES;
	}

	printf("---------------------------------------------\n");
	printf("%d volume%s total size %s\n",
		NumVolumes, (NumVolumes == 1 ? "" : "s"), sizetostr(total));
	printf("boot-area-size:      %s\n", sizetostr(BootAreaSize));
	printf("memory-log-size:     %s\n", sizetostr(MemAreaSize));
	printf("\n");

	/*
	 * Format the volumes.
	 */
	for (i = 0; i < NumVolumes; ++i) {
		format_volume(get_volume(i), NumVolumes, label);
	}
	flush_all_volumes();
	return(0);
}

static
void
usage(void)
{
	fprintf(stderr, "newfs_hammer vol0 [vol1 ...]\n");
	exit(1);
}

/*
 * Convert the size in bytes to a human readable string.
 */
static const char *
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
 * Generate a transaction id
 */
static hammer_tid_t
createtid(void)
{
	static hammer_tid_t lasttid;
	struct timeval tv;

	if (lasttid == 0) {
		gettimeofday(&tv, NULL);
		lasttid = tv.tv_sec * 1000000000LL +
			  tv.tv_usec * 1000LL;
	}
	return(lasttid++);
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
		 * Allow the formatting of regular filews as HAMMER volumes
		 */
		if (fstat(vol->fd, &st) < 0)
			err(1, "Unable to stat %s", vol->name);
		vol->size = st.st_size;
		vol->type = "REGFILE";
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
		vol->type = "DEVICE";
	}
	printf("Volume %d %s %-15s size %s\n",
	       vol->vol_no, vol->type, vol->name,
	       sizetostr(vol->size));

	/*
	 * Reserve space for (future) header junk
	 */
	vol->vol_alloc = HAMMER_BUFSIZE * 16;
}

/*
 * Format a HAMMER volume.  Cluster 0 will be initially placed in volume 0.
 */
static
void
format_volume(struct volume_info *vol, int nvols, const char *label)
{
	struct hammer_volume_ondisk *ondisk;

	/*
	 * Initialize basic information in the on-disk volume structure.
	 */
	ondisk = vol->ondisk;

	ondisk->vol_fsid = Hammer_FSId;
	ondisk->vol_fstype = Hammer_FSType;
	snprintf(ondisk->vol_name, sizeof(ondisk->vol_name), "%s", label);
	ondisk->vol_no = vol->vol_no;
	ondisk->vol_count = nvols;
	ondisk->vol_version = 1;

	ondisk->vol_bot_beg = vol->vol_alloc;
	vol->vol_alloc += BootAreaSize;
	ondisk->vol_mem_beg = vol->vol_alloc;
	vol->vol_alloc += MemAreaSize;
	ondisk->vol_buf_beg = vol->vol_alloc;
	ondisk->vol_buf_end = vol->size & ~(int64_t)HAMMER_BUFMASK;

	if (ondisk->vol_buf_end < ondisk->vol_buf_beg) {
		errx(1, "volume %d %s is too small to hold the volume header",
		     vol->vol_no, vol->name);
	}

	ondisk->vol_nblocks = (ondisk->vol_buf_end - ondisk->vol_buf_beg) /
			      HAMMER_BUFSIZE;
	ondisk->vol_blocksize = HAMMER_BUFSIZE;

	/*
	 * Assign the root volume to volume 0.
	 */
	ondisk->vol_rootvol = 0;
	ondisk->vol_signature = HAMMER_FSBUF_VOLUME;
	if (ondisk->vol_no == (int)ondisk->vol_rootvol) {
		/*
		 * Create an empty FIFO starting at the first buffer
		 * in volume 0.  hammer_off_t must be properly formatted
		 * (see vfs/hammer/hammer_disk.h)
		 */
		ondisk->vol0_fifo_beg = HAMMER_ENCODE_RAW_BUFFER(0, 0);
		ondisk->vol0_fifo_end = ondisk->vol0_fifo_beg;
		ondisk->vol0_next_tid = createtid();
		ondisk->vol0_next_seq = 1;

		ondisk->vol0_btree_root = format_root();
		++ondisk->vol0_stat_inodes;	/* root inode */

	}
}

/*
 * Format the root directory.
 */
static
hammer_off_t
format_root(void)
{
	hammer_off_t btree_off;
	hammer_off_t rec_off;
	hammer_node_ondisk_t bnode;
	hammer_record_ondisk_t rec;
	struct hammer_inode_data *idata;
	hammer_btree_elm_t elm;

	bnode = alloc_btree_element(&btree_off);
	rec = alloc_record_element(&rec_off, HAMMER_RECTYPE_INODE,
				   sizeof(rec->inode), sizeof(*idata),
				   (void **)&idata);

	/*
	 * Populate the inode data and inode record for the root directory.
	 */
	idata->version = HAMMER_INODE_DATA_VERSION;
	idata->mode = 0755;

	rec->base.base.btype = HAMMER_BTREE_TYPE_RECORD;
	rec->base.base.obj_id = HAMMER_OBJID_ROOT;
	rec->base.base.key = 0;
	rec->base.base.create_tid = createtid();
	rec->base.base.delete_tid = 0;
	rec->base.base.rec_type = HAMMER_RECTYPE_INODE;
	rec->base.base.obj_type = HAMMER_OBJTYPE_DIRECTORY;
	/* rec->base.data_offset - initialized by alloc_record_element */
	/* rec->base.data_len 	 - initialized by alloc_record_element */
	rec->base.head.hdr_crc = crc32(idata, sizeof(*idata));
	rec->inode.ino_atime  = rec->base.base.create_tid;
	rec->inode.ino_mtime  = rec->base.base.create_tid;
	rec->inode.ino_size   = 0;
	rec->inode.ino_nlinks = 1;

	/*
	 * Create the root of the B-Tree.  The root is a leaf node so we
	 * do not have to worry about boundary elements.
	 */
	bnode->count = 1;
	bnode->type = HAMMER_BTREE_TYPE_LEAF;

	elm = &bnode->elms[0];
	elm->base = rec->base.base;
	elm->leaf.rec_offset = rec_off;
	elm->leaf.data_offset = rec->base.data_off;
	elm->leaf.data_len = rec->base.data_len;
	elm->leaf.data_crc = rec->base.head.hdr_crc;
	return(btree_off);
}

