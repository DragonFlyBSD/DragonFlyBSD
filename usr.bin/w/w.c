/*-
 * Copyright (c) 1980, 1991, 1993, 1994
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
 * @(#) Copyright (c) 1980, 1991, 1993, 1994 The Regents of the University of California.  All rights reserved.
 * @(#)w.c	8.4 (Berkeley) 4/16/94
 * $FreeBSD: src/usr.bin/w/w.c,v 1.38.2.6 2002/03/12 19:51:51 phantom Exp $
 */

/*
 * w - print system status (who and what)
 *
 * This program is similar to the systat command on Tenex/Tops 10/20
 *
 */
#include <sys/user.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/tty.h>

#include <machine/cpu.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <langinfo.h>
#include <locale.h>
#include <netdb.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef SUPPORT_UTMP
#include <utmp.h>
#endif
#ifdef SUPPORT_UTMPX
#include <utmpx.h>
#endif
#include <vis.h>

#include <arpa/nameser.h>
#include <resolv.h>

#include "extern.h"

struct timeval	boottime;
#ifdef SUPPORT_UTMP
struct utmp	utmp;
#endif
struct winsize	ws;
kvm_t	       *kd;
time_t		now;		/* the current time of day */
time_t		then;
time_t		uptime;		/* time of last reboot & elapsed time since */
int		ttywidth;	/* width of tty */
int		argwidth;	/* width of tty */
int		header = 1;	/* true if -h flag: don't print heading */
int		nflag;		/* true if -n flag: don't convert addrs */
int		dflag;		/* true if -d flag: output debug info */
int		sortidle;	/* sort by idle time */
int		use_ampm;	/* use AM/PM time */
int             use_comma;      /* use comma as floats separator */
char	      **sel_users;	/* login array of particular users selected */
char		domain[MAXHOSTNAMELEN];
int maxname = 8, maxline = 3, maxhost = 16;

/*
 * One of these per active utmp entry.
 */
struct	entry {
	struct	entry *next;
	char name[UTX_USERSIZE + 1];
	char line[UTX_LINESIZE + 1];
	char host[UTX_HOSTSIZE + 1];
	char type[2];
	struct timeval tv;
	dev_t	tdev;			/* dev_t of terminal */
	time_t	idle;			/* idle time of terminal in seconds */
	struct	kinfo_proc *kp;		/* `most interesting' proc */
	char	*args;			/* arg list of interesting process */
	struct	kinfo_proc *dkp;	/* debug option proc list */
	pid_t	pid;			/* pid or ~0 if not known */
} *ep, *ehead = NULL, **nextp = &ehead;

#define debugproc(p) *((struct kinfo_proc **)&(p)->kp_spare[0])

static void		 pr_header(time_t *, int);
#if defined(SUPPORT_UTMP) || defined(SUPPORT_UTMPX)
static struct stat	*ttystat(char *, int);
static void	process(struct entry *);
#endif
static void		 usage(int);
static int		 this_is_uptime(const char *s);

char *fmt_argv(char **, char *, int);	/* ../../bin/ps/fmt.c */

