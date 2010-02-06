/*-
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
 * @(#)score.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/robots/score.c,v 1.5 1999/11/30 03:49:20 billf Exp $
 * $DragonFly: src/games/robots/score.c,v 1.4 2008/08/23 23:23:37 swildner Exp $
 */

#include "robots.h"
#include <fcntl.h>
#include <sys/types.h>
#include <pwd.h>
#include "pathnames.h"

typedef struct {
	int	s_uid;
	int	s_score;
	char	s_name[MAXNAME];
} SCORE;

const char *Scorefile = _PATH_SCORE;

int Max_per_uid = MAX_PER_UID;

static SCORE Top[MAXSCORES];

static int cmp_sc(const void *, const void *);
static void set_name(SCORE *);

/*
 * score:
 *	Post the player's score, if reasonable, and then print out the
 *	top list.
 */
void
score(void)
{
	int inf;
	SCORE *scp;
	int uid;
	bool done_show = false;
	static int numscores, max_uid;

	Newscore = false;
	if ((inf = open(Scorefile, O_RDWR)) < 0) {
		perror(Scorefile);
		return;
	}

	if (read(inf, &max_uid, sizeof max_uid) == sizeof max_uid)
		read(inf, Top, sizeof Top);
	else {
		for (scp = Top; scp < &Top[MAXSCORES]; scp++)
			scp->s_score = -1;
		max_uid = Max_per_uid;
	}

	uid = getuid();
	if (Top[MAXSCORES-1].s_score <= Score) {
		numscores = 0;
		for (scp = Top; scp < &Top[MAXSCORES]; scp++)
			if (scp->s_score < 0 ||
			    (scp->s_uid == uid && ++numscores == max_uid)) {
				if (scp->s_score > Score)
					break;
				scp->s_score = Score;
				scp->s_uid = uid;
				set_name(scp);
				Newscore = true;
				break;
			}
		if (scp == &Top[MAXSCORES]) {
			Top[MAXSCORES-1].s_score = Score;
			Top[MAXSCORES-1].s_uid = uid;
			set_name(&Top[MAXSCORES-1]);
			Newscore = true;
		}
		if (Newscore)
			qsort(Top, MAXSCORES, sizeof Top[0], cmp_sc);
	}

	if (!Newscore) {
		Full_clear = false;
		close(inf);
		return;
	}
	else
		Full_clear = true;

	for (scp = Top; scp < &Top[MAXSCORES]; scp++) {
		if (scp->s_score < 0)
			break;
		move((scp - Top) + 1, 15);
		if (!done_show && scp->s_uid == uid && scp->s_score == Score)
			standout();
		printw(" %d\t%d\t%-8.8s ", (scp - Top) + 1, scp->s_score, scp->s_name);
		if (!done_show && scp->s_uid == uid && scp->s_score == Score) {
			standend();
			done_show = true;
		}
	}
	Num_scores = scp - Top;
	refresh();

	if (Newscore) {
		lseek(inf, 0L, SEEK_SET);
		write(inf, &max_uid, sizeof max_uid);
		write(inf, Top, sizeof Top);
	}
	close(inf);
}

static void
set_name(SCORE *scp)
{
	struct passwd *pp;

	if ((pp = getpwuid(scp->s_uid)) == NULL)
		strncpy(scp->s_name, "???", MAXNAME);
	else
		strncpy(scp->s_name, pp->pw_name, MAXNAME);
}

/*
 * cmp_sc:
 *	Compare two scores.
 */
static int
cmp_sc(const void *s1, const void *s2)
{
	return (((const SCORE *)s2)->s_score - ((const SCORE *)s1)->s_score);
}

/*
 * show_score:
 *	Show the score list for the '-s' option.
 */
void
show_score(void)
{
	SCORE *scp;
	int inf;
	static int max_score;

	if ((inf = open(Scorefile, O_RDONLY)) < 0) {
		perror(Scorefile);
		return;
	}

	for (scp = Top; scp < &Top[MAXSCORES]; scp++)
		scp->s_score = -1;

	read(inf, &max_score, sizeof max_score);
	read(inf, Top, sizeof Top);
	close(inf);
	inf = 1;
	for (scp = Top; scp < &Top[MAXSCORES]; scp++)
		if (scp->s_score >= 0)
			printf("%d\t%d\t%.*s\n", inf++, scp->s_score,
			    (int)sizeof(scp->s_name), scp->s_name);
}
