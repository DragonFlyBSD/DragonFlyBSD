/*
 * Copyright (c) 1986, 1993
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
 * @(#)acucommon.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/tip/libacu/acucommon.c,v 1.3 1999/08/28 01:06:30 peter Exp $
 */

/*
 * Routines for calling up on a Courier modem.
 * Derived from Hayes driver.
 */
#include "acucommon.h"
#include "tip.h"

#include <err.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/times.h>

void
acu_nap(unsigned int how_long)
{
	struct timeval t;
	t.tv_usec = (how_long % 1000) * 1000;
	t.tv_sec = how_long / 1000;
	(void) select (0, NULL, NULL, NULL, &t);
}

void
acu_hw_flow_control(int hw_flow_control)
{
	struct termios t;
	if (tcgetattr (FD, &t) == 0) {
		if (hw_flow_control)
			t.c_cflag |= CRTSCTS;
		else
			t.c_cflag &= ~CRTSCTS;
		tcsetattr (FD, TCSANOW, &t);
	}
}

int
acu_flush(void)
{
	int flags = 0;
	return (ioctl (FD, TIOCFLUSH, &flags) == 0);	/* flush any clutter */
}

int
acu_getspeed(void)
{
	struct termios term;
	tcgetattr (FD, &term);
	return (term.c_ospeed);
}

int
acu_setspeed(int speed)
{
	int rc = 0;
	struct termios term;
	if (tcgetattr (FD, &term) == 0) {
		cfsetspeed (&term, speed);
		if (tcsetattr (FD, TCSANOW, &term) == 0)
			++rc;
	}
	return (rc);
}

void
acu_hupcl(void)
{
	struct termios term;
	tcgetattr (FD, &term);
	term.c_cflag |= HUPCL;
	tcsetattr (FD, TCSANOW, &term);
}

/* end of acucommon.c */
