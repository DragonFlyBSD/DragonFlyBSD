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
 * $FreeBSD: src/lib/libc/gen/scandir.c,v 1.5.6.1 2001/03/05 09:52:13 obrien Exp $
 *
 * @(#)scandir.c	8.3 (Berkeley) 1/2/94
 */

/*
 * Scan the directory dirname calling select to make a list of selected
 * directory entries then sort using qsort and compare routine dcomp.
 * Returns the number of entries and a pointer to a list of pointers to
 * struct dirent (through namelist). Returns -1 if there were any errors.
 */

#include "namespace.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "un-namespace.h"

#include "gen_private.h"

int
scandir(const char *dirname, struct dirent ***namelist,
	int (*select)(const struct dirent *),
	int (*dcomp)(const struct dirent **, const struct dirent **))
{
	struct dirent *d, *p, **names = NULL;
	size_t arraysz, nitems = 0;
	struct stat stb;
	DIR *dirp;

	if ((dirp = opendir(dirname)) == NULL)
		return(-1);
	if (_fstat(dirp->dd_fd, &stb) < 0)
		goto fail;

	/*
	 * Estimate the array size by taking the size of the directory file
	 * and dividing it by a multiple of the minimum size entry.
	 * Ensure that the size does fit into memory without overflooding.
	 */
	if (stb.st_size / 24 > SIZE_T_MAX)
		goto fail;
	arraysz = stb.st_size / 24;
	names = (struct dirent **)malloc(arraysz * sizeof(struct dirent *));
	if (names == NULL)
		goto fail;

	while ((d = readdir(dirp)) != NULL) {
		if (select != NULL && !(*select)(d))
			continue;	/* just selected names */
		/*
		 * Make a minimum size copy of the data
		 */
		p = (struct dirent *)malloc(_DIRENT_DIRSIZ(d));
		if (p == NULL)
			goto fail;
		p->d_ino = d->d_ino;
		p->d_type = d->d_type;
		p->d_namlen = d->d_namlen;
		bcopy(d->d_name, p->d_name, p->d_namlen + 1);
		/*
		 * Check to make sure the array has space left and
		 * realloc the maximum size.
		 */
		if (nitems >= arraysz) {
			const int inc = 10;	/* increase by this much */
			struct dirent **names2;

			names2 = (struct dirent **)realloc((char *)names,
				(arraysz + inc) * sizeof(struct dirent *));
			if (names2 == NULL) {
				free(p);
				goto fail;
			}
			names = names2;
			arraysz += inc;
		}
		names[nitems++] = p;
	}
	closedir(dirp);
	if (nitems && dcomp != NULL)
		qsort(names, nitems, sizeof(struct dirent *),
		    (int (*)(const void *, const void *))dcomp);
	*namelist = names;
	return(nitems);

fail:
	while (nitems > 0)
		free(names[--nitems]);
	free(names);
	closedir(dirp);
	return -1;
}

/*
 * Alphabetic order comparison routine for those who want it.
 */
int
alphasort(const struct dirent **d1, const struct dirent **d2)
{
	return(strcmp((*d1)->d_name, (*d2)->d_name));
}
