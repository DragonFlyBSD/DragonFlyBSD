/*
 * Copyright (c) 1996 John M. Vinopal
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project
 *	by John M. Vinopal.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $NetBSD: lastlogin.c,v 1.4 1998/02/03 04:45:35 perry Exp $
 * $FreeBSD: src/usr.sbin/lastlogin/lastlogin.c,v 1.2.2.2 2001/07/19 05:02:46 kris Exp $
 */

#include <err.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <utmpx.h>
#include <unistd.h>

static	void	outputx(struct passwd *, struct lastlogx *);
static	void	usage(void);

int
main(int argc, char **argv)
{
	int	ch, i;
	struct passwd	*passwd;
	struct lastlogx	lastx;

	while ((ch = getopt(argc, argv, "")) != -1) {
		usage();
	}

	setpassent(1);	/* Keep passwd file pointers open */

	/* Process usernames given on the command line. */
	if (argc > 1) {
		for (i = 1; i < argc; ++i) {
			if ((passwd = getpwnam(argv[i])) == NULL) {
				warnx("user '%s' not found", argv[i]);
				continue;
			}
			if ((getlastlogx(_PATH_LASTLOGX, passwd->pw_uid,
			    &lastx)) != NULL) {
				outputx(passwd, &lastx);
				goto done;
			}
		}
	}
	/* Read all lastlogx entries, looking for active ones */
	else {
		while ((passwd = getpwent()) != NULL) {
			if ((getlastlogx(_PATH_LASTLOGX, passwd->pw_uid,
			    &lastx)) != NULL) {
				if (lastx.ll_tv.tv_sec == 0)
					continue;
				outputx(passwd, &lastx);
			}
		}
	}

done:
	setpassent(0);	/* Close passwd file pointers */

	exit(0);
}

static void
outputx(struct passwd *p, struct lastlogx *l)
{
	printf("%-10s  %-8s %-22.22s   %s",
		p->pw_name, l->ll_line, l->ll_host,
		(l->ll_tv.tv_sec) ? ctime((&l->ll_tv.tv_sec)) : "Never logged in\n");
}

static void
usage(void)
{
	fprintf(stderr, "usage: lastlogin [user ...]\n");
	exit(1);
}
