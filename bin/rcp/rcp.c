/*
 * Copyright (c) 1983, 1990, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
 * @(#) Copyright (c) 1983, 1990, 1992, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)rcp.c	8.2 (Berkeley) 4/2/94
 * $FreeBSD: src/bin/rcp/rcp.c,v 1.26.2.6 2004/09/16 12:16:10 delphij Exp $
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <netdb.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#ifdef KERBEROS
#include <openssl/des.h>
#include <krb.h>
#include "bsd_locl.h"

static char	dst_realm_buf[REALM_SZ];
char	*dest_realm = NULL;
int	use_kerberos = 1;
CREDENTIALS	cred;
Key_schedule	schedule;
extern	char	*krb_realmofhost();
#ifdef CRYPT
int	doencrypt = 0;
#define	OPTIONS	"46dfKk:prtx"
#else
#define	OPTIONS	"46dfKk:prt"
#endif
#else
#define	OPTIONS "46dfprt"
#endif

static struct passwd *pwd;
static u_short	port;
static uid_t	userid;
static int errs, rem;
static int pflag, targetshouldbedirectory, iamrecursive;
int iamremote;
static int family = PF_UNSPEC;

static int argc_copy;
static char **argv_copy;

#define	CMDNEEDS	64
static char cmd[CMDNEEDS];		/* must hold "rcp -r -p -d\0" */

#ifdef KERBEROS
int	 kerberos(char **, char *, char *, char *);
void	 oldw(const char *, ...) __printflike(1, 2);
#endif
int	 response(void);
void	 rsource(char *, struct stat *);
void	 sink(int, char *[]);
void	 source(int, char *[]);
void	 tolocal(int, char *[]);
void	 toremote(char *, int, char *[]);
void	 usage(void);

