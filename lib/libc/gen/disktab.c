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
 * Copyright (c) 1983, 1987, 1993
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
 */
/*
 * $DragonFly: src/lib/libc/gen/disktab.c,v 1.11 2007/06/17 23:50:13 dillon Exp $
 * @(#)disklabel.c     8.2 (Berkeley) 5/3/95
 * $FreeBSD: src/lib/libc/gen/disklabel.c,v 1.9.2.1 2001/03/05 08:40:47 obrien
 */

#include <sys/param.h>
#define DKTYPENAMES
#include <sys/disklabel.h>
#include <sys/dtype.h>
#include <vfs/ufs/dinode.h>
#include <vfs/ufs/fs.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <disktab.h>

static int	gettype (const char *, const char **);

struct disktab *
getdisktabbyname(const char *name)
{
	static struct disktab dtab;
	struct disktab *dt = &dtab;
	struct dt_partition *pp;
	char	*buf;
	char  	*db_array[2] = { _PATH_DISKTAB, 0 };
	char	*cp, *cq;	/* can't be register */
	char	p, max, psize[3], pbsize[3],
		pfsize[3], poffset[3], ptype[3];
#if 0
	u_int32_t *dx;
#endif

	if (cgetent(&buf, db_array, (char *)name) < 0)
		return NULL;

	bzero(dt, sizeof(*dt));
	/*
	 * typename
	 */
	cq = dt->d_typename;
	cp = buf;
	while (cq < dt->d_typename + sizeof(dt->d_typename) - 1 &&
	    (*cq = *cp) && *cq != '|' && *cq != ':')
		cq++, cp++;
	*cq = '\0';

#define getnumdflt(field, dname, dflt) \
        { long f; (field) = (cgetnum(buf, dname, &f) == -1) ? (dflt) : f; }

	getnumdflt(dt->d_media_blksize, "se", DEV_BSIZE);
	getnumdflt(dt->d_nheads, "nt", 0);
	getnumdflt(dt->d_secpertrack, "ns", 0);
	getnumdflt(dt->d_ncylinders, "nc", 0);

	if (cgetstr(buf, "dt", &cq) > 0)
		dt->d_typeid = gettype(cq, dktypenames);
	else
		getnumdflt(dt->d_typeid, "dt", 0);
	getnumdflt(dt->d_secpercyl, "sc", dt->d_secpertrack * dt->d_nheads);
	getnumdflt(dt->d_media_blocks, "su", dt->d_secpercyl * dt->d_ncylinders);
	getnumdflt(dt->d_rpm, "rm", 3600);
	getnumdflt(dt->d_interleave, "il", 1);
	getnumdflt(dt->d_trackskew, "sk", 0);
	getnumdflt(dt->d_cylskew, "cs", 0);
	getnumdflt(dt->d_headswitch, "hs", 0);
	getnumdflt(dt->d_trkseek, "ts", 0);
	getnumdflt(dt->d_bbsize, "bs", BBSIZE);
	getnumdflt(dt->d_sbsize, "sb", SBSIZE);
	strcpy(psize, "px");
	strcpy(pbsize, "bx");
	strcpy(pfsize, "fx");
	strcpy(poffset, "ox");
	strcpy(ptype, "tx");
	max = 'a' - 1;
	pp = &dt->d_partitions[0];
	for (p = 'a'; p < 'a' + MAXDTPARTITIONS; p++, pp++) {
		long l;
		psize[1] = pbsize[1] = pfsize[1] = poffset[1] = ptype[1] = p;
		if (cgetnum(buf, psize, &l) == -1) {
			pp->p_size = 0;
		} else {
			pp->p_size = l;
			cgetnum(buf, poffset, &l);
			pp->p_offset = l;
			getnumdflt(pp->p_fsize, pfsize, 0);
			if (pp->p_fsize) {
				long bsize;

				if (cgetnum(buf, pbsize, &bsize) == 0)
					pp->p_frag = bsize / pp->p_fsize;
				else
					pp->p_frag = 8;
			}
			getnumdflt(pp->p_fstype, ptype, 0);
			if (pp->p_fstype == 0 && cgetstr(buf, ptype, &cq) > 0) {
				pp->p_fstype = gettype(cq, fstypenames);
				snprintf(pp->p_fstypestr,
					 sizeof(pp->p_fstypestr), "%s", cq);
			} else if (pp->p_fstype >= 0 &&
				   pp->p_fstype < FSMAXTYPES) {
				snprintf(pp->p_fstypestr,
					 sizeof(pp->p_fstypestr), "%s",
					 fstypenames[pp->p_fstype]);
			}
			max = p;
		}
	}
	dt->d_npartitions = max + 1 - 'a';
#if 0
	strcpy(psize, "dx");
	dx = dt->d_drivedata;
	for (p = '0'; p < '0' + NDDATA; p++, dx++) {
		psize[1] = p;
		getnumdflt(*dx, psize, 0);
	}
	dp->d_magic = DISKMAGIC;
	dp->d_magic2 = DISKMAGIC;
#endif
	free(buf);
	return (dt);
}

static int
gettype(const char *t, const char **names)
{
	const char **nm;

	for (nm = names; *nm; nm++)
		if (strcasecmp(t, *nm) == 0)
			return (nm - names);
	if (isdigit((unsigned char)*t))
		return (atoi(t));
	return (0);
}
