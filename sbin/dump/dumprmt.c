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
 * @(#)dumprmt.c	8.3 (Berkeley) 4/28/95
 * $FreeBSD: src/sbin/dump/dumprmt.c,v 1.14.2.1 2000/07/01 06:31:52 ps Exp $
 */

#include <sys/param.h>
#include <sys/mtio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <vfs/ufs/dinode.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <protocols/dumprestore.h>

#include <ctype.h>
#include <err.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"
#include "dump.h"

#define	TS_CLOSED	0
#define	TS_OPEN		1

static	int rmtstate = TS_CLOSED;
static	int rmtape;
static	char *rmtpeer;

static	int okname(char *);
static	int rmtcall(const char *, const char *);
static	void rmtconnaborted(int);
static	int rmtgetb(void);
static	void rmtgetconn(void);
static	void rmtgets(char *, int);
static	int rmtreply(const char *);
#ifdef KERBEROS
int	krcmd(char **, int /*u_short*/, char *, char *, int *, char *);
#endif

static	int errfd = -1;

int
rmthost(const char *hostname)
{

	if ((rmtpeer = strdup(hostname)) == NULL)
		err(1, "strdup failed");
	signal(SIGPIPE, rmtconnaborted);
	rmtgetconn();
	if (rmtape < 0)
		return (0);
	return (1);
}

static void
rmtconnaborted(int signo __unused)
{
	msg("Lost connection to remote host.\n");
	if (errfd != -1) {
		fd_set r;
		struct timeval t;

		FD_ZERO(&r);
		FD_SET(errfd, &r);
		t.tv_sec = 0;
		t.tv_usec = 0;
		if (select(errfd + 1, &r, NULL, NULL, &t)) {
			int i;
			char buf[2048];

			if ((i = read(errfd, buf, sizeof(buf) - 1)) > 0) {
				buf[i] = '\0';
				msg("on %s: %s%s", rmtpeer, buf,
					buf[i - 1] == '\n' ? "" : "\n");
			}
		}
	}

	exit(X_ABORT);
}

void
rmtgetconn(void)
{
	char *cp;
	const char *rmt;
	static struct servent *sp = NULL;
	static struct passwd *pwd = NULL;
	char *tuser;
	int size;
	int throughput;
	int on;

	if (sp == NULL) {
		sp = getservbyname(dokerberos ? "kshell" : "shell", "tcp");
		if (sp == NULL) {
			msg("%s/tcp: unknown service\n",
			    dokerberos ? "kshell" : "shell");
			exit(X_STARTUP);
		}
		pwd = getpwuid(getuid());
		if (pwd == NULL) {
			msg("who are you?\n");
			exit(X_STARTUP);
		}
	}
	if ((cp = strchr(rmtpeer, '@')) != NULL) {
		tuser = rmtpeer;
		*cp = '\0';
		if (!okname(tuser))
			exit(X_STARTUP);
		rmtpeer = ++cp;
	} else
		tuser = pwd->pw_name;
	if ((rmt = getenv("RMT")) == NULL)
		rmt = _PATH_RMT;
	msg("%s", "");
#ifdef KERBEROS
	if (dokerberos)
		rmtape = krcmd(&rmtpeer, sp->s_port, tuser, rmt, &errfd, NULL);
	else
#endif
		rmtape = rcmd(&rmtpeer, (u_short)sp->s_port, pwd->pw_name,
			      tuser, rmt, &errfd);
	if (rmtape < 0) {
		msg("login to %s as %s failed.\n", rmtpeer, tuser);
		return;
	}
	fprintf(stderr, "Connection to %s established.\n", rmtpeer);
	size = ntrec * TP_BSIZE;
	if (size > 60 * 1024)		/* XXX */
		size = 60 * 1024;
	/* Leave some space for rmt request/response protocol */
	size += 2 * 1024;
	while (size > TP_BSIZE &&
	    setsockopt(rmtape, SOL_SOCKET, SO_SNDBUF, &size, sizeof (size)) < 0)
		    size -= TP_BSIZE;
	setsockopt(rmtape, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size));
	throughput = IPTOS_THROUGHPUT;
	if (setsockopt(rmtape, IPPROTO_IP, IP_TOS,
	    &throughput, sizeof(throughput)) < 0)
		perror("IP_TOS:IPTOS_THROUGHPUT setsockopt");
	on = 1;
	if (setsockopt(rmtape, IPPROTO_TCP, TCP_NODELAY, &on, sizeof (on)) < 0)
		perror("TCP_NODELAY setsockopt");
}