int
main(int argc, char *argv[])
{
	struct servent *sp;
	int ch, fflag, i, tflag;
	char *targ, *shell;
#ifdef KERBEROS
	char *k;
#endif

	/*
	 * Prepare for execing ourselves.
	 */
	argc_copy = argc + 1;
	argv_copy = malloc((argc_copy + 1) * sizeof(*argv_copy));
	if (argv_copy == NULL)
		err(1, "malloc");
	argv_copy[0] = argv[0];
	argv_copy[1] = __DECONST(char *, "-K");
	for (i = 1; i < argc; ++i) {
		argv_copy[i + 1] = strdup(argv[i]);
		if (argv_copy[i + 1] == NULL)
			errx(1, "strdup: out of memory");
	}
	argv_copy[argc + 1] = NULL;

	fflag = tflag = 0;
	while ((ch = getopt(argc, argv, OPTIONS)) != -1)
		switch(ch) {			/* User-visible flags. */
		case '4':
			family = PF_INET;
			break;

		case '6':
			family = PF_INET6;
			break;

		case 'K':
#ifdef KERBEROS
			use_kerberos = 0;
#endif
			break;
#ifdef	KERBEROS
		case 'k':
			dest_realm = dst_realm_buf;
			strncpy(dst_realm_buf, optarg, REALM_SZ - 1);
			dst_realm_buf[REALM_SZ - 1] = '\0';
			break;
#ifdef CRYPT
		case 'x':
			doencrypt = 1;
			/* des_set_key(cred.session, schedule); */
			break;
#endif
#endif
		case 'p':
			pflag = 1;
			break;
		case 'r':
			iamrecursive = 1;
			break;
						/* Server options. */
		case 'd':
			targetshouldbedirectory = 1;
			break;
		case 'f':			/* "from" */
			iamremote = 1;
			fflag = 1;
			break;
		case 't':			/* "to" */
			iamremote = 1;
			tflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

#ifdef KERBEROS
	k = auth_getval("auth_list");
	if (k && !strstr(k, "kerberos"))
	    use_kerberos = 0;
	if (use_kerberos) {
#ifdef CRYPT
		shell = doencrypt ? __DECONST(char *, "ekshell") :
		                    __DECONST(char *, "kshell");
#else
		shell = __DECONST(char *, "kshell");
#endif
		if ((sp = getservbyname(shell, "tcp")) == NULL) {
			use_kerberos = 0;
			oldw("can't get entry for %s/tcp service", shell);
			sp = getservbyname(shell = "shell", "tcp");
		}
	} else
		shell = __DECONST(char *, "shell");
		sp = getservbyname(shell, "tcp");
#else
	shell = __DECONST(char *, "shell");
	sp = getservbyname(shell, "tcp");
#endif
	if (sp == NULL)
		errx(1, "%s/tcp: unknown service", shell);
	port = sp->s_port;

	if ((pwd = getpwuid(userid = getuid())) == NULL)
		errx(1, "unknown user %d", (int)userid);

	rem = STDIN_FILENO;		/* XXX */

	if (fflag) {			/* Follow "protocol", send data. */
		response();
		setuid(userid);
		source(argc, argv);
		exit(errs);
	}

	if (tflag) {			/* Receive data. */
		setuid(userid);
		sink(argc, argv);
		exit(errs);
	}

	if (argc < 2)
		usage();
	if (argc > 2)
		targetshouldbedirectory = 1;

	rem = -1;
	/* Command to be executed on remote system using "rsh". */
#ifdef	KERBEROS
	snprintf(cmd, sizeof(cmd),
	    "rcp%s%s%s%s", iamrecursive ? " -r" : "",
#ifdef CRYPT
	    (doencrypt && use_kerberos ? " -x" : ""),
#else
	    "",
#endif
	    pflag ? " -p" : "", targetshouldbedirectory ? " -d" : "");
#else
	snprintf(cmd, sizeof(cmd), "rcp%s%s%s",
	    iamrecursive ? " -r" : "", pflag ? " -p" : "",
	    targetshouldbedirectory ? " -d" : "");
#endif

	signal(SIGPIPE, lostconn);

	if ((targ = colon(argv[argc - 1])))	/* Dest is remote host. */
		toremote(targ, argc, argv);
	else {
		tolocal(argc, argv);		/* Dest is local host. */
		if (targetshouldbedirectory)
			verifydir(argv[argc - 1]);
	}
	exit(errs);
}

void
toremote(char *targ, int argc, char *argv[])
{
	int i, len, tos;
	char *bp, *host, *src, *suser, *thost, *tuser;

	*targ++ = 0;
	if (*targ == 0)
		targ = __DECONST(char *, ".");

	if ((thost = strchr(argv[argc - 1], '@'))) {
		/* user@host */
		*thost++ = 0;
		tuser = argv[argc - 1];
		if (*tuser == '\0')
			tuser = NULL;
		else if (!okname(tuser))
			exit(1);
	} else {
		thost = argv[argc - 1];
		tuser = NULL;
	}

	for (i = 0; i < argc - 1; i++) {
		src = colon(argv[i]);
		if (src) {			/* remote to remote */
			*src++ = 0;
			if (*src == 0)
				src = __DECONST(char *, ".");
			host = strchr(argv[i], '@');
			len = strlen(_PATH_RSH) + strlen(argv[i]) +
			    strlen(src) + (tuser ? strlen(tuser) : 0) +
			    strlen(thost) + strlen(targ) + CMDNEEDS + 20;
			if (!(bp = malloc(len)))
				err(1, "malloc");
			if (host) {
				*host++ = 0;
				suser = argv[i];
				if (*suser == '\0')
					suser = pwd->pw_name;
				else if (!okname(suser)) {
					++errs;
					continue;
				}
				snprintf(bp, len,
				    "%s %s -l %s -n %s %s '%s%s%s:%s'",
				    _PATH_RSH, host, suser, cmd, src,
				    tuser ? tuser : "", tuser ? "@" : "",
				    thost, targ);
			} else
				snprintf(bp, len,
				    "exec %s %s -n %s %s '%s%s%s:%s'",
				    _PATH_RSH, argv[i], cmd, src,
				    tuser ? tuser : "", tuser ? "@" : "",
				    thost, targ);
			susystem(bp, userid);
			free(bp);
		} else {			/* local to remote */
			if (rem == -1) {
				len = strlen(targ) + CMDNEEDS + 20;
				if (!(bp = malloc(len)))
					err(1, "malloc");
				snprintf(bp, len, "%s -t %s", cmd, targ);
				host = thost;
#ifdef KERBEROS
				if (use_kerberos)
					rem = kerberos(&host, bp,
					    pwd->pw_name,
					    tuser ? tuser : pwd->pw_name);
				else
#endif
					rem = rcmd_af(&host, port,
					    pwd->pw_name,
					    tuser ? tuser : pwd->pw_name,
					    bp, 0, family);
				if (rem < 0)
					exit(1);
				if (family == PF_INET) {
					tos = IPTOS_THROUGHPUT;
					if (setsockopt(rem, IPPROTO_IP, IP_TOS,
					    &tos, sizeof(int)) < 0)
						warn("TOS (ignored)");
				}
				if (response() < 0)
					exit(1);
				free(bp);
				setuid(userid);
			}
			source(1, argv+i);
		}
	}
}

void
tolocal(int argc, char *argv[])
{
	int i, len, tos;
	char *bp, *host, *src, *suser;

	for (i = 0; i < argc - 1; i++) {
		if (!(src = colon(argv[i]))) {		/* Local to local. */
			len = strlen(_PATH_CP) + strlen(argv[i]) +
			    strlen(argv[argc - 1]) + 20;
			if (!(bp = malloc(len)))
				err(1, "malloc");
			snprintf(bp, len, "exec %s%s%s %s %s", _PATH_CP,
			    iamrecursive ? " -PR" : "", pflag ? " -p" : "",
			    argv[i], argv[argc - 1]);
			if (susystem(bp, userid))
				++errs;
			free(bp);
			continue;
		}
		*src++ = 0;
		if (*src == 0)
			src = __DECONST(char *, ".");
		if ((host = strchr(argv[i], '@')) == NULL) {
			host = argv[i];
			suser = pwd->pw_name;
		} else {
			*host++ = 0;
			suser = argv[i];
			if (*suser == '\0')
				suser = pwd->pw_name;
			else if (!okname(suser)) {
				++errs;
				continue;
			}
		}
		len = strlen(src) + CMDNEEDS + 20;
		if ((bp = malloc(len)) == NULL)
			err(1, "malloc");
		snprintf(bp, len, "%s -f %s", cmd, src);
		rem =
#ifdef KERBEROS
		    use_kerberos ?
			kerberos(&host, bp, pwd->pw_name, suser) :
#endif
			rcmd_af(&host, port, pwd->pw_name, suser, bp, 0,
			    family);
		free(bp);
		if (rem < 0) {
			++errs;
			continue;
		}
		seteuid(userid);
		if (family == PF_INET) {
			tos = IPTOS_THROUGHPUT;
			if (setsockopt(rem, IPPROTO_IP, IP_TOS, &tos,
			    sizeof(int)) < 0)
				warn("TOS (ignored)");
		}
		sink(1, argv + argc - 1);
		seteuid(0);
		close(rem);
		rem = -1;
	}
}

void
source(int argc, char *argv[])
{
	struct stat stb;
	static BUF buffer;
	BUF *bp;
	off_t i;
	int amt, fd, haderr, indx, result;
	char *last, *name, buf[BUFSIZ];

	for (indx = 0; indx < argc; ++indx) {
		name = argv[indx];
		if ((fd = open(name, O_RDONLY, 0)) < 0)
			goto syserr;
		if (fstat(fd, &stb)) {
syserr:			run_err("%s: %s", name, strerror(errno));
			goto next;
		}
		switch (stb.st_mode & S_IFMT) {
		case S_IFREG:
			break;
		case S_IFDIR:
			if (iamrecursive) {
				rsource(name, &stb);
				goto next;
			}
			/* FALLTHROUGH */
		default:
			run_err("%s: not a regular file", name);
			goto next;
		}
		if ((last = strrchr(name, '/')) == NULL)
			last = name;
		else
			++last;
		if (pflag) {
			/*
			 * Make it compatible with possible future
			 * versions expecting microseconds.
			 */
			snprintf(buf, sizeof(buf), "T%ld 0 %ld 0\n",
			    (long)stb.st_mtimespec.tv_sec,
			    (long)stb.st_atimespec.tv_sec);
			write(rem, buf, strlen(buf));
			if (response() < 0)
				goto next;
		}
#define	MODEMASK	(S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO)
		snprintf(buf, sizeof(buf), "C%04o %jd %s\n",
		    stb.st_mode & MODEMASK, (intmax_t)stb.st_size, last);
		write(rem, buf, strlen(buf));
		if (response() < 0)
			goto next;
		if ((bp = allocbuf(&buffer, fd, BUFSIZ)) == NULL) {
next:			(void)close(fd);
			continue;
		}

		/* Keep writing after an error so that we stay sync'd up. */
		for (haderr = i = 0; i < stb.st_size; i += bp->cnt) {
			amt = bp->cnt;
			if (i + amt > stb.st_size)
				amt = stb.st_size - i;
			if (!haderr) {
				result = read(fd, bp->buf, amt);
				if (result != amt)
					haderr = result >= 0 ? EIO : errno;
			}
			if (haderr)
				write(rem, bp->buf, amt);
			else {
				result = write(rem, bp->buf, amt);
				if (result != amt)
					haderr = result >= 0 ? EIO : errno;
			}
		}
		if (close(fd) && !haderr)
			haderr = errno;
		if (!haderr)
			write(rem, "", 1);
		else
			run_err("%s: %s", name, strerror(haderr));
		response();
	}
}

void
rsource(char *name, struct stat *statp)
{
	DIR *dirp;
	struct dirent *dp;
	char *last, *vect[1], path[PATH_MAX];

	if (!(dirp = opendir(name))) {
		run_err("%s: %s", name, strerror(errno));
		return;
	}
	last = strrchr(name, '/');
	if (last == NULL)
		last = name;
	else
		last++;
	if (pflag) {
		snprintf(path, sizeof(path), "T%ld 0 %ld 0\n",
		    (long)statp->st_mtimespec.tv_sec,
		    (long)statp->st_atimespec.tv_sec);
		write(rem, path, strlen(path));
		if (response() < 0) {
			closedir(dirp);
			return;
		}
	}
	snprintf(path, sizeof(path),
	    "D%04o %d %s\n", statp->st_mode & MODEMASK, 0, last);
	write(rem, path, strlen(path));
	if (response() < 0) {
		closedir(dirp);
		return;
	}
	while ((dp = readdir(dirp))) {
		if (dp->d_ino == 0)
			continue;
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;
		if (strlen(name) + 1 + strlen(dp->d_name) >= sizeof(path)) {
			run_err("%s/%s: name too long", name, dp->d_name);
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", name, dp->d_name);
		vect[0] = path;
		source(1, vect);
	}
	closedir(dirp);
	write(rem, "E\n", 2);
	response();
}

void
sink(int argc, char *argv[])
{
	static BUF buffer;
	struct stat stb;
	struct timeval tv[2];
	enum { YES, NO, DISPLAYED } wrerr;
	BUF *bp;
	off_t i, j, size;
	size_t count;
	int amt, exists, first, mask, mode, ofd, omode;
	int setimes, targisdir, wrerrno = 0;
	char ch, *cp, *np, *targ, *why, *vect[1], buf[BUFSIZ], path[PATH_MAX];

#define	atime	tv[0]
#define	mtime	tv[1]
#define	SCREWUP(str)	{ why = __DECONST(char *, str); goto screwup; }

	setimes = targisdir = 0;
	mask = umask(0);
	if (!pflag)
		umask(mask);
	if (argc != 1) {
		run_err("ambiguous target");
		exit(1);
	}
	targ = *argv;
	if (targetshouldbedirectory)
		verifydir(targ);
	write(rem, "", 1);
	if (stat(targ, &stb) == 0 && S_ISDIR(stb.st_mode))
		targisdir = 1;
	for (first = 1;; first = 0) {
		cp = buf;
		if (read(rem, cp, 1) <= 0)
			return;
		if (*cp++ == '\n')
			SCREWUP("unexpected <newline>");
		do {
			if (read(rem, &ch, sizeof(ch)) != sizeof(ch))
				SCREWUP("lost connection");
			*cp++ = ch;
		} while (cp < &buf[BUFSIZ - 1] && ch != '\n');
		*cp = 0;

		if (buf[0] == '\01' || buf[0] == '\02') {
			if (iamremote == 0)
				write(STDERR_FILENO,
				    buf + 1, strlen(buf + 1));
			if (buf[0] == '\02')
				exit(1);
			++errs;
			continue;
		}
		if (buf[0] == 'E') {
			write(rem, "", 1);
			return;
		}

		if (ch == '\n')
			*--cp = 0;

		cp = buf;
		if (*cp == 'T') {
			setimes++;
			cp++;
			mtime.tv_sec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != ' ')
				SCREWUP("mtime.sec not delimited");
			mtime.tv_usec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != ' ')
				SCREWUP("mtime.usec not delimited");
			atime.tv_sec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != ' ')
				SCREWUP("atime.sec not delimited");
			atime.tv_usec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != '\0')
				SCREWUP("atime.usec not delimited");
			write(rem, "", 1);
			continue;
		}
		if (*cp != 'C' && *cp != 'D') {
			/*
			 * Check for the case "rcp remote:foo\* local:bar".
			 * In this case, the line "No match." can be returned
			 * by the shell before the rcp command on the remote is
			 * executed so the ^Aerror_message convention isn't
			 * followed.
			 */
			if (first) {
				run_err("%s", cp);
				exit(1);
			}
			SCREWUP("expected control record");
		}
		mode = 0;
		for (++cp; cp < buf + 5; cp++) {
			if (*cp < '0' || *cp > '7')
				SCREWUP("bad mode");
			mode = (mode << 3) | (*cp - '0');
		}
		if (*cp++ != ' ')
			SCREWUP("mode not delimited");

		for (size = 0; isdigit(*cp);)
			size = size * 10 + (*cp++ - '0');
		if (*cp++ != ' ')
			SCREWUP("size not delimited");
		if (targisdir) {
			if (strlen(targ) + (*targ ? 1 : 0) + strlen(cp)
				    >= sizeof(path)) {
				run_err("%s%s%s: name too long", targ,
					*targ ? "/" : "", cp);
				exit(1);
			}
			snprintf(path, sizeof(path), "%s%s%s", targ,
				*targ ? "/" : "", cp);
			np = path;
		} else
			np = targ;
		exists = stat(np, &stb) == 0;
		if (buf[0] == 'D') {
			int mod_flag = pflag;
			if (exists) {
				if (!S_ISDIR(stb.st_mode)) {
					errno = ENOTDIR;
					goto bad;
				}
				if (pflag)
					chmod(np, mode);
			} else {
				/* Handle copying from a read-only directory */
				mod_flag = 1;
				if (mkdir(np, mode | S_IRWXU) < 0)
					goto bad;
			}
			vect[0] = np;
			sink(1, vect);
			if (setimes) {
				setimes = 0;
				if (utimes(np, tv) < 0)
				    run_err("%s: set times: %s",
					np, strerror(errno));
			}
			if (mod_flag)
				chmod(np, mode);
			continue;
		}
		omode = mode;
		mode |= S_IWRITE;
		if ((ofd = open(np, O_WRONLY|O_CREAT, mode)) < 0) {
bad:			run_err("%s: %s", np, strerror(errno));
			continue;
		}
		write(rem, "", 1);
		if ((bp = allocbuf(&buffer, ofd, BUFSIZ)) == NULL) {
			close(ofd);
			continue;
		}
		cp = bp->buf;
		wrerr = NO;
		for (count = i = 0; i < size; i += BUFSIZ) {
			amt = BUFSIZ;
			if (i + amt > size)
				amt = size - i;
			count += amt;
			do {
				j = read(rem, cp, amt);
				if (j <= 0) {
					run_err("%s", j ? strerror(errno) :
					    "dropped connection");
					exit(1);
				}
				amt -= j;
				cp += j;
			} while (amt > 0);
			if (count == bp->cnt) {
				/* Keep reading so we stay sync'd up. */
				if (wrerr == NO) {
					j = write(ofd, bp->buf, count);
					if (j != (off_t)count) {
						wrerr = YES;
						wrerrno = j >= 0 ? EIO : errno;
					}
				}
				count = 0;
				cp = bp->buf;
			}
		}
		if (count != 0 && wrerr == NO &&
		    (j = write(ofd, bp->buf, count)) != (off_t)count) {
			wrerr = YES;
			wrerrno = j >= 0 ? EIO : errno;
		}
		if (ftruncate(ofd, size)) {
			run_err("%s: truncate: %s", np, strerror(errno));
			wrerr = DISPLAYED;
		}
		if (pflag) {
			if (exists || omode != mode)
				if (fchmod(ofd, omode))
					run_err("%s: set mode: %s",
					    np, strerror(errno));
		} else {
			if (!exists && omode != mode)
				if (fchmod(ofd, omode & ~mask))
					run_err("%s: set mode: %s",
					    np, strerror(errno));
		}
		close(ofd);
		response();
		if (setimes && wrerr == NO) {
			setimes = 0;
			if (utimes(np, tv) < 0) {
				run_err("%s: set times: %s",
				    np, strerror(errno));
				wrerr = DISPLAYED;
			}
		}
		switch(wrerr) {
		case YES:
			run_err("%s: %s", np, strerror(wrerrno));
			break;
		case NO:
			write(rem, "", 1);
			break;
		case DISPLAYED:
			break;
		}
	}
