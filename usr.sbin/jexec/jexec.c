/*-
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>
 * Copyright (c) 2003 Mike Barcroft <mike@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.sbin/jexec/jexec.c,v 1.2 2003/07/04 19:14:27 bmilekic Exp $
 * $DragonFly: src/usr.sbin/jexec/jexec.c,v 1.1 2005/01/31 22:29:59 joerg Exp $
 */

#include <sys/param.h>
#include <sys/jail.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int	getjailid(const char *str);
static void	usage(void);

int
main(int argc, char **argv)
{
	int jid;

	if (argc < 3)
		usage();
	jid = getjailid(argv[1]);
	if (jail_attach(jid) == -1)
		err(1, "jail_attach(%d) failed", jid);
	if (chdir("/") == -1)
		err(1, "chdir(\"/\") failed");
	if (execvp(argv[2], argv + 2) == -1)
		err(1, "execvp(%s) failed", argv[2]);
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: jexec jid command [...]\n");
	exit(1); 
}

static int
getjailid(const char *str)
{
	long v;
	char *ep;

	errno = 0;
	v = strtol(str, &ep, 10);
	if (v < INT_MIN || v > INT_MAX || errno == ERANGE)
		errc(1, ERANGE, "invalid jail id", str);
	if (ep == str || *ep != '\0')
		errx(1, "cannot parse jail id: %s.", str);

	return((int)(v));
}
