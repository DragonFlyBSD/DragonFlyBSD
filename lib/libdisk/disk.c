/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/lib/libdisk/disk.c,v 1.50.2.15 2001/12/30 09:56:12 phk Exp $
 * $DragonFly: src/lib/libdisk/Attic/disk.c,v 1.4 2005/03/13 15:10:03 swildner Exp $
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <paths.h>
#include "libdisk.h"

#define DOSPTYP_EXTENDED        5
#define DOSPTYP_ONTRACK         84

const char *chunk_n[] = {
	"whole",
	"unknown",
	"fat",
	"freebsd",
	"extended",
	"part",
	"unused",
	NULL
};

struct disk *
Open_Disk(const char *name)
{
	return Int_Open_Disk(name, 0);
}

static u_int32_t
Read_Int32(u_int32_t *p)
{
    u_int8_t *bp = (u_int8_t *)p;
    return bp[0] | (bp[1] << 8) | (bp[2] << 16) | (bp[3] << 24);
}

struct disk *
Int_Open_Disk(const char *name, u_long size)
{
	int i,fd;
	struct diskslices ds;
	struct disklabel dl;
	char device[64], *buf;
	struct disk *d;
	u_long sector_size;
	struct dos_partition *dp;
	void *p;
	u_long offset = 0;

	strcpy(device, _PATH_DEV);
	strcat(device, name);

	d = (struct disk *)malloc(sizeof *d);
	if(!d) return NULL;
	memset(d, 0, sizeof *d);

	fd = open(device, O_RDONLY);
	if (fd < 0) {
#ifdef DEBUG
		warn("open(%s) failed", device);
#endif
		return 0;
	}

	memset(&dl, 0, sizeof dl);
	ioctl(fd, DIOCGDINFO, &dl);
	i = ioctl(fd, DIOCGSLICEINFO, &ds);
	if (i < 0) {
#ifdef DEBUG
		warn("DIOCGSLICEINFO(%s) failed", device);
#endif
		close(fd);
		return 0;
	}

#ifdef DEBUG
	for(i = 0; i < ds.dss_nslices; i++)
		if(ds.dss_slices[i].ds_openmask)
			printf("  open(%d)=0x%2x",
				i, ds.dss_slices[i].ds_openmask);
	printf("\n");
#endif

/* XXX --- ds.dss_slice[WHOLE_DISK_SLICE].ds.size of MO disk is wrong!!! */
	if (!size)
		size = ds.dss_slices[WHOLE_DISK_SLICE].ds_size;

	/* determine media sector size */
	if ((buf = malloc(MAX_SEC_SIZE)) == NULL)
		return NULL;
	for (sector_size = MIN_SEC_SIZE; sector_size <= MAX_SEC_SIZE; sector_size *= 2) {
		if (read(fd, buf, sector_size) == sector_size) {
			d->sector_size = sector_size;
			break;
		}
	}
	free (buf);
	if (sector_size > MAX_SEC_SIZE)
		return NULL; /* could not determine sector size */

	p = read_block(fd, 0, sector_size);
	dp = (struct dos_partition*)(p + DOSPARTOFF);
	for (i = 0; i < NDOSPART; i++) {
		if (Read_Int32(&dp->dp_start) >= size)
		    continue;
		if (Read_Int32(&dp->dp_start) + Read_Int32(&dp->dp_size) >= size)
		    continue;
		if (!Read_Int32(&dp->dp_size))
		    continue;

		if (dp->dp_typ == DOSPTYP_ONTRACK) {
			d->flags |= DISK_ON_TRACK;
			offset = 63;
		}

	}
	free(p);

	d->bios_sect = dl.d_nsectors;
	d->bios_hd = dl.d_ntracks;

	d->name = strdup(name);


	if (dl.d_ntracks && dl.d_nsectors)
		d->bios_cyl = size / (dl.d_ntracks * dl.d_nsectors);

	if (Add_Chunk(d, -offset, size, name, whole, 0, 0))
#ifdef DEBUG
		warn("Failed to add 'whole' chunk");
#else
		{}
#endif

#ifdef __i386__
	for(i=BASE_SLICE;i<ds.dss_nslices;i++) {
		char sname[20];
		chunk_e ce;
		u_long flags=0;
		int subtype=0;

		if (! ds.dss_slices[i].ds_size)
			continue;
		ds.dss_slices[i].ds_offset -= offset;
		sprintf(sname, "%ss%d", name, i - 1);
		subtype = ds.dss_slices[i].ds_type;
		switch (ds.dss_slices[i].ds_type) {
			case 0xa5:
				ce = freebsd;
				break;
			case 0x1:
			case 0x6:
			case 0x4:
			case 0xb:
			case 0xc:
			case 0xe:
				ce = fat;
				break;
			case DOSPTYP_EXTENDED:
			case 0xf:
				ce = extended;
				break;
			default:
				ce = unknown;
				break;
		}
		if (Add_Chunk(d, ds.dss_slices[i].ds_offset,
			ds.dss_slices[i].ds_size, sname, ce, subtype, flags))
#ifdef DEBUG
			warn("failed to add chunk for slice %d", i - 1);
#else
			{}
#endif

		if (ds.dss_slices[i].ds_type != 0xa5)
			continue;
		{
		struct disklabel dl;
		char pname[20];
		int j, k;

		strcpy(pname, _PATH_DEV);
		strcat(pname, sname);
		j = open(pname, O_RDONLY);
		if (j < 0) {
#ifdef DEBUG
			warn("open(%s)", pname);
#endif
			continue;
		}
		k = ioctl(j, DIOCGDINFO, &dl);
		if (k < 0) {
#ifdef DEBUG
			warn("ioctl(%s, DIOCGDINFO)", pname);
#endif
			close(j);
			continue;
		}
		close(j);

		for(j = 0; j <= dl.d_npartitions; j++) {
			if (j == RAW_PART)
				continue;
			if (j == 3)
				continue;
			if (j == dl.d_npartitions) {
				j = 3;
				dl.d_npartitions = 0;
			}
			if (!dl.d_partitions[j].p_size)
				continue;
			if (dl.d_partitions[j].p_size +
			    dl.d_partitions[j].p_offset >
			    ds.dss_slices[i].ds_size)
				continue;
			sprintf(pname, "%s%c", sname, j + 'a');
			if (Add_Chunk(d,
				dl.d_partitions[j].p_offset +
				ds.dss_slices[i].ds_offset,
				dl.d_partitions[j].p_size,
				pname,part,
				dl.d_partitions[j].p_fstype,
				0) && j != 3)
#ifdef DEBUG
				warn(
			"Failed to add chunk for partition %c [%lu,%lu]",
			j + 'a', dl.d_partitions[j].p_offset,
			dl.d_partitions[j].p_size);
#else
				{}
#endif
		}
		}
	}
#endif /* __i386__ */
	close(fd);
	Fixup_Names(d);
	return d;
}

