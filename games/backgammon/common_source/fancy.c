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
 * @(#)fancy.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/backgammon/common_source/fancy.c,v 1.7 1999/11/30 03:48:25 billf Exp $
 */

#include <string.h>
#include <termcap.h>
#include "back.h"

static void	bsect(int, int, int, int);
static void	fixpos(int, int, int, int, int);
static void	fixcol(int, int, int, int, int);
static void	newline(void);

char	PC;			/* padding character */
char	*BC;			/* backspace sequence */
char	*CD;			/* clear to end of screen sequence */
char	*CE;			/* clear to end of line sequence */
char	*CL;			/* clear screen sequence */
char	*CM;			/* cursor movement instructions */
char	*HO;			/* home cursor sequence */
char	*MC;			/* column cursor movement map */
char	*ML;			/* row cursor movement map */
char	*ND;			/* forward cursor sequence */
char	*UP;			/* up cursor sequence */

int	lHO;			/* length of HO */
int	lBC;			/* length of BC */
int	lND;			/* length of ND */
int	lUP;			/* length of UP */
int	CO;			/* number of columns */
int	LI;			/* number of lines */
int	*linect;		/* array of lengths of lines on screen
				   (the actual screen is not stored) */

				/* two letter codes */
char	tcap[] = "bccdceclcmhomcmlndup";
				/* corresponding strings */
char	**tstr[] = { &BC, &CD, &CE, &CL, &CM, &HO, &MC, &ML, &ND, &UP };

int	buffnum;		/* pointer to output buffer */

char	tbuf[1024];		/* buffer for decoded termcap entries */

int	oldb[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

int	oldr;
int	oldw;
/*
 * "real" cursor positions, so it knows when to reposition. These are -1 if
 * curr and curc are accurate
 */
int	realr;
int	realc;

void
fboard(void)
{
	int i, j, l;

	curmove(0, 0);		/* do top line */
	for (i = 0; i < 53; i++)
		fancyc('_');

	curmove(15, 0);		/* do bottom line */
	for (i = 0; i < 53; i++)
		fancyc('_');

	l = 1;			/* do vertical lines */
	for (i = 52; i > -1; i -= 28) {
		curmove((l == 1 ? 1 : 15), i);
		fancyc('|');
		for (j = 0; j < 14; j++) {
			curmove(curr + l, curc - 1);
			fancyc('|');
		}
		if (i == 24)
			i += 32;
		l = -l;		/* alternate directions */
	}

	curmove(2, 1);		/* label positions 13-18 */
	for (i = 13; i < 18; i++) {
		fancyc('1');
		fancyc((i % 10) + '0');
		curmove(curr, curc + 2);
	}
	fancyc('1');
	fancyc('8');

	curmove(2, 29);		/* label positions 19-24 */
	fancyc('1');
	fancyc('9');
	for (i = 20; i < 25; i++) {
		curmove(curr, curc + 2);
		fancyc('2');
		fancyc((i % 10) + '0');
	}

	curmove(14, 1);		/* label positions 12-7 */
	fancyc('1');
	fancyc('2');
	for (i = 11; i > 6; i--) {
		curmove(curr, curc + 2);
		fancyc(i > 9 ? '1' : ' ');
		fancyc((i % 10) + '0');
	}

	curmove(14, 30);	/* label positions 6-1 */
	fancyc('6');
	for (i = 5; i > 0; i--) {
		curmove(curr, curc + 3);
		fancyc(i + '0');
	}

	for (i = 12; i > 6; i--)	/* print positions 12-7 */
		if (board[i])
			bsect(board[i], 13, 1 + 4 * (12 - i), -1);

	if (board[0])		/* print red men on bar */
		bsect(board[0], 13, 25, -1);

	for (i = 6; i > 0; i--)	/* print positions 6-1 */
		if (board[i])
			bsect(board[i], 13, 29 + 4 * (6 - i), -1);

	l = (off[1] < 0 ? off[1] + 15 : off[1]);	/* print white's home */
	bsect(l, 3, 54, 1);

	curmove(8, 25);		/* print the word BAR */
	fancyc('B');
	fancyc('A');
	fancyc('R');

	for (i = 13; i < 19; i++)	/* print positions 13-18 */
		if (board[i])
			bsect(board[i], 3, 1 + 4 * (i - 13), 1);

	if (board[25])		/* print white's men on bar */
		bsect(board[25], 3, 25, 1);

	for (i = 19; i < 25; i++)	/* print positions 19-24 */
		if (board[i])
			bsect(board[i], 3, 29 + 4 * (i - 19), 1);

	l = (off[0] < 0 ? off[0] + 15 : off[0]);	/* print red's home */
	bsect(-l, 13, 54, -1);

	for (i = 0; i < 26; i++)	/* save board position
					* for refresh later */
		oldb[i] = board[i];
	oldr = (off[1] < 0 ? off[1] + 15 : off[1]);
	oldw = -(off[0] < 0 ? off[0] + 15 : off[0]);
}

/*
 * bsect (b,rpos,cpos,cnext)
 *	Print the contents of a board position.  "b" has the value of the
 * position, "rpos" is the row to start printing, "cpos" is the column to
 * start printing, and "cnext" is positive if the position starts at the top
 * and negative if it starts at the bottom.  The value of "cpos" is checked
 * to see if the position is a player's home, since those are printed
 * differently.
 */
static void
bsect(int b, int rpos, int cpos, int cnext)
{
	int j;			/* index */
	int n;			/* number of men on position */
	int bct;		/* counter */
	int k;			/* index */
	char pc;		/* color of men on position */

	bct = 0;
	n = abs(b);			/* initialize n and pc */
	pc = (b > 0 ? 'r' : 'w');

	if (n < 6 && cpos < 54)		/* position cursor at start */
		curmove(rpos, cpos + 1);
	else
		curmove(rpos, cpos);

	for (j = 0; j < 5; j++) {	/* print position row by row */
		for (k = 0; k < 15; k += 5)	/* print men */
			if (n > j + k)
				fancyc(pc);

		if (j < 4) {	/* figure how far to back up for next row */
			if (n < 6) {		/* stop if none left */
				if (j + 1 == n)
					break;
				bct = 1;	/* single column */
			} else {
				if (n < 11) {	/* two columns */
					if (cpos == 54) {	/* home pos */
						if (j + 5 >= n)
							bct = 1;
						else
							bct = 2;
					}
					if (cpos < 54) {	/* not home */
						if (j + 6 >= n)
							bct = 1;
						else
							bct = 2;
					}
				} else {	/* three columns */
					if (j + 10 >= n)
						bct = 2;
					else
						bct = 3;
				}
			}
			/* reposition cursor */
			curmove(curr + cnext, curc - bct);
		}
	}
}

void
refresh(void)
{
	int i, r, c;

	r = curr;			/* save current position */
	c = curc;

	for (i = 12; i > 6; i--)	/* fix positions 12-7 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 13, 1 + (12 - i) * 4, -1);
			oldb[i] = board[i];
		}
	if (board[0] != oldb[0]) {	/* fix red men on bar */
		fixpos(oldb[0], board[0], 13, 25, -1);
		oldb[0] = board[0];
	}
	for (i = 6; i > 0; i--)		/* fix positions 6-1 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 13, 29 + (6 - i) * 4, -1);
			oldb[i] = board[i];
		}
	i = -(off[0] < 0 ? off[0] + 15 : off[0]);	/* fix white's home */
	if (oldw != i) {
		fixpos(oldw, i, 13, 54, -1);
		oldw = i;
	}
	for (i = 13; i < 19; i++)	/* fix positions 13-18 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 3, 1 + (i - 13) * 4, 1);
			oldb[i] = board[i];
		}
	if (board[25] != oldb[25]) {	/* fix white men on bar */
		fixpos(oldb[25], board[25], 3, 25, 1);
		oldb[25] = board[25];
	}
	for (i = 19; i < 25; i++)	/* fix positions 19-24 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 3, 29 + (i - 19) * 4, 1);
			oldb[i] = board[i];
		}
	i = (off[1] < 0 ? off[1] + 15 : off[1]);	/* fix red's home */
	if (oldr != i) {
		fixpos(oldr, i, 3, 54, 1);
		oldr = i;
	}
	curmove(r, c);			/* return to saved position */
	newpos();
	buflush();
}

