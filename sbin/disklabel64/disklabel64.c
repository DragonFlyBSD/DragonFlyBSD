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
/*
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Symmetric Computer Systems.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)disklabel.c	1.2 (Symmetric) 11/28/85
 * @(#)disklabel.c      8.2 (Berkeley) 1/7/94
 * $FreeBSD: src/sbin/disklabel/disklabel.c,v 1.28.2.15 2003/01/24 16:18:16 des Exp $
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define DKTYPENAMES
#include <sys/disklabel64.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/dtype.h>
#include <sys/sysctl.h>
#include <disktab.h>
#include <fstab.h>

#include <vfs/ufs/dinode.h>
#include <vfs/ufs/fs.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <uuid.h>
#include "pathnames.h"

extern uint32_t crc32(const void *buf, size_t size);

/*
 * Disklabel64: read and write 64 bit disklabels.
 * The label is usually placed on one of the first sectors of the disk.
 * Many machines also place a bootstrap in the same area,
 * in which case the label is embedded in the bootstrap.
 * The bootstrap source must leave space at the proper offset
 * for the label on such machines.
 */

#define LABELSIZE	((sizeof(struct disklabel64) + 4095) & ~4095)
#define BOOTSIZE	32768

/* FIX!  These are too low, but are traditional */
#define DEFAULT_NEWFS_BLOCK  8192U
#define DEFAULT_NEWFS_FRAG   1024U
#define DEFAULT_NEWFS_CPG    16U

#define BIG_NEWFS_BLOCK  16384U
#define BIG_NEWFS_FRAG   2048U
#define BIG_NEWFS_CPG    64U

void	makelabel(const char *, const char *, struct disklabel64 *);
int	writelabel(int, struct disklabel64 *);
void	l_perror(const char *);
struct disklabel64 *readlabel(int);
struct disklabel64 *makebootarea(int);
void	display(FILE *, const struct disklabel64 *);
int	edit(struct disklabel64 *, int);
int	editit(void);
char	*skip(char *);
char	*word(char *);
int	getasciilabel(FILE *, struct disklabel64 *);
int	getasciipartspec(char *, struct disklabel64 *, int, int, uint32_t);
int	getasciipartuuid(char *, struct disklabel64 *, int, int, uint32_t);
int	checklabel(struct disklabel64 *);
void	Warning(const char *, ...) __printflike(1, 2);
void	usage(void);
struct disklabel64 *getvirginlabel(void);

#define	DEFEDITOR	_PATH_VI
#define	streq(a,b)	(strcmp(a,b) == 0)

char	*dkname;
char	*specname;
char	tmpfil[] = PATH_TMPFILE;

struct	disklabel64 lab;

#define MAX_PART ('z')
#define MAX_NUM_PARTS (1 + MAX_PART - 'a')
char    part_size_type[MAX_NUM_PARTS];
char    part_offset_type[MAX_NUM_PARTS];
int     part_set[MAX_NUM_PARTS];

int	installboot;	/* non-zero if we should install a boot program */
int	boot1size;
int	boot1lsize;
int	boot2size;
char	*boot1buf;
char	*boot2buf;
char	*boot1path;
char	*boot2path;

enum	{
	UNSPEC, EDIT, NOWRITE, READ, RESTORE, WRITE, WRITEABLE, WRITEBOOT
} op = UNSPEC;

int	rflag;
int	Vflag;
int	disable_write;   /* set to disable writing to disk label */
u_int32_t slice_start_lba;

#ifdef DEBUG
int	debug;
#define OPTIONS	"BNRWb:denrs:Vw"
#else
#define OPTIONS	"BNRWb:enrs:Vw"
#endif