int
main(int argc, char **argv)
{
	struct kinfo_proc *kp;
	struct kinfo_proc *dkp;
	struct hostent *hp;
	in_addr_t l;
	int ch, i, nentries, nusers, wcmd, longidle, dropgid;
	char *memf, *nlistf, *p, *x;
#ifdef SUPPORT_UTMP
	struct utmp *ut;
#endif
#ifdef SUPPORT_UTMPX
	struct utmpx *utx;
#endif
	char buf[MAXHOSTNAMELEN], errbuf[_POSIX2_LINE_MAX];

	(void)setlocale(LC_ALL, "");
	use_ampm = (*nl_langinfo(T_FMT_AMPM) != '\0');
	use_comma = (*nl_langinfo(RADIXCHAR) != ',');

	/* Are we w(1) or uptime(1)? */
	if (this_is_uptime(argv[0]) == 0) {
		wcmd = 0;
		p = "";
	} else {
		wcmd = 1;
		p = "dhiflM:N:nsuw";
	}

	dropgid = 0;
	memf = nlistf = _PATH_DEVNULL;
	while ((ch = getopt(argc, argv, p)) != -1)
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'h':
			header = 0;
			break;
		case 'i':
			sortidle = 1;
			break;
		case 'M':
			header = 0;
			memf = optarg;
			dropgid = 1;
			break;
		case 'N':
			nlistf = optarg;
			dropgid = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'f': case 'l': case 's': case 'u': case 'w':
			warnx("[-flsuw] no longer supported");
			/* FALLTHROUGH */
		case '?':
		default:
			usage(wcmd);
		}
	argc -= optind;
	argv += optind;

	if (!(_res.options & RES_INIT))
		res_init();
	_res.retrans = 2;	/* resolver timeout to 2 seconds per try */
	_res.retry = 1;		/* only try once.. */

	/*
	 * Discard setgid privileges if not the running kernel so that bad
	 * guys can't print interesting stuff from kernel memory.
	 */
	if (dropgid)
		setgid(getgid());

	if ((kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, errbuf)) == NULL)
		errx(1, "%s", errbuf);

	(void)time(&now);
#ifdef SUPPORT_UTMPX
	setutxent();
#endif
#ifdef SUPPORT_UTMP
	setutent();
#endif

	if (*argv)
		sel_users = argv;

	nusers = 0;
#ifdef SUPPORT_UTMPX
	while ((utx = getutxent()) != NULL) {
		if (utx->ut_type != USER_PROCESS)
			continue;
		++nusers;

		if (sel_users) {
			int usermatch;
			char **user;

			usermatch = 0;
			for (user = sel_users; !usermatch && *user; user++)
				if (!strncmp(utx->ut_name, *user, UTX_USERSIZE))
					usermatch = 1;
			if (!usermatch)
				continue;
		}

		if ((ep = calloc(1, sizeof(struct entry))) == NULL)
			err(1, NULL);
		(void)memcpy(ep->name, utx->ut_name, sizeof(utx->ut_name));
		(void)memcpy(ep->line, utx->ut_line, sizeof(utx->ut_line));
		(void)memcpy(ep->host, utx->ut_host, sizeof(utx->ut_host));
		ep->name[sizeof(utx->ut_name)] = '\0';
		ep->line[sizeof(utx->ut_line)] = '\0';
		ep->host[sizeof(utx->ut_host)] = '\0';
#if 1
		/* XXX: Actually we don't support the utx->ut_ss stuff yet */
		if (!nflag || getnameinfo((struct sockaddr *)&utx->ut_ss,
		    utx->ut_ss.ss_len, ep->host, sizeof(ep->host), NULL, 0,
		    NI_NUMERICHOST) != 0) {
			(void)memcpy(ep->host, utx->ut_host,
			    sizeof(utx->ut_host));
			ep->host[sizeof(utx->ut_host)] = '\0';
		}
#endif
		ep->type[0] = 'x';
		ep->tv = utx->ut_tv;
		ep->pid = utx->ut_pid;

		*nextp = ep;
		nextp = &(ep->next);
		if (wcmd != 0)
			process(ep);
	}
#endif