static int
okname(char *cp0)
{
	char *cp;
	int c;

	for (cp = cp0; *cp; cp++) {
		c = *cp;
		if (!isascii(c) || !(isalnum(c) || c == '_' || c == '-')) {
			msg("invalid user name %s\n", cp0);
			return (0);
		}
	}
	return (1);
}

int
rmtopen(const char *rtape, int mode)
{
	char buf[256];

	snprintf(buf, sizeof (buf), "O%.226s\n%d\n", rtape, mode);
	rmtstate = TS_OPEN;
	return (rmtcall(rtape, buf));
}

void
rmtclose(void)
{

	if (rmtstate != TS_OPEN)
		return;
	rmtcall("close", "C\n");
	rmtstate = TS_CLOSED;
}

#ifdef RRESTORE
int
rmtread(char *buf, int count)
{
	char line[30];
	int n, i, cc;

	snprintf(line, sizeof (line), "R%d\n", count);
	n = rmtcall("read", line);
	if (n < 0)
		/* rmtcall() properly sets errno for us on errors. */
		return (n);
	for (i = 0; i < n; i += cc) {
		cc = read(rmtape, buf+i, n - i);
		if (cc <= 0)
			rmtconnaborted(0);
	}
	return (n);
}

int
rmtseek(int offset, int pos)
{
	char line[80];

	snprintf(line, sizeof (line), "L%d\n%d\n", offset, pos);
	return (rmtcall("seek", line));
}

int
rmtioctl(int cmd, int count)
{
	char buf[256];

	if (count < 0)
		return (-1);
	snprintf(buf, sizeof (buf), "I%d\n%d\n", cmd, count);
	return (rmtcall("ioctl", buf));
}
#endif	/* RRESTORE */

int
rmtwrite(const void *buf, int count)
{
	char line[30];

	snprintf(line, sizeof (line), "W%d\n", count);
	write(rmtape, line, strlen(line));
	write(rmtape, buf, count);
	return (rmtreply("write"));
}

static int
rmtcall(const char *cmd, const char *buf)
{
	ssize_t len = (ssize_t)strlen(buf);

	if (write(rmtape, buf, len) != len)
		rmtconnaborted(0);
	return (rmtreply(cmd));
}

static int
rmtreply(const char *cmd)
{
	char *cp;
	char code[30], emsg[BUFSIZ];

	rmtgets(code, sizeof (code));
	if (*code == 'E' || *code == 'F') {
		rmtgets(emsg, sizeof (emsg));
		msg("%s: %s", cmd, emsg);
		errno = atoi(code + 1);
		if (*code == 'F')
			rmtstate = TS_CLOSED;
		return (-1);
	}
	if (*code != 'A') {
		/* Kill trailing newline */
		cp = code + strlen(code);
		if (cp > code && *--cp == '\n')
			*cp = '\0';

		msg("Protocol to remote tape server botched (code \"%s\").\n",
		    code);
		rmtconnaborted(0);
	}
	return (atoi(code + 1));
}

int
rmtgetb(void)
{
	char c;

	if (read(rmtape, &c, 1) != 1)
		rmtconnaborted(0);
	return (c);
}

/* Get a line (guaranteed to have a trailing newline). */
void
rmtgets(char *line, int len)
{
	char *cp = line;

	while (len > 1) {
		*cp = rmtgetb();
		if (*cp == '\n') {
			cp[1] = '\0';
			return;
		}
		cp++;
		len--;
	}
	*cp = '\0';
	msg("Protocol to remote tape server botched.\n");
	msg("(rmtgets got \"%s\").\n", line);
	rmtconnaborted(0);
}