int
main(int argc, char *argv[])
{
	struct disklabel64 *lp;
	FILE *t;
	int ch, f = 0, flag, error = 0;
	char *name = NULL;

	while ((ch = getopt(argc, argv, OPTIONS)) != -1)
		switch (ch) {
			case 'B':
				++installboot;
				break;
			case 'b':
				boot1path = optarg;
				break;

			case 's':
				boot2path = optarg;
				break;
			case 'N':
				if (op != UNSPEC)
					usage();
				op = NOWRITE;
				break;
			case 'n':
				disable_write = 1;
				break;
			case 'R':
				if (op != UNSPEC)
					usage();
				op = RESTORE;
				break;
			case 'W':
				if (op != UNSPEC)
					usage();
				op = WRITEABLE;
				break;
			case 'e':
				if (op != UNSPEC)
					usage();
				op = EDIT;
				break;
			case 'V':
				++Vflag;
				break;
			case 'r':
				++rflag;
				break;
			case 'w':
				if (op != UNSPEC)
					usage();
				op = WRITE;
				break;
#ifdef DEBUG
			case 'd':
				debug++;
				break;
#endif
			case '?':
			default:
				usage();
		}
	argc -= optind;
	argv += optind;
	if (installboot) {
		rflag++;
		if (op == UNSPEC)
			op = WRITEBOOT;
	} else {
		if (op == UNSPEC)
			op = READ;
		boot1path = NULL;
		boot2path = NULL;
	}
	if (argc < 1)
		usage();

	dkname = getdevpath(argv[0], 0);
	specname = dkname;
	f = open(specname, op == READ ? O_RDONLY : O_RDWR);
	if (f < 0)
		err(4, "%s", specname);

	switch(op) {

	case UNSPEC:
		break;

	case EDIT:
		if (argc != 1)
			usage();
		lp = readlabel(f);
		error = edit(lp, f);
		break;

	case NOWRITE:
		flag = 0;
		if (ioctl(f, DIOCWLABEL, (char *)&flag) < 0)
			err(4, "ioctl DIOCWLABEL");
		break;

	case READ:
		if (argc != 1)
			usage();
		lp = readlabel(f);
		display(stdout, lp);
		error = checklabel(lp);
		break;

	case RESTORE:
		if (installboot && argc == 3) {
			makelabel(argv[2], 0, &lab);
			argc--;

			/*
			 * We only called makelabel() for its side effect
			 * of setting the bootstrap file names.  Discard
			 * all changes to `lab' so that all values in the
			 * final label come from the ASCII label.
			 */
			bzero((char *)&lab, sizeof(lab));
		}
		if (argc != 2)
			usage();
		if (!(t = fopen(argv[1], "r")))
			err(4, "%s", argv[1]);
		if (!getasciilabel(t, &lab))
			exit(1);
		lp = makebootarea(f);
		bcopy(&lab.d_magic, &lp->d_magic,
		      sizeof(lab) - offsetof(struct disklabel64, d_magic));
		error = writelabel(f, lp);
		break;

	case WRITE:
		if (argc == 3) {
			name = argv[2];
			argc--;
		}
		if (argc != 2)
			usage();
		makelabel(argv[1], name, &lab);
		lp = makebootarea(f);
		bcopy(&lab.d_magic, &lp->d_magic,
		      sizeof(lab) - offsetof(struct disklabel64, d_magic));
		if (checklabel(lp) == 0)
			error = writelabel(f, lp);
		break;

	case WRITEABLE:
		flag = 1;
		if (ioctl(f, DIOCWLABEL, (char *)&flag) < 0)
			err(4, "ioctl DIOCWLABEL");
		break;

	case WRITEBOOT:
	{
		struct disklabel64 tlab;

		lp = readlabel(f);
		tlab = *lp;
		if (argc == 2)
			makelabel(argv[1], 0, &lab);
		lp = makebootarea(f);
		bcopy(&tlab.d_magic, &lp->d_magic,
		      sizeof(tlab) - offsetof(struct disklabel64, d_magic));
		if (checklabel(lp) == 0)
			error = writelabel(f, lp);
		break;
	}
	}
	exit(error);
}

/*
 * Construct a prototype disklabel from /etc/disktab.  As a side
 * effect, set the names of the primary and secondary boot files
 * if specified.
 */
void
makelabel(const char *type, const char *name, struct disklabel64 *lp)
{
	struct disklabel64 *dp;

	if (strcmp(type, "auto") == 0)
		dp = getvirginlabel();
	else
		dp = NULL;
	if (dp == NULL)
		errx(1, "%s: unknown disk type", type);
	*lp = *dp;

	/*
	 * NOTE: boot control files may no longer be specified in disktab.
	 */
	if (name)
		strncpy(lp->d_packname, name, sizeof(lp->d_packname));
}

int
writelabel(int f, struct disklabel64 *lp)
{
	struct disklabel64 *blp;
	int flag;
	int r;
	size_t lpsize;
	size_t lpcrcsize;

	lpsize = offsetof(struct disklabel64, d_partitions[lp->d_npartitions]);
	lpcrcsize = lpsize - offsetof(struct disklabel64, d_magic);

	if (disable_write) {
		Warning("write to disk label suppressed - label was as follows:");
		display(stdout, lp);
		return (0);
	} else {
		lp->d_magic = DISKMAGIC64;
		lp->d_crc = 0;
		lp->d_crc = crc32(&lp->d_magic, lpcrcsize);
		if (rflag) {
			/*
			 * Make sure the boot area is not too large
			 */
			if (boot2buf) {
				int lpbsize = (int)(lp->d_pbase - lp->d_bbase);
				if (lp->d_pbase == 0) {
					errx(1, "no space was set aside in "
						"the disklabel for boot2!");
				}
				if (boot2size > lpbsize) {
					errx(1, "label did not reserve enough "
						"space for boot!  %d/%d",
					     boot2size, lpbsize);
				}
			}

			/*
			 * First set the kernel disk label,
			 * then write a label to the raw disk.
			 * If the SDINFO ioctl fails because it is
			 * unimplemented, keep going; otherwise, the kernel
			 * consistency checks may prevent us from changing
			 * the current (in-core) label.
			 */
			if (ioctl(f, DIOCSDINFO64, lp) < 0 &&
				errno != ENODEV && errno != ENOTTY) {
				l_perror("ioctl DIOCSDINFO");
				return (1);
			}
			lseek(f, (off_t)0, SEEK_SET);

			/*
			 * The disklabel embeds areas which we may not
			 * have wanted to change.  Merge those areas in
			 * from disk.
			 */
			blp = makebootarea(f);
			if (blp != lp) {
				bcopy(&lp->d_magic, &blp->d_magic,
				      sizeof(*lp) -
				      offsetof(struct disklabel64, d_magic));
			}
			
			/*
			 * write enable label sector before write
			 * (if necessary), disable after writing.
			 */
			flag = 1;
			if (ioctl(f, DIOCWLABEL, &flag) < 0)
				warn("ioctl DIOCWLABEL");

			r = write(f, boot1buf, boot1lsize);
			if (r != (ssize_t)boot1lsize) {
				warn("write");
				return (1);
			}
			/*
			 * Output the remainder of the disklabel
			 */
			if (boot2buf) {
				lseek(f, lp->d_bbase, 0);
				r = write(f, boot2buf, boot2size);
				if (r != boot2size) {
					warn("write");
					return(1);
				}
			}
			flag = 0;
			ioctl(f, DIOCWLABEL, &flag);
		} else if (ioctl(f, DIOCWDINFO64, lp) < 0) {
			l_perror("ioctl DIOCWDINFO64");
			return (1);
		}
	}
	return (0);
}