static void
fixpos(int cur, int new, int r, int c, int inc)
{
	int o, n, nv;
	int ov, nc;
	char col;

	nc = 0;
	if (cur * new >= 0) {
		ov = abs(cur);
		nv = abs(new);
		col = (cur + new > 0 ? 'r' : 'w');
		o = (ov - 1) / 5;
		n = (nv - 1) / 5;
		if (o == n) {
			if (o == 2)
				nc = c + 2;
			if (o == 1)
				nc = c < 54 ? c : c + 1;
			if (o == 0)
				nc = c < 54 ? c + 1 : c;
			if (ov > nv)
				fixcol(r + inc * (nv - n * 5), nc,
				    abs(ov - nv), ' ', inc);
			else
				fixcol(r + inc * (ov - o * 5), nc,
				    abs(ov - nv), col, inc);
			return;
		} else {
			if (c < 54) {
				if (o + n == 1) {
					if (n) {
						fixcol(r, c, abs(nv - 5), col,
						    inc);
						if (ov != 5)
							fixcol(r + inc * ov,
							    c + 1, abs(ov - 5),
							    col, inc);
					} else {
						fixcol(r, c, abs(ov - 5), ' ',
						    inc);
						if (nv != 5)
							fixcol(r + inc * nv,
							    c + 1, abs(nv - 5),
							    ' ', inc);
					}
					return;
				}
				if (n == 2) {
					if (ov != 10)
						fixcol(r + inc * (ov - 5), c,
						    abs(ov - 10), col, inc);
					fixcol(r, c + 2, abs(nv - 10), col, inc);
				} else {
					if (nv != 10)
						fixcol(r + inc * (nv - 5), c,
						    abs(nv - 10), ' ', inc);
					fixcol(r, c + 2, abs(ov - 10), ' ', inc);
				}
				return;
			}
			if (n > o) {
				fixcol(r + inc * (ov % 5), c + o,
				    abs(5 * n - ov), col, inc);
				if (nv != 5 * n)
					fixcol(r, c + n, abs(5 * n - nv),
					    col, inc);
			} else {
				fixcol(r + inc * (nv % 5), c + n,
				    abs(5 * n - nv), ' ', inc);
				if (ov != 5 * o)
					fixcol(r, c + o, abs(5 * o - ov),
					    ' ', inc);
			}
			return;
		}
	}
	nv = abs(new);
	fixcol(r, c + 1, nv, new > 0 ? 'r' : 'w', inc);
	if (abs(cur) <= abs(new))
		return;
	fixcol(r + inc * new, c + 1, abs(cur + new), ' ', inc);
}

