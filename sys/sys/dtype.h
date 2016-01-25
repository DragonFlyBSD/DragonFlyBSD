/*
 * Copyright (c) 1987, 1988, 1993
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
 *	@(#)disklabel.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD: src/sys/sys/disklabel.h,v 1.49.2.7 2001/05/27 05:58:26 jkh Exp $
 */

#ifndef _SYS_DTYPE_H_
#define	_SYS_DTYPE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/* d_type values: */
#define	DTYPE_SMD		1		/* SMD, XSMD; VAX hp/up */
#define	DTYPE_MSCP		2		/* MSCP */
#define	DTYPE_DEC		3		/* other DEC (rk, rl) */
#define	DTYPE_SCSI		4		/* SCSI */
#define	DTYPE_ESDI		5		/* ESDI interface */
#define	DTYPE_ST506		6		/* ST506 etc. */
#define	DTYPE_HPIB		7		/* CS/80 on HP-IB */
#define	DTYPE_HPFL		8		/* HP Fiber-link */
#define	DTYPE_FLOPPY		10		/* floppy */
#define	DTYPE_CCD		11		/* concatenated disk */
#define	DTYPE_VINUM		12		/* vinum volume */
#define	DTYPE_DOC2K		13		/* Msys DiskOnChip */

#ifdef DKTYPENAMES
static const char *dktypenames[] = {
	"unknown",
	"SMD",
	"MSCP",
	"old DEC",
	"SCSI",
	"ESDI",
	"ST506",
	"HP-IB",
	"HP-FL",
	"type 9",
	"floppy",
	"CCD",
	"Vinum",
	"DOC2K",
	NULL
};
#define DKMAXTYPES	(NELEM(dktypenames) - 1)
#endif

/*
 * Filesystem type and version.
 * Used to interpret other filesystem-specific
 * per-partition information.
 */
#define	FS_UNUSED	0		/* unused */
#define	FS_SWAP		1		/* swap */
#define	FS_V6		2		/* Sixth Edition */
#define	FS_V7		3		/* Seventh Edition */
#define	FS_SYSV		4		/* System V */
#define	FS_V71K		5		/* V7 with 1K blocks (4.1, 2.9) */
#define	FS_V8		6		/* Eighth Edition, 4K blocks */
#define	FS_BSDFFS	7		/* 4.2BSD fast file system */
#define	FS_MSDOS	8		/* MSDOS file system */
#define	FS_BSDLFS	9		/* 4.4BSD log-structured file system */
#define	FS_OTHER	10		/* in use, but unknown/unsupported */
#define	FS_HPFS		11		/* OS/2 high-performance file system */
#define	FS_ISO9660	12		/* ISO 9660, normally CD-ROM */
#define	FS_BOOT		13		/* partition contains bootstrap */
#define	FS_VINUM	14		/* Vinum drive partition */
#define	FS_RAID		15
#define FS_RESERVED16	16
#define FS_RESERVED17	17
#define FS_RESERVED18	18
#define FS_CCD		19		/* CCD drive partition */
#define FS_RESERVED20	20		/* (CCD under FreeBSD) */
#define FS_JFS2		21
#define FS_HAMMER	22
#define FS_HAMMER2	23
#define FS_UDF		24
#define FS_EFS		26
#define FS_ZFS		27

#ifdef	DKTYPENAMES

static const char *fstypenames[] = {
	"unused",		/* 0	*/
	"swap",			/* 1	*/
	"Version 6",		/* 2	*/
	"Version 7",		/* 3	*/
	"System V",		/* 4	*/
	"4.1BSD",		/* 5	*/
	"Eighth Edition",	/* 6	*/
	"4.2BSD",		/* 7	*/
	"MSDOS",		/* 8	*/
	"4.4LFS",		/* 9	*/
	"unknown",		/* 10	*/
	"HPFS",			/* 11	*/
	"ISO9660",		/* 12	*/
	"boot",			/* 13	*/
	"vinum",		/* 14	*/
	"raid",			/* 15	*/
	"?",			/* 16	*/
	"?",			/* 17	*/
	"?",			/* 18	*/
	"ccd",			/* 19	*/
	"?",			/* 20	(do not reuse, bug in freebsd) */
	"jfs",			/* 21	*/
	"HAMMER",		/* 22	*/
	"HAMMER2",		/* 23	*/
	"UDF",			/* 24	*/
	"?",			/* 25	*/
	"EFS",			/* 26	*/
	"ZFS",			/* 27	*/
	NULL
};

static const char *fstype_to_vfsname[] = {
	NULL,			/* 0	*/
	NULL,			/* 1	*/
	NULL,			/* 2	*/
	NULL,			/* 3	*/
	NULL,			/* 4	*/
	NULL,			/* 5	*/
	NULL,			/* 6	*/
	"ufs",			/* 7	*/
	"msdos",		/* 8	*/
	NULL,			/* 9	*/
	NULL,			/* 10	*/
	"hpfs",			/* 11	*/
	"cd9660",		/* 12	*/
	NULL,			/* 13	*/
	NULL,			/* 14	*/
	NULL,			/* 15	*/
	NULL,			/* 16	*/
	NULL,			/* 17	*/
	NULL,			/* 18	*/
	NULL,			/* 19	*/
	NULL,			/* 20	*/
	NULL,			/* 21	*/
	"hammer",		/* 22	*/
	"hammer2",		/* 23	*/
	"udf",			/* 24	*/
	NULL,			/* 25	*/
	NULL,			/* 26	*/
	NULL,			/* 27	*/
	NULL
};

#define FSMAXTYPES	(NELEM(fstypenames) - 1)

#endif

#endif /* SYS_DTYPE_H_ */