void
l_perror(const char *s)
{
	switch (errno) {

	case ESRCH:
		warnx("%s: no disk label on disk;", s);
		fprintf(stderr, "add \"-r\" to install initial label\n");
		break;

	case EINVAL:
		warnx("%s: label magic number or checksum is wrong!", s);
		fprintf(stderr, "(disklabel or kernel is out of date?)\n");
		break;

	case EBUSY:
		warnx("%s: open partition would move or shrink", s);
		break;

	case ENOATTR:
		warnx("%s: the disk already has a label of a different type,\n"
		      "probably a 32 bit disklabel.  It must be cleaned out "
		      "first.\n", s);
		break;

	default:
		warn(NULL);
		break;
	}
}

/*
 * Fetch disklabel for disk.
 * Use ioctl to get label unless -r flag is given.
 */
struct disklabel64 *
readlabel(int f)
{
	struct disklabel64 *lp;
	u_int32_t savecrc;
	size_t lpcrcsize;

	if (rflag) {
		/*
		 * Allocate space for the label.  The boot1 code, if any,
		 * is embedded in the label.  The label overlaps the boot1
		 * code.
		 */
		lp = makebootarea(f);
		lpcrcsize = offsetof(struct disklabel64,
				     d_partitions[lp->d_npartitions]) -
			    offsetof(struct disklabel64, d_magic);
		savecrc = lp->d_crc;
		lp->d_crc = 0;
		if (lp->d_magic != DISKMAGIC64)
			errx(1, "bad pack magic number");
		if (lp->d_npartitions > MAXPARTITIONS64 ||
		    savecrc != crc32(&lp->d_magic, lpcrcsize)
		) {
			errx(1, "corrupted disklabel64");
		}
		lp->d_crc = savecrc;
	} else {
		/*
		 * Just use a static structure to hold the label.  Note
		 * that DIOCSDINFO64 does not overwrite the boot1 area
		 * even though it is part of the disklabel64 structure.
		 */
		lp = &lab;
		if (Vflag) {
			if (ioctl(f, DIOCGDVIRGIN64, lp) < 0) {
				l_perror("ioctl DIOCGDVIRGIN64");
				exit(4);
			}
		} else {
			if (ioctl(f, DIOCGDINFO64, lp) < 0) {
				l_perror("ioctl DIOCGDINFO64");
				exit(4);
			}
		}
	}
	return (lp);
}

/*
 * Construct a boot area for boot1 and boot2 and return the location of
 * the label within the area.  The caller will overwrite the label so
 * we don't actually have to read it.
 */
struct disklabel64 *
makebootarea(int f)
{
	struct disklabel64 *lp;
	struct partinfo info;
	u_int32_t secsize;
	struct stat st;
	int fd;
	int r;

	if (ioctl(f, DIOCGPART, &info) == 0)
		secsize = info.media_blksize;
	else
		secsize = 512;

	if (boot1buf == NULL) {
		size_t rsize;

		rsize = (sizeof(struct disklabel64) + secsize - 1) &
			~(secsize - 1);
		boot1size = offsetof(struct disklabel64, d_magic);
		boot1lsize = rsize;
		boot1buf = malloc(rsize);
		bzero(boot1buf, rsize);
		r = read(f, boot1buf, rsize);
		if (r != (int)rsize)
			err(4, "%s", specname);
	}
	lp = (void *)boot1buf;

	if (installboot == 0)
		return(lp);

	if (boot2buf == NULL) {
		boot2size = 32768;
		boot2buf = malloc(boot2size);
		bzero(boot2buf, boot2size);
	}

	/*
	 * If installing the boot code, read it into the appropriate portions
	 * of the buffer(s)
	 */
	if (boot1path == NULL)
		asprintf(&boot1path, "%s/boot1_64", _PATH_BOOTDIR);
	if (boot2path == NULL)
		asprintf(&boot2path, "%s/boot2_64", _PATH_BOOTDIR);

	if ((fd = open(boot1path, O_RDONLY)) < 0)
		err(4, "%s", boot1path);
	if (fstat(fd, &st) < 0)
		err(4, "%s", boot1path);
	if (st.st_size > boot1size)
		err(4, "%s must be exactly %d bytes!", boot1path, boot1size);
	if (read(fd, boot1buf, boot1size) != boot1size)
		err(4, "%s must be exactly %d bytes!", boot1path, boot1size);
	close(fd);

	if ((fd = open(boot2path, O_RDONLY)) < 0)
		err(4, "%s", boot2path);
	if (fstat(fd, &st) < 0)
		err(4, "%s", boot2path);
	if (st.st_size > boot2size)
		err(4, "%s must be <= %d bytes!", boot2path, boot2size);
	if ((r = read(fd, boot2buf, boot2size)) < 1)
		err(4, "%s is empty!", boot2path);
	boot2size = (r + secsize - 1) & ~(secsize - 1);
	close(fd);

	/*
	 * XXX dangerously dedicated support goes here XXX
	 */
	return (lp);
}

