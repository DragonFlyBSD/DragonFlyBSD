/*
 * Copyright (c) 1980, 1993
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
 * @(#)mkheaders.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.sbin/config/mkheaders.c,v 1.14.2.2 2001/01/23 00:09:32 peter Exp $
 */

/*
 * Make all the .h files for the optional entries
 */

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "config.h"
#include "y.tab.h"

static void do_header(const char *, char *, int);
static void do_count(const char *, char *, int);
static char *toheader(const char *);
static char *tomacro(const char *);

void
headers(void)
{
	struct file_list *fl;
	struct device *dp;

	for (fl = ftab; fl != NULL; fl = fl->f_next)
		if (fl->f_needs != NULL)
			do_count(fl->f_needs, fl->f_needs, 1);
	for (dp = dtab; dp != NULL; dp = dp->d_next) {
		if ((dp->d_type & TYPEMASK) == PSEUDO_DEVICE) {
			if (!(dp->d_type & DEVDONE)) {
				printf("Warning: pseudo-device \"%s\" is unknown\n",
				       dp->d_name);
				exit(1);
			}
		}
		if ((dp->d_type & TYPEMASK) == DEVICE) {
			if (!(dp->d_type & DEVDONE)) {
				printf("Warning: device \"%s\" is unknown\n",
				       dp->d_name);
				exit(1);
			}
		}
	}
}

/*
 * count all the devices of a certain type and recurse to count
 * whatever the device is connected to
 */
static void
do_count(const char *dev, char *hname, int search)
{
	struct device *dp;
	int count, hicount;
	const char *mp;

	/*
	 * After this loop, "count" will be the actual number of units,
	 * and "hicount" will be the highest unit declared. do_header()
	 * must use the higher of these values.
	 */
	for (dp = dtab; dp != NULL; dp = dp->d_next) {
		if (strcmp(dp->d_name, dev) == 0) {
			if ((dp->d_type & TYPEMASK) == PSEUDO_DEVICE)
				dp->d_type |= DEVDONE;
			else if ((dp->d_type & TYPEMASK) == DEVICE)
				dp->d_type |= DEVDONE;
		}
	}
	for (hicount = count = 0, dp = dtab; dp != NULL; dp = dp->d_next) {
		if (dp->d_unit != -1 && strcmp(dp->d_name, dev) == 0) {
			if ((dp->d_type & TYPEMASK) == PSEUDO_DEVICE) {
				count =
				    dp->d_count != UNKNOWN ? dp->d_count : 1;
				break;
			}
			count++;
			/*
			 * Allow holes in unit numbering,
			 * assumption is unit numbering starts
			 * at zero.
			 */
			if (dp->d_unit + 1 > hicount)
				hicount = dp->d_unit + 1;
			if (search) {
				mp = dp->d_conn;
				if (mp != NULL && dp->d_connunit < 0)
					mp = NULL;
				if (mp != NULL && strcmp(mp, "nexus") == 0)
					mp = NULL;
				if (mp != NULL) {
					do_count(mp, hname, 0);
					search = 0;
				}
			}
		}
	}
	do_header(dev, hname, count > hicount ? count : hicount);
}

static void
do_header(const char *dev, char *hname, int count)
{
	char *file, *name, *inw;
	struct file_list *fl, *fl_head, *tflp;
	FILE *inf, *outf;
	int inc, oldcount;

	file = toheader(hname);
	name = tomacro(dev);
	inf = fopen(file, "r");
	oldcount = -1;
	if (inf == NULL) {
		outf = fopen(file, "w");
		if (outf == NULL)
			err(1, "%s", file);
		fprintf(outf, "#define %s %d\n", name, count);
		fclose(outf);
		return;
	}
	fl_head = NULL;
	for (;;) {
		char *cp;
		if ((inw = get_word(inf)) == NULL || inw == (char *)EOF)
			break;
		if ((inw = get_word(inf)) == NULL || inw == (char *)EOF)
			break;
		inw = strdup(inw);
		cp = get_word(inf);
		if (cp == NULL || cp == (char *)EOF)
			break;
		inc = atoi(cp);
		if (strcmp(inw, name) == 0) {
			oldcount = inc;
			inc = count;
		}
		cp = get_word(inf);
		if (cp == (char *)EOF)
			break;
		fl = malloc(sizeof(*fl));
		bzero(fl, sizeof(*fl));
		fl->f_fn = inw;		/* malloced */
		fl->f_type = inc;
		fl->f_next = fl_head;
		fl_head = fl;
	}
	fclose(inf);
	if (count == oldcount) {
		for (fl = fl_head; fl != NULL; fl = tflp) {
			tflp = fl->f_next;
			free(fl->f_fn);
			free(fl);
		}
		return;
	}
	if (oldcount == -1) {
		fl = malloc(sizeof(*fl));
		bzero(fl, sizeof(*fl));
		fl->f_fn = strdup(name);
		fl->f_type = count;
		fl->f_next = fl_head;
		fl_head = fl;
	}
	outf = fopen(file, "w");
	if (outf == NULL)
		err(1, "%s", file);
	for (fl = fl_head; fl != NULL; fl = tflp) {
		fprintf(outf,
		    "#define %s %u\n", fl->f_fn, count ? fl->f_type : 0);
		tflp = fl->f_next;
		free(fl->f_fn);
		free(fl);
	}
	fclose(outf);
}

/*
 * convert a dev name to a .h file name
 */
static char *
toheader(const char *dev)
{
	static char hbuf[MAXPATHLEN];
	static char udev[MAXPATHLEN];

	snprintf(udev, sizeof(udev), "use_%s", dev);

	snprintf(hbuf, sizeof(hbuf), "%s.h", path(udev));
	return(hbuf);
}

/*
 * convert a dev name to a macro name
 */
static char *
tomacro(const char *dev)
{
	static char mbuf[20];
	char *cp;

	cp = mbuf;
	*cp++ = 'N';
	while (*dev != 0)
		*cp++ = islower(*dev) ? toupper(*dev++) : *dev++;
	*cp++ = 0;
	return(mbuf);
}
