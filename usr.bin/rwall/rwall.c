/*
 * Copyright (c) 1993 Christopher G. Demetriou
 * Copyright (c) 1988, 1990 Regents of the University of California.
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
 * @(#) Copyright (c) 1988 Regents of the University of California. All rights reserved.
 * @(#)wall.c	5.14 (Berkeley) 3/2/91
 * $FreeBSD: src/usr.bin/rwall/rwall.c,v 1.8.2.1 2001/02/18 02:27:54 kris Exp $
 * $DragonFly: src/usr.bin/rwall/rwall.c,v 1.4 2004/09/15 19:58:05 joerg Exp $
 */

/*
 * This program is not related to David Wall, whose Stanford Ph.D. thesis
 * is entitled "Mechanisms for Broadcast and Selective Broadcast".
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <err.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include <rpc/rpc.h>
#include <rpcsvc/rwall.h>

static size_t	mbufsize;
static char	*mbuf;

static void	makemsg(char *);
static void	usage(void);

int
main(int argc, char **argv)
{
	char *wallhost, res;
	CLIENT *cl;
	struct timeval tv;
	int c;

	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1 || argc > 2)
		usage();

	wallhost = argv[0];

	makemsg(argv[1]);

	/*
	 * Create client "handle" used for calling MESSAGEPROG on the
	 * server designated on the command line. We tell the rpc package
	 * to use the "tcp" protocol when contacting the server.
	*/
	cl = clnt_create(wallhost, WALLPROG, WALLVERS, "udp");
	if (cl == NULL) {
		/*
		 * Couldn't establish connection with server.
		 * Print error message and die.
		 */
		clnt_pcreateerror(wallhost);
		exit(1);
	}

	tv.tv_sec = 15;		/* XXX ?? */
	tv.tv_usec = 0;
	if (clnt_call(cl, WALLPROC_WALL, xdr_wrapstring, &mbuf, xdr_void,
		      &res, tv) != RPC_SUCCESS) {
		/*
		 * An error occurred while calling the server.
		 * Print error message and die.
		 */
		clnt_perror(cl, wallhost);
		exit(1);
	}

	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: rwall hostname [file]\n");
	exit(1);
}

static void
makemsg(char *fname)
{
	struct tm *lt;
	struct passwd *pw;
	struct stat sbuf;
	time_t now;
	FILE *fp;
	int fd;
	char hostname[MAXHOSTNAMELEN], lbuf[256], tmpname[64];
	const char *whom, *tty;

	snprintf(tmpname, sizeof(tmpname), "%s/wall.XXXXXX", _PATH_TMP);
	if ((fd = mkstemp(tmpname)) == -1 || (fp = fdopen(fd, "r+")) == NULL)
		err(1, "can't open temporary file");
	
	if (unlink(tmpname) == -1)
		err(1, "unlink failed");

	if ((whom = getlogin()) == NULL)
		whom = (pw = getpwuid(getuid())) ? pw->pw_name : "???";
	gethostname(hostname, sizeof(hostname));

	time(&now);
	lt = localtime(&now);

	/*
	 * all this stuff is to blank out a square for the message;
	 * we wrap message lines at column 79, not 80, because some
	 * terminals wrap after 79, some do not, and we can't tell.
	 * Which means that we may leave a non-blank character
	 * in column 80, but that can't be helped.
	 */
	fprintf(fp, "Remote Broadcast Message from %s@%s\n", whom, hostname);
	tty = ttyname(STDERR_FILENO);
	if (tty == NULL)
		tty = "no tty";
	fprintf(fp, "        (%s) at %d:%02d ...\n", tty,
		lt->tm_hour, lt->tm_min);

	putc('\n', fp);

	if (fname && !(freopen(fname, "r", stdin)))
		err(1, "can't read %s", fname);
	while (fgets(lbuf, sizeof(lbuf), stdin))
		fputs(lbuf, fp);
	rewind(fp);

	if (fstat(fd, &sbuf))
		err(1, "can't stat temporary file");
	mbufsize = (size_t)sbuf.st_size;
	if ((mbuf = malloc(mbufsize)) == NULL)
		err(1, "out of memory");
	if (fread(mbuf, sizeof(*mbuf), mbufsize, fp) != mbufsize)
		err(1, "can't read temporary file");
	if (close(fd))
		warn("close failed");
}