void
display(FILE *f, const struct disklabel64 *lp)
{
	const struct partition64 *pp;
	char *str;
	unsigned int part;
	int didany;
	uint32_t blksize;

	/*
	 * Use a human readable block size if possible.  This is for
	 * display and editing purposes only.
	 */
	if (lp->d_align > 1024)
		blksize = 1024;
	else
		blksize = lp->d_align;

	fprintf(f, "# %s:\n", specname);
	fprintf(f, "#\n");
	fprintf(f, "# Informational fields calculated from the above\n");
	fprintf(f, "# All byte equivalent offsets must be aligned\n");
	fprintf(f, "#\n");
	fprintf(f, "# boot space: %10ju bytes\n",
		(intmax_t)(lp->d_pbase - lp->d_bbase));
	fprintf(f, "# data space: %10ju blocks\t# %6.2f MB (%ju bytes)\n",
			(intmax_t)(lp->d_pstop - lp->d_pbase) / blksize,
			(double)(lp->d_pstop - lp->d_pbase) / 1024.0 / 1024.0,
			(intmax_t)(lp->d_pstop - lp->d_pbase));
	fprintf(f, "#\n");
	fprintf(f, "# NOTE: If the partition data base looks odd it may be\n");
	fprintf(f, "#       physically aligned instead of slice-aligned\n");
	fprintf(f, "#\n");

	uuid_to_string(&lp->d_stor_uuid, &str, NULL);
	fprintf(f, "diskid: %s\n", str ? str : "<unknown>");
	free(str);

	fprintf(f, "label: %.*s\n", (int)sizeof(lp->d_packname),
		lp->d_packname);
	fprintf(f, "boot2 data base:      0x%012jx\n", (intmax_t)lp->d_bbase);
	fprintf(f, "partitions data base: 0x%012jx\n", (intmax_t)lp->d_pbase);
	fprintf(f, "partitions data stop: 0x%012jx\n", (intmax_t)lp->d_pstop);
	fprintf(f, "backup label:         0x%012jx\n", (intmax_t)lp->d_abase);
	fprintf(f, "total size:           0x%012jx\t# %6.2f MB\n",
		(intmax_t)lp->d_total_size,
		(double)lp->d_total_size / 1024.0 / 1024.0);
	fprintf(f, "alignment: %u\n", lp->d_align);
	fprintf(f, "display block size: %u\t# for partition display only\n",
		blksize);

	fprintf(f, "\n");
	fprintf(f, "%u partitions:\n", lp->d_npartitions);
	fprintf(f, "#          size     offset    fstype   fsuuid\n");
	didany = 0;
	for (part = 0; part < lp->d_npartitions; part++) {
		pp = &lp->d_partitions[part];
		const u_long onemeg = 1024 * 1024;

		if (pp->p_bsize == 0)
			continue;
		didany = 1;
		fprintf(f, "  %c: ", 'a' + part);

		if (pp->p_bsize % lp->d_align)
		    fprintf(f, "%10s  ", "ILLEGAL");
		else
		    fprintf(f, "%10ju ", (intmax_t)pp->p_bsize / blksize);

		if ((pp->p_boffset - lp->d_pbase) % lp->d_align)
		    fprintf(f, "%10s  ", "ILLEGAL");
		else
		    fprintf(f, "%10ju  ",
			    (intmax_t)(pp->p_boffset - lp->d_pbase) / blksize);

		if (pp->p_fstype < FSMAXTYPES)
			fprintf(f, "%8.8s", fstypenames[pp->p_fstype]);
		else
			fprintf(f, "%8d", pp->p_fstype);
		fprintf(f, "\t# %11.3fMB", (double)pp->p_bsize / onemeg);
		fprintf(f, "\n");
	}
	for (part = 0; part < lp->d_npartitions; part++) {
		pp = &lp->d_partitions[part];

		if (pp->p_bsize == 0)
			continue;

		if (uuid_is_nil(&lp->d_stor_uuid, NULL) == 0) {
			fprintf(f, "  %c-stor_uuid: ", 'a' + part);
			str = NULL;
			uuid_to_string(&pp->p_stor_uuid, &str, NULL);
			if (str) {
				fprintf(f, "%s", str);
				free(str);
			}
			fprintf(f, "\n");
		}
	}
	if (didany == 0) {
		fprintf(f, "# EXAMPLE\n");
		fprintf(f, "#a:          4g          0    4.2BSD\n");
		fprintf(f, "#a:          *           *    4.2BSD\n");

	}
	fflush(f);
}

int
edit(struct disklabel64 *lp, int f)
{
	int c, fd;
	struct disklabel64 label;
	FILE *fp;

	if ((fd = mkstemp(tmpfil)) == -1 ||
	    (fp = fdopen(fd, "w")) == NULL) {
		warnx("can't create %s", tmpfil);
		return (1);
	}
	display(fp, lp);
	fclose(fp);
	for (;;) {
		if (!editit())
			break;
		fp = fopen(tmpfil, "r");
		if (fp == NULL) {
			warnx("can't reopen %s for reading", tmpfil);
			break;
		}
		bzero((char *)&label, sizeof(label));
		if (getasciilabel(fp, &label)) {
			*lp = label;
			if (writelabel(f, lp) == 0) {
				fclose(fp);
				unlink(tmpfil);
				return (0);
			}
		}
		fclose(fp);
		printf("re-edit the label? [y]: "); fflush(stdout);
		c = getchar();
		if (c != EOF && c != (int)'\n')
			while (getchar() != (int)'\n')
				;
		if  (c == (int)'n')
			break;
	}
	unlink(tmpfil);
	return (1);
}