static void
fixcol(int r, int c, int l, int ch, int inc)
{
	int i;

	curmove(r, c);
	fancyc(ch);
	for (i = 1; i < l; i++) {
		curmove(curr + inc, curc - 1);
		fancyc(ch);
	}
}

void
curmove(int r, int c)
{
	if (curr == r && curc == c)
		return;
	if (realr == -1) {
		realr = curr;
		realc = curc;
	}
	curr = r;
	curc = c;
}

void
newpos(void)
{
	int r;			/* destination row */
	int c;			/* destination column */
	int mode;		/* mode of movement */

	int ccount;		/* character count */
	int i;			/* index */
	int n;			/* temporary variable */
	char *m;		/* string containing CM movement */

	mode = -1;
	ccount = 1000;
	m = NULL;
	if (realr == -1)	/* see if already there */
		return;

	r = curr;		/* set current and dest. positions */
	c = curc;
	curr = realr;
	curc = realc;

	/* double check position */
	if (curr == r && curc == c) {
		realr = realc = -1;
		return;
	}
	if (CM) {		/* try CM to get there */
		mode = 0;
		m = (char *)tgoto(CM, c, r);
		ccount = strlen(m);
	}
	/* try HO and local movement */
	if (HO && (n = r + c * lND + lHO) < ccount) {
		mode = 1;
		ccount = n;
	}
	/* try various LF combinations */
	if (r >= curr) {
		/* CR, LF, and ND */
		if ((n = (r - curr) + c * lND + 1) < ccount) {
			mode = 2;
			ccount = n;
		}
		/* LF, ND */
		if (c >= curc && (n = (r - curr) + (c - curc) * lND) < ccount) {
			mode = 3;
			ccount = n;
		}
		/* LF, BS */
		if (c < curc && (n = (r - curr) + (curc - c) * lBC) < ccount) {
			mode = 4;
			ccount = n;
		}
	}
	/* try corresponding UP combinations */
	if (r < curr) {
		/* CR, UP, and ND */
		if ((n = (curr - r) * lUP + c * lND + 1) < ccount) {
			mode = 5;
			ccount = n;
		}
		/* UP and ND */
		if (c >= curc &&
		    (n = (curr - r) * lUP + (c - curc) * lND) < ccount) {
			mode = 6;
			ccount = n;
		}
		/* UP and BS */
		if (c < curc &&
		    (n = (curr - r) * lUP + (curc - c) * lBC) < ccount) {
			mode = 7;
			ccount = n;
		}
	}
	/* space over */
	if (curr == r && c > curc && linect[r] < curc && c - curc < ccount)
		mode = 8;

	switch (mode) {
	case -1:	/* error! */
		write(2, "\r\nInternal cursor error.\r\n", 26);
		getout(0);

	case 0:		/* direct cursor motion */
		tputs(m, abs(curr - r), addbuf);
		break;

	case 1:		/* relative to "home" */
		tputs(HO, r, addbuf);
		for (i = 0; i < r; i++)
			addbuf('\012');
		for (i = 0; i < c; i++)
			tputs(ND, 1, addbuf);
		break;

	case 2:		/* CR and down and over */
		addbuf('\015');
		for (i = 0; i < r - curr; i++)
			addbuf('\012');
		for (i = 0; i < c; i++)
			tputs(ND, 1, addbuf);
		break;

	case 3:		/* down and over */
		for (i = 0; i < r - curr; i++)
			addbuf('\012');
		for (i = 0; i < c - curc; i++)
			tputs(ND, 1, addbuf);
		break;

	case 4:		/* down and back */
		for (i = 0; i < r - curr; i++)
			addbuf('\012');
		for (i = 0; i < curc - c; i++)
			addbuf('\010');
		break;

	case 5:		/* CR and up and over */
		addbuf('\015');
		for (i = 0; i < curr - r; i++)
			tputs(UP, 1, addbuf);
		for (i = 0; i < c; i++)
			tputs(ND, 1, addbuf);
		break;

	case 6:		/* up and over */
		for (i = 0; i < curr - r; i++)
			tputs(UP, 1, addbuf);
		for (i = 0; i < c - curc; i++)
			tputs(ND, 1, addbuf);
		break;

	case 7:		/* up and back */
		for (i = 0; i < curr - r; i++)
			tputs(UP, 1, addbuf);
		for (i = 0; i < curc - c; i++) {
			if (BC)
				tputs(BC, 1, addbuf);
			else
				addbuf('\010');
		}
		break;

	case 8:		/* safe space */
		for (i = 0; i < c - curc; i++)
			addbuf(' ');
	}

	/* fix positions */
	curr = r;
	curc = c;
	realr = -1;
	realc = -1;
}

