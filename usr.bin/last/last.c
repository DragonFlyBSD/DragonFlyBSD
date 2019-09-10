/*	@(#)last.c	8.2 (Berkeley) 4/2/94 */
/*	$NetBSD: last.c,v 1.15 2000/06/30 06:19:58 simonb Exp $	*/

/*
 * Copyright (c) 1987, 1993, 1994
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
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tzfile.h>
#include <unistd.h>
#include <utmpx.h>

#ifndef UT_NAMESIZE
#define UT_NAMESIZE 8
#define UT_LINESIZE 8
#define UT_HOSTSIZE 16
#endif
#ifndef SIGNATURE
#define SIGNATURE -1
#endif



#define	NO	0			/* false/no */
#define	YES	1			/* true/yes */

#define	TBUFLEN	30			/* length of time string buffer */
#define	TFMT	"%a %b %d %R"		/* strftime format string */
#define	LTFMT	"%a %b %d %Y %T"	/* strftime long format string */
#define	TFMTS	"%R"			/* strftime format string - time only */
#define	LTFMTS	"%T"			/* strftime long format string - " */

/* fmttime() flags */
#define	FULLTIME	0x1		/* show year, seconds */
#define	TIMEONLY	0x2		/* show time only, not date */
#define	GMT		0x4		/* show time at GMT, for offsets only */

#define MAXUTMP		1024;

typedef struct arg {
	char	*name;			/* argument */
#define	HOST_TYPE	-2
#define	TTY_TYPE	-3
#define	USER_TYPE	-4
	int	type;			/* type of arg */
	struct arg	*next;		/* linked list pointer */
} ARG;
static ARG	*arglist;		/* head of linked list */

typedef struct ttytab {
	time_t	logout;			/* log out time */
	char	tty[128];		/* terminal name */
	struct ttytab	*next;		/* linked list pointer */
} TTY;
static TTY	*ttylist;		/* head of linked list */

static struct utmpx *bufx;
static time_t	currentout;		/* current logout value */
static long	maxrec;			/* records to display */
static int	fulltime = 0;		/* Display seconds? */

static void	 addarg(int, char *);
static TTY	*addtty(const char *);
static void	 hostconv(char *);
static char	*ttyconv(char *);
static void	 wtmpx(const char *, int, int, int);
static char	*fmttime(time_t, int);
static void	 usage(void);
static void	 onintrx(int);
static int	 wantx(struct utmpx *, int);

static
void usage(void)
{
	fprintf(stderr, "Usage: %s [-#%s] [-T] [-f file]"
	    " [-h host] [-H hostsize] [-L linesize]\n"
	    "\t    [-N namesize] [-t tty] [user ...]\n", getprogname(),
#if 0 /* XXX NOTYET_SUPPORT_UTMPX??? */
	    "w"
#else
	    ""
#endif
	);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ch;
	char *p;
	const char *file = NULL;
	int namesize = UT_NAMESIZE;
	int linesize = UT_LINESIZE;
	int hostsize = UT_HOSTSIZE;

	maxrec = -1;

	while ((ch = getopt(argc, argv, "0123456789f:h:H:L:N:t:T")) != -1)
		switch (ch) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			/*
			 * kludge: last was originally designed to take
			 * a number after a dash.
			 */
			if (maxrec == -1) {
				p = argv[optind - 1];
				if (p[0] == '-' && p[1] == ch && !p[2])
					maxrec = atol(++p);
				else
					maxrec = atol(argv[optind] + 1);
				if (!maxrec)
					exit(0);
			}
			break;
		case 'f':
			file = optarg;
			break;
		case 'h':
			hostconv(optarg);
			addarg(HOST_TYPE, optarg);
			break;
		case 't':
			addarg(TTY_TYPE, ttyconv(optarg));
			break;
		case 'U':
			namesize = atoi(optarg);
			break;
		case 'L':
			linesize = atoi(optarg);
			break;
		case 'H':
			hostsize = atoi(optarg);
			break;
		case 'T':
			fulltime = 1;
			break;
		case '?':
		default:
			usage();
		}

	if (argc) {
		setlinebuf(stdout);
		for (argv += optind; *argv; ++argv) {
#define	COMPATIBILITY
#ifdef	COMPATIBILITY
			/* code to allow "last p5" to work */
			addarg(TTY_TYPE, ttyconv(*argv));
#endif
			addarg(USER_TYPE, *argv);
		}
	}
	if (file == NULL) {
		if (access(_PATH_WTMPX, R_OK) == 0)
			file = _PATH_WTMPX;
		if (file == NULL)
			errx(1, "Cannot access `%s'", _PATH_WTMPX);
	}
	wtmpx(file, namesize, linesize, hostsize);
	exit(0);
}