screwup:
	run_err("protocol error: %s", why);
	exit(1);
}

#ifdef KERBEROS
int
kerberos(char **host, char *bp, char *locuser, char *user)
{
	if (use_kerberos) {
		setuid(getuid());
		rem = KSUCCESS;
		errno = 0;
		if (dest_realm == NULL)
			dest_realm = krb_realmofhost(*host);
		rem =
#ifdef CRYPT
		    doencrypt ?
			krcmd_mutual(host,
			    port, user, bp, 0, dest_realm, &cred, schedule) :
#endif
			krcmd(host, port, user, bp, 0, dest_realm);

		if (rem < 0) {
			if (errno == ECONNREFUSED)
			    oldw("remote host doesn't support Kerberos");
			else if (errno == ENOENT)
			    oldw("can't provide Kerberos authentication data");
			execv(_PATH_RCP, argv_copy);
			err(1, "execv: %s", _PATH_RCP);
		}
	} else {
#ifdef CRYPT
		if (doencrypt)
			errx(1,
			   "the -x option requires Kerberos authentication");
#endif
		rem = rcmd_af(host, port, locuser, user, bp, 0, family);
	}
	return (rem);
}
#endif /* KERBEROS */

int
response(void)
{
	char ch, *cp, resp, rbuf[BUFSIZ];

	if (read(rem, &resp, sizeof(resp)) != sizeof(resp))
		lostconn(0);

	cp = rbuf;
	switch(resp) {
	case 0:				/* ok */
		return (0);
	default:
		*cp++ = resp;
		/* FALLTHROUGH */
	case 1:				/* error, followed by error msg */
	case 2:				/* fatal error, "" */
		do {
			if (read(rem, &ch, sizeof(ch)) != sizeof(ch))
				lostconn(0);
			*cp++ = ch;
		} while (cp < &rbuf[BUFSIZ] && ch != '\n');

		if (!iamremote)
			write(STDERR_FILENO, rbuf, cp - rbuf);
		++errs;
		if (resp == 1)
			return (-1);
		exit(1);
	}
	/* NOTREACHED */
}

