/*
 * Copyright (c) 1983, 1990, 1993
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
 * @(#) Copyright (c) 1983, 1990, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)rlogin.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/rlogin/rlogin.c,v 1.30 2002/04/28 11:16:43 markm Exp $
 */

/*
 * rlogin - remote login
 */
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <netdb.h>
#include <paths.h>
#include <pwd.h>
#include <setjmp.h>
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef KERBEROS
#include <openssl/des.h>
#include <krb.h>

#include "krb.h"

CREDENTIALS cred;
Key_schedule schedule;
int use_kerberos = 1, doencrypt;
char dst_realm_buf[REALM_SZ], *dest_realm = NULL;
#endif

#ifndef TIOCPKT_WINDOW
#define	TIOCPKT_WINDOW	0x80
#endif

/* concession to Sun */
#ifndef SIGUSR1
#define	SIGUSR1	30
#endif

int eight, litout, rem;
int family = PF_UNSPEC;

int noescape;
u_char escapechar = '~';

const char *speeds[] = {
	"0", "50", "75", "110", "134", "150", "200", "300", "600", "1200",
	"1800", "2400", "4800", "9600", "19200", "38400", "57600", "115200"
#define	MAX_SPEED_LENGTH	(sizeof("115200") - 1)
};

#define	get_window_size(fd, wp)	ioctl(fd, TIOCGWINSZ, wp)
struct	winsize winsize;

void		catch_child(int);
void		copytochild(int);
void		doit(long) __dead2;
void		done(int) __dead2;
void		echo(char);
u_int		getescape(char *);
void		lostpeer(int);
void		mode(int);
void		msg(const char *);
void		oob(int);
int		reader(int);
void		sendwindow(void);
void		setsignal(int);
void		sigwinch(int);
void		stop(char);
void		usage(void) __dead2;
void		writer(void);
void		writeroob(int);

