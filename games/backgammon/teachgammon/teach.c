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
 * @(#) Copyright (c) 1980, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)teach.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/backgammon/teachgammon/teach.c,v 1.12 1999/11/30 03:48:30 billf Exp $
 */

#include <string.h>
#include <sys/types.h>
#include <termcap.h>
#include <unistd.h>
#include <signal.h>
#include "back.h"
#include "tutor.h"

const char *const helpm[] = {
	"\nEnter a space or newline to roll, or",
	"     b   to display the board",
	"     d   to double",
	"     q   to quit\n",
	0
};

const char *const contin[] = {
	"",
	0
};

int
main(int argc, char **argv)
{
	int i;

	/* revoke privs */
	setgid(getgid());

	acnt = 1;
	signal(SIGINT, getout);
	if (tcgetattr(0, &tty) == -1)			/* get old tty mode */
		errexit("teachgammon(tcgetattr)");
	old = tty.c_lflag;
	raw = ((noech = old & ~ECHO) & ~ICANON);	/* set up modes */
	ospeed = cfgetospeed(&tty);			/* for termlib */
	tflag = getcaps(getenv("TERM"));
	getarg(argc, argv);
	if (tflag) {
		noech &= ~(ICRNL | OXTABS);
		raw &= ~(ICRNL | OXTABS);
		clear();
	}
	text(hello);
	text(list);
	i = text(contin);
	if (i == 0)
		i = 2;
	init();
	while (i)
		switch (i) {
		case 1:
			leave();

		case 2:
			if ((i = text(intro1)) != 0)
				break;
			wrboard();
			if ((i = text(intro2)) != 0)
				break;

		case 3:
			if ((i = text(moves)) != 0)
				break;

		case 4:
			if ((i = text(remove)) != 0)
				break;

		case 5:
			if ((i = text(hits)) != 0)
				break;

		case 6:
			if ((i = text(endgame)) != 0)
				break;

		case 7:
			if ((i = text(doubl)) != 0)
				break;

		case 8:
			if ((i = text(stragy)) != 0)
				break;

		case 9:
			if ((i = text(prog)) != 0)
				break;

		case 10:
			if ((i = text(lastch)) != 0)
				break;
		}
	tutor();
	/* NOTREACHED */
	return (0);
}

void
leave(void)
{
	int i;

	if (tflag)
		clear();
	else
		writec('\n');
	fixtty(old);
	args[0] = strdup("backgammon");
	args[acnt++] = strdup("-n");
	args[acnt] = 0;
	execv(EXEC, args);
	for (i = 0; i < acnt; i++)
		free(args[i]);
	writel("Help! Backgammon program is missing\007!!\n");
	exit(-1);
}