/*
 * addarg --
 *	add an entry to a linked list of arguments
 */
static void
addarg(int type, char *arg)
{
	ARG *cur;

	if (!(cur = (ARG *)malloc((u_int)sizeof(ARG))))
		err(1, "malloc failure");
	cur->next = arglist;
	cur->type = type;
	cur->name = arg;
	arglist = cur;
}

/*
 * addtty --
 *	add an entry to a linked list of ttys
 */
static TTY *
addtty(const char *tty)
{
	TTY *cur;

	if (!(cur = (TTY *)malloc((u_int)sizeof(TTY))))
		err(1, "malloc failure");
	cur->next = ttylist;
	cur->logout = currentout;
	memmove(cur->tty, tty, sizeof(cur->tty));
	return (ttylist = cur);
}

/*
 * hostconv --
 *	convert the hostname to search pattern; if the supplied host name
 *	has a domain attached that is the same as the current domain, rip
 *	off the domain suffix since that's what login(1) does.
 */
static void
hostconv(char *arg)
{
	static int first = 1;
	static char *hostdot, name[MAXHOSTNAMELEN + 1];
	char *argdot;

	if (!(argdot = strchr(arg, '.')))
		return;
	if (first) {
		first = 0;
		if (gethostname(name, sizeof(name)))
			err(1, "gethostname");
		name[sizeof(name) - 1] = '\0';
		hostdot = strchr(name, '.');
	}
	if (hostdot && !strcasecmp(hostdot, argdot))
		*argdot = '\0';
}

/*
 * ttyconv --
 *	convert tty to correct name.
 */
static char *
ttyconv(char *arg)
{
	char *mval;

	/*
	 * kludge -- we assume that all tty's end with
	 * a two character suffix.
	 */
	if (strlen(arg) == 2) {
		/* either 6 for "ttyxx" or 8 for "console" */
		if (!(mval = malloc((u_int)8)))
			err(1, "malloc failure");
		if (!strcmp(arg, "co"))
			strcpy(mval, "console");
		else {
			strcpy(mval, "tty");
			strcpy(mval + 3, arg);
		}
		return (mval);
	}
	if (!strncmp(arg, _PATH_DEV, sizeof(_PATH_DEV) - 1))
		return (arg + 5);
	return (arg);
}

/*
 * fmttime --
 *	return pointer to (static) formatted time string.
 */
static char *
fmttime(time_t t, int flags)
{
	struct tm *tm;
	static char tbuf[TBUFLEN];

	tm = (flags & GMT) ? gmtime(&t) : localtime(&t);
	strftime(tbuf, sizeof(tbuf),
	    (flags & TIMEONLY)
	     ? (flags & FULLTIME ? LTFMTS : TFMTS)
	     : (flags & FULLTIME ? LTFMT : TFMT),
	    tm);
	return (tbuf);
}

/*
 * wtmpx --
 *	read through the wtmpx file
 */