int
editit(void)
{
	int pid, xpid;
	int status, omask;
	const char *ed;

	omask = sigblock(sigmask(SIGINT)|sigmask(SIGQUIT)|sigmask(SIGHUP));
	while ((pid = fork()) < 0) {
		if (errno == EPROCLIM) {
			warnx("you have too many processes");
			return(0);
		}
		if (errno != EAGAIN) {
			warn("fork");
			return(0);
		}
		sleep(1);
	}
	if (pid == 0) {
		sigsetmask(omask);
		setgid(getgid());
		setuid(getuid());
		if ((ed = getenv("EDITOR")) == NULL)
			ed = DEFEDITOR;
		execlp(ed, ed, tmpfil, NULL);
		err(1, "%s", ed);
	}
	while ((xpid = wait(&status)) >= 0)
		if (xpid == pid)
			break;
	sigsetmask(omask);
	return(!status);
}

char *
skip(char *cp)
{

	while (*cp != '\0' && isspace(*cp))
		cp++;
	if (*cp == '\0' || *cp == '#')
		return (NULL);
	return (cp);
}

char *
word(char *cp)
{
	char c;

	while (*cp != '\0' && !isspace(*cp) && *cp != '#')
		cp++;
	if ((c = *cp) != '\0') {
		*cp++ = '\0';
		if (c != '#')
			return (skip(cp));
	}
	return (NULL);
}

/*
 * Read an ascii label in from fd f,
 * in the same format as that put out by display(),
 * and fill in lp.
 */
int
getasciilabel(FILE *f, struct disklabel64 *lp)
{
	char *cp;
	u_int part;
	char *tp, line[BUFSIZ];
	u_long v;
	uint32_t blksize = 0;
	uint64_t vv;
	int lineno = 0, errors = 0;
	char empty[] = "";

	bzero(&part_set, sizeof(part_set));
	bzero(&part_size_type, sizeof(part_size_type));
	bzero(&part_offset_type, sizeof(part_offset_type));
	while (fgets(line, sizeof(line) - 1, f)) {
		lineno++;
		if ((cp = strchr(line,'\n')) != NULL)
			*cp = '\0';
		cp = skip(line);
		if (cp == NULL)
			continue;
		tp = strchr(cp, ':');
		if (tp == NULL) {
			fprintf(stderr, "line %d: syntax error\n", lineno);
			errors++;
			continue;
		}
		*tp++ = '\0', tp = skip(tp);
		if (sscanf(cp, "%lu partitions", &v) == 1) {
			if (v == 0 || v > MAXPARTITIONS64) {
				fprintf(stderr,
				    "line %d: bad # of partitions\n", lineno);
				lp->d_npartitions = MAXPARTITIONS64;
				errors++;
			} else
				lp->d_npartitions = v;
			continue;
		}
		if (tp == NULL)
			tp = empty;

		if (streq(cp, "diskid")) {
			uint32_t status = 0;
			uuid_from_string(tp, &lp->d_stor_uuid, &status);
			if (status != uuid_s_ok) {
				fprintf(stderr,
				    "line %d: %s: illegal UUID\n",
				    lineno, tp);
				errors++;
			}
			continue;
		}
		if (streq(cp, "label")) {
			strncpy(lp->d_packname, tp, sizeof (lp->d_packname));
			continue;
		}

		if (streq(cp, "alignment")) {
			v = strtoul(tp, NULL, 0);
			if (v <= 0 || (v & DEV_BMASK) != 0 || v > 1024*1024) {
				fprintf(stderr,
				    "line %d: %s: bad alignment\n",
				    lineno, tp);
				errors++;
			} else {
				lp->d_align = v;
			}
			continue;
		}
		if (streq(cp, "total size")) {
			vv = strtoull(tp, NULL, 0);
			if (vv == 0 || vv == (uint64_t)-1) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else {
				lp->d_total_size = vv;
			}
			continue;
		}
		if (streq(cp, "boot2 data base")) {
			vv = strtoull(tp, NULL, 0);
			if (vv == 0 || vv == (uint64_t)-1) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else {
				lp->d_bbase = vv;
			}
			continue;
		}
		if (streq(cp, "partitions data base")) {
			vv = strtoull(tp, NULL, 0);
			if (vv == 0 || vv == (uint64_t)-1) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else {
				lp->d_pbase = vv;
			}
			continue;
		}
		if (streq(cp, "partitions data stop")) {
			vv = strtoull(tp, NULL, 0);
			if (vv == 0 || vv == (uint64_t)-1) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else {
				lp->d_pstop = vv;
			}
			continue;
		}
		if (streq(cp, "backup label")) {
			vv = strtoull(tp, NULL, 0);
			if (vv == 0 || vv == (uint64_t)-1) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else {
				lp->d_abase = vv;
			}
			continue;
		}
		if (streq(cp, "display block size")) {
			v = strtoul(tp, NULL, 0);
			if (v <= 0 || (v & DEV_BMASK) != 0 || v > 1024*1024) {
				fprintf(stderr,
				    "line %d: %s: bad alignment\n",
				    lineno, tp);
				errors++;
			} else {
				blksize = v;
			}
			continue;
		}

		/* the ':' was removed above */

		/*
		 * Handle main partition data, e.g. a:, b:, etc.
		 */
		if (*cp < 'a' || *cp > MAX_PART) {
			fprintf(stderr,
			    "line %d: %s: Unknown disklabel field\n", lineno,
			    cp);
			errors++;
			continue;
		}

		/* Process a partition specification line. */
		part = *cp - 'a';
		if (part >= lp->d_npartitions) {
			fprintf(stderr,
			    "line %d: partition name out of range a-%c: %s\n",
			    lineno, 'a' + lp->d_npartitions - 1, cp);
			errors++;
			continue;
		}

		if (blksize == 0) {
			fprintf(stderr, "block size to use for partition "
					"display was not specified!\n");
			errors++;
			continue;
		}

		if (strcmp(cp + 1, "-stor_uuid") == 0) {
			if (getasciipartuuid(tp, lp, part, lineno, blksize)) {
				errors++;
				break;
			}
			continue;
		} else if (cp[1] == 0) {
			part_set[part] = 1;
			if (getasciipartspec(tp, lp, part, lineno, blksize)) {
				errors++;
				break;
			}
			continue;
		}
		fprintf(stderr, "line %d: %s: Unknown disklabel field\n",
			lineno, cp);
		errors++;
		continue;
	}
	errors += checklabel(lp);
	return (errors == 0);
}

