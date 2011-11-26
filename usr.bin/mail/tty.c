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
 * @(#)tty.c	8.2 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/mail/tty.c,v 1.2.8.3 2003/01/06 05:46:04 mikeh Exp $
 * $DragonFly: src/usr.bin/mail/tty.c,v 1.5 2004/09/08 03:01:11 joerg Exp $
 */

/*
 * Mail -- a mail program
 *
 * Generally useful tty stuff.
 */

#include "rcv.h"
#include "extern.h"

static cc_t	c_erase;		/* Current erase char */
static cc_t	c_kill;			/* Current kill char */
static jmp_buf	rewrite;		/* Place to go when continued */
static jmp_buf	intjmp;			/* Place to go when interrupted */

/*
 * Read all relevant header fields.
 */

int
grabh(struct header *hp, int gflags)
{
	struct termios ttybuf;
	sig_t saveint;
	sig_t savetstp;
	sig_t savettou;
	sig_t savettin;
	int errs;
	int extproc, flag;

	savetstp = signal(SIGTSTP, SIG_DFL);
	savettou = signal(SIGTTOU, SIG_DFL);
	savettin = signal(SIGTTIN, SIG_DFL);
	errs = 0;
	if (tcgetattr(fileno(stdin), &ttybuf) < 0) {
		warn("tcgetattr(stdin)");
		return (-1);
	}
	c_erase = ttybuf.c_cc[VERASE];
	c_kill = ttybuf.c_cc[VKILL];
	extproc = ((ttybuf.c_lflag & EXTPROC) ? 1 : 0);
	if (extproc) {
		flag = 0;
		if (ioctl(fileno(stdin), TIOCEXT, &flag) < 0)
			warn("TIOCEXT: off");
	}
	if (setjmp(intjmp))
		goto out;
	saveint = signal(SIGINT, ttyint);
	if (gflags & GTO) {
		hp->h_to =
			extract(readtty("To: ", detract(hp->h_to, 0)), GTO);
	}
	if (gflags & GSUBJECT) {
		hp->h_subject = readtty("Subject: ", hp->h_subject);
	}
	if (gflags & GCC) {
		hp->h_cc =
			extract(readtty("Cc: ", detract(hp->h_cc, 0)), GCC);
	}
	if (gflags & GBCC) {
		hp->h_bcc =
			extract(readtty("Bcc: ", detract(hp->h_bcc, 0)), GBCC);
	}
out:
	signal(SIGTSTP, savetstp);
	signal(SIGTTOU, savettou);
	signal(SIGTTIN, savettin);
	if (extproc) {
		flag = 1;
		if (ioctl(fileno(stdin), TIOCEXT, &flag) < 0)
			warn("TIOCEXT: on");
	}
	signal(SIGINT, saveint);
	return (errs);
}

/*
 * Read up a header from standard input.
 * The source string has the preliminary contents to
 * be read.
 *
 */

char *
readtty(const char *pr, char *src)
{
	char ch, canonb[BUFSIZ];
	int c;
	char *cp, *cp2;

	fputs(pr, stdout);
	fflush(stdout);
	if (src != NULL && strlen(src) > BUFSIZ - 2) {
		printf("too long to edit\n");
		return (src);
	}
	cp = src == NULL ? "" : src;
	while ((c = *cp++) != '\0') {
		if ((c_erase != _POSIX_VDISABLE && c == c_erase) ||
		    (c_kill != _POSIX_VDISABLE && c == c_kill)) {
			ch = '\\';
			ioctl(0, TIOCSTI, &ch);
		}
		ch = c;
		ioctl(0, TIOCSTI, &ch);
	}
	cp = canonb;
	*cp = '\0';
	cp2 = cp;
	while (cp2 < canonb + BUFSIZ)
		*cp2++ = '\0';
	cp2 = cp;
	if (setjmp(rewrite))
		goto redo;
	signal(SIGTSTP, ttystop);
	signal(SIGTTOU, ttystop);
	signal(SIGTTIN, ttystop);
	clearerr(stdin);
	while (cp2 < canonb + BUFSIZ) {
		c = getc(stdin);
		if (c == EOF || c == '\n')
			break;
		*cp2++ = c;
	}
	*cp2 = '\0';
	signal(SIGTSTP, SIG_DFL);
	signal(SIGTTOU, SIG_DFL);
	signal(SIGTTIN, SIG_DFL);
	if (c == EOF && ferror(stdin)) {
redo:
		cp = strlen(canonb) > 0 ? canonb : NULL;
		clearerr(stdin);
		return (readtty(pr, cp));
	}
	if (equal("", canonb))
		return (NULL);
	return (savestr(canonb));
}

/*
 * Receipt continuation.
 */
void
ttystop(int s)
{
	sig_t old_action = signal(s, SIG_DFL);
	sigset_t nset;

	sigemptyset(&nset);
	sigaddset(&nset, s);
	sigprocmask(SIG_BLOCK, &nset, NULL);
	kill(0, s);
	sigprocmask(SIG_UNBLOCK, &nset, NULL);
	signal(s, old_action);
	longjmp(rewrite, 1);
}

/*ARGSUSED*/
void
ttyint(int s)
{
	longjmp(intjmp, 1);
}
