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
 * @(#) Copyright (c) 1987, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)disklabel.c	1.2 (Symmetric) 11/28/85
 * @(#)disklabel.c      8.2 (Berkeley) 1/7/94
 * $FreeBSD: src/sbin/disklabel/disklabel.c,v 1.28.2.15 2003/01/24 16:18:16 des Exp $
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define DKTYPENAMES
#include <sys/disklabel32.h>
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
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include "pathnames.h"

/*
 * Disklabel32: read and write 32 bit disklabels.
 * The label is usually placed on one of the first sectors of the disk.
 * Many machines also place a bootstrap in the same area,
 * in which case the label is embedded in the bootstrap.
 * The bootstrap source must leave space at the proper offset
 * for the label on such machines.
 */

#ifndef BBSIZE
#define	BBSIZE	8192			/* size of boot area, with label */
#endif

/* FIX!  These are too low, but are traditional */
#define DEFAULT_NEWFS_BLOCK  8192U
#define DEFAULT_NEWFS_FRAG   1024U
#define DEFAULT_NEWFS_CPG    16U

#define BIG_NEWFS_BLOCK  16384U
#define BIG_NEWFS_FRAG   2048U
#define BIG_NEWFS_CPG    64U

#define	NUMBOOT	2

void	makelabel(const char *, const char *, struct disklabel32 *);
int	writelabel(int, const char *, struct disklabel32 *);
void	l_perror(const char *);
struct disklabel32 *readlabel(int);
struct disklabel32 *makebootarea(char *, struct disklabel32 *, int);
void	display(FILE *, const struct disklabel32 *);
int	edit(struct disklabel32 *, int);
int	editit(void);
char	*skip(char *);
char	*word(char *);
int	getasciilabel(FILE *, struct disklabel32 *);
int	getasciipartspec(char *, struct disklabel32 *, int, int);
int	checklabel(struct disklabel32 *);
void	setbootflag(struct disklabel32 *);
void	Warning(const char *, ...) __printflike(1, 2);
void	usage(void);
int	checkoldboot(int, const char *);
const char *fixlabel(int, struct disklabel32 *, int);
struct disklabel32 *getvirginlabel(void);
struct disklabel32 *getdisklabelfromdisktab(const char *name);

#define	DEFEDITOR	_PATH_VI
#define	streq(a,b)	(strcmp(a,b) == 0)

char	*dkname;
char	*specname;
char	tmpfil[] = PATH_TMPFILE;

char	namebuf[BBSIZE];
struct	disklabel32 lab;
char	bootarea[BBSIZE];

#define MAX_PART ('z')
#define MAX_NUM_PARTS (1 + MAX_PART - 'a')
char    part_size_type[MAX_NUM_PARTS];
char    part_offset_type[MAX_NUM_PARTS];
int     part_set[MAX_NUM_PARTS];

#if NUMBOOT > 0
int	installboot;	/* non-zero if we should install a boot program */
char	*bootbuf;	/* pointer to buffer with remainder of boot prog */
int	bootsize;	/* size of remaining boot program */
char	*xxboot;	/* primary boot */
char	*bootxx;	/* secondary boot */
char	boot0[MAXPATHLEN];
char	boot1[MAXPATHLEN];
#endif

enum	{
	UNSPEC, EDIT, NOWRITE, READ, RESTORE, WRITE, WRITEABLE, WRITEBOOT
} op = UNSPEC;

int	rflag;
int	disable_write;   /* set to disable writing to disk label */
int	forceflag;
u_int32_t slice_start_lba;

#ifdef DEBUG
int	debug;
#define OPTIONS	"BNRWb:def:nrs:w"
#else
#define OPTIONS	"BNRWb:ef:nrs:w"
#endif

int
main(int argc, char *argv[])
{
	struct disklabel32 *lp;
	FILE *t;
	int ch, f = 0, flag, error = 0;
	char *name = NULL;

	while ((ch = getopt(argc, argv, OPTIONS)) != -1)
		switch (ch) {
#if NUMBOOT > 0
			case 'B':
				++installboot;
				break;
			case 'b':
				xxboot = optarg;
				break;

			case 'f':
				forceflag = 1;
				slice_start_lba = strtoul(optarg, NULL, 0);
				break;
#if NUMBOOT > 1
			case 's':
				bootxx = optarg;
				break;
#endif
#endif
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
#if NUMBOOT > 0
	if (installboot) {
		rflag++;
		if (op == UNSPEC)
			op = WRITEBOOT;
	} else {
		if (op == UNSPEC)
			op = READ;
		xxboot = bootxx = NULL;
	}
#else
	if (op == UNSPEC)
		op = READ;
#endif
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
		if (checkoldboot(f, NULL))
			warnx("Warning, old bootblocks detected, install new bootblocks & reinstall the disklabel");
		break;

	case RESTORE:
#if NUMBOOT > 0
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
#endif
		if (argc != 2)
			usage();
		if (!(t = fopen(argv[1], "r")))
			err(4, "%s", argv[1]);
		if (!getasciilabel(t, &lab))
			exit(1);
		lp = makebootarea(bootarea, &lab, f);
		*lp = lab;
		error = writelabel(f, bootarea, lp);
		break;

	case WRITE:
		if (argc == 3) {
			name = argv[2];
			argc--;
		}
		if (argc != 2)
			usage();
		makelabel(argv[1], name, &lab);
		lp = makebootarea(bootarea, &lab, f);
		*lp = lab;
		if (checklabel(lp) == 0)
			error = writelabel(f, bootarea, lp);
		break;

	case WRITEABLE:
		flag = 1;
		if (ioctl(f, DIOCWLABEL, (char *)&flag) < 0)
			err(4, "ioctl DIOCWLABEL");
		break;

#if NUMBOOT > 0
	case WRITEBOOT:
	{
		struct disklabel32 tlab;

		lp = readlabel(f);
		tlab = *lp;
		if (argc == 2)
			makelabel(argv[1], 0, &lab);
		lp = makebootarea(bootarea, &lab, f);
		*lp = tlab;
		if (checklabel(lp) == 0)
			error = writelabel(f, bootarea, lp);
		break;
	}
#endif
	}
	exit(error);
}

/*
 * Construct a prototype disklabel from /etc/disktab.  As a side
 * effect, set the names of the primary and secondary boot files
 * if specified.
 */
void
makelabel(const char *type, const char *name, struct disklabel32 *lp)
{
	struct disklabel32 *dp;

	if (strcmp(type, "auto") == 0)
		dp = getvirginlabel();
	else
		dp = getdisklabelfromdisktab(type);
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
writelabel(int f, const char *boot, struct disklabel32 *lp)
{
	const char *msg;
	int flag;
	int r;

	if (disable_write) {
		Warning("write to disk label suppressed - label was as follows:");
		display(stdout, lp);
		return (0);
	} else {
		/* make sure we are not overwriting our boot code */
		if (checkoldboot(f, boot))
			errx(4, "Will not overwrite old bootblocks w/ label, install new boot blocks first!");
		setbootflag(lp);
		lp->d_magic = DISKMAGIC32;
		lp->d_magic2 = DISKMAGIC32;
		lp->d_checksum = 0;
		lp->d_checksum = dkcksum32(lp);
		if (rflag) {
			/*
			 * First set the kernel disk label,
			 * then write a label to the raw disk.
			 * If the SDINFO ioctl fails because it is unimplemented,
			 * keep going; otherwise, the kernel consistency checks
			 * may prevent us from changing the current (in-core)
			 * label.
			 */
			if (ioctl(f, DIOCSDINFO32, lp) < 0 &&
				errno != ENODEV && errno != ENOTTY) {
				l_perror("ioctl DIOCSDINFO32");
				return (1);
			}
			lseek(f, (off_t)0, SEEK_SET);

			/*
			 * write enable label sector before write
			 * (if necessary), disable after writing.
			 */
			flag = 1;
			if (ioctl(f, DIOCWLABEL, &flag) < 0)
				warn("ioctl DIOCWLABEL");
			msg = fixlabel(f, lp, 1);
			if (msg) {
				warn("%s", msg);
				return (1);
			}
			r = write(f, boot, lp->d_bbsize);
			fixlabel(f, lp, 0);
			if (r != ((ssize_t)lp->d_bbsize)) {
				warn("write");
				return (1);
			}
#if NUMBOOT > 0
			/*
			 * Output the remainder of the disklabel
			 */
			if (bootbuf) {
				fixlabel(f, lp, 1);
				r = write(f, bootbuf, bootsize);
				fixlabel(f, lp, 0);
				if (r != bootsize) {
					warn("write");
					return(1);
				}
			}
#endif
			flag = 0;
			ioctl(f, DIOCWLABEL, &flag);
		} else if (ioctl(f, DIOCWDINFO32, lp) < 0) {
			l_perror("ioctl DIOCWDINFO32");
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

	case EXDEV:
		warnx("%s: '%c' partition must start at beginning of disk",
		    s, 'a' + RAW_PART);
		break;

	case ENOATTR:
		warnx("%s: the disk already has a label of a different type,\n"
		      "probably a 64 bit disklabel.  It must be cleaned out "
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
struct disklabel32 *
readlabel(int f)
{
	const char *msg;
	struct disklabel32 *lp;
	int r;

	if (rflag) {
		r = read(f, bootarea, BBSIZE);
		if (r < BBSIZE)
			err(4, "%s", specname);
		for (lp = (struct disklabel32 *)bootarea;
		    lp <= (struct disklabel32 *)(bootarea + BBSIZE - sizeof(*lp));
		    lp = (struct disklabel32 *)((char *)lp + 16)) {
			if (lp->d_magic == DISKMAGIC32 &&
			    lp->d_magic2 == DISKMAGIC32)
				break;
		}
		if (lp > (struct disklabel32 *)(bootarea+BBSIZE-sizeof(*lp)) ||
		    lp->d_magic != DISKMAGIC32 || lp->d_magic2 != DISKMAGIC32 ||
		    dkcksum32(lp) != 0) {
			errx(1, "bad pack magic number (label is damaged, "
				"or pack is unlabeled)");
		}
		if ((msg = fixlabel(f, lp, 0)) != NULL)
			errx(1, "%s", msg);
	} else {
		lp = &lab;
		if (ioctl(f, DIOCGDINFO32, lp) < 0) {
			l_perror("ioctl DIOCGDINFO32");
			exit(4);
		}
	}
	return (lp);
}

/*
 * Construct a bootarea (d_bbsize bytes) in the specified buffer ``boot''
 * Returns a pointer to the disklabel portion of the bootarea.
 */
struct disklabel32 *
makebootarea(char *boot, struct disklabel32 *dp, int f)
{
	struct disklabel32 *lp;
	char *p;
	int b;
#if NUMBOOT > 0
	struct stat sb;
#endif
#ifdef __i386__
	char *tmpbuf;
	unsigned int i, found;
#endif

	/* XXX */
	if (dp->d_secsize == 0) {
		dp->d_secsize = DEV_BSIZE;
		dp->d_bbsize = BBSIZE;
	}
	lp = (struct disklabel32 *)
		(boot + (LABELSECTOR32 * dp->d_secsize) + LABELOFFSET32);
	bzero((char *)lp, sizeof *lp);
#if NUMBOOT > 0
	/*
	 * If we are not installing a boot program but we are installing a
	 * label on disk then we must read the current bootarea so we don't
	 * clobber the existing boot.
	 */
	if (!installboot) {
		if (rflag) {
			if (read(f, boot, BBSIZE) < BBSIZE)
				err(4, "%s", specname);
			bzero((char *)lp, sizeof *lp);
		}
		return (lp);
	}
	/*
	 * We are installing a boot program.  Determine the name(s) and
	 * read them into the appropriate places in the boot area.
	 */
	if (!xxboot || !bootxx) {
		if (!xxboot) {
			sprintf(boot0, "%s/boot1", _PATH_BOOTDIR);
			xxboot = boot0;
		}
#if NUMBOOT > 1
		if (!bootxx) {
			sprintf(boot1, "%s/boot2", _PATH_BOOTDIR);
			bootxx = boot1;
		}
#endif
	}
#ifdef DEBUG
	if (debug)
		fprintf(stderr, "bootstraps: xxboot = %s, bootxx = %s\n",
			xxboot, bootxx ? bootxx : "NONE");
#endif

	/*
	 * Strange rules:
	 * 1. One-piece bootstrap (hp300/hp800)
	 *	up to d_bbsize bytes of ``xxboot'' go in bootarea, the rest
	 *	is remembered and written later following the bootarea.
	 * 2. Two-piece bootstraps (vax/i386?/mips?)
	 *	up to d_secsize bytes of ``xxboot'' go in first d_secsize
	 *	bytes of bootarea, remaining d_bbsize-d_secsize filled
	 *	from ``bootxx''.
	 */
	b = open(xxboot, O_RDONLY);
	if (b < 0)
		err(4, "%s", xxboot);
#if NUMBOOT > 1
#ifdef __i386__
	/*
	 * XXX Botch alert.
	 * The i386 has the so-called fdisk table embedded into the
	 * primary bootstrap.  We take care to not clobber it, but
	 * only if it does already contain some data.  (Otherwise,
	 * the xxboot provides a template.)
	 */
	if ((tmpbuf = (char *)malloc((int)dp->d_secsize)) == NULL)
		err(4, "%s", xxboot);
	memcpy((void *)tmpbuf, (void *)boot, (int)dp->d_secsize);
#endif /* i386 */
	if (read(b, boot, (int)dp->d_secsize) < 0)
		err(4, "%s", xxboot);
	close(b);
#ifdef __i386__
	for (i = DOSPARTOFF, found = 0;
	     !found && i < DOSPARTOFF + NDOSPART*sizeof(struct dos_partition);
	     i++)
		found = tmpbuf[i] != 0;
	if (found)
		memcpy((void *)&boot[DOSPARTOFF],
		       (void *)&tmpbuf[DOSPARTOFF],
		       NDOSPART * sizeof(struct dos_partition));
	free(tmpbuf);
#endif /* i386 */
	b = open(bootxx, O_RDONLY);
	if (b < 0)
		err(4, "%s", bootxx);
	if (fstat(b, &sb) != 0)
		err(4, "%s", bootxx);
	if (dp->d_secsize + sb.st_size > dp->d_bbsize)
		errx(4, "%s too large", bootxx);
	if (read(b, &boot[dp->d_secsize],
		 (int)(dp->d_bbsize-dp->d_secsize)) < 0)
		err(4, "%s", bootxx);
#else /* !(NUMBOOT > 1) */
	if (read(b, boot, (int)dp->d_bbsize) < 0)
		err(4, "%s", xxboot);
	if (fstat(b, &sb) != 0)
		err(4, "%s", xxboot);
	bootsize = (int)sb.st_size - dp->d_bbsize;
	if (bootsize > 0) {
		/* XXX assume d_secsize is a power of two */
		bootsize = (bootsize + dp->d_secsize-1) & ~(dp->d_secsize-1);
		bootbuf = (char *)malloc((size_t)bootsize);
		if (bootbuf == NULL)
			err(4, "%s", xxboot);
		if (read(b, bootbuf, bootsize) < 0) {
			free(bootbuf);
			err(4, "%s", xxboot);
		}
	}
#endif /* NUMBOOT > 1 */
	close(b);
#endif /* NUMBOOT > 0 */
	/*
	 * Make sure no part of the bootstrap is written in the area
	 * reserved for the label.
	 */
	for (p = (char *)lp; p < (char *)lp + sizeof(struct disklabel32); p++)
		if (*p)
			errx(2, "bootstrap doesn't leave room for disk label");
	return (lp);
}

void
display(FILE *f, const struct disklabel32 *lp)
{
	int i, j;
	const struct partition32 *pp;

	fprintf(f, "# %s:\n", specname);
	if (lp->d_type < DKMAXTYPES)
		fprintf(f, "type: %s\n", dktypenames[lp->d_type]);
	else
		fprintf(f, "type: %u\n", lp->d_type);
	fprintf(f, "disk: %.*s\n", (int)sizeof(lp->d_typename),
		lp->d_typename);
	fprintf(f, "label: %.*s\n", (int)sizeof(lp->d_packname),
		lp->d_packname);
	fprintf(f, "flags:");
	fprintf(f, "\n");
	fprintf(f, "bytes/sector: %lu\n", (u_long)lp->d_secsize);
	fprintf(f, "sectors/track: %lu\n", (u_long)lp->d_nsectors);
	fprintf(f, "tracks/cylinder: %lu\n", (u_long)lp->d_ntracks);
	fprintf(f, "sectors/cylinder: %lu\n", (u_long)lp->d_secpercyl);
	fprintf(f, "cylinders: %lu\n", (u_long)lp->d_ncylinders);
	fprintf(f, "sectors/unit: %lu\n", (u_long)lp->d_secperunit);
	fprintf(f, "rpm: %u\n", lp->d_rpm);
	fprintf(f, "interleave: %u\n", lp->d_interleave);
	fprintf(f, "trackskew: %u\n", lp->d_trackskew);
	fprintf(f, "cylinderskew: %u\n", lp->d_cylskew);
	fprintf(f, "headswitch: %lu\t\t# milliseconds\n",
	    (u_long)lp->d_headswitch);
	fprintf(f, "track-to-track seek: %ld\t# milliseconds\n",
	    (u_long)lp->d_trkseek);
	fprintf(f, "drivedata: ");
	for (i = NDDATA32 - 1; i >= 0; i--) {
		if (lp->d_drivedata[i])
			break;
	}
	if (i < 0)
		i = 0;
	for (j = 0; j <= i; j++)
		fprintf(f, "%lu ", (u_long)lp->d_drivedata[j]);
	fprintf(f, "\n\n%u partitions:\n", lp->d_npartitions);
	fprintf(f,
	    "#          size     offset    fstype\n");
	pp = lp->d_partitions;
	for (i = 0; i < lp->d_npartitions; i++, pp++) {
		if (pp->p_size) {
			u_long onemeg = 1024 * 1024 / lp->d_secsize;
			fprintf(f, "  %c: ", 'a' + i);

			fprintf(f, "%10lu ", (u_long)pp->p_size);
			fprintf(f, "%10lu  ", (u_long)pp->p_offset);
			if (pp->p_fstype < FSMAXTYPES)
				fprintf(f, "%8.8s", fstypenames[pp->p_fstype]);
			else
				fprintf(f, "%8d", pp->p_fstype);

			fprintf(f, "\t# %11.3fMB", (double)pp->p_size / onemeg);
			fprintf(f, "\n");
		}
	}
	fflush(f);
}

int
edit(struct disklabel32 *lp, int f)
{
	int c, fd;
	struct disklabel32 label;
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
			if (writelabel(f, bootarea, lp) == 0) {
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
getasciilabel(FILE *f, struct disklabel32 *lp)
{
	char *cp;
	const char **cpp;
	u_int part;
	char *tp, line[BUFSIZ];
	u_long v;
	int lineno = 0, errors = 0;
	int i;
	char empty[] = "";
	char unknown[] = "unknown";

	bzero(&part_set, sizeof(part_set));
	bzero(&part_size_type, sizeof(part_size_type));
	bzero(&part_offset_type, sizeof(part_offset_type));
	lp->d_bbsize = BBSIZE;				/* XXX */
	lp->d_sbsize = SBSIZE;				/* XXX */
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
		if (streq(cp, "type")) {
			if (tp == NULL)
				tp = unknown;
			cpp = dktypenames;
			for (; cpp < &dktypenames[DKMAXTYPES]; cpp++) {
				if (*cpp && strcasecmp(*cpp, tp) == 0) {
					lp->d_type = cpp - dktypenames;
					break;
				}
			}
			if (cpp < &dktypenames[DKMAXTYPES])
				continue;
			v = strtoul(tp, NULL, 10);
			if (v >= DKMAXTYPES) {
				fprintf(stderr, "line %d:%s %lu\n", lineno,
				    "Warning, unknown disk type", v);
			}
			lp->d_type = v;
			continue;
		}
		if (streq(cp, "flags")) {
			for (v = 0; (cp = tp) && *cp != '\0';) {
				tp = word(cp);
				if (streq(cp, "removeable"))
					v |= 0;	/* obsolete */
				else if (streq(cp, "ecc"))
					v |= 0;	/* obsolete */
				else if (streq(cp, "badsect"))
					v |= 0;	/* obsolete */
				else {
					fprintf(stderr,
					    "line %d: %s: bad flag\n",
					    lineno, cp);
					errors++;
				}
			}
			lp->d_flags = v;
			continue;
		}
		if (streq(cp, "drivedata")) {
			for (i = 0; (cp = tp) && *cp != '\0' && i < NDDATA32;) {
				lp->d_drivedata[i++] = strtoul(cp, NULL, 10);
				tp = word(cp);
			}
			continue;
		}
		if (sscanf(cp, "%lu partitions", &v) == 1) {
			if (v == 0 || v > MAXPARTITIONS32) {
				fprintf(stderr,
				    "line %d: bad # of partitions\n", lineno);
				lp->d_npartitions = MAXPARTITIONS32;
				errors++;
			} else
				lp->d_npartitions = v;
			continue;
		}
		if (tp == NULL)
			tp = empty;
		if (streq(cp, "disk")) {
			strncpy(lp->d_typename, tp, sizeof (lp->d_typename));
			continue;
		}
		if (streq(cp, "label")) {
			strncpy(lp->d_packname, tp, sizeof (lp->d_packname));
			continue;
		}
		if (streq(cp, "bytes/sector")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0 || (v % DEV_BSIZE) != 0) {
				fprintf(stderr,
				    "line %d: %s: bad sector size\n",
				    lineno, tp);
				errors++;
			} else
				lp->d_secsize = v;
			continue;
		}
		if (streq(cp, "sectors/track")) {
			v = strtoul(tp, NULL, 10);
#if (ULONG_MAX != 0xffffffffUL)
			if (v == 0 || v > 0xffffffff) {
#else
			if (v == 0) {
#endif
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_nsectors = v;
			continue;
		}
		if (streq(cp, "sectors/cylinder")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_secpercyl = v;
			continue;
		}
		if (streq(cp, "tracks/cylinder")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_ntracks = v;
			continue;
		}
		if (streq(cp, "cylinders")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_ncylinders = v;
			continue;
		}
		if (streq(cp, "sectors/unit")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_secperunit = v;
			continue;
		}
		if (streq(cp, "rpm")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0 || v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_rpm = v;
			continue;
		}
		if (streq(cp, "interleave")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0 || v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_interleave = v;
			continue;
		}
		if (streq(cp, "trackskew")) {
			v = strtoul(tp, NULL, 10);
			if (v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_trackskew = v;
			continue;
		}
		if (streq(cp, "cylinderskew")) {
			v = strtoul(tp, NULL, 10);
			if (v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_cylskew = v;
			continue;
		}
		if (streq(cp, "headswitch")) {
			v = strtoul(tp, NULL, 10);
			lp->d_headswitch = v;
			continue;
		}
		if (streq(cp, "track-to-track seek")) {
			v = strtoul(tp, NULL, 10);
			lp->d_trkseek = v;
			continue;
		}
		/* the ':' was removed above */
		if (*cp < 'a' || *cp > MAX_PART || cp[1] != '\0') {
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
		part_set[part] = 1;

		if (getasciipartspec(tp, lp, part, lineno) != 0) {
			errors++;
			break;
		}
	}
	errors += checklabel(lp);
	return (errors == 0);
}

#define NXTNUM(n) do { \
	if (tp == NULL) { \
		fprintf(stderr, "line %d: too few numeric fields\n", lineno); \
		return (1); \
	} else { \
		cp = tp, tp = word(cp); \
		(n) = strtoul(cp, NULL, 10); \
	} \
} while (0)

/* retain 1 character following number */
#define NXTWORD(w,n) do { \
	if (tp == NULL) { \
		fprintf(stderr, "line %d: too few numeric fields\n", lineno); \
		return (1); \
	} else { \
	        char *tmp; \
		cp = tp, tp = word(cp); \
	        (n) = strtoul(cp, &tmp, 10); \
		if (tmp) (w) = *tmp; \
	} \
} while (0)

/*
 * Read a partition line into partition `part' in the specified disklabel.
 * Return 0 on success, 1 on failure.
 */
int
getasciipartspec(char *tp, struct disklabel32 *lp, int part, int lineno)
{
	struct partition32 *pp;
	char *cp;
	const char **cpp;
	u_long v;

	pp = &lp->d_partitions[part];
	cp = NULL;

	/*
	 * size
	 */
	v = 0;
	NXTWORD(part_size_type[part],v);
	if (v == 0 && part_size_type[part] != '*') {
		fprintf(stderr,
		    "line %d: %s: bad partition size\n", lineno, cp);
		return (1);
	}
	pp->p_size = v;

	/*
	 * offset
	 */
	v = 0;
	NXTWORD(part_offset_type[part],v);
	if (v == 0 && part_offset_type[part] != '*' &&
	    part_offset_type[part] != '\0') {
		fprintf(stderr,
		    "line %d: %s: bad partition offset\n", lineno, cp);
		return (1);
	}
	pp->p_offset = v;

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
			v = strtoul(cp, NULL, 10);
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

	pp->p_fsize = 0;
	pp->p_frag = 0;
	pp->p_cpg = 0;

	cp = tp;
	if (tp) {
		fprintf(stderr, "line %d: Warning, fragment, block, "
				"and bps/cpg fields are no\n"
				"longer supported and must be specified "
				"via newfs options instead.\n",
			lineno);
	}
	return(0);
}

/*
 * Check disklabel for errors and fill in
 * derived fields according to supplied values.
 */
int
checklabel(struct disklabel32 *lp)
{
	struct partition32 *pp;
	int i, errors = 0;
	char part;
	u_long base_offset, needed, total_size, total_percent, current_offset;
	long free_space;
	int seen_default_offset;
	int hog_part;
	int j;
	struct partition32 *pp2;

	if (lp->d_secsize == 0) {
		fprintf(stderr, "sector size 0\n");
		return (1);
	}
	if (lp->d_nsectors == 0) {
		fprintf(stderr, "sectors/track 0\n");
		return (1);
	}
	if (lp->d_ntracks == 0) {
		fprintf(stderr, "tracks/cylinder 0\n");
		return (1);
	}
	if  (lp->d_ncylinders == 0) {
		fprintf(stderr, "cylinders/unit 0\n");
		errors++;
	}
	if (lp->d_rpm == 0)
		Warning("revolutions/minute 0");
	if (lp->d_secpercyl == 0)
		lp->d_secpercyl = lp->d_nsectors * lp->d_ntracks;
	if (lp->d_secperunit == 0)
		lp->d_secperunit = lp->d_secpercyl * lp->d_ncylinders;
	if (lp->d_bbsize == 0) {
		fprintf(stderr, "boot block size 0\n");
		errors++;
	} else if (lp->d_bbsize % lp->d_secsize)
		Warning("boot block size %% sector-size != 0");
	if (lp->d_sbsize == 0) {
		fprintf(stderr, "super block size 0\n");
		errors++;
	} else if (lp->d_sbsize % lp->d_secsize)
		Warning("super block size %% sector-size != 0");
	if (lp->d_npartitions > MAXPARTITIONS32)
		Warning("number of partitions (%lu) > MAXPARTITIONS (%d)",
		    (u_long)lp->d_npartitions, MAXPARTITIONS32);

	/* first allocate space to the partitions, then offsets */
	total_size = 0; /* in sectors */
	total_percent = 0; /* in percent */
	hog_part = -1;
	/* find all fixed partitions */
	for (i = 0; i < lp->d_npartitions; i++) {
		pp = &lp->d_partitions[i];
		if (part_set[i]) {

			if (part_size_type[i] == '*') {
				if (i == RAW_PART) {
					pp->p_size = lp->d_secperunit;
				} else {
					if (part_offset_type[i] != '*') {
						if (total_size < pp->p_offset)
							total_size = pp->p_offset;
					}
					if (hog_part != -1)
						Warning("Too many '*' partitions (%c and %c)",
						    hog_part + 'a',i + 'a');
					else
						hog_part = i;
				}
			} else {
				off_t size;

				size = pp->p_size;
				switch (part_size_type[i]) {
				case '%':
					total_percent += size;
					break;
				case 't':
				case 'T':
					size *= 1024ULL;
					/* FALLTHROUGH */
				case 'g':
				case 'G':
					size *= 1024ULL;
					/* FALLTHROUGH */
				case 'm':
				case 'M':
					size *= 1024ULL;
					/* FALLTHROUGH */
				case 'k':
				case 'K':
					size *= 1024ULL;
					break;
				case '\0':
					break;
				default:
					Warning("unknown size specifier '%c' (K/M/G/T are valid)",part_size_type[i]);
					break;
				}
				/* don't count %'s yet */
				if (part_size_type[i] != '%') {
					/*
					 * for all not in sectors, convert to
					 * sectors
					 */
					if (part_size_type[i] != '\0') {
						if (size % lp->d_secsize != 0)
							Warning("partition %c not an integer number of sectors",
							    i + 'a');
						size /= lp->d_secsize;
						pp->p_size = size;
					}
					/* else already in sectors */
					if (i != RAW_PART)
						total_size += size;
				}
			}
		}
	}

	/* Find out the total free space, excluding the boot block area. */
	base_offset = BBSIZE / lp->d_secsize;
	free_space = 0;
	for (i = 0; i < lp->d_npartitions; i++) {
		pp = &lp->d_partitions[i];
		if (!part_set[i] || i == RAW_PART ||
		    part_size_type[i] == '%' || part_size_type[i] == '*')
			continue;
		if (pp->p_offset > base_offset)
			free_space += pp->p_offset - base_offset;
		if (pp->p_offset + pp->p_size > base_offset)
			base_offset = pp->p_offset + pp->p_size;
	}
	if (base_offset < lp->d_secperunit)
		free_space += lp->d_secperunit - base_offset;

	/* handle % partitions - note %'s don't need to add up to 100! */
	if (total_percent != 0) {
		if (total_percent > 100) {
			fprintf(stderr,"total percentage %lu is greater than 100\n",
			    total_percent);
			errors++;
		}

		if (free_space > 0) {
			for (i = 0; i < lp->d_npartitions; i++) {
				pp = &lp->d_partitions[i];
				if (part_set[i] && part_size_type[i] == '%') {
					/* careful of overflows! and integer roundoff */
					pp->p_size = ((double)pp->p_size/100) * free_space;
					total_size += pp->p_size;

					/* FIX we can lose a sector or so due to roundoff per
					   partition.  A more complex algorithm could avoid that */
				}
			}
		} else {
			fprintf(stderr,
			    "%ld sectors available to give to '*' and '%%' partitions\n",
			    free_space);
			errors++;
			/* fix?  set all % partitions to size 0? */
		}
	}
	/* give anything remaining to the hog partition */
	if (hog_part != -1) {
		/*
		 * Find the range of offsets usable by '*' partitions around
		 * the hog partition and how much space they need.
		 */
		needed = 0;
		base_offset = BBSIZE / lp->d_secsize;
		for (i = hog_part - 1; i >= 0; i--) {
			pp = &lp->d_partitions[i];
			if (!part_set[i] || i == RAW_PART)
				continue;
			if (part_offset_type[i] == '*') {
				needed += pp->p_size;
				continue;
			}
			base_offset = pp->p_offset + pp->p_size;
			break;
		}
		current_offset = lp->d_secperunit;
		for (i = lp->d_npartitions - 1; i > hog_part; i--) {
			pp = &lp->d_partitions[i];
			if (!part_set[i] || i == RAW_PART)
				continue;
			if (part_offset_type[i] == '*') {
				needed += pp->p_size;
				continue;
			}
			current_offset = pp->p_offset;
		}

		if (current_offset - base_offset <= needed) {
			fprintf(stderr, "Cannot find space for partition %c\n",
			    hog_part + 'a');
			fprintf(stderr,
			    "Need more than %lu sectors between %lu and %lu\n",
			    needed, base_offset, current_offset);
			errors++;
			lp->d_partitions[hog_part].p_size = 0;
		} else {
			lp->d_partitions[hog_part].p_size = current_offset -
			    base_offset - needed;
			total_size += lp->d_partitions[hog_part].p_size;
		}
	}

	/* Now set the offsets for each partition */
	current_offset = BBSIZE / lp->d_secsize; /* in sectors */
	seen_default_offset = 0;
	for (i = 0; i < lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (part_set[i]) {
			if (part_offset_type[i] == '*') {
				if (i == RAW_PART) {
					pp->p_offset = 0;
				} else {
					pp->p_offset = current_offset;
					seen_default_offset = 1;
				}
			} else {
				/* allow them to be out of order for old-style tables */
				if (pp->p_offset < current_offset &&
				    seen_default_offset && i != RAW_PART &&
				    pp->p_fstype != FS_VINUM) {
					fprintf(stderr,
"Offset %ld for partition %c overlaps previous partition which ends at %lu\n",
					    (long)pp->p_offset,i+'a',current_offset);
					fprintf(stderr,
"Labels with any *'s for offset must be in ascending order by sector\n");
					errors++;
				} else if (pp->p_offset != current_offset &&
				    i != RAW_PART && seen_default_offset) {
					/*
					 * this may give unneeded warnings if
					 * partitions are out-of-order
					 */
					Warning(
"Offset %ld for partition %c doesn't match expected value %ld",
					    (long)pp->p_offset, i + 'a', current_offset);
				}
			}
			if (i != RAW_PART)
				current_offset = pp->p_offset + pp->p_size;
		}
	}

	for (i = 0; i < lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_size == 0 && pp->p_offset != 0)
			Warning("partition %c: size 0, but offset %lu",
			    part, (u_long)pp->p_offset);
#ifdef notdef
		if (pp->p_size % lp->d_secpercyl)
			Warning("partition %c: size %% cylinder-size != 0",
			    part);
		if (pp->p_offset % lp->d_secpercyl)
			Warning("partition %c: offset %% cylinder-size != 0",
			    part);
#endif
		if (pp->p_offset > lp->d_secperunit) {
			fprintf(stderr,
			    "partition %c: offset past end of unit\n", part);
			errors++;
		}
		if (pp->p_offset + pp->p_size > lp->d_secperunit) {
			fprintf(stderr,
			"partition %c: partition extends past end of unit\n",
			    part);
			errors++;
		}
		if (i == RAW_PART)
		{
			if (pp->p_fstype != FS_UNUSED)
				Warning("partition %c is not marked as unused!",part);
			if (pp->p_offset != 0)
				Warning("partition %c doesn't start at 0!",part);
			if (pp->p_size != lp->d_secperunit)
				Warning("partition %c doesn't cover the whole unit!",part);

			if ((pp->p_fstype != FS_UNUSED) || (pp->p_offset != 0) ||
			    (pp->p_size != lp->d_secperunit)) {
				Warning("An incorrect partition %c may cause problems for "
				    "standard system utilities",part);
			}
		}

		/* check for overlaps */
		/* this will check for all possible overlaps once and only once */
		for (j = 0; j < i; j++) {
			pp2 = &lp->d_partitions[j];
			if (j != RAW_PART && i != RAW_PART &&
			    pp->p_fstype != FS_VINUM &&
			    pp2->p_fstype != FS_VINUM &&
			    part_set[i] && part_set[j]) {
				if (pp2->p_offset < pp->p_offset + pp->p_size &&
				    (pp2->p_offset + pp2->p_size > pp->p_offset ||
					pp2->p_offset >= pp->p_offset)) {
					fprintf(stderr,"partitions %c and %c overlap!\n",
					    j + 'a', i + 'a');
					errors++;
				}
			}
		}
	}
	for (; i < 8 || i < lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_size || pp->p_offset)
			Warning("unused partition %c: size %d offset %lu",
			    'a' + i, pp->p_size, (u_long)pp->p_offset);
	}
	return (errors);
}

/*
 * When operating on a "virgin" disk, try getting an initial label
 * from the associated device driver.  This might work for all device
 * drivers that are able to fetch some initial device parameters
 * without even having access to a (BSD) disklabel, like SCSI disks,
 * most IDE drives, or vn devices.
 */
static struct disklabel32 dlab;

struct disklabel32 *
getvirginlabel(void)
{
	struct partinfo info;
	struct disklabel32 *dl = &dlab;
	int f;

	if ((f = open(dkname, O_RDONLY)) == -1) {
		warn("cannot open %s", dkname);
		return (NULL);
	}

	/*
	 * Check to see if the media is too big for a 32 bit disklabel.
	 */
	if (ioctl(f, DIOCGPART, &info) == 0) {
		 if (info.media_size >= 0x100000000ULL * 512) {
			warnx("The media is too large for a 32 bit disklabel,"
			      " please use disklabel64.");
			return (NULL);
		 }
	}

	/*
	 * Generate a virgin disklabel via ioctl
	 */
	if (ioctl(f, DIOCGDVIRGIN32, dl) < 0) {
		l_perror("ioctl DIOCGDVIRGIN32");
		close(f);
		return(NULL);
	}
	close(f);
	return (dl);
}

struct disklabel32 *
getdisklabelfromdisktab(const char *name)
{
	struct disktab *dt;
	struct disklabel32 *dl = &dlab;
	int i;

	if ((dt = getdisktabbyname(name)) == NULL)
		return(NULL);
	dl->d_magic = DISKMAGIC32;
	dl->d_type = dt->d_typeid;
	dl->d_subtype = 0;
	dl->d_secsize = dt->d_media_blksize;
	dl->d_nsectors = dt->d_secpertrack;
	dl->d_ntracks = dt->d_nheads;
	dl->d_ncylinders = dt->d_ncylinders;
	dl->d_secpercyl = dt->d_secpercyl;
	dl->d_secperunit = dt->d_media_blocks;
	dl->d_rpm = dt->d_rpm;
	dl->d_interleave = dt->d_interleave;
	dl->d_trackskew = dt->d_trackskew;
	dl->d_cylskew = dt->d_cylskew;
	dl->d_headswitch = dt->d_headswitch;
	dl->d_trkseek = dt->d_trkseek;
	dl->d_magic2 = DISKMAGIC32;
	dl->d_npartitions = dt->d_npartitions;
	dl->d_bbsize = dt->d_bbsize;
	dl->d_sbsize = dt->d_sbsize;
	for (i = 0; i < dt->d_npartitions; ++i) {
		struct partition32 *dlp = &dl->d_partitions[i];
		struct dt_partition *dtp = &dt->d_partitions[i];

		dlp->p_size	= dtp->p_size;
		dlp->p_offset	= dtp->p_offset;
		dlp->p_fsize	= dtp->p_fsize;
		dlp->p_fstype	= dtp->p_fstype;
		dlp->p_frag	= dtp->p_fsize;
	}
	return(dl);
}

/*
 * If we are installing a boot program that doesn't fit in d_bbsize
 * we need to mark those partitions that the boot overflows into.
 * This allows newfs to prevent creation of a filesystem where it might
 * clobber bootstrap code.
 */
void
setbootflag(struct disklabel32 *lp)
{
	struct partition32 *pp;
	int i, errors = 0;
	char part;
	u_long boffset;

	if (bootbuf == NULL)
		return;
	boffset = bootsize / lp->d_secsize;
	for (i = 0; i < lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_size == 0)
			continue;
		if (boffset <= pp->p_offset) {
			if (pp->p_fstype == FS_BOOT)
				pp->p_fstype = FS_UNUSED;
		} else if (pp->p_fstype != FS_BOOT) {
			if (pp->p_fstype != FS_UNUSED) {
				fprintf(stderr,
					"boot overlaps used partition %c\n",
					part);
				errors++;
			} else {
				pp->p_fstype = FS_BOOT;
				Warning("boot overlaps partition %c, %s",
					part, "marked as FS_BOOT");
			}
		}
	}
	if (errors)
		errx(4, "cannot install boot program");
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

/*
 * Check to see if the bootblocks are in the wrong place.  FBsd5 bootblocks
 * and earlier DFly bb's are packed against the old disklabel and a new
 * disklabel would blow them up.  This is a hack that should be removed
 * in 2006 sometime (if ever).
 */

int
checkoldboot(int f, const char *bootbuffer)
{
	char buf[BBSIZE];

	if (bootbuffer && strncmp(bootbuffer + 0x402, "BTX", 3) == 0)
		return(0);
	lseek(f, (off_t)0, SEEK_SET);
	if (read(f, buf, sizeof(buf)) != sizeof(buf))
		return(0);
	if (strncmp(buf + 0x402, "BTX", 3) == 0)  /* new location */
		return(0);
	if (strncmp(buf + 0x316, "BTX", 3) == 0)  /* old location */
		return(1);
	return(0);
}

/*
 * Traditional 32 bit disklabels actually use absolute sector numbers on
 * disk, NOT slice relative sector numbres.   The OS hides this from us
 * when we use DIOC ioctls to access the label, but newer versions of
 * Dragonfly no longer adjusts the disklabel when snooping reads or writes
 * so we have to figure it out ourselves.
 */
const char *
fixlabel(int f, struct disklabel32 *lp, int writeadj)
{
	const char *msg = NULL;
	struct partinfo info;
	struct partition32 *pp;
	u_int64_t start;
	u_int64_t end;
	u_int64_t offset;
	int part;
	int rev;
	size_t rev_len = sizeof(rev);

	if (sysctlbyname("kern.osrevision", &rev, &rev_len, NULL, 0) < 0) {
		errx(1, "Cannot use raw mode on non-DragonFly systems\n");
	}
	if (rev < 200701) {
		warnx("Warning running new disklabel on old DragonFly systems,\n"
		      "assuming the disk layer will fixup the label.\n");
		sleep(3);
		return(NULL);
	}

	pp = &lp->d_partitions[RAW_PART];

	if (forceflag) {
		info.media_offset = slice_start_lba * lp->d_secsize;
		info.media_blocks = pp->p_size;
		info.media_blksize = lp->d_secsize;
	} else if (ioctl(f, DIOCGPART, &info) < 0) {
		msg = "Unable to extract the slice starting LBA, "
		      "you must use the -f <slice_start_lba> option\n"
		      "to specify it manually, or perhaps try without "
		      "using -r and let the kernel deal with it\n";
		return(msg);
	}

	if (lp->d_magic != DISKMAGIC32 || lp->d_magic2 != DISKMAGIC32)
		return ("fixlabel: invalid magic");
	if (dkcksum32(lp) != 0)
		return ("fixlabel: invalid checksum");

	/*
	 * What a mess.  For ages old backwards compatibility the disklabel
	 * on-disk stores absolute offsets instead of slice-relative offsets.
	 * So fix it up when reading, writing, or snooping.
	 *
	 * The in-core label is always slice-relative.
	 */
	if (writeadj) {
		/*
		 * incore -> disk
		 */
		start = 0;
		offset = info.media_offset / info.media_blksize;
	} else {
		/*
		 * disk -> incore
		 */
		start = info.media_offset / info.media_blksize;
		offset = -info.media_offset / info.media_blksize;
	}
	if (pp->p_offset != start)
		return ("fixlabel: raw partition offset != slice offset");
	if (pp->p_size != info.media_blocks) {
		if (pp->p_size > info.media_blocks)
			return ("fixlabel: raw partition size > slice size");
	}
	end = start + info.media_blocks;
	if (start > end)
		return ("fixlabel: slice wraps");
	if (lp->d_secpercyl <= 0)
		return ("fixlabel: d_secpercyl <= 0");
	pp -= RAW_PART;
	for (part = 0; part < lp->d_npartitions; part++, pp++) {
		if (pp->p_offset != 0 || pp->p_size != 0) {
			if (pp->p_offset < start
			    || pp->p_offset + pp->p_size > end
			    || pp->p_offset + pp->p_size < pp->p_offset) {
				/* XXX else silently discard junk. */
				bzero(pp, sizeof *pp);
			} else {
				pp->p_offset += offset;
			}
		}
	}
	lp->d_checksum = 0;
	lp->d_checksum = dkcksum32(lp);
	return (NULL);
}

void
usage(void)
{
#if NUMBOOT > 0
	fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
		"usage: disklabel32 [-r] disk",
		"\t\t(to read label)",
		"       disklabel32 -w [-r] [-n] disk type [packid]",
		"\t\t(to write label with existing boot program)",
		"       disklabel32 -e [-r] [-n] disk",
		"\t\t(to edit label)",
		"       disklabel32 -R [-r] [-n] disk protofile",
		"\t\t(to restore label with existing boot program)",
#if NUMBOOT > 1
		"       disklabel32 -B [-n] [-b boot1 -s boot2] disk [type]",
		"\t\t(to install boot program with existing label)",
		"       disklabel32 -w -B [-n] [-b boot1 -s boot2] disk type [packid]",
		"\t\t(to write label and boot program)",
		"       disklabel32 -R -B [-n] [-b boot1 -s boot2] disk protofile [type]",
		"\t\t(to restore label and boot program)",
#else
		"       disklabel32 -B [-n] [-b bootprog] disk [type]",
		"\t\t(to install boot program with existing on-disk label)",
		"       disklabel32 -w -B [-n] [-b bootprog] disk type [packid]",
		"\t\t(to write label and install boot program)",
		"       disklabel32 -R -B [-n] [-b bootprog] disk protofile [type]",
		"\t\t(to restore label and install boot program)",
#endif
		"       disklabel32 [-NW] disk",
		"\t\t(to write disable/enable label)");
#else
	fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
		"usage: disklabel32 [-r] disk", "(to read label)",
		"       disklabel32 -w [-r] [-n] disk type [packid]",
		"\t\t(to write label)",
		"       disklabel32 -e [-r] [-n] disk",
		"\t\t(to edit label)",
		"       disklabel32 -R [-r] [-n] disk protofile",
		"\t\t(to restore label)",
		"       disklabel32 [-NW] disk",
		"\t\t(to write disable/enable label)");
#endif
	fprintf(stderr, "%s\n%s\n",
		"       disklabel32 [-f slice_start_lba] [options]",
		"\t\t(to force using manual offset)");
	exit(1);
}