static
int
parse_field_val(char **tp, char **cp, u_int64_t *vv, int lineno)
{
	char *tmp;

	if (*tp == NULL || **tp == 0) {
		fprintf(stderr, "line %d: too few numeric fields\n", lineno);
		return(-1);
	}
	*cp = *tp;
	*tp = word(*cp);
	*vv = strtoull(*cp, &tmp, 0);
	if (*vv == ULLONG_MAX) {
		fprintf(stderr, "line %d: illegal number\n", lineno);
		return(-1);
	}
	if (tmp)
		return(*tmp);
	else
		return(0);
}

/*
 * Read a partition line into partition `part' in the specified disklabel.
 * Return 0 on success, 1 on failure.
 */
int
getasciipartspec(char *tp, struct disklabel64 *lp, int part,
		 int lineno, uint32_t blksize)
{
	struct partition64 *pp;
	char *cp;
	const char **cpp;
	int r;
	u_long v;
	uint64_t vv;
	uint64_t mpx;

	pp = &lp->d_partitions[part];
	cp = NULL;

	/*
	 * size
	 */
	r = parse_field_val(&tp, &cp, &vv, lineno);
	if (r < 0)
		return (1);

	mpx = 1;
	switch(r) {
	case 0:
		mpx = blksize;
		break;
	case '%':
		/* mpx = 1; */
		break;
	case '*':
		mpx = 0;
		break;
	case 't':
	case 'T':
		mpx *= 1024ULL;
		/* fall through */
	case 'g':
	case 'G':
		mpx *= 1024ULL;
		/* fall through */
	case 'm':
	case 'M':
		mpx *= 1024ULL;
		/* fall through */
	case 'k':
	case 'K':
		mpx *= 1024ULL;
		r = 0;			/* eat the suffix */
		break;
	default:
		Warning("unknown size specifier '%c' (*/%%/K/M/G/T are valid)",
			r);
		return(1);
	}

	part_size_type[part] = r;
	if (vv == 0 && r != '*') {
		fprintf(stderr,
		    "line %d: %s: bad partition size (0)\n", lineno, cp);
		return (1);
	}
	pp->p_bsize = vv * mpx;

	/*
	 * offset
	 */
	r = parse_field_val(&tp, &cp, &vv, lineno);
	if (r < 0)
		return (1);
	part_offset_type[part] = r;
	switch(r) {
	case '*':
		pp->p_boffset = 0;
		break;
	case 0:
		pp->p_boffset = vv * blksize + lp->d_pbase;
		break;
	default:
		fprintf(stderr,
		    "line %d: %s: bad suffix on partition offset (%c)\n",
		    lineno, cp, r);
		return (1);
	}

	/*
	 * fstype
	 */
	if (tp == NULL) {
		fprintf(stderr,
		    "line %d: no filesystem type was specified\n", lineno);
		return(1);
	}
	cp = tp;
	tp = word(cp);
	for (cpp = fstypenames; cpp < &fstypenames[FSMAXTYPES]; cpp++) {
		if (*cpp && strcasecmp(*cpp, cp) == 0)
			break;
	}
	if (*cpp != NULL) {
		pp->p_fstype = cpp - fstypenames;
	} else {
		if (isdigit(*cp))
			v = strtoul(cp, NULL, 0);
		else
			v = FSMAXTYPES;
		if (v >= FSMAXTYPES) {
			fprintf(stderr,
			    "line %d: Warning, unknown filesystem type %s\n",
			    lineno, cp);
			v = FS_UNUSED;
		}
		pp->p_fstype = v;
	}

	cp = tp;
	if (tp) {
		fprintf(stderr, "line %d: Warning, extra data on line\n",
			lineno);
	}
	return(0);
}