void
Debug_Disk(struct disk *d)
{
	printf("Debug_Disk(%s)", d->name);
	printf("  flags=%lx", d->flags);
#if 0
	printf("  real_geom=%lu/%lu/%lu", d->real_cyl, d->real_hd, d->real_sect);
#endif
	printf("  bios_geom=%lu/%lu/%lu = %lu\n",
		d->bios_cyl, d->bios_hd, d->bios_sect,
		d->bios_cyl * d->bios_hd * d->bios_sect);
#if defined(__i386__)
	printf("  boot1=%p, boot2=%p, bootmgr=%p\n",
		d->boot1, d->boot2, d->bootmgr);
#endif
	Debug_Chunk(d->chunks);
}

void
Free_Disk(struct disk *d)
{
	if(d->chunks) Free_Chunk(d->chunks);
	if(d->name) free(d->name);
	if(d->bootmgr) free(d->bootmgr);
	if(d->boot1) free(d->boot1);
#if defined(__i386__)
	if(d->boot2) free(d->boot2);
#endif
	free(d);
}

struct disk *
Clone_Disk(struct disk *d)
{
	struct disk *d2;

	d2 = (struct disk*) malloc(sizeof *d2);
	if(!d2) return NULL;
	*d2 = *d;
	d2->name = strdup(d2->name);
	d2->chunks = Clone_Chunk(d2->chunks);
	if(d2->bootmgr) {
		d2->bootmgr = malloc(d2->bootmgr_size);
		memcpy(d2->bootmgr, d->bootmgr, d2->bootmgr_size);
	}
#if defined(__i386__)
	if(d2->boot1) {
		d2->boot1 = malloc(512);
		memcpy(d2->boot1, d->boot1, 512);
	}
	if(d2->boot2) {
		d2->boot2 = malloc(512 * 15);
		memcpy(d2->boot2, d->boot2, 512 * 15);
	}
#endif
	return d2;
}

#if 0
void
Collapse_Disk(struct disk *d)
{

	while(Collapse_Chunk(d, d->chunks))
		;
}
#endif

static char * device_list[] = {"aacd", "ad", "da", "afd", "fla", "idad", "mlxd", "amrd", "twed", "ar", "fd", 0};

int qstrcmp(const void* a, const void* b) {

	char *str1 = *(char**)a;
	char *str2 = *(char**)b;
	return strcmp(str1, str2);

}