static void
wtmpx(const char *file, int namesz, int linesz, int hostsz)
{
	struct utmpx	*bp;		/* current structure */
	TTY	*T;			/* tty list entry */
	struct stat	stb;		/* stat of file for sz */
	time_t	delta;			/* time difference */
	off_t	bl;
	int	bytes, wfd;
	char	*ct;
	const char *crmsg;
	size_t  len = sizeof(*bufx) * MAXUTMP;

	if ((bufx = malloc(len)) == NULL)
		err(1, "Cannot allocate utmpx buffer");

	crmsg = NULL;

	if ((wfd = open(file, O_RDONLY, 0)) < 0 || fstat(wfd, &stb) == -1)
		err(1, "%s", file);
	bl = (stb.st_size + len - 1) / len;

	bufx[1].ut_xtime = time(NULL);
	(void)signal(SIGINT, onintrx);
	(void)signal(SIGQUIT, onintrx);

	while (--bl >= 0) {
		if (lseek(wfd, bl * len, SEEK_SET) == -1 ||
		    (bytes = read(wfd, bufx, len)) == -1)
			err(1, "%s", file);
		for (bp = &bufx[bytes / sizeof(*bufx) - 1]; bp >= bufx; --bp) {
			/*
			 * if the terminal line is '~', the machine stopped.
			 * see utmpx(5) for more info.
			 */
			if (bp->ut_line[0] == '~' && !bp->ut_line[1]) {
				/* everybody just logged out */
				for (T = ttylist; T; T = T->next)
					T->logout = -bp->ut_xtime;
				currentout = -bp->ut_xtime;
#ifdef __DragonFly__	/* XXX swildner: this should not be needed afaict */
				if (!strncmp(bp->ut_name, "shutdown", namesz))
					crmsg = "shutdown";
				else if (!strncmp(bp->ut_name, "reboot", namesz))
					crmsg = "reboot";
				else
					crmsg = "crash";
#else
				crmsg = strncmp(bp->ut_name, "shutdown",
				    namesz) ? "crash" : "shutdown";
#endif
				if (wantx(bp, NO)) {
					ct = fmttime(bp->ut_xtime, fulltime);
					printf("%-*.*s  %-*.*s %-*.*s %s\n",
					    namesz, namesz, bp->ut_name,
					    linesz, linesz, bp->ut_line,
					    hostsz, hostsz, bp->ut_host, ct);
					if (maxrec != -1 && !--maxrec)
						return;
				}
				continue;
			}
			/*
			 * if the line is '{' or '|', date got set; see
			 * utmpx(5) for more info.
			 */
			if ((bp->ut_line[0] == '{' || bp->ut_line[0] == '|')
			    && !bp->ut_line[1]) {
				if (wantx(bp, NO)) {
					ct = fmttime(bp->ut_xtime, fulltime);
				printf("%-*.*s  %-*.*s %-*.*s %s\n",
				    namesz, namesz,
				    bp->ut_name,
				    linesz, linesz,
				    bp->ut_line,
				    hostsz, hostsz,
				    bp->ut_host,
				    ct);
					if (maxrec && !--maxrec)
						return;
				}
				continue;
			}
			/* find associated tty */
			for (T = ttylist;; T = T->next) {
				if (!T) {
					/* add new one */
					T = addtty(bp->ut_line);
					break;
				}
				if (!strncmp(T->tty, bp->ut_line, UTX_LINESIZE))
					break;
			}
			if (bp->ut_type == SIGNATURE)
				continue;
			if (bp->ut_name[0] && wantx(bp, YES)) {
				ct = fmttime(bp->ut_xtime, fulltime);
				printf("%-*.*s  %-*.*s %-*.*s %s ",
				    namesz, namesz, bp->ut_name,
				    linesz, linesz, bp->ut_line,
				    hostsz, hostsz, bp->ut_host,
				    ct);
				if (!T->logout)
					puts("  still logged in");
				else {
					if (T->logout < 0) {
						T->logout = -T->logout;
						printf("- %s", crmsg);
					}
					else
						printf("- %s",
						    fmttime(T->logout,
						    fulltime | TIMEONLY));
					delta = T->logout - bp->ut_xtime;
					if (delta < SECSPERDAY)
						printf("  (%s)\n",
						    fmttime(delta,
						    fulltime | TIMEONLY | GMT));
					else
						printf(" (%ld+%s)\n",
						    delta / SECSPERDAY,
						    fmttime(delta,
						    fulltime | TIMEONLY | GMT));
				}
				if (maxrec != -1 && !--maxrec)
					return;
			}
			T->logout = bp->ut_xtime;
		}
	}
	fulltime = 1;	/* show full time */
	crmsg = fmttime(bufx[1].ut_xtime, FULLTIME);
	if ((ct = strrchr(file, '/')) != NULL)
		ct++;
	printf("\n%s begins %s\n", ct ? ct : file, crmsg);
}

/*
 * wantx --
 *	see if want this entry
 */
static int
wantx(struct utmpx *bp, int check)
{
	ARG *step;

	if (check) {
		/*
		 * when uucp and ftp log in over a network, the entry in
		 * the utmpx file is the name plus their process id.  See
		 * etc/ftpd.c and usr.bin/uucp/uucpd.c for more information.
		 */
		if (!strncmp(bp->ut_line, "ftp", sizeof("ftp") - 1))
			bp->ut_line[3] = '\0';
		else if (!strncmp(bp->ut_line, "uucp", sizeof("uucp") - 1))
			bp->ut_line[4] = '\0';
	}
	if (!arglist)
		return (YES);

	for (step = arglist; step; step = step->next)
		switch(step->type) {
		case HOST_TYPE:
			if (!strncasecmp(step->name, bp->ut_host, UTX_HOSTSIZE))
				return (YES);
			break;
		case TTY_TYPE:
			if (!strncmp(step->name, bp->ut_line, UTX_LINESIZE))
				return (YES);
			break;
		case USER_TYPE:
			if (!strncmp(step->name, bp->ut_name, UTX_USERSIZE))
				return (YES);
			break;
	}
	return (NO);
}

/*
 * onintrx --
 *	on interrupt, we inform the user how far we've gotten
 */
static void
onintrx(int signo)
{

	printf("\ninterrupted %s\n", fmttime(bufx[1].ut_xtime,
	    FULLTIME));
	if (signo == SIGINT)
		exit(1);
	(void)fflush(stdout);		/* fix required for rsh */
}