#ifdef SUPPORT_UTMP
	while ((ut = getutent()) != NULL) {
		if (ut->ut_name[0] == '\0')
			continue;
		++nusers;
		if (sel_users) {
			int usermatch;
			char **user;

			usermatch = 0;
			for (user = sel_users; !usermatch && *user; user++)
				if (!strncmp(ut->ut_name, *user, UT_NAMESIZE))
					usermatch = 1;
			if (!usermatch)
				continue;
		}

		/* Don't process entries that we have utmpx for */
		for (ep = ehead; ep != NULL; ep = ep->next) {
			if (strncmp(ep->line, ut->ut_line,
			    sizeof(ut->ut_line)) == 0)
				break;
		}
		if (ep != NULL) {
			--nusers; /* Duplicate entry */
			continue;
		}

		if ((ep = calloc(1, sizeof(struct entry))) == NULL)
			err(1, NULL);
		(void)memcpy(ep->name, ut->ut_name, sizeof(ut->ut_name));
		(void)memcpy(ep->line, ut->ut_line, sizeof(ut->ut_line));
		(void)memcpy(ep->host, ut->ut_host, sizeof(ut->ut_host));
		ep->name[sizeof(ut->ut_name)] = '\0';
		ep->line[sizeof(ut->ut_line)] = '\0';
		ep->host[sizeof(ut->ut_host)] = '\0';
		ep->tv.tv_sec = ut->ut_time;

		*nextp = ep;
		nextp = &(ep->next);
		if (wcmd != 0)
			process(ep);
	}
#endif

#ifdef SUPPORT_UTMPX
	endutxent();
#endif
#ifdef SUPPORT_UTMP
	endutent();
#endif	
	
	if (header || wcmd == 0) {
		pr_header(&now, nusers);
		if (wcmd == 0) {
			(void)kvm_close(kd);
			exit(0);
		}

#define HEADER_USER		"USER"
#define HEADER_TTY		"TTY"
#define HEADER_FROM		"FROM"
#define HEADER_LOGIN_IDLE	"LOGIN@  IDLE "
#define HEADER_WHAT		"WHAT\n"
#define WUSED  (maxname + maxline + maxhost + \
		sizeof(HEADER_LOGIN_IDLE) + 3)	/* header width incl. spaces */ 
		(void)printf("%-*.*s %-*.*s %-*.*s  %s", 
				maxname, maxname, HEADER_USER,
				maxline, maxline, HEADER_TTY,
				maxhost, maxhost, HEADER_FROM,
				HEADER_LOGIN_IDLE HEADER_WHAT);
	}

	if ((kp = kvm_getprocs(kd, KERN_PROC_ALL, 0, &nentries)) == NULL)
		err(1, "%s", kvm_geterr(kd));
	for (i = 0; i < nentries; i++, kp++) {
		if (kp->kp_stat == SIDL || kp->kp_stat == SZOMB)
			continue;
		for (ep = ehead; ep != NULL; ep = ep->next) {
			if (ep->tdev == kp->kp_tdev) {
				/*
				 * proc is associated with this terminal
				 */
				if (ep->kp == NULL && kp->kp_pgid == kp->kp_tpgid) {
					/*
					 * Proc is 'most interesting'
					 */
					if (proc_compare(ep->kp, kp))
						ep->kp = kp;
				}
				/*
				 * Proc debug option info; add to debug
				 * list using kinfo_proc kp_eproc.e_spare
				 * as next pointer; ptr to ptr avoids the
				 * ptr = long assumption.
				 */
				dkp = ep->dkp;
				ep->dkp = kp;
				debugproc(kp) = dkp;
			}
			if (ep->pid != 0 && ep->pid == kp->kp_pid) {
				ep->kp = kp;
				break;
			}
		}
	}
	if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 &&
	     ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == -1 &&
	     ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) || ws.ws_col == 0)
	       ttywidth = 79;
        else
	       ttywidth = ws.ws_col - 1;
	argwidth = ttywidth - WUSED;
	if (argwidth < 4)
		argwidth = 8;
	for (ep = ehead; ep != NULL; ep = ep->next) {
		if (ep->kp == NULL) {
			ep->args = "-";
			continue;
		}
		ep->args = fmt_argv(kvm_getargv(kd, ep->kp, argwidth),
		    ep->kp->kp_comm, MAXCOMLEN);
		if (ep->args == NULL)
			err(1, NULL);
	}
	/* sort by idle time */
	if (sortidle && ehead != NULL) {
		struct entry *from, *save;

		from = ehead;
		ehead = NULL;
		while (from != NULL) {
			for (nextp = &ehead;
			    (*nextp) && from->idle >= (*nextp)->idle;
			    nextp = &(*nextp)->next)
				continue;
			save = from;
			from = from->next;
			save->next = *nextp;
			*nextp = save;
		}
	}