char **
Disk_Names()
{
    int i,j,disk_cnt;
    char disk[25];
    char diskname[25];
    struct stat st;
    struct diskslices ds;
    int fd;
    static char **disks;
    int error;
    size_t listsize;
    char *disklist, **dp;

    disks = malloc(sizeof *disks * (1 + MAX_NO_DISKS));
    memset(disks,0,sizeof *disks * (1 + MAX_NO_DISKS));
#if !defined(KERN_DISKS_BROKEN)
    error = sysctlbyname("kern.disks", NULL, &listsize, NULL, 0);
    if (!error) {
	    disklist = (char *)malloc(listsize);
	    error = sysctlbyname("kern.disks", disklist, &listsize, NULL, 0);
	    if (error) 
		    return NULL;
	    disk_cnt = 0;
	    for (dp = disks; ((*dp = strsep(&disklist, " ")) != NULL) && 
			 disk_cnt < MAX_NO_DISKS; disk_cnt++, dp++);
    } else {
    warn("kern.disks sysctl not available");
#endif
    disk_cnt = 0;
	for (j = 0; device_list[j]; j++) {
		if(disk_cnt >= MAX_NO_DISKS)
			break;
		for (i = 0; i < MAX_NO_DISKS; i++) {
			sprintf(diskname, "%s%d", device_list[j], i);
			sprintf(disk, _PATH_DEV"%s", diskname);
			if (stat(disk, &st) || !(st.st_mode & S_IFCHR))
				continue;
			if ((fd = open(disk, O_RDWR)) == -1)
				continue;
			if (ioctl(fd, DIOCGSLICEINFO, &ds) == -1) {
#ifdef DEBUG
				warn("DIOCGSLICEINFO %s", disk);
#endif
				close(fd);
				continue;
			}
			close(fd);
			disks[disk_cnt++] = strdup(diskname);
			if(disk_cnt >= MAX_NO_DISKS)
				break;
		}
	}
#if !defined(KERN_DISKS_BROKEN)
    }
#endif
    qsort(disks, disk_cnt, sizeof(char*), qstrcmp);
    
    return disks;
}

void
Set_Boot_Mgr(struct disk *d, const u_char *b, const size_t s)
{
	if (s % d->sector_size != 0)
		return;
	if (d->bootmgr)
		free(d->bootmgr);
	if (!b) {
		d->bootmgr = NULL;
	} else {
		d->bootmgr_size = s;
		d->bootmgr = malloc(s);
		if(!d->bootmgr) return;
		memcpy(d->bootmgr, b, s);
	}
}

int
Set_Boot_Blocks(struct disk *d, const u_char *b1, const u_char *b2)
{
#if defined(__i386__)
	if (d->boot1) free(d->boot1);
	d->boot1 = malloc(512);
	if(!d->boot1) return -1;
	memcpy(d->boot1, b1, 512);
	if (d->boot2) free(d->boot2);
	d->boot2 = malloc(15 * 512);
	if(!d->boot2) return -1;
	memcpy(d->boot2, b2, 15 * 512);
#endif
	return 0;
}

const char *
slice_type_name( int type, int subtype )
{
	switch (type) {
		case 0:		return "whole";
		case 1:		switch (subtype) {
					case 1:		return "fat (12-bit)";
					case 2:		return "XENIX /";
					case 3:		return "XENIX /usr";
					case 4:         return "fat (16-bit,<=32Mb)";
					case 5:		return "extended DOS";
					case 6:         return "fat (16-bit,>32Mb)";
					case 7:         return "NTFS/HPFS/QNX";
					case 8:         return "AIX bootable";
					case 9:         return "AIX data";
					case 10:	return "OS/2 bootmgr";
					case 11:        return "fat (32-bit)";
					case 12:        return "fat (32-bit,LBA)";
					case 14:        return "fat (16-bit,>32Mb,LBA)";
					case 15:        return "extended DOS, LBA";
					case 18:        return "Compaq Diagnostic";
					case 84:	return "OnTrack diskmgr";
					case 100:	return "Netware 2.x";
					case 101:	return "Netware 3.x";
					case 115:	return "SCO UnixWare";
					case 128:	return "Minix 1.1";
					case 129:	return "Minix 1.5";
					case 130:	return "linux_swap";
					case 131:	return "ext2fs";
					case 166:	return "OpenBSD FFS";	/* 0xA6 */
					case 169:	return "NetBSD FFS";	/* 0xA9 */
					case 182:	return "OpenBSD";		/* dedicated */
					case 183:	return "bsd/os";
					case 184:	return "bsd/os swap";
					default:	return "unknown";
				}
		case 2:		return "fat";
		case 3:		switch (subtype) {
					case 165:	return "freebsd";
					default:	return "unknown";
				}
		case 4:		return "extended";
		case 5:		return "part";
		case 6:		return "unused";
		default:	return "unknown";
	}
}