int
getasciipartuuid(char *tp, struct disklabel64 *lp, int part,
		 int lineno, uint32_t blksize __unused)
{
	struct partition64 *pp;
	uint32_t status;
	char *cp;

	pp = &lp->d_partitions[part];

	cp = tp;
	tp = word(cp);
	uuid_from_string(cp, &pp->p_stor_uuid, &status);
	if (status != uuid_s_ok) {
		fprintf(stderr, "line %d: Illegal storage uuid specification\n",
			lineno);
		return(1);
	}
	return(0);
}

/*
 * Check disklabel for errors and fill in
 * derived fields according to supplied values.
 */
int
checklabel(struct disklabel64 *lp)
{
	struct partition64 *pp;
	int errors = 0;
	char part;
	u_int64_t total_size;
	u_int64_t current_offset;
	u_long total_percent;
	int seen_default_offset;
	int hog_part;
	int i, j;
	struct partition64 *pp2;
	u_int64_t off;

	if (lp->d_align < 512 ||
	    (lp->d_align ^ (lp->d_align - 1)) != lp->d_align * 2 - 1) {
		Warning("Illegal alignment specified: %u\n", lp->d_align);
		return (1);
	}
	if (lp->d_npartitions > MAXPARTITIONS64) {
		Warning("number of partitions (%u) > MAXPARTITIONS (%d)",
			lp->d_npartitions, MAXPARTITIONS64);
		return (1);
	}
	off = offsetof(struct disklabel64, d_partitions[lp->d_npartitions]);
	off = (off + lp->d_align - 1) & ~(int64_t)(lp->d_align - 1);

	if (lp->d_bbase < off || lp->d_bbase % lp->d_align) {
		Warning("illegal boot2 data base ");
		return (1);
	}

	/*
	 * pbase can be unaligned slice-relative but will be
	 * aligned physically.
	 */
	if (lp->d_pbase < lp->d_bbase) {
		Warning("illegal partition data base");
		return (1);
	}
	if (lp->d_pstop < lp->d_pbase) {
		Warning("illegal partition data stop");
		return (1);
	}
	if (lp->d_pstop > lp->d_total_size) {
		printf("%012jx\n%012jx\n",
			(intmax_t)lp->d_pstop, (intmax_t)lp->d_total_size);
		Warning("disklabel control info is beyond the total size");
		return (1);
	}
	if (lp->d_abase &&
	    (lp->d_abase < lp->d_pstop ||
	     lp->d_abase > lp->d_total_size - off)) {
		Warning("illegal backup label location");
		return (1);
	}

	/* first allocate space to the partitions, then offsets */
	total_size = 0;		/* in bytes */
	total_percent = 0;	/* in percent */
	hog_part = -1;
	/* find all fixed partitions */
	for (i = 0; i < (int)lp->d_npartitions; i++) {
		pp = &lp->d_partitions[i];
		if (part_set[i]) {
			if (part_size_type[i] == '*') {
				if (part_offset_type[i] != '*') {
					if (total_size < pp->p_boffset)
						total_size = pp->p_boffset;
				}
				if (hog_part != -1) {
					Warning("Too many '*' partitions (%c and %c)",
					    hog_part + 'a',i + 'a');
				} else {
					hog_part = i;
				}
			} else {
				off_t size;

				size = pp->p_bsize;
				if (part_size_type[i] == '%') {
					/* 
					 * don't count %'s yet
					 */
					total_percent += size;
				} else {
					/*
					 * Value has already been converted
					 * to bytes.
					 */
					if (size % lp->d_align != 0) {
						Warning("partition %c's size is not properly aligned",
							i + 'a');
					}
					total_size += size;
				}
			}
		}
	}
	/* handle % partitions - note %'s don't need to add up to 100! */
	if (total_percent != 0) {
		int64_t free_space;
		int64_t space_left;

		free_space = (int64_t)(lp->d_pstop - lp->d_pbase - total_size);
		free_space &= ~(u_int64_t)(lp->d_align - 1);

		space_left = free_space;
		if (total_percent > 100) {
			fprintf(stderr,"total percentage %lu is greater than 100\n",
			    total_percent);
			errors++;
		}

		if (free_space > 0) {
			for (i = 0; i < (int)lp->d_npartitions; i++) {
				pp = &lp->d_partitions[i];
				if (part_set[i] && part_size_type[i] == '%') {
					/* careful of overflows! and integer roundoff */
					pp->p_bsize = ((double)pp->p_bsize/100) * free_space;
					pp->p_bsize = (pp->p_bsize + lp->d_align - 1) & ~(u_int64_t)(lp->d_align - 1);
					if ((int64_t)pp->p_bsize > space_left)
						pp->p_bsize = (u_int64_t)space_left;
					total_size += pp->p_bsize;
					space_left -= pp->p_bsize;
				}
			}
		} else {
			fprintf(stderr, "%jd bytes available to give to "
					"'*' and '%%' partitions\n",
				(intmax_t)free_space);
			errors++;
			/* fix?  set all % partitions to size 0? */
		}
	}
	/* give anything remaining to the hog partition */
	if (hog_part != -1) {
		off = lp->d_pstop - lp->d_pbase - total_size;
		off &= ~(u_int64_t)(lp->d_align - 1);
		lp->d_partitions[hog_part].p_bsize = off;
		total_size = lp->d_pstop - lp->d_pbase;
	}

	/* Now set the offsets for each partition */
	current_offset = lp->d_pbase;
	seen_default_offset = 0;
	for (i = 0; i < (int)lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_bsize == 0)
			continue;
		if (part_set[i]) {
			if (part_offset_type[i] == '*') {
				pp->p_boffset = current_offset;
				seen_default_offset = 1;
			} else {
				/* allow them to be out of order for old-style tables */
				if (pp->p_boffset < current_offset && 
				    seen_default_offset &&
				    pp->p_fstype != FS_VINUM) {
					fprintf(stderr,
"Offset 0x%012jx for partition %c overlaps previous partition which ends at 0x%012jx\n",
					    (intmax_t)pp->p_boffset,
					    i + 'a',
					    (intmax_t)current_offset);
					fprintf(stderr,
"Labels with any *'s for offset must be in ascending order by sector\n");
					errors++;
				} else if (pp->p_boffset != current_offset &&
					   seen_default_offset) {
					/* 
					 * this may give unneeded warnings if 
					 * partitions are out-of-order
					 */
					Warning(
"Offset 0x%012jx for partition %c doesn't match expected value 0x%012jx",
					    pp->p_boffset, i + 'a',
					    (intmax_t)current_offset);
				}
			}
			current_offset = pp->p_boffset + pp->p_bsize; 
		}
	}

	for (i = 0; i < (int)lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_bsize == 0 && pp->p_boffset != 0)
			Warning("partition %c: size 0, but offset 0x%012jx",
				part, (intmax_t)pp->p_boffset);
		if (pp->p_bsize == 0) {
			pp->p_boffset = 0;
			continue;
		}
		if (uuid_is_nil(&pp->p_stor_uuid, NULL))
			uuid_create(&pp->p_stor_uuid, NULL);

		if (pp->p_boffset < lp->d_pbase) {
			fprintf(stderr,
			    "partition %c: offset out of bounds (%jd)\n",
			    part, (intmax_t)(pp->p_boffset - lp->d_pbase));
			errors++;
		}
		if (pp->p_boffset > lp->d_pstop) {
			fprintf(stderr,
			    "partition %c: offset out of bounds (%jd)\n",
			    part, (intmax_t)(pp->p_boffset - lp->d_pbase));
			errors++;
		}
		if (pp->p_boffset + pp->p_bsize > lp->d_pstop) {
			fprintf(stderr,
			    "partition %c: size out of bounds (%jd)\n",
			    part, (intmax_t)(pp->p_boffset - lp->d_pbase));
			errors++;
		}

		/* check for overlaps */
		/* this will check for all possible overlaps once and only once */
		for (j = 0; j < i; j++) {
			pp2 = &lp->d_partitions[j];
			if (pp->p_fstype != FS_VINUM &&
			    pp2->p_fstype != FS_VINUM &&
			    part_set[i] && part_set[j]) {
				if (pp2->p_boffset < pp->p_boffset + pp->p_bsize &&
				    (pp2->p_boffset + pp2->p_bsize > pp->p_boffset ||
					pp2->p_boffset >= pp->p_boffset)) {
					fprintf(stderr,"partitions %c and %c overlap!\n",
					    j + 'a', i + 'a');
					errors++;
				}
			}
		}
	}
	for (; i < (int)lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_bsize || pp->p_boffset)
			Warning("unused partition %c: size 0x%012jx "
				"offset 0x%012jx",
				'a' + i, (intmax_t)pp->p_bsize,
				(intmax_t)pp->p_boffset);
	}
	return (errors);
}

