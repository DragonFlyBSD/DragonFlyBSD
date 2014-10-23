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
 * @(#)df.c	8.1 (Berkeley) 6/6/93
 */

/*
 * Dial the DF02-AC or DF03-AC
 */

#include "acucommon.h"
#include "tipconf.h"
#include "tip.h"

static jmp_buf Sjbuf;
static int	df_dialer(char *, char *, int);
static void	timeout(int);

int
df02_dialer(char *num, char *acu)
{

	return (df_dialer(num, acu, 0));
}

int
df03_dialer(char *num, char *acu)
{

	return (df_dialer(num, acu, 1));
}

static int
df_dialer(char *num, char *acu, int df03)
{
	int f = FD;
	int speed = 0, rw = 2;
	char c = '\0';

	acu_hupcl ();

	if (setjmp(Sjbuf)) {
		printf("connection timed out\r\n");
		df_disconnect();
		return (0);
	}
	if (boolean(value(VERBOSE)))
		printf("\ndialing...");
	fflush(stdout);
#ifdef TIOCMSET
	if (df03) {
		int st = TIOCM_ST;	/* secondary Transmit flag */

		if ((speed = acu_getspeed ()) != B1200) {	/* must dial at 1200 baud */
			acu_setspeed (B1200);
			ioctl(f, TIOCMBIC, &st); /* clear ST for 300 baud */
		} else
			ioctl(f, TIOCMBIS, &st); /* set ST for 1200 baud */
	}
#endif
	signal(SIGALRM, timeout);
	alarm(5 * strlen(num) + 10);
	ioctl(f, TIOCFLUSH, &rw);
	write(f, "\001", 1);
	sleep(1);
	write(f, "\002", 1);
	write(f, num, strlen(num));
	read(f, &c, 1);
#ifdef TIOCMSET
	if (df03 && speed) {
		acu_setspeed (speed);
	}
#endif
	return (c == 'A');
}

void
df_disconnect(void)
{
	int rw = 2;

	write(FD, "\001", 1);
	sleep(1);
	ioctl(FD, TIOCFLUSH, &rw);
}

void
df_abort(void)
{

	df_disconnect();
}


static void
timeout(int signo)
{

	longjmp(Sjbuf, 1);
}
