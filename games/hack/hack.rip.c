/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.rip.c - version 1.0.2 */
/* $FreeBSD: src/games/hack/hack.rip.c,v 1.4 1999/11/16 10:26:37 marcel Exp $ */
/* $DragonFly: src/games/hack/hack.rip.c,v 1.3 2005/05/22 03:37:05 y0netan1 Exp $ */

#include <stdio.h>
#include "hack.h"

static	void center(int , const char *);
extern char plname[];

static char rip[][60] = {
"                       ----------",
"                      /          \\",
"                     /    REST    \\",
"                    /      IN      \\",
"                   /     PEACE      \\",
"                  /                  \\",
"                  |                  |",
"                  |                  |",
"                  |                  |",
"                  |                  |",
"                  |                  |",
"                  |       1001       |",
"                 *|     *  *  *      | *",
"        _________)/\\\\_//(\\/(/\\)/\\//\\/|_)_______\n",
};
static const int n_rips = sizeof(rip) / sizeof(rip[0]);

outrip(){
	char *dpx;
	char buf[BUFSZ];
	int i, x, y;

	cls();
	(void) strcpy(buf, plname);
	buf[16] = 0;
	center(6, buf);
	(void) sprintf(buf, "%ld AU", u.ugold);
	center(7, buf);
	(void) sprintf(buf, "killed by%s",
		!strncmp(killer, "the ", 4) ? "" :
		!strcmp(killer, "starvation") ? "" :
		index(vowels, *killer) ? " an" : " a");
	center(8, buf);
	(void) strcpy(buf, killer);
	if(strlen(buf) > 16) {
	    int i,i0,i1;
		i0 = i1 = 0;
		for(i = 0; i <= 16; i++)
			if(buf[i] == ' ') i0 = i, i1 = i+1;
		if(!i0) i0 = i1 = 16;
		buf[i1 + 16] = 0;
		center(10, buf+i1);
		buf[i0] = 0;
	}
	center(9, buf);
	(void) sprintf(buf, "%4d", getyear());
	center(11, buf);
	for(y = 8, i = 0; i < n_rips; y++, i++){
		x = 0;
		dpx = rip[i];
		while(dpx[x]) {
			while(dpx[x] == ' ') x++;
			curs(x,y);
			while(dpx[x] && dpx[x] != ' '){
				extern int done_stopprint;
				if(done_stopprint)
					return;
				curx++;
				(void) putchar(dpx[x++]);
			}
		}
	}
	getret();
}

static void
center(int line, const char *text)
{
	const char *ip = text;
	char *op;

	op = &rip[line][28 - ((strlen(text)+1)/2)];
	while(*ip) *op++ = *ip++;
}
