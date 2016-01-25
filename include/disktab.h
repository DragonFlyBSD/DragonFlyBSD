/*
 * Copyright (c) 1983, 1993
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
 * @(#)disktab.h	8.1 (Berkeley) 6/2/93
 * $DragonFly: src/include/disktab.h,v 1.4 2007/06/18 05:13:33 dillon Exp $
 */

#ifndef	_DISKTAB_H_
#define	_DISKTAB_H_

#ifndef _MACHINE_TYPES_H_
#include <machine/types.h>
#endif

/*
 * Disk description table, see disktab(5)
 */
#ifndef _PATH_DISKTAB
#define	_PATH_DISKTAB	"/etc/disktab"
#endif

#define MAXDTPARTITIONS		16

struct	disktab {
	char	d_typename[16];		/* drive type (string) */
	int	d_typeid;		/* drive type (id) */

	/*
	 * disk_info mandatory fields (not necessarily mandatory for disktab).
	 * If d_media_blksize and one of d_media_size or d_media_blocks
	 * is set, the remainining d_media_size/d_media_blocks field will
	 * be constructed by getdisktabbyname().
	 */
	__uint64_t d_media_size;
	__uint64_t d_media_blocks;
	int	d_media_blksize;
	int	d_dsflags;

	/*
	 * disk_info optional fields.  
	 */
	unsigned int	d_nheads;	/* (used to be d_ntracks) */
	unsigned int	d_secpertrack;	/* (used to be d_nsectors) */
	unsigned int	d_secpercyl;
	unsigned int	d_ncylinders;

	int	d_rpm;			/* revolutions/minute */
	int	d_badsectforw;		/* supports DEC bad144 std */
	int	d_sectoffset;		/* use sect rather than cyl offsets */
	int	d_npartitions;		/* number of partitions */
	int	d_interleave;
	int	d_trackskew;
	int	d_cylskew;
	int	d_headswitch;
	int	d_trkseek;

	unsigned int	d_bbsize;	/* size of boot area */
	unsigned int	d_sbsize;	/* max size of fs superblock */

	/*
	 * The partition table is variable length but does not necessarily
	 * represent the maximum possible number of partitions for any
	 * particular type of disklabel.
	 */
	struct	dt_partition {
		__uint64_t p_offset;	/* offset, in sectors */
		__uint64_t p_size;	/* #sectors in partition */
		int	  p_fstype;
		int	  p_fsize;	/* fragment size */
		int	  p_frag;	/* bsize = fsize * frag */
		char	  p_fstypestr[32];
	} d_partitions[MAXDTPARTITIONS];
};

#ifndef _KERNEL
__BEGIN_DECLS
struct disklabel32;
struct disktab *getdisktabbyname (const char *);
struct disklabel32 *getdiskbyname (const char *);
__END_DECLS
#endif

#endif /* !_DISKTAB_H_ */
