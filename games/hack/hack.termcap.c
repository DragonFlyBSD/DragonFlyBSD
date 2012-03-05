/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.termcap.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.termcap.c,v 1.10 1999/11/16 10:26:38 marcel Exp $ */

#include <termcap.h>
#include "hack.h"

static char tbuf[512];
static char *HO, *CL, *CE, *tcUP, *CM, *ND, *XD, *tcBC, *SO, *SE, *TI, *TE;
static char *VS, *VE;
static int SG;
static char tcPC = '\0';
char *CD;		/* tested in pri.c: docorner() */
int CO, LI;		/* used in pri.c and whatis.c */

static void nocmov(int, int);
static void cmov(int, int);
static int xputc(int);
static int xputs(char *);

void
startup(void)
{
	char *term;
	char *tptr;
	char *tbufptr, *pc;

	tptr = alloc(1024);

	tbufptr = tbuf;
	if (!(term = getenv("TERM")))
		error("Can't get TERM.");
	if (tgetent(tptr, term) < 1)
		error("Unknown terminal type: %s.", term);
	if (tgetflag(__DECONST(char *, "NP")) ||
	    tgetflag(__DECONST(char *, "nx")))
		flags.nonull = 1;
	if ((pc = tgetstr(__DECONST(char *, "pc"), &tbufptr)))
		tcPC = *pc;
	if (!(tcBC = tgetstr(__DECONST(char *, "bc"), &tbufptr))
	    && !(tcBC = tgetstr(__DECONST(char *, "le"), &tbufptr))) {
		if (!tgetflag(__DECONST(char *, "bs")))
			error("Terminal must backspace.");
		tcBC = tbufptr;
		tbufptr += 2;
		*tcBC = '\b';
	}
	HO = tgetstr(__DECONST(char *, "ho"), &tbufptr);
	CO = tgetnum(__DECONST(char *, "co"));
	LI = tgetnum(__DECONST(char *, "li"));
	if (CO < COLNO || LI < ROWNO + 2)
		setclipped();
	if (!(CL = tgetstr(__DECONST(char *, "cl"), &tbufptr)))
		error("Hack needs CL.");
	ND = tgetstr(__DECONST(char *, "nd"), &tbufptr);
	if (tgetflag(__DECONST(char *, "os")))
		error("Hack can't have OS.");
	CE = tgetstr(__DECONST(char *, "ce"), &tbufptr);
	tcUP = tgetstr(__DECONST(char *, "up"), &tbufptr);
	/* It seems that xd is no longer supported, and we should use
	 * a linefeed instead; unfortunately this requires resetting
	 * CRMOD, and many output routines will have to be modified
	 * slightly. Let's leave that till the next release. */
	XD = tgetstr(__DECONST(char *, "xd"), &tbufptr);
/* not:                 XD = tgetstr("do", &tbufptr); */
	if (!(CM = tgetstr(__DECONST(char *, "cm"), &tbufptr))) {
		if (!tcUP && !HO)
			error("Hack needs CM or UP or HO.");
		printf("Playing hack on terminals without cm is suspect...\n");
		getret();
	}
	SO = tgetstr(__DECONST(char *, "so"), &tbufptr);
	SE = tgetstr(__DECONST(char *, "se"), &tbufptr);
	SG = tgetnum(__DECONST(char *, "sg"));
	if (!SO || !SE || (SG > 0)) SO = SE = NULL;
	CD = tgetstr(__DECONST(char *, "cd"), &tbufptr);
	set_whole_screen();             /* uses LI and CD */
	if (tbufptr - tbuf > (int)sizeof(tbuf)) error(
			"TERMCAP entry too big...\n");
	free(tptr);
}

void
start_screen(void)
{
	xputs(TI);
	xputs(VS);
}

void
end_screen(void)
{
	xputs(VE);
	xputs(TE);
}

/* not xchar: perhaps xchar is unsigned and curx-x would be unsigned as well */
void
curs(int x, int y)
{
	if (y == cury && x == curx)
		return;
	if (!ND && (curx != x || x <= 3)) {	/* Extremely primitive */
		cmov(x, y);			/* bunker!wtm */
		return;
	}
	if (abs(cury - y) <= 3 && abs(curx - x) <= 3)
		nocmov(x, y);
	else if ((x <= 3 && abs(cury - y) <= 3) || (!CM && x < abs(curx - x))) {
		putchar('\r');
		curx = 1;
		nocmov(x, y);
	} else if (!CM)
		nocmov(x, y);
	else
		cmov(x, y);
}

static void
nocmov(int x, int y)
{
	if (cury > y) {
		if (tcUP)
			while (cury > y) {	/* Go up. */
				xputs(tcUP);
				cury--;
			}
		else if (CM)
			cmov(x, y);
		else if (HO) {
			home();
			curs(x, y);
		}		/* else impossible("..."); */
	} else if (cury < y) {
		if (XD) {
			while (cury < y) {
				xputs(XD);
				cury++;
			}
		} else if (CM) {
			cmov(x, y);
		} else {
			while (cury < y) {
				xputc('\n');
				curx = 1;
				cury++;
			}
		}
	}
	if (curx < x) {		/* Go to the right. */
		if (!ND)
			cmov(x, y);
		else	/* bah */
			/* should instead print what is there already */
			while (curx < x) {
				xputs(ND);
				curx++;
			}
	} else if (curx > x) {
		while (curx > x) {	/* Go to the left. */
			xputs(tcBC);
			curx--;
		}
	}
}

static void
cmov(int x, int y)
{
	xputs(tgoto(CM, x - 1, y - 1));
	cury = y;
	curx = x;
}

static int
xputc(int c)
{
	return (fputc(c, stdout));
}

static int
xputs(char *s)
{
	return (tputs(s, 1, xputc));
}

void
cl_end(void)
{
	if (CE)
		xputs(CE);
	else {	/* no-CE fix - free after Harold Rynes */
		/* this looks terrible, especially on a slow terminal
		 * but is better than nothing */
		int cx = curx, cy = cury;

		while (curx < COLNO) {
			xputc(' ');
			curx++;
		}
		curs(cx, cy);
	}
}

void
clear_screen(void)
{
	xputs(CL);
	curx = cury = 1;
}

void
home(void)
{
	if (HO)
		xputs(HO);
	else if (CM)
		xputs(tgoto(CM, 0, 0));
	else
		curs(1, 1);	/* using tcUP ... */
	curx = cury = 1;
}

void
standoutbeg(void)
{
	if (SO)
		xputs(SO);
}

void
standoutend(void)
{
	if (SE)
		xputs(SE);
}

void
backsp(void)
{
	xputs(tcBC);
	curx--;
}

void
bell(void)
{
	putchar('\007');	/* curx does not change */
	fflush(stdout);
}

void
cl_eos(void)		/* free after Robert Viduya */
{			/* must only be called with curx = 1 */
	if (CD)
		xputs(CD);
	else {
		int cx = curx, cy = cury;
		while (cury <= LI - 2) {
			cl_end();
			xputc('\n');
			curx = 1;
			cury++;
		}
		cl_end();
		curs(cx, cy);
	}
}