#if defined(SUPPORT_UTMP) && defined(SUPPORT_UTMPX)
	else if (ehead != NULL) {
		struct entry *from = ehead, *save;

		ehead = NULL;
		while (from != NULL) {
			for (nextp = &ehead;
			    (*nextp) && strcmp(from->line, (*nextp)->line) > 0;
			    nextp = &(*nextp)->next)
				continue;
			save = from;
			from = from->next;
			save->next = *nextp;
			*nextp = save;
		}
	}
#endif

	if (!nflag) {
		if (gethostname(domain, sizeof(domain)) < 0 ||
		    (p = strchr(domain, '.')) == NULL)
			domain[0] = '\0';
		else {
			domain[sizeof(domain) - 1] = '\0';
			memmove(domain, p, strlen(p) + 1);
		}
	}

	for (ep = ehead; ep != NULL; ep = ep->next) {
		char host_buf[UTX_HOSTSIZE + 1];

		host_buf[UTX_HOSTSIZE] = '\0';
		strncpy(host_buf, ep->host, maxhost);
		p = *host_buf ? host_buf : "-";
		if ((x = strchr(p, ':')) != NULL)
			*x++ = '\0';
		if (!nflag && isdigit(*p) &&
		    (l = inet_addr(p)) != INADDR_NONE &&
		    (hp = gethostbyaddr(&l, sizeof(l), AF_INET))) {
			if (domain[0] != '\0') {
				p = hp->h_name;
				p += strlen(hp->h_name);
				p -= strlen(domain);
				if (p > hp->h_name &&
				    strcasecmp(p, domain) == 0)
					*p = '\0';
			}
			p = hp->h_name;
		}
		if (nflag && *p && strcmp(p, "-") &&
		    inet_addr(p) == INADDR_NONE) {
			hp = gethostbyname(p);

			if (hp != NULL) {
				struct in_addr in;

				memmove(&in, hp->h_addr, sizeof(in));
				p = inet_ntoa(in);
			}
		}
		if (x) {
			(void)snprintf(buf, sizeof(buf), "%s:%s", p, x);
			p = buf;
		}
		if (dflag) {
			for (dkp = ep->dkp; dkp != NULL; dkp = debugproc(dkp)) {
				char *ptr;

				ptr = fmt_argv(kvm_getargv(kd, dkp, argwidth),
				    dkp->kp_comm, MAXCOMLEN);
				if (ptr == NULL)
					ptr = "-";
				(void)printf("\t\t%-9d %s\n",
				    dkp->kp_pid, ptr);
			}
		}
		(void)printf("%-*.*s %-*.*s %-*.*s ",
		    maxname, maxname, ep->name,
		    maxline, maxline,
		    strncmp(ep->line, "tty", 3) &&
		    strncmp(ep->line, "cua", 3) ?
		    ep->line : ep->line + 3,
		    maxhost, maxhost, *p ? p : "-");
		then = (time_t)ep->tv.tv_sec;
		pr_attime(&then, &now);
		longidle = pr_idle(ep->idle);
		(void)printf("%.*s\n", argwidth - longidle, ep->args);
	}
	(void)kvm_close(kd);
	exit(0);
}

