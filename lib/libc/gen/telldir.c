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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $FreeBSD: src/lib/libc/gen/telldir.c,v 1.4.12.1 2001/03/05 09:39:59 obrien Exp $
 *
 * @(#)telldir.c	8.1 (Berkeley) 6/4/93
 */

#include "namespace.h"
#include <sys/param.h>
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "gen_private.h"

/*
 * One of these structures is malloced to describe the current directory
 * position each time telldir is called. It records the current magic
 * cookie returned by getdirentries and the offset within the buffer
 * associated with that return value.
 */
struct ddloc {
	struct	ddloc *loc_next;/* next structure in list */
	long	loc_index;	/* key associated with structure */
	off_t	loc_seek;	/* magic cookie returned by getdirentries */
	long	loc_loc;	/* offset of entry in buffer */
	const DIR* loc_dirp;	/* directory which used this entry */
};

#define	NDIRHASH	32	/* Num of hash lists, must be a power of 2 */
#define	LOCHASH(i)	((i)&(NDIRHASH-1))

static long	dd_loccnt;	/* Index of entry for sequential readdir's */
static struct	ddloc *dd_hash[NDIRHASH];   /* Hash list heads for ddlocs */
static pthread_mutex_t dd_hash_lock;

/*
 * return a pointer into a directory
 */
long
telldir(DIR *dirp)
{
	long index;
	struct ddloc *lp;

	if (__isthreaded) {
		_pthread_mutex_lock(&dirp->dd_lock);
		_pthread_mutex_lock(&dd_hash_lock);
	}

	/*
	 * Reduce memory use by reusing a ddloc that might already exist
	 * for this position.
	 */
	for (lp = dd_hash[LOCHASH(dirp->dd_lastseek)]; lp; lp = lp->loc_next) {
		if (lp->loc_dirp == dirp && lp->loc_seek == dirp->dd_seek &&
		    lp->loc_loc == dirp->dd_loc) {
			index = lp->loc_index;
			goto done;
		}
	}

	if ((lp = (struct ddloc *)malloc(sizeof(struct ddloc))) == NULL) {
		index = -1;
		goto done;
	}
	index = dd_loccnt++;
	lp->loc_index = index;
	lp->loc_seek = dirp->dd_seek;
	lp->loc_loc = dirp->dd_loc;
	lp->loc_dirp = dirp;

	lp->loc_next = dd_hash[LOCHASH(index)];
	dd_hash[LOCHASH(index)] = lp;

done:
	if (__isthreaded) {
		_pthread_mutex_unlock(&dd_hash_lock);
		_pthread_mutex_unlock(&dirp->dd_lock);
	}
	return (index);
}

/*
 * seek to an entry in a directory.
 * Only values returned by "telldir" should be passed to seekdir.
 */
void
_seekdir(DIR *dirp, long loc)
{
	struct ddloc *lp;
	struct dirent *dp;

        if (__isthreaded)
		_pthread_mutex_lock(&dd_hash_lock);
	for (lp = dd_hash[LOCHASH(loc)]; lp; lp = lp->loc_next) {
		if (lp->loc_dirp == dirp && lp->loc_index == loc)
			break;
	}
        if (__isthreaded)
		_pthread_mutex_unlock(&dd_hash_lock);
	if (lp == NULL)
		return;
	if (lp->loc_loc == dirp->dd_loc && lp->loc_seek == dirp->dd_seek)
		return;
	lseek(dirp->dd_fd, lp->loc_seek, SEEK_SET);
	dirp->dd_seek = lp->loc_seek;
	dirp->dd_loc = 0;
	dirp->dd_lastseek = loc;

	/*
	 * Scan the buffer until we find dd_loc.  If the directory
	 * changed between the tell and seek it is possible to
	 * load a new buffer or for dd_loc to not match directly.
	 */
	while (dirp->dd_loc < lp->loc_loc && dirp->dd_seek == lp->loc_seek) {
		dp = _readdir_unlocked(dirp, 0);
		if (dp == NULL)
			break;
	}
}

/*
 * Reclaim memory for telldir cookies which weren't used.
 */
void
_reclaim_telldir(DIR *dirp)
{
	struct ddloc *lp;
	struct ddloc **prevlp;
	int i;

        if (__isthreaded)
		_pthread_mutex_lock(&dd_hash_lock);
	for (i = 0; i < NDIRHASH; i++) {
		prevlp = &dd_hash[i];
		lp = *prevlp;
		while (lp != NULL) {
			if (lp->loc_dirp == dirp) {
				*prevlp = lp->loc_next;
				free((caddr_t)lp);
				lp = *prevlp;
				continue;
			}
			prevlp = &lp->loc_next;
			lp = lp->loc_next;
		}
	}
        if (__isthreaded)
		_pthread_mutex_unlock(&dd_hash_lock);
}