void
usage(void)
{
#ifdef KERBEROS
#ifdef CRYPT
	fprintf(stderr, "%s\n%s\n",
	    "usage: rcp [-46Kpx] [-k realm] f1 f2",
	    "       rcp [-46Kprx] [-k realm] f1 ... fn directory");
#else
	fprintf(stderr, "%s\n%s\n",
	    "usage: rcp [-46Kp] [-k realm] f1 f2",
	    "       rcp [-46Kpr] [-k realm] f1 ... fn directory");
#endif
#else
	fprintf(stderr, "%s\n%s\n",
	    "usage: rcp [-46p] f1 f2",
	    "       rcp [-46pr] f1 ... fn directory");
#endif
	exit(1);
}

#include <stdarg.h>

#ifdef KERBEROS
void
oldw(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "rcp: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ", using standard rcp\n");
	va_end(ap);
}
#endif

void
run_err(const char *fmt, ...)
{
	static FILE *fp;
	va_list ap;
	va_start(ap, fmt);

	++errs;
	if (fp == NULL && !(fp = fdopen(rem, "w")))
		return;
	fprintf(fp, "%c", 0x01);
	fprintf(fp, "rcp: ");
	vfprintf(fp, fmt, ap);
	fprintf(fp, "\n");
	fflush(fp);

	if (!iamremote)
		vwarnx(fmt, ap);

	va_end(ap);
}
