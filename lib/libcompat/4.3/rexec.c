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
 * $FreeBSD: src/lib/libcompat/4.3/rexec.c,v 1.5.8.3 2000/11/22 13:36:00 ben Exp $
 *
 * @(#)rexec.c	8.1 (Berkeley) 6/4/93
 */

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>
#include <err.h>
#include <stdlib.h>

#define	SA_LEN(addr)		((addr)->sa_len)
#define	__set_errno(val)	errno = (val)

int	rexecoptions;
char	*getpass(), *getlogin();

/*
 * Options and other state info.
 */
struct macel {
	char mac_name[9];	/* macro name */
	char *mac_start;	/* start of macro in macbuf */
	char *mac_end;		/* end of macro in macbuf */
};

int macnum;			/* number of defined macros */
struct macel macros[16];
char macbuf[4096];

static	FILE *cfile;

#define	DEFAULT	1
#define	LOGIN	2
#define	PASSWD	3
#define	ACCOUNT 4
#define MACDEF  5
#define	ID	10
#define	MACH	11

static char tokval[100];

static struct toktab {
	char *tokstr;
	int tval;
} toktab[]= {
	{ "default",	DEFAULT },
	{ "login",	LOGIN },
	{ "password",	PASSWD },
	{ "passwd",	PASSWD },
	{ "account",	ACCOUNT },
	{ "machine",	MACH },
	{ "macdef",	MACDEF },
	{ NULL,		0 }
};

static int
token(void)
{
	char *cp;
	int c;
	struct toktab *t;

	if (feof(cfile) || ferror(cfile))
		return (0);
	while ((c = getc(cfile)) != EOF &&
	    (c == '\n' || c == '\t' || c == ' ' || c == ','))
		continue;
	if (c == EOF)
		return (0);
	cp = tokval;
	if (c == '"') {
		while ((c = getc(cfile)) != EOF && c != '"') {
			if (c == '\\')
				c = getc(cfile);
			*cp++ = c;
		}
	} else {
		*cp++ = c;
		while ((c = getc(cfile)) != EOF
		    && c != '\n' && c != '\t' && c != ' ' && c != ',') {
			if (c == '\\')
				c = getc(cfile);
			*cp++ = c;
		}
	}
	*cp = 0;
	if (tokval[0] == 0)
		return (0);
	for (t = toktab; t->tokstr; t++)
		if (!strcmp(t->tokstr, tokval))
			return (t->tval);
	return (ID);
}

static int
ruserpass(char *host, const char **aname, const char **apass, char **aacct)
{
	char *hdir, buf[BUFSIZ], *tmp;
	char myname[MAXHOSTNAMELEN], *mydomain;
	int t, i, c, usedefault = 0;
	struct stat stb;

	hdir = getenv("HOME");
	if (hdir == NULL)
		hdir = ".";
	if (strlen(hdir) + 8 > sizeof(buf))
		return (0);
	(void) sprintf(buf, "%s/.netrc", hdir);
	cfile = fopen(buf, "r");
	if (cfile == NULL) {
		if (errno != ENOENT)
			warn("%s", buf);
		return (0);
	}
	if (gethostname(myname, sizeof(myname)) < 0)
		myname[0] = '\0';
	if ((mydomain = strchr(myname, '.')) == NULL)
		mydomain = "";
next:
	while ((t = token())) switch(t) {

	case DEFAULT:
		usedefault = 1;
		/* FALL THROUGH */

	case MACH:
		if (!usedefault) {
			if (token() != ID)
				continue;
			/*
			 * Allow match either for user's input host name
			 * or official hostname.  Also allow match of
			 * incompletely-specified host in local domain.
			 */
			if (strcasecmp(host, tokval) == 0)
				goto match;
			if ((tmp = strchr(host, '.')) != NULL &&
			    strcasecmp(tmp, mydomain) == 0 &&
			    strncasecmp(host, tokval, tmp - host) == 0 &&
			    tokval[tmp - host] == '\0')
				goto match;
			continue;
		}
	match:
		while ((t = token()) && t != MACH && t != DEFAULT) switch(t) {

		case LOGIN:
			if (token()) {
				if (*aname == NULL) {
					char *tmp;
					tmp = malloc(strlen(tokval) + 1);
					strcpy(tmp, tokval);
					*aname = tmp;
				} else {
					if (strcmp(*aname, tokval))
						goto next;
				}
			}
			break;
		case PASSWD:
			if ((*aname == NULL || strcmp(*aname, "anonymous")) &&
			    fstat(fileno(cfile), &stb) >= 0 &&
			    (stb.st_mode & 077) != 0) {
	warnx("Error: .netrc file is readable by others.");
	warnx("Remove password or make file unreadable by others.");
				goto bad;
			}
			if (token() && *apass == NULL) {
				char *tmp;
				tmp = malloc(strlen(tokval) + 1);
				strcpy(tmp, tokval);
				*apass = tmp;
			}
			break;
		case ACCOUNT:
			if (fstat(fileno(cfile), &stb) >= 0
			    && (stb.st_mode & 077) != 0) {
	warnx("Error: .netrc file is readable by others.");
	warnx("Remove account or make file unreadable by others.");
				goto bad;
			}
			if (token() && *aacct == NULL) {
				*aacct = malloc((unsigned) strlen(tokval) + 1);
				(void) strcpy(*aacct, tokval);
			}
			break;
		case MACDEF:
			while ((c=getc(cfile)) != EOF &&
						(c == ' ' || c == '\t'))
				;
			if (c == EOF || c == '\n') {
				printf("Missing macdef name argument.\n");
				goto bad;
			}
			if (macnum == 16) {
				printf("Limit of 16 macros have already been defined\n");
				goto bad;
			}
			tmp = macros[macnum].mac_name;
			*tmp++ = c;
			for (i=0; i < 8 && (c=getc(cfile)) != EOF &&
			    !isspace(c); ++i) {
				*tmp++ = c;
			}
			if (c == EOF) {
				printf("Macro definition missing null line terminator.\n");
				goto bad;
			}
			*tmp = '\0';
			if (c != '\n') {
				while ((c=getc(cfile)) != EOF && c != '\n');
			}
			if (c == EOF) {
				printf("Macro definition missing null line terminator.\n");
				goto bad;
			}
			if (macnum == 0) {
				macros[macnum].mac_start = macbuf;
			}
			else {
				macros[macnum].mac_start = macros[macnum-1].mac_end + 1;
			}
			tmp = macros[macnum].mac_start;
			while (tmp != macbuf + 4096) {
				if ((c=getc(cfile)) == EOF) {
				printf("Macro definition missing null line terminator.\n");
					goto bad;
				}
				*tmp = c;
				if (*tmp == '\n') {
					if (*(tmp-1) == '\0') {
					   macros[macnum++].mac_end = tmp - 1;
					   break;
					}
					*tmp = '\0';
				}
				tmp++;
			}
			if (tmp == macbuf + 4096) {
				printf("4K macro buffer exceeded\n");
				goto bad;
			}
			break;
		default:
			warnx("Unknown .netrc keyword %s", tokval);
			break;
		}
		goto done;
	}
done:
	(void) fclose(cfile);
	return (0);
bad:
	(void) fclose(cfile);
	return (-1);
}

int
rexec_af(char **ahost, int rport, const char *name, const char *pass,
    const char *cmd, int *fd2p, sa_family_t *af)
{
	struct sockaddr_storage sa2, from;
	struct addrinfo hints, *res0;
	const char *orig_name = name;
	const char *orig_pass = pass;
	static char *ahostbuf;
	u_short port = 0;
	int s, timo = 1, s3;
	char c;
	int gai;
	char servbuff[NI_MAXSERV];

	snprintf(servbuff, sizeof(servbuff), "%d", ntohs(rport));
	servbuff[sizeof(servbuff) - 1] = '\0';

	memset(&hints, '\0', sizeof(hints));
	if (af)
		hints.ai_family = *af;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;
	gai = getaddrinfo(*ahost, servbuff, &hints, &res0);
	if (gai){
		/* XXX: set errno? */
		return -1;
	}

	if (res0->ai_canonname){
		free (ahostbuf);
		ahostbuf = strdup (res0->ai_canonname);
		if (ahostbuf == NULL) {
			perror ("rexec: strdup");
			return (-1);
		}
		*ahost = ahostbuf;
	} else {
		*ahost = NULL;
		__set_errno (ENOENT);
		return -1;
	}
	ruserpass(res0->ai_canonname, &name, &pass, 0);
retry:
	s = socket(res0->ai_family, res0->ai_socktype, 0);
	if (s < 0) {
		perror("rexec: socket");
		return (-1);
	}
	if (connect(s, res0->ai_addr, res0->ai_addrlen) < 0) {
		if (errno == ECONNREFUSED && timo <= 16) {
			(void) close(s);
			sleep(timo);
			timo *= 2;
			goto retry;
		}
		perror(res0->ai_canonname);
		return (-1);
	}
	if (fd2p == NULL) {
		(void) write(s, "", 1);
		port = 0;
	} else {
		char num[32];
		int s2;
		socklen_t sa2len;

		s2 = socket(res0->ai_family, res0->ai_socktype, 0);
		if (s2 < 0) {
			(void) close(s);
			return (-1);
		}
		listen(s2, 1);
		sa2len = sizeof (sa2);
		if (getsockname(s2, (struct sockaddr *)&sa2, &sa2len) < 0) {
			perror("getsockname");
			(void) close(s2);
			goto bad;
		} else if (sa2len != SA_LEN((struct sockaddr *)&sa2)) {
			__set_errno(EINVAL);
			(void) close(s2);
			goto bad;
		}
		port = 0;
		if (!getnameinfo((struct sockaddr *)&sa2, sa2len,
				 NULL, 0, servbuff, sizeof(servbuff),
				 NI_NUMERICSERV))
			port = atoi(servbuff);
		(void) sprintf(num, "%u", port);
		(void) write(s, num, strlen(num)+1);
		{ socklen_t len = sizeof (from);
		  s3 = accept(s2, (struct sockaddr *)&from,
						  &len);
		  close(s2);
		  if (s3 < 0) {
			perror("accept");
			port = 0;
			goto bad;
		  }
		}
		*fd2p = s3;
	}

	(void) write(s, name, strlen(name) + 1);
	/* should public key encypt the password here */
	(void) write(s, pass, strlen(pass) + 1);
	(void) write(s, cmd, strlen(cmd) + 1);

	/* We don't need the memory allocated for the name and the password
	   in ruserpass anymore.  */
	if (name != orig_name)
	  free ((char *) name);
	if (pass != orig_pass)
	  free ((char *) pass);

	if (read(s, &c, 1) != 1) {
		perror(*ahost);
		goto bad;
	}
	if (c != 0) {
		while (read(s, &c, 1) == 1) {
			(void) write(2, &c, 1);
			if (c == '\n')
				break;
		}
		goto bad;
	}
	freeaddrinfo(res0);
	return (s);
bad:
	if (port)
		(void) close(*fd2p);
	(void) close(s);
	freeaddrinfo(res0);
	return (-1);
}


int
rexec(char **ahost, int rport, const char *name, const char *pass, char *cmd, int *fd2p)
{
	struct sockaddr_in sin, sin2, from;
	struct hostent *hp;
	u_short port;
	int s, timo = 1, s3;
	char c;
	char *acct = NULL;

	hp = gethostbyname(*ahost);
	if (hp == NULL) {
		herror(*ahost);
		return (-1);
	}
	*ahost = hp->h_name;
	ruserpass(hp->h_name, &name, &pass, &acct);
	if (acct != NULL)
		free(acct);
retry:
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		perror("rexec: socket");
		return (-1);
	}
	sin.sin_family = hp->h_addrtype;
	sin.sin_port = rport;
	bcopy(hp->h_addr, (caddr_t)&sin.sin_addr, hp->h_length);
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		if (errno == ECONNREFUSED && timo <= 16) {
			(void) close(s);
			sleep(timo);
			timo *= 2;
			goto retry;
		}
		perror(hp->h_name);
		return (-1);
	}
	if (fd2p == NULL) {
		(void) write(s, "", 1);
		port = 0;
	} else {
		char num[8];
		int s2, sin2len;

		s2 = socket(AF_INET, SOCK_STREAM, 0);
		if (s2 < 0) {
			(void) close(s);
			return (-1);
		}
		listen(s2, 1);
		sin2len = sizeof (sin2);
		if (getsockname(s2, (struct sockaddr *)&sin2, &sin2len) < 0 ||
		  sin2len != sizeof (sin2)) {
			perror("getsockname");
			(void) close(s2);
			goto bad;
		}
		port = ntohs((u_short)sin2.sin_port);
		(void) sprintf(num, "%hu", port);
		(void) write(s, num, strlen(num)+1);
		{ int len = sizeof (from);
		  s3 = accept(s2, (struct sockaddr *)&from, &len);
		  close(s2);
		  if (s3 < 0) {
			perror("accept");
			port = 0;
			goto bad;
		  }
		}
		*fd2p = s3;
	}
	(void) write(s, name, strlen(name) + 1);
	/* should public key encypt the password here */
	(void) write(s, pass, strlen(pass) + 1);
	(void) write(s, cmd, strlen(cmd) + 1);
	if (read(s, &c, 1) != 1) {
		perror(*ahost);
		goto bad;
	}
	if (c != 0) {
		while (read(s, &c, 1) == 1) {
			(void) write(2, &c, 1);
			if (c == '\n')
				break;
		}
		goto bad;
	}
	return (s);
bad:
	if (port)
		(void) close(*fd2p);
	(void) close(s);
	return (-1);
}