void
clear(void)
{
	int i;

	/* double space if can't clear */
	if (CL == NULL) {
		writel("\n\n");
		return;
	}
	curr = curc = 0;		/* fix position markers */
	realr = realc = -1;
	for (i = 0; i < 24; i++)	/* clear line counts */
		linect[i] = -1;
	buffnum = -1;			/* ignore leftover buffer contents */
	tputs(CL, CO, addbuf);		/* put CL in buffer */
}

/* input is character to output */
void
fancyc(char c)
{
	int sp;			/* counts spaces in a tab */

	if (c == '\007') {	/* bells go in blindly */
		addbuf(c);
		return;
	}

	/*
	 * process tabs, use spaces if the the tab should be erasing things,
	 * otherwise use cursor movement routines.  Note this does not use
	 * hardware tabs at all.
	 */
	if (c == '\t') {
		sp = (curc + 8) & (~7);		/* compute spaces */
		/* check line length */
		if (linect[curr] >= curc || sp < 4) {
			for (; sp > curc; sp--)
				addbuf(' ');
			curc = sp;		/* fix curc */
		} else
			curmove(curr, sp);
		return;
	}
	/* do newline be calling newline */
	if (c == '\n') {
		newline();
		return;
	}
	/* ignore any other control chars */
	if (c < ' ')
		return;

	/*
	 * if an erasing space or non-space, just add it to buffer.  Otherwise
	 * use cursor movement routine, so that multiple spaces will be grouped
	 * together
	 */
	if (c > ' ' || linect[curr] >= curc) {
		newpos();		/* make sure position correct */
		addbuf(c);		/* add character to buffer */
		/* fix line length */
		if (c == ' ' && linect[curr] == curc)
			linect[curr]--;
		else
			if (linect[curr] < curc)
				linect[curr] = curc;
		curc++;			/* fix curc */
	} else
		/* use cursor movement routine */
		curmove(curr, curc + 1);
}

