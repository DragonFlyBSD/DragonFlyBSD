/*	$NetBSD: utmpentry.c,v 1.16 2008/10/28 14:01:46 christos Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/stat.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utmpx.h>

#include "utmpentry.h"

/* Operations on timespecs. */
#define timespec2ns(x) (((uint64_t)(x)->tv_sec) * 1000000000L + (x)->tv_nsec)


#define	COMPILE_ASSERT(x)	_Static_assert(x, "assertion failed")


static void getentryx(struct utmpentry *, struct utmpx *);
static struct timespec utmpxtime = {0, 0};
static int setup(const char *);
static void adjust_size(struct utmpentry *e);

int maxname = 8, maxline = 8, maxhost = 16;
int etype = 1 << USER_PROCESS;
static int numutmp = 0;
static struct utmpentry *ehead;

static void
adjust_size(struct utmpentry *e)
{
	int max;

	if ((max = strlen(e->name)) > maxname)
		maxname = max;
	if ((max = strlen(e->line)) > maxline)
		maxline = max;
	if ((max = strlen(e->host)) > maxhost)
		maxhost = max;
}

static int
setup(const char *fname)
{
	int what = 3;
	struct stat st;
	const char *sfname;

	if (fname == NULL) {
		setutxent();
	} else {
		size_t len = strlen(fname);
		if (len == 0)
			errx(1, "Filename cannot be 0 length.");
		what = fname[len - 1] == 'x' ? 1 : 2;
		if (what == 1) {
			if (utmpxname(fname) == 0)
				warnx("Cannot set utmpx file to `%s'",
				    fname);
		}
	}
	if (what & 1) {
		sfname = fname ? fname : _PATH_UTMPX;
		if (stat(sfname, &st) == -1) {
			warn("Cannot stat `%s'", sfname);
			what &= ~1;
		} else {
			if (timespeccmp(&st.st_mtimespec, &utmpxtime, >))
			    utmpxtime = st.st_mtimespec;
			else
			    what &= ~1;
		}
	}
	return what;
}

void
endutentries(void)
{
	struct utmpentry *ep;

	timespecclear(&utmpxtime);
	ep = ehead;
	while (ep) {
		struct utmpentry *sep = ep;
		ep = ep->next;
		free(sep);
	}
	ehead = NULL;
	numutmp = 0;
}

int
getutentries(const char *fname, struct utmpentry **epp)
{
	struct utmpx *utx;
	struct utmpentry *ep;
	int what = setup(fname);
	struct utmpentry **nextp = &ehead;
	switch (what) {
	case 0:
		/* No updates */
		*epp = ehead;
		return numutmp;
	default:
		/* Need to re-scan */
		ehead = NULL;
		numutmp = 0;
	}

	while ((what & 1) && (utx = getutxent()) != NULL) {
		if (fname == NULL && ((1 << utx->ut_type) & etype) == 0) {
			continue;
		}
		if ((ep = calloc(1, sizeof(struct utmpentry))) == NULL) {
			warn(NULL);
			return 0;
		}
		getentryx(ep, utx);
		*nextp = ep;
		nextp = &(ep->next);
	}

	numutmp = 0;
	if (ehead != NULL) {
		struct utmpentry *from = ehead, *save;
		
		ehead = NULL;
		while (from != NULL) {
			for (nextp = &ehead;
			    (*nextp) && strcmp(from->line, (*nextp)->line) > 0;
			    nextp = &(*nextp)->next)
				continue;
			save = from;
			from = from->next;
			save->next = *nextp;
			*nextp = save;
			numutmp++;
		}
	}
	*epp = ehead;
	return numutmp;
}

static void
getentryx(struct utmpentry *e, struct utmpx *up)
{
	COMPILE_ASSERT(sizeof(e->name) > sizeof(up->ut_name));
	COMPILE_ASSERT(sizeof(e->line) > sizeof(up->ut_line));
	COMPILE_ASSERT(sizeof(e->host) > sizeof(up->ut_host));

	/*
	 * e has just been calloc'd. We don't need to clear it or
	 * append null-terminators, because its length is strictly
	 * greater than the source string. Use strncpy to _read_
	 * up->ut_* because they may not be terminated. For this
	 * reason we use the size of the _source_ as the length
	 * argument.
	 */
	snprintf(e->name, sizeof(e->name), "%.*s",
		 (int)sizeof(up->ut_name), up->ut_name);
	snprintf(e->line, sizeof(e->line), "%.*s",
		 (int)sizeof(up->ut_line), up->ut_line);
	snprintf(e->host, sizeof(e->host), "%.*s",
		 (int)sizeof(up->ut_host), up->ut_host);

	e->tv = up->ut_tv;
	e->pid = up->ut_pid;
	e->term = up->ut_exit.e_termination;
	e->exit = up->ut_exit.e_exit;
	e->sess = up->ut_session;
	e->type = up->ut_type;
	adjust_size(e);
}