static void
pr_header(time_t *nowp, int nusers)
{
	double avenrun[3];
	time_t uptime;
	int days, hrs, i, mins, secs;
	int mib[2];
	size_t size;
	char buf[256];

	/*
	 * Print time of day. (use "AM"/"PM" for all locales)
	 */
	(void)strftime_l(buf, sizeof(buf) - 1,
		       use_ampm	? "%l:%M%p" : "%k:%M",
		       localtime(nowp), NULL);
	buf[sizeof(buf) - 1] = '\0';
	(void)printf("%s ", buf);

	/*
	 * Print how long system has been up.
	 * (Found by looking at "boottime" from the kernel)
	 */
	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;
	size = sizeof(boottime);
	if (sysctl(mib, 2, &boottime, &size, NULL, 0) != -1 &&
	    boottime.tv_sec != 0) {
		uptime = now - boottime.tv_sec;
		if (uptime > 60)
			uptime += 30;
		days = uptime / 86400;
		uptime %= 86400;
		hrs = uptime / 3600;
		uptime %= 3600;
		mins = uptime / 60;
		secs = uptime % 60;
		(void)printf(" up");
		if (days > 0)
			(void)printf(" %d day%s,", days, days > 1 ? "s" : "");
		if (hrs > 0 && mins > 0)
			(void)printf(" %2d:%02d,", hrs, mins);
		else if (hrs > 0)
			(void)printf(" %d hr%s,", hrs, hrs > 1 ? "s" : "");
		else if (mins > 0)
			(void)printf(" %d min%s,", mins, mins > 1 ? "s" : "");
		else
			(void)printf(" %d sec%s,", secs, secs > 1 ? "s" : "");
	}

	/* Print number of users logged in on system */
	(void)printf(" %d user%s", nusers, nusers == 1 ? "" : "s");

	/*
	 * Print 1, 5, and 15 minute load averages.
	 */
	if (getloadavg(avenrun, sizeof(avenrun) / sizeof(avenrun[0])) == -1)
		(void)printf(", no load average information available\n");
	else {
		(void)printf(", load averages:");
		for (i = 0; i < (sizeof(avenrun) / sizeof(avenrun[0])); i++) {
			if (use_comma && i > 0)
				(void)printf(",");
			(void)printf(" %.2f", avenrun[i]);
		}
		(void)printf("\n");
	}
}

static struct stat *
ttystat(char *line, int sz)
{
	static struct stat sb;
	char ttybuf[MAXPATHLEN];

	(void)snprintf(ttybuf, sizeof(ttybuf), "%s%.*s", _PATH_DEV, sz, line);
	if (stat(ttybuf, &sb)) {
		warn("%s", ttybuf);
		return (NULL);
	}
	return (&sb);
}

static void
usage(int wcmd)
{
	if (wcmd)
		(void)fprintf(stderr,
		    "usage: w [-dhin] [-M core] [-N system] [user ...]\n");
	else
		(void)fprintf(stderr, "usage: uptime\n");
	exit(1);
}

static int 
this_is_uptime(const char *s)
{
	const char *u;

	if ((u = strrchr(s, '/')) != NULL)
		++u;
	else
		u = s;
	if (strcmp(u, "uptime") == 0)
		return (0);
	return (-1);
}

#if defined(SUPPORT_UTMP) || defined(SUPPORT_UTMPX)
static void
process(struct entry *ep)
{
	struct stat *stp;
	time_t touched;
	int max;

	if ((max = strlen(ep->name)) > maxname)
		maxname = max;
	if ((max = strlen(ep->line)) > maxline)
		maxline = max;
	if ((max = strlen(ep->host)) > maxhost)
		maxhost = max;

	ep->tdev = 0;
	ep->idle = (time_t)-1;

	if (!(stp = ttystat(ep->line, maxline)))
		return;	/* corrupted record */

	ep->tdev = stp->st_rdev;
#ifdef CPU_CONSDEV
	/*
	 * If this is the console device, attempt to ascertain
	 * the true console device dev_t.
	 */
	if (ep->tdev == 0) {
		int mib[2];
		size_t size;

		mib[0] = CTL_MACHDEP;
		mib[1] = CPU_CONSDEV;
		size = sizeof(dev_t);
		(void)sysctl(mib, 2, &ep->tdev, &size, NULL, 0);
	}
#endif

	touched = stp->st_atime;
	if (touched < ep->tv.tv_sec) {
		/* tty untouched since before login */
		touched = ep->tv.tv_sec;
	}
	if ((ep->idle = now - touched) < 0)
		ep->idle = 0;
}
#endif