void
clend(void)
{
	int i;

	if (CD) {
		tputs(CD, CO - curr, addbuf);
		for (i = curr; i < LI; i++)
			linect[i] = -1;
		return;
	}
	curmove(i = curr, 0);
	cline();
	while (curr < LI - 1) {
		curmove(curr + 1, 0);
		if (linect[curr] > -1)
			cline();
	}
	curmove(i, 0);
}

void
cline(void)
{
	int c;

	if (curc > linect[curr])
		return;
	newpos();
	if (CE) {
		tputs(CE, 1, addbuf);
		linect[curr] = curc - 1;
	} else {
		c = curc - 1;
		while (linect[curr] > c) {
			addbuf(' ');
			curc++;
			linect[curr]--;
		}
		curmove(curr, c + 1);
	}
}

static void
newline(void)
{
	cline();
	if (curr == LI - 1)
		curmove(begscr, 0);
	else
		curmove(curr + 1, 0);
}

int
getcaps(const char *s)
{
	char   *code;		/* two letter code */
	char ***cap;		/* pointer to cap string */
	char   *bufp;		/* pointer to cap buffer */
	char    tentry[1024];	/* temporary uncoded caps buffer */

	tgetent(tentry, s);	/* get uncoded termcap entry */

	LI = tgetnum("li");	/* get number of lines */
	if (LI == -1)
		LI = 12;
	CO = tgetnum("co");	/* get number of columns */
	if (CO == -1)
		CO = 65;

	bufp = tbuf;		/* get padding character */
	tgetstr("pc", &bufp);
	if (bufp != tbuf)
		PC = *tbuf;
	else
		PC = 0;

	bufp = tbuf;		/* get string entries */
	cap = tstr;
	for (code = tcap; *code; code += 2)
		**cap++ = (char *)tgetstr(code, &bufp);

	/* get pertinent lengths */
	if (HO)
		lHO = strlen(HO);
	if (BC)
		lBC = strlen(BC);
	else
		lBC = 1;
	if (UP)
		lUP = strlen(UP);
	if (ND)
		lND = strlen(ND);
	if (LI < 24 || CO < 72 || !(CL && UP && ND))
		return (0);
	linect = calloc(LI + 1, sizeof(int));
	return (1);
}
