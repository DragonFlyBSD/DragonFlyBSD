/*
 * Copyright (c) 1980, 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Bob Toxen.
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
 * @(#) Copyright (c) 1980, 1987, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)lock.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/lock/lock.c,v 1.8.2.1 2002/09/15 22:32:56 dd Exp $
 */

/*
 * Lock a terminal up until the given key is entered or the given
 * interval times out.
 *
 * Timeout interval is by default TIMEOUT, it can be changed with
 * an argument of the form -time where time is in minutes
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/consio.h>

#include <err.h>
#include <pwd.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

#define	TIMEOUT	15

static void	quit(int);
static void	bye(int);
static void	hi(int);
static void	usage(void);

static struct timeval	timeout;
static struct timeval	zerotime;
static struct termios	tty, ntty;
static long		nexttime;			/* keep the timeout time */
static int     		no_timeout;                     /* lock terminal forever */
static int		vtyunlock;			/* Unlock flag and code. */

/*ARGSUSED*/
int
main(int argc, char **argv)
{
	struct passwd *pw;
	time_t timval;
	struct itimerval ntimer, otimer;
	struct tm *timp;
	int ch, failures, sectimeout, usemine, vtylock;
	long tmp;
	char *ap, *ep, *mypw, *cryptpw, *ttynam, *tzn;
	char hostname[MAXHOSTNAMELEN + 1], s[BUFSIZ], s1[BUFSIZ];

	openlog("lock", 0, LOG_AUTH);

	sectimeout = TIMEOUT;
	pw = NULL;
	mypw = NULL;
	usemine = 0;
	no_timeout = 0;
	vtylock = 0;
	while ((ch = getopt(argc, argv, "npt:v")) != -1)
		switch(ch) {
		case 't':
			tmp = strtol(optarg, &ep, 10);
			if (*ep != '\0' || tmp > INT_MAX || tmp < INT_MIN)	
				errx(1, "illegal timeout value");
			sectimeout = (int)tmp;
			break;
		case 'p':
			usemine = 1;
			if (!(pw = getpwuid(getuid())))
				errx(1, "unknown uid %d", getuid());
			mypw = strdup(pw->pw_passwd);
			break;
		case 'n':
			no_timeout = 1;
			break;
		case 'v':
			vtylock = 1;
			break;
		case '?':
		default:
			usage();
		}
	timeout.tv_sec = sectimeout * 60;

	if (setuid(getuid()) != 0)			/* discard privs */
		errx(1, "setuid failed");

	if (tcgetattr(STDIN_FILENO, &tty))		/* get information for header */
		err(1, "tcgetattr failed");

	gethostname(hostname, sizeof(hostname));
	if (!(ttynam = ttyname(STDIN_FILENO)))
		errx(1, "not a terminal?");
	timval = time(NULL);
	nexttime = timval + (sectimeout * 60);
	timp = localtime(&timval);
	ap = asctime(timp);
	tzn = timp->tm_zone;

	signal(SIGINT, quit);
	signal(SIGQUIT, quit);
	ntty = tty; ntty.c_lflag &= ~ECHO;
	tcsetattr(STDIN_FILENO, TCSADRAIN|TCSASOFT, &ntty);

	if (!mypw) {
		/* get key and check again */
		printf("Key: ");
		if (!fgets(s, sizeof(s), stdin) || *s == '\n')
			quit(0);
		printf("\nAgain: ");
		/*
		 * Don't need EOF test here, if we get EOF, then s1 != s
		 * and the right things will happen.
		 */
		fgets(s1, sizeof(s1), stdin);
		putchar('\n');
		if (strcmp(s1, s)) {
			printf("\07lock: passwords didn't match.\n");
			tcsetattr(STDIN_FILENO, TCSADRAIN|TCSASOFT, &tty);
			exit(1);
		}
		s[0] = '\0';
		mypw = s1;
	}

	/* set signal handlers */
	signal(SIGINT, hi);
	signal(SIGQUIT, hi);
	signal(SIGTSTP, hi);
	signal(SIGALRM, bye);

	ntimer.it_interval = zerotime;
	ntimer.it_value = timeout;
	if (!no_timeout)
		setitimer(ITIMER_REAL, &ntimer, &otimer);
	if (vtylock) {
		/*
		 * If this failed, we want to err out; warn isn't good
		 * enough, since we don't want the user to think that
		 * everything is nice and locked because they got a
		 * "Key:" prompt.
		 */
		if (ioctl(STDIN_FILENO, VT_LOCKSWITCH, &vtylock) == -1) {
			tcsetattr(0, TCSADRAIN|TCSASOFT, &tty);
			err(1, "locking vty");
		}
		vtyunlock = 0x2;
	}

	/* header info */
	if (pw != NULL)
		printf("lock: %s using %s on %s.", pw->pw_name,
		    ttynam, hostname);
	else
		printf("lock: %s on %s.", ttynam, hostname);
	if (no_timeout)
		printf(" no timeout.");
	else
		printf(" timeout in %d minute%s.", sectimeout,
		    sectimeout != 1 ? "s" : "");
	if (vtylock)
		printf(" vty locked.");
	printf("\ntime now is %.20s%s%s", ap, tzn, ap + 19);

	failures = 0;

	for (;;) {
		printf("Key: ");
		if (!fgets(s, sizeof(s), stdin)) {
			clearerr(stdin);
			hi(0);
			goto tryagain;
		}
		if (usemine) {
			s[strlen(s) - 1] = '\0';
			cryptpw = crypt(s, mypw);
			if (cryptpw == NULL || !strcmp(mypw, cryptpw))
				break;
		}
		else if (!strcmp(s, s1))
			break;
		printf("\07\n");
	    	failures++;
		if (getuid() == 0)
	    	    syslog(LOG_NOTICE, "%d ROOT UNLOCK FAILURE%s (%s on %s)",
			failures, failures > 1 ? "S": "", ttynam, hostname);
tryagain:
		if (tcgetattr(0, &ntty) && (errno != EINTR))
			exit(1);
		sleep(1);		/* to discourage guessing */
	}
	if (getuid() == 0)
		syslog(LOG_NOTICE, "ROOT UNLOCK ON hostname %s port %s",
		    hostname, ttynam);
	quit(0);
	return(0); /* not reached */
}


static void
usage(void)
{
	fprintf(stderr, "usage: lock [-npv] [-t timeout]\n");
	exit(1);
}

static void
hi(int signo __unused)
{
	time_t timval;

	if ((timval = time(NULL)) != (time_t)-1) {
		printf("lock: type in the unlock key. ");
		if (no_timeout) {
			putchar('\n');
		} else {
			printf("timeout in %jd:%jd minutes\n",
			    (intmax_t)(nexttime - timval) / 60,
			    (intmax_t)(nexttime - timval) % 60);
		}
	}
}

static void
quit(int signo __unused)
{
	putchar('\n');
	tcsetattr(STDIN_FILENO, TCSADRAIN|TCSASOFT, &tty);
	if (vtyunlock)
		ioctl(STDIN_FILENO, VT_LOCKSWITCH, &vtyunlock);
	exit(0);
}

static void
bye(int signo __unused)
{
	if (!no_timeout) {
		tcsetattr(STDIN_FILENO, TCSADRAIN|TCSASOFT, &tty);
		if (vtyunlock)
			ioctl(STDIN_FILENO, VT_LOCKSWITCH, &vtyunlock);
		printf("lock: timeout\n");
		exit(1);
	}
}
