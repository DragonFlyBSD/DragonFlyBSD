/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * @(#)setup.c	8.10 (Berkeley) 5/9/95
 * $FreeBSD: src/sbin/fsck/setup.c,v 1.17.2.4 2002/06/24 05:10:41 dillon Exp $
 */

#define DKTYPENAMES
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/diskslice.h>
#include <sys/file.h>

#include <vfs/ufs/dinode.h>
#include <vfs/ufs/fs.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <string.h>

#include "fsck.h"

struct bufarea asblk;
#define altsblock (*asblk.b_un.b_fs)
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

static void badsb(int listerr, char *s);
static int readsb(int listerr);

/*
 * Read in a superblock finding an alternate if necessary.
 * Return 1 if successful, 0 if unsuccessful, -1 if filesystem
 * is already clean (preen mode only).
 */
int
setup(char *dev)
{
	long size, asked, i, j;
	long skipclean, bmapsize;
	off_t sizepb;
	struct stat statb;

	havesb = 0;
	fswritefd = -1;
	skipclean = fflag ? 0 : preen;
	if (stat(dev, &statb) < 0) {
		printf("Can't stat %s: %s\n", dev, strerror(errno));
		return (0);
	}
	if ((statb.st_mode & S_IFMT) != S_IFCHR &&
	    (statb.st_mode & S_IFMT) != S_IFBLK) {
		pfatal("%s is not a disk device", dev);
		if (reply("CONTINUE") == 0)
			return (0);
	}
	if ((fsreadfd = open(dev, O_RDONLY)) < 0) {
		printf("Can't open %s: %s\n", dev, strerror(errno));
		return (0);
	}
	if (preen == 0)
		printf("** %s", dev);
	if (nflag || (fswritefd = open(dev, O_WRONLY)) < 0) {
		fswritefd = -1;
		if (preen)
			pfatal("NO WRITE ACCESS");
		printf(" (NO WRITE)");
	}
	if (preen == 0)
		printf("\n");
	fsmodified = 0;
	lfdir = 0;
	initbarea(&sblk);
	initbarea(&asblk);
	sblk.b_un.b_buf = malloc(SBSIZE);
	asblk.b_un.b_buf = malloc(SBSIZE);
	if (sblk.b_un.b_buf == NULL || asblk.b_un.b_buf == NULL)
		errx(EEXIT, "cannot allocate space for superblock");

	/*
	 * Figure out the device block size and the sector size.  The
	 * block size is updated by readsb() later on.
	 */
	{
		struct partinfo pinfo;

		if (ioctl(fsreadfd, DIOCGPART, &pinfo) == 0) {
			dev_bsize = secsize = pinfo.media_blksize;
		} else {
			dev_bsize = secsize = DEV_BSIZE;
		}
	}

	/*
	 * Read in the superblock, looking for alternates if necessary
	 */
	if (readsb(1) == 0) {
		skipclean = 0;
		if (bflag || preen)
			return(0);
		if (reply("LOOK FOR ALTERNATE SUPERBLOCKS") == 0)
			return (0);
		bflag = 32;
		if (readsb(0) == 0) {
			printf(
			    "YOU MUST USE THE -b OPTION TO FSCK TO SPECIFY\n"
			    "THE LOCATION OF AN ALTERNATE SUPER-BLOCK TO\n"
			    "SUPPLY NEEDED INFORMATION; SEE fsck(8).");
			bflag = 0;
			return(0);
		}
		pwarn("USING ALTERNATE SUPERBLOCK AT %d\n", bflag);
		bflag = 0;
	}
	if (skipclean && sblock.fs_clean) {
		pwarn("FILESYSTEM CLEAN; SKIPPING CHECKS\n");
		return (-1);
	}
	maxfsblock = sblock.fs_size;
	maxino = sblock.fs_ncg * sblock.fs_ipg;
	/*
	 * Check and potentially fix certain fields in the super block.
	 */
	if (sblock.fs_optim != FS_OPTTIME && sblock.fs_optim != FS_OPTSPACE) {
		pfatal("UNDEFINED OPTIMIZATION IN SUPERBLOCK");
		if (reply("SET TO DEFAULT") == 1) {
			sblock.fs_optim = FS_OPTTIME;
			sbdirty();
		}
	}
	if ((sblock.fs_minfree < 0 || sblock.fs_minfree > 99)) {
		pfatal("IMPOSSIBLE MINFREE=%d IN SUPERBLOCK",
			sblock.fs_minfree);
		if (reply("SET TO DEFAULT") == 1) {
			sblock.fs_minfree = 10;
			sbdirty();
		}
	}
	if (sblock.fs_interleave < 1 ||
	    sblock.fs_interleave > sblock.fs_nsect) {
		pwarn("IMPOSSIBLE INTERLEAVE=%d IN SUPERBLOCK",
			sblock.fs_interleave);
		sblock.fs_interleave = 1;
		if (preen)
			printf(" (FIXED)\n");
		if (preen || reply("SET TO DEFAULT") == 1) {
			sbdirty();
			dirty(&asblk);
		}
	}
	if (sblock.fs_npsect < sblock.fs_nsect ||
	    sblock.fs_npsect > sblock.fs_nsect*2) {
		pwarn("IMPOSSIBLE NPSECT=%d IN SUPERBLOCK",
			sblock.fs_npsect);
		sblock.fs_npsect = sblock.fs_nsect;
		if (preen)
			printf(" (FIXED)\n");
		if (preen || reply("SET TO DEFAULT") == 1) {
			sbdirty();
			dirty(&asblk);
		}
	}
	if (sblock.fs_inodefmt >= FS_44INODEFMT) {
		newinofmt = 1;
	} else {
		sblock.fs_qbmask = ~sblock.fs_bmask;
		sblock.fs_qfmask = ~sblock.fs_fmask;
		/* This should match the kernel limit in ffs_oldfscompat(). */
		sblock.fs_maxfilesize = (u_int64_t)1 << 39;
		newinofmt = 0;
	}
	/*
	 * Convert to new inode format.
	 */
	if (cvtlevel >= 2 && sblock.fs_inodefmt < FS_44INODEFMT) {
		if (preen)
			pwarn("CONVERTING TO NEW INODE FORMAT\n");
		else if (!reply("CONVERT TO NEW INODE FORMAT"))
			return(0);
		doinglevel2++;
		sblock.fs_inodefmt = FS_44INODEFMT;
		sizepb = sblock.fs_bsize;
		sblock.fs_maxfilesize = sblock.fs_bsize * NDADDR - 1;
		for (i = 0; i < NIADDR; i++) {
			sizepb *= NINDIR(&sblock);
			sblock.fs_maxfilesize += sizepb;
		}
		sblock.fs_maxsymlinklen = MAXSYMLINKLEN;
		sblock.fs_qbmask = ~sblock.fs_bmask;
		sblock.fs_qfmask = ~sblock.fs_fmask;
		sbdirty();
		dirty(&asblk);
	}
	/*
	 * Convert to new cylinder group format.
	 */
	if (cvtlevel >= 1 && sblock.fs_postblformat == FS_42POSTBLFMT) {
		if (preen)
			pwarn("CONVERTING TO NEW CYLINDER GROUP FORMAT\n");
		else if (!reply("CONVERT TO NEW CYLINDER GROUP FORMAT"))
			return(0);
		doinglevel1++;
		sblock.fs_postblformat = FS_DYNAMICPOSTBLFMT;
		sblock.fs_nrpos = 8;
		sblock.fs_postbloff =
		    (char *)(&sblock.fs_opostbl[0][0]) -
		    (char *)(&sblock.fs_firstfield);
		sblock.fs_rotbloff = &sblock.fs_space[0] -
		    (u_char *)(&sblock.fs_firstfield);
		sblock.fs_cgsize =
			fragroundup(&sblock, CGSIZE(&sblock));
		sbdirty();
		dirty(&asblk);
	}
	if (asblk.b_dirty && !bflag) {
		memmove(&altsblock, &sblock, (size_t)sblock.fs_sbsize);
		flush(fswritefd, &asblk);
	}
	/*
	 * read in the summary info.
	 */
	asked = 0;
	sblock.fs_csp = calloc(1, sblock.fs_cssize);
	for (i = 0, j = 0; i < sblock.fs_cssize; i += sblock.fs_bsize, j++) {
		size = sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize;
		if (bread(fsreadfd, (char *)sblock.fs_csp + i,
		    fsbtodb(&sblock, sblock.fs_csaddr + j * sblock.fs_frag),
		    size) != 0 && !asked) {
			pfatal("BAD SUMMARY INFORMATION");
			if (reply("CONTINUE") == 0) {
				ckfini(0);
				exit(EEXIT);
			}
			asked++;
		}
	}
	/*
	 * allocate and initialize the necessary maps
	 */
	bmapsize = roundup(howmany(maxfsblock, NBBY), sizeof(short));
	blockmap = calloc((unsigned)bmapsize, sizeof (char));
	if (blockmap == NULL) {
		printf("cannot alloc %u bytes for blockmap\n",
		    (unsigned)bmapsize);
		goto badsb;
	}
	inostathead = calloc((unsigned)(sblock.fs_ncg),
	    sizeof(struct inostatlist));
	if (inostathead == NULL) {
		printf("cannot alloc %u bytes for inostathead\n",
		    (unsigned)(sizeof(struct inostatlist) * (sblock.fs_ncg)));
		goto badsb;
	}
	numdirs = sblock.fs_cstotal.cs_ndir;

	/*
	 * Calculate the directory hash table size.  Do not allocate 
	 * a ridiculous amount of memory if we have a lot of directories.
	 */
	for (dirhash = 16; dirhash < numdirs; dirhash <<= 1)
		;
	if (dirhash > 1024*1024)
		dirhash /= 8;
	dirhashmask = dirhash - 1;

	if (numdirs == 0) {
		printf("numdirs is zero, try using an alternate superblock\n");
		goto badsb;
	}
	inplast = 0;
	listmax = numdirs + 10;
	inpsort = calloc((unsigned)listmax, sizeof(struct inoinfo *));
	inphead = calloc((unsigned)dirhash, sizeof(struct inoinfo *));
	if (inpsort == NULL || inphead == NULL) {
		printf("cannot allocate base structures for %ld directories\n",
			numdirs);
		goto badsb;
	}
	bufinit();
	if (sblock.fs_flags & FS_DOSOFTDEP)
		usedsoftdep = 1;
	else
		usedsoftdep = 0;
	return (1);

badsb:
	ckfini(0);
	return (0);
}

/*
 * Read in the super block and its summary info.
 */
static int
readsb(int listerr)
{
	ufs_daddr_t super = bflag ? bflag : SBOFF / dev_bsize;

	if (bread(fsreadfd, (char *)&sblock, super, (long)SBSIZE) != 0)
		return (0);
	sblk.b_bno = super;
	sblk.b_size = SBSIZE;
	/*
	 * run a few consistency checks of the super block
	 */
	if (sblock.fs_magic != FS_MAGIC)
		{ badsb(listerr, "MAGIC NUMBER WRONG"); return (0); }
	if (sblock.fs_ncg < 1)
		{ badsb(listerr, "NCG OUT OF RANGE"); return (0); }
	if (sblock.fs_cpg < 1)
		{ badsb(listerr, "CPG OUT OF RANGE"); return (0); }
	if (sblock.fs_ncg * sblock.fs_cpg < sblock.fs_ncyl ||
	    (sblock.fs_ncg - 1) * sblock.fs_cpg >= sblock.fs_ncyl)
		{ badsb(listerr, "NCYL LESS THAN NCG*CPG"); return (0); }
	if (sblock.fs_sbsize > SBSIZE)
		{ badsb(listerr, "SIZE PREPOSTEROUSLY LARGE"); return (0); }
	/*
	 * Compute block size that the filesystem is based on,
	 * according to fsbtodb, and adjust superblock block number
	 * so we can tell if this is an alternate later.
	 */
	super *= dev_bsize;
	dev_bsize = sblock.fs_fsize / fsbtodb(&sblock, 1);
	sblk.b_bno = super / dev_bsize;
	if (bflag) {
		havesb = 1;
		return (1);
	}
	/*
	 * Compare all fields that should not differ in alternate super block.
	 * When an alternate super-block is specified this check is skipped.
	 */
	getblk(&asblk, cgsblock(&sblock, sblock.fs_ncg - 1), sblock.fs_sbsize);
	if (asblk.b_errs)
		return (0);
	if (altsblock.fs_sblkno != sblock.fs_sblkno ||
	    altsblock.fs_cblkno != sblock.fs_cblkno ||
	    altsblock.fs_iblkno != sblock.fs_iblkno ||
	    altsblock.fs_dblkno != sblock.fs_dblkno ||
	    altsblock.fs_cgoffset != sblock.fs_cgoffset ||
	    altsblock.fs_cgmask != sblock.fs_cgmask ||
	    altsblock.fs_ncg != sblock.fs_ncg ||
	    altsblock.fs_bsize != sblock.fs_bsize ||
	    altsblock.fs_fsize != sblock.fs_fsize ||
	    altsblock.fs_frag != sblock.fs_frag ||
	    altsblock.fs_bmask != sblock.fs_bmask ||
	    altsblock.fs_fmask != sblock.fs_fmask ||
	    altsblock.fs_bshift != sblock.fs_bshift ||
	    altsblock.fs_fshift != sblock.fs_fshift ||
	    altsblock.fs_fragshift != sblock.fs_fragshift ||
	    altsblock.fs_fsbtodb != sblock.fs_fsbtodb ||
	    altsblock.fs_sbsize != sblock.fs_sbsize ||
	    altsblock.fs_nindir != sblock.fs_nindir ||
	    altsblock.fs_inopb != sblock.fs_inopb ||
	    altsblock.fs_cssize != sblock.fs_cssize ||
	    altsblock.fs_cpg != sblock.fs_cpg ||
	    altsblock.fs_ipg != sblock.fs_ipg ||
	    altsblock.fs_fpg != sblock.fs_fpg ||
	    altsblock.fs_magic != sblock.fs_magic) {
		badsb(listerr,
		"VALUES IN SUPER BLOCK DISAGREE WITH THOSE IN FIRST ALTERNATE");
		return (0);
	}
	havesb = 1;
	return (1);
}

static void
badsb(int listerr, char *s)
{

	if (!listerr)
		return;
	if (preen)
		printf("%s: ", cdevname);
	pfatal("BAD SUPER BLOCK: %s\n", s);
}