/*
 * When operating on a "virgin" disk, try getting an initial label
 * from the associated device driver.  This might work for all device
 * drivers that are able to fetch some initial device parameters
 * without even having access to a (BSD) disklabel, like SCSI disks,
 * most IDE drives, or vn devices.
 *
 * The device name must be given in its "canonical" form.
 */
static struct disklabel64 dlab;

struct disklabel64 *
getvirginlabel(void)
{
	struct disklabel64 *dl = &dlab;
	int f;

	if ((f = open(dkname, O_RDONLY)) == -1) {
		warn("cannot open %s", dkname);
		return (NULL);
	}

	/*
	 * Try to use the new get-virgin-label ioctl.  If it fails,
	 * fallback to the old get-disk-info ioctl.
	 */
	if (ioctl(f, DIOCGDVIRGIN64, dl) < 0) {
		l_perror("ioctl DIOCGDVIRGIN64");
		close(f);
		return (NULL);
	}
	close(f);
	return (dl);
}

/*VARARGS1*/
void
Warning(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "Warning, ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
		"usage: disklabel64 [-r] disk",
		"\t\t(to read label)",
		"       disklabel64 -w [-r] [-n] disk type [packid]",
		"\t\t(to write label with existing boot program)",
		"       disklabel64 -e [-r] [-n] disk",
		"\t\t(to edit label)",
		"       disklabel64 -R [-r] [-n] disk protofile",
		"\t\t(to restore label with existing boot program)",
		"       disklabel64 -B [-n] [-b boot1 -s boot2] disk [type]",
		"\t\t(to install boot program with existing label)",
		"       disklabel64 -w -B [-n] [-b boot1 -s boot2] disk type [packid]",
		"\t\t(to write label and boot program)",
		"       disklabel64 -R -B [-n] [-b boot1 -s boot2] disk protofile [type]",
		"\t\t(to restore label and boot program)",
		"       disklabel64 [-NW] disk",
		"\t\t(to write disable/enable label)");
	exit(1);
}