int
main(int argc, char *argv[])
{
	struct passwd *pw;
	struct servent *sp;
	struct sgttyb ttyb;
	long omask;
	int argoff, ch, dflag, Dflag, one, uid;
	char *host, *localname, *p, *user, term[1024];
#ifdef KERBEROS
	char *k;
#endif
	struct sockaddr_storage ss;
	int sslen;

	argoff = dflag = Dflag = 0;
	one = 1;
	host = localname = user = NULL;

	if ((p = strrchr(argv[0], '/')))
		++p;
	else
		p = argv[0];

	if (strcmp(p, "rlogin"))
		host = p;

	/* handle "rlogin host flags" */
	if (!host && argc > 2 && argv[1][0] != '-') {
		host = argv[1];
		argoff = 1;
	}

#ifdef KERBEROS
#define	OPTIONS	"468DEKLde:i:k:l:x"
#else
#define	OPTIONS	"468DEKLde:i:l:"
#endif
	while ((ch = getopt(argc - argoff, argv + argoff, OPTIONS)) != -1)
		switch(ch) {
		case '4':
			family = PF_INET;
			break;

		case '6':
			family = PF_INET6;
			break;

		case '8':
			eight = 1;
			break;
		case 'D':
			Dflag = 1;
			break;
		case 'E':
			noescape = 1;
			break;
		case 'K':
#ifdef KERBEROS
			use_kerberos = 0;
#endif
			break;
		case 'L':
			litout = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'e':
			noescape = 0;
			escapechar = getescape(optarg);
			break;
		case 'i':
			if (getuid() != 0)
				errx(1, "-i user: permission denied");
			localname = optarg;
			break;
#ifdef KERBEROS
		case 'k':
			dest_realm = dst_realm_buf;
			(void)strncpy(dest_realm, optarg, REALM_SZ);
			break;
#endif
		case 'l':
			user = optarg;
			break;
#ifdef CRYPT
#ifdef KERBEROS
		case 'x':
			doencrypt = 1;
			break;
#endif
#endif
		case '?':
		default:
			usage();
		}
	optind += argoff;

	/* if haven't gotten a host yet, do so */
	if (!host && !(host = argv[optind++]))
		usage();

	if (argv[optind])
		usage();

	if (!(pw = getpwuid(uid = getuid())))
		errx(1, "unknown user id");
	if (!user)
		user = pw->pw_name;
	if (!localname)
		localname = pw->pw_name;

	sp = NULL;
#ifdef KERBEROS
	k = auth_getval("auth_list");
	if (k && !strstr(k, "kerberos"))
	    use_kerberos = 0;
	if (use_kerberos) {
		sp = getservbyname((doencrypt ? "eklogin" : "klogin"), "tcp");
		if (sp == NULL) {
			use_kerberos = 0;
			warn("can't get entry for %s/tcp service",
			    doencrypt ? "eklogin" : "klogin");
		}
	}
#endif
	if (sp == NULL)
		sp = getservbyname("login", "tcp");
	if (sp == NULL)
		errx(1, "login/tcp: unknown service");

#define	MAX_TERM_LENGTH	(sizeof(term) - 1 - MAX_SPEED_LENGTH - 1)

	(void)strncpy(term, (p = getenv("TERM")) ? p : "network",
		      MAX_TERM_LENGTH);
	term[MAX_TERM_LENGTH] = '\0';
	if (ioctl(0, TIOCGETP, &ttyb) == 0) {
		(void)strcat(term, "/");
		(void)strcat(term, speeds[(int)ttyb.sg_ospeed]);
	}

	(void)get_window_size(0, &winsize);

	(void)signal(SIGPIPE, lostpeer);
	/* will use SIGUSR1 for window size hack, so hold it off */
	omask = sigblock(sigmask(SIGURG) | sigmask(SIGUSR1));
	/*
	 * We set SIGURG and SIGUSR1 below so that an
	 * incoming signal will be held pending rather than being
	 * discarded. Note that these routines will be ready to get
	 * a signal by the time that they are unblocked below.
	 */
	(void)signal(SIGURG, copytochild);
	(void)signal(SIGUSR1, writeroob);

#ifdef KERBEROS
	if (use_kerberos) {
		setuid(getuid());
		rem = KSUCCESS;
		errno = 0;
		if (dest_realm == NULL)
			dest_realm = krb_realmofhost(host);

#ifdef CRYPT
		if (doencrypt) {
			rem = krcmd_mutual(&host, sp->s_port, user, term, 0,
			    dest_realm, &cred, schedule);
			des_set_key(&cred.session, schedule);
		} else
#endif /* CRYPT */
			rem = krcmd(&host, sp->s_port, user, term, 0,
			    dest_realm);
		if (rem < 0) {
			int i;
			char **newargv;

			sp = getservbyname("login", "tcp");
			if (sp == NULL)
				errx(1, "unknown service login/tcp");
			if (errno == ECONNREFUSED)
				warn("remote host doesn't support Kerberos");
			if (errno == ENOENT)
				warn("can't provide Kerberos auth data");
			newargv = malloc((argc + 2) * sizeof(*newargv));
			if (newargv == NULL)
				err(1, "malloc");
			newargv[0] = argv[0];
			newargv[1] = "-K";
			for(i = 1; i < argc; ++i)
				newargv[i + 1] = argv[i];
			newargv[argc + 1] = NULL;
			execv(_PATH_RLOGIN, newargv);
		}
	} else {
#ifdef CRYPT
		if (doencrypt)
			errx(1, "the -x flag requires Kerberos authentication");
#endif /* CRYPT */
		rem = rcmd_af(&host, sp->s_port, localname, user, term, 0,
			      family);
	}
#else
	rem = rcmd_af(&host, sp->s_port, localname, user, term, 0, family);
#endif /* KERBEROS */

	if (rem < 0)
		exit(1);

	if (dflag &&
	    setsockopt(rem, SOL_SOCKET, SO_DEBUG, &one, sizeof(one)) < 0)
		warn("setsockopt");
	if (Dflag &&
	    setsockopt(rem, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0)
		warn("setsockopt NODELAY (ignored)");

	sslen = sizeof(ss);
	one = IPTOS_LOWDELAY;
	if (getsockname(rem, (struct sockaddr *)&ss, &sslen) == 0 &&
	    ss.ss_family == AF_INET) {
		if (setsockopt(rem, IPPROTO_IP, IP_TOS, (char *)&one,
			       sizeof(int)) < 0)
			warn("setsockopt TOS (ignored)");
	} else
		if (ss.ss_family == AF_INET)
			warn("setsockopt getsockname failed");

	(void)setuid(uid);
	doit(omask);
	/*NOTREACHED*/
}

int child, defflags, deflflags, tabflag;
char deferase, defkill;
struct tchars deftc;
struct ltchars defltc;
struct tchars notc = { -1, -1, -1, -1, -1, -1 };
struct ltchars noltc = { -1, -1, -1, -1, -1, -1 };

void
doit(long omask)
{
	struct sgttyb sb;

	(void)ioctl(0, TIOCGETP, (char *)&sb);
	defflags = sb.sg_flags;
	tabflag = defflags & TBDELAY;
	defflags &= ECHO | CRMOD;
	deferase = sb.sg_erase;
	defkill = sb.sg_kill;
	(void)ioctl(0, TIOCLGET, &deflflags);
	(void)ioctl(0, TIOCGETC, &deftc);
	notc.t_startc = deftc.t_startc;
	notc.t_stopc = deftc.t_stopc;
	(void)ioctl(0, TIOCGLTC, &defltc);
	(void)signal(SIGINT, SIG_IGN);
	setsignal(SIGHUP);
	setsignal(SIGQUIT);
	child = fork();
	if (child == -1) {
		warn("fork");
		done(1);
	}
	if (child == 0) {
		mode(1);
		if (reader(omask) == 0) {
			msg("connection closed.");
			exit(0);
		}
		sleep(1);
		msg("\007connection closed.");
		exit(1);
	}

	/*
	 * We may still own the socket, and may have a pending SIGURG (or might
	 * receive one soon) that we really want to send to the reader.  When
	 * one of these comes in, the trap copytochild simply copies such
	 * signals to the child. We can now unblock SIGURG and SIGUSR1
	 * that were set above.
	 */
	(void)sigsetmask(omask);
	(void)signal(SIGCHLD, catch_child);
	writer();
	msg("closed connection.");
	done(0);
}

/* trap a signal, unless it is being ignored. */
void
setsignal(int sig)
{
	int omask = sigblock(sigmask(sig));

	if (signal(sig, exit) == SIG_IGN)
		(void)signal(sig, SIG_IGN);
	(void)sigsetmask(omask);
}

void
done(int status)
{
	int w, wstatus;

	mode(0);
	if (child > 0) {
		/* make sure catch_child does not snap it up */
		(void)signal(SIGCHLD, SIG_DFL);
		if (kill(child, SIGKILL) >= 0)
			while ((w = wait(&wstatus)) > 0 && w != child);
	}
	exit(status);
}

int dosigwinch;

/*
 * This is called when the reader process gets the out-of-band (urgent)
 * request to turn on the window-changing protocol.
 */
void
writeroob(int signo __unused)
{
	if (dosigwinch == 0) {
		sendwindow();
		(void)signal(SIGWINCH, sigwinch);
	}
	dosigwinch = 1;
}

void
catch_child(int signo __unused)
{
	pid_t pid;
	int status;

	for (;;) {
		pid = wait3(&status, WNOHANG|WUNTRACED, NULL);
		if (pid == 0)
			return;
		/* if the child (reader) dies, just quit */
		if (pid < 0 || (pid == child && !WIFSTOPPED(status)))
			done(WTERMSIG(status) | WEXITSTATUS(status));
	}
	/* NOTREACHED */
}

/*
 * writer: write to remote: 0 -> line.
 * ~.				terminate
 * ~^Z				suspend rlogin process.
 * ~<delayed-suspend char>	suspend rlogin process, but leave reader alone.
 */
void
writer(void)
{
	int bol, local, n;
	char c;

	bol = 1;			/* beginning of line */
	local = 0;
	for (;;) {
		n = read(STDIN_FILENO, &c, 1);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		/*
		 * If we're at the beginning of the line and recognize a
		 * command character, then we echo locally.  Otherwise,
		 * characters are echo'd remotely.  If the command character
		 * is doubled, this acts as a force and local echo is
		 * suppressed.
		 */
		if (bol) {
			bol = 0;
			if (!noescape && c == escapechar) {
				local = 1;
				continue;
			}
		} else if (local) {
			local = 0;
			if (c == '.' || c == deftc.t_eofc) {
				echo(c);
				break;
			}
			if (c == defltc.t_suspc || c == defltc.t_dsuspc) {
				bol = 1;
				echo(c);
				stop(c);
				continue;
			}
			if (c != escapechar)
#ifdef CRYPT
#ifdef KERBEROS
				if (doencrypt)
					(void)des_enc_write(rem,
					    (char *)&escapechar, 1,
						schedule, &cred.session);
				else
#endif
#endif
					(void)write(rem, &escapechar, 1);
		}

#ifdef CRYPT
#ifdef KERBEROS
		if (doencrypt) {
			if (des_enc_write(rem, &c, 1, schedule, &cred.session) == 0) {
				msg("line gone");
				break;
			}
		} else
#endif
#endif
			if (write(rem, &c, 1) == 0) {
				msg("line gone");
				break;
			}
		bol = c == defkill || c == deftc.t_eofc ||
		    c == deftc.t_intrc || c == defltc.t_suspc ||
		    c == '\r' || c == '\n';
	}
}

void
echo(char c)
{
	char *p;
	char buf[8];

	p = buf;
	c &= 0177;
	*p++ = escapechar;
	if (c < ' ') {
		*p++ = '^';
		*p++ = c + '@';
	} else if (c == 0177) {
		*p++ = '^';
		*p++ = '?';
	} else
		*p++ = c;
	*p++ = '\r';
	*p++ = '\n';
	(void)write(STDOUT_FILENO, buf, p - buf);
}

void
stop(char cmdc)
{
	mode(0);
	(void)signal(SIGCHLD, SIG_IGN);
	(void)kill(cmdc == defltc.t_suspc ? 0 : getpid(), SIGTSTP);
	(void)signal(SIGCHLD, catch_child);
	mode(1);
	sigwinch(0);			/* check for size changes */
}

void
sigwinch(int signo __unused)
{
	struct winsize ws;

	if (dosigwinch && get_window_size(0, &ws) == 0 &&
	    bcmp(&ws, &winsize, sizeof(ws))) {
		winsize = ws;
		sendwindow();
	}
}

/*
 * Send the window size to the server via the magic escape
 */
void
sendwindow(void)
{
	struct winsize *wp;
	char obuf[4 + sizeof (struct winsize)];

	wp = (struct winsize *)(obuf+4);
	obuf[0] = 0377;
	obuf[1] = 0377;
	obuf[2] = 's';
	obuf[3] = 's';
	wp->ws_row = htons(winsize.ws_row);
	wp->ws_col = htons(winsize.ws_col);
	wp->ws_xpixel = htons(winsize.ws_xpixel);
	wp->ws_ypixel = htons(winsize.ws_ypixel);

#ifdef CRYPT
#ifdef KERBEROS
	if(doencrypt)
		(void)des_enc_write(rem, obuf, sizeof(obuf),
			schedule, &cred.session);
	else
#endif
#endif
		(void)write(rem, obuf, sizeof(obuf));
}

/*
 * reader: read from remote: line -> 1
 */
#define	READING	1
#define	WRITING	2

jmp_buf rcvtop;
int ppid, rcvcnt, rcvstate;
char rcvbuf[8 * 1024];

void
oob(int signo __unused)
{
	struct sgttyb sb;
	int atmark, n, out, rcvd;
	char waste[BUFSIZ], mark;

	out = O_RDWR;
	rcvd = 0;
	while (recv(rem, &mark, 1, MSG_OOB) < 0) {
		switch (errno) {
		case EWOULDBLOCK:
			/*
			 * Urgent data not here yet.  It may not be possible
			 * to send it yet if we are blocked for output and
			 * our input buffer is full.
			 */
			if (rcvcnt < (int)sizeof(rcvbuf)) {
				n = read(rem, rcvbuf + rcvcnt,
				    sizeof(rcvbuf) - rcvcnt);
				if (n <= 0)
					return;
				rcvd += n;
			} else {
				n = read(rem, waste, sizeof(waste));
				if (n <= 0)
					return;
			}
			continue;
		default:
			return;
		}
	}
	if (mark & TIOCPKT_WINDOW) {
		/* Let server know about window size changes */
		(void)kill(ppid, SIGUSR1);
	}
	if (!eight && (mark & TIOCPKT_NOSTOP)) {
		(void)ioctl(0, TIOCGETP, (char *)&sb);
		sb.sg_flags &= ~CBREAK;
		sb.sg_flags |= RAW;
		(void)ioctl(0, TIOCSETN, (char *)&sb);
		notc.t_stopc = -1;
		notc.t_startc = -1;
		(void)ioctl(0, TIOCSETC, (char *)&notc);
	}
	if (!eight && (mark & TIOCPKT_DOSTOP)) {
		(void)ioctl(0, TIOCGETP, (char *)&sb);
		sb.sg_flags &= ~RAW;
		sb.sg_flags |= CBREAK;
		(void)ioctl(0, TIOCSETN, (char *)&sb);
		notc.t_stopc = deftc.t_stopc;
		notc.t_startc = deftc.t_startc;
		(void)ioctl(0, TIOCSETC, (char *)&notc);
	}
	if (mark & TIOCPKT_FLUSHWRITE) {
		(void)ioctl(1, TIOCFLUSH, (char *)&out);
		for (;;) {
			if (ioctl(rem, SIOCATMARK, &atmark) < 0) {
				warn("ioctl");
				break;
			}
			if (atmark)
				break;
			n = read(rem, waste, sizeof (waste));
			if (n <= 0)
				break;
		}
		/*
		 * Don't want any pending data to be output, so clear the recv
		 * buffer.  If we were hanging on a write when interrupted,
		 * don't want it to restart.  If we were reading, restart
		 * anyway.
		 */
		rcvcnt = 0;
		longjmp(rcvtop, 1);
	}

	/* oob does not do FLUSHREAD (alas!) */

	/*
	 * If we filled the receive buffer while a read was pending, longjmp
	 * to the top to restart appropriately.  Don't abort a pending write,
	 * however, or we won't know how much was written.
	 */
	if (rcvd && rcvstate == READING)
		longjmp(rcvtop, 1);
}

/* reader: read from remote: line -> 1 */
int
reader(int omask)
{
	int pid, n, remaining;
	char *bufp;

#if BSD >= 43 || defined(SUNOS4)
	pid = getpid();		/* modern systems use positives for pid */
#else
	pid = -getpid();	/* old broken systems use negatives */
#endif
	(void)signal(SIGTTOU, SIG_IGN);
	(void)signal(SIGURG, oob);
	(void)signal(SIGUSR1, oob); /* When propogating SIGURG from parent */
	ppid = getppid();
	(void)fcntl(rem, F_SETOWN, pid);
	(void)setjmp(rcvtop);
	(void)sigsetmask(omask);
	bufp = rcvbuf;
	for (;;) {
		while ((remaining = rcvcnt - (bufp - rcvbuf)) > 0) {
			rcvstate = WRITING;
			n = write(STDOUT_FILENO, bufp, remaining);
			if (n < 0) {
				if (errno != EINTR)
					return (-1);
				continue;
			}
			bufp += n;
		}
		bufp = rcvbuf;
		rcvcnt = 0;
		rcvstate = READING;

#ifdef CRYPT
#ifdef KERBEROS
		if (doencrypt)
			rcvcnt = des_enc_read(rem, rcvbuf, sizeof(rcvbuf),
				schedule, &cred.session);
		else
#endif
#endif
			rcvcnt = read(rem, rcvbuf, sizeof (rcvbuf));
		if (rcvcnt == 0)
			return (0);
		if (rcvcnt < 0) {
			if (errno == EINTR)
				continue;
			warn("read");
			return (-1);
		}
	}
}

void
mode(int f)
{
	struct ltchars *ltc;
	struct sgttyb sb;
	struct tchars *tc;
	int lflags;

	(void)ioctl(0, TIOCGETP, (char *)&sb);
	(void)ioctl(0, TIOCLGET, (char *)&lflags);
	switch(f) {
	case 0:
		sb.sg_flags &= ~(CBREAK|RAW|TBDELAY);
		sb.sg_flags |= defflags|tabflag;
		tc = &deftc;
		ltc = &defltc;
		sb.sg_kill = defkill;
		sb.sg_erase = deferase;
		lflags = deflflags;
		break;
	case 1:
		sb.sg_flags |= (eight ? RAW : CBREAK);
		sb.sg_flags &= ~defflags;
		/* preserve tab delays, but turn off XTABS */
		if ((sb.sg_flags & TBDELAY) == XTABS)
			sb.sg_flags &= ~TBDELAY;
		tc = &notc;
		ltc = &noltc;
		sb.sg_kill = sb.sg_erase = -1;
		if (litout)
			lflags |= LLITOUT;
		break;
	default:
		return;
	}
	(void)ioctl(0, TIOCSLTC, (char *)ltc);
	(void)ioctl(0, TIOCSETC, (char *)tc);
	(void)ioctl(0, TIOCSETN, (char *)&sb);
	(void)ioctl(0, TIOCLSET, (char *)&lflags);
}

void
lostpeer(int signo __unused)
{
	(void)signal(SIGPIPE, SIG_IGN);
	msg("\007connection closed.");
	done(1);
}

/* copy SIGURGs to the child process via SIGUSR1. */
void
copytochild(int signo __unused)
{
	(void)kill(child, SIGUSR1);
}

void
msg(const char *str)
{
	(void)fprintf(stderr, "rlogin: %s\r\n", str);
}

void
usage(void)
{
	(void)fprintf(stderr,
	"usage: rlogin [-46%s]%s[-e char] [-i localname] [-l username] host\n",
#ifdef KERBEROS
#ifdef CRYPT
	    "8DEKLdx", " [-k realm] ");
#else
	    "8DEKLd", " [-k realm] ");
#endif
#else
	    "8DEKLd", " ");
#endif
	exit(1);
}

u_int
getescape(char *p)
{
	long val;
	int len;

	if ((len = strlen(p)) == 1)	/* use any single char, including '\' */
		return ((u_int)*p);
					/* otherwise, \nnn */
	if (*p == '\\' && len >= 2 && len <= 4) {
		val = strtol(++p, NULL, 8);
		for (;;) {
			if (!*++p)
				return ((u_int)val);
			if (*p < '0' || *p > '8')
				break;
		}
	}
	msg("illegal option value -- e");
	usage();
	/* NOTREACHED */
}
