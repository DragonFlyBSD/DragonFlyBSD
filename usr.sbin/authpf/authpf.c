/*	$OpenBSD: authpf.c,v 1.75 2004/01/29 01:55:10 deraadt Exp $	*/
/*	$DragonFly: src/usr.sbin/authpf/authpf.c,v 1.1 2004/09/21 21:25:28 joerg Exp $ */

/*
 * Copyright (C) 1998 - 2002 Bob Beck (beck@openbsd.org).
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
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/pf/pfvar.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "pfctl_parser.h"
#include "pfctl.h"

#include "pathnames.h"

#define __dead __dead2

extern int	symset(const char *, const char *, int);

static int	read_config(FILE *);
static void	print_message(const char *);
static int	allowed_luser(const char *);
static int	check_luser(const char *, const char *);
static int	remove_stale_rulesets(void);
static int	change_filter(int, const char *, const char *);
static void	authpf_kill_states(void);

int	dev_fd;			/* pf device */
char	anchorname[PF_ANCHOR_NAME_SIZE] = "authpf";
char	rulesetname[PF_RULESET_NAME_SIZE];

FILE	*pidfp;
char	*infile;		/* file name printed by yyerror() in parse.y */
char	 luser[MAXLOGNAME];	/* username */
char	 ipsrc[256];		/* ip as a string */
char	 pidfile[MAXPATHLEN];	/* we save pid in this file. */

struct timeval	Tstart, Tend;	/* start and end times of session */

volatile sig_atomic_t	want_death;
static void		need_death(int signo);
static __dead void	do_death(int);

/*
 * User shell for authenticating gateways. Sole purpose is to allow
 * a user to ssh to a gateway, and have the gateway modify packet
 * filters to allow access, then remove access when the user finishes
 * up. Meant to be used only from ssh(1) connections.
 */
int
main(int argc __unused, char **argv __unused)
{
	int		 lockcnt = 0, n, pidfd;
	FILE		*config;
	struct in_addr	 ina;
	struct passwd	*pw;
	char		*cp;
	uid_t		 uid;

	config = fopen(PATH_CONFFILE, "r");

	if ((cp = getenv("SSH_TTY")) == NULL) {
		syslog(LOG_ERR, "non-interactive session connection for authpf");
		exit(1);
	}

	if ((cp = getenv("SSH_CLIENT")) == NULL) {
		syslog(LOG_ERR, "cannot determine connection source");
		exit(1);
	}

	if (strlcpy(ipsrc, cp, sizeof(ipsrc)) >= sizeof(ipsrc)) {
		syslog(LOG_ERR, "SSH_CLIENT variable too long");
		exit(1);
	}
	cp = strchr(ipsrc, ' ');
	if (!cp) {
		syslog(LOG_ERR, "corrupt SSH_CLIENT variable %s", ipsrc);
		exit(1);
	}
	*cp = '\0';
	if (inet_pton(AF_INET, ipsrc, &ina) != 1) {
		syslog(LOG_ERR,
		    "cannot determine IP from SSH_CLIENT %s", ipsrc);
		exit(1);
	}
	/* open the pf device */
	dev_fd = open(PATH_DEVFILE, O_RDWR);
	if (dev_fd == -1) {
		syslog(LOG_ERR, "cannot open packet filter device (%m)");
		goto die;
	}

	uid = getuid();
	pw = getpwuid(uid);
	if (pw == NULL) {
		syslog(LOG_ERR, "cannot find user for uid %u", uid);
		goto die;
	}
	if (strcmp(pw->pw_shell, PATH_AUTHPF_SHELL)) {
		syslog(LOG_ERR, "wrong shell for user %s, uid %u",
		    pw->pw_name, pw->pw_uid);
		goto die;
	}

	/*
	 * Paranoia, but this data _does_ come from outside authpf, and
	 * truncation would be bad.
	 */
	if (strlcpy(luser, pw->pw_name, sizeof(luser)) >= sizeof(luser)) {
		syslog(LOG_ERR, "username too long: %s", pw->pw_name);
		goto die;
	}

	if ((n = snprintf(rulesetname, sizeof(rulesetname), "%s(%ld)",
	    luser, (long)getpid())) < 0 || n >= (int)sizeof(rulesetname)) {
		syslog(LOG_INFO, "%s(%ld) too large, ruleset name will be %ld",
		    luser, (long)getpid(), (long)getpid());
		if ((n = snprintf(rulesetname, sizeof(rulesetname), "%ld",
		    (long)getpid())) < 0 || n >= (int)sizeof(rulesetname)) {
			syslog(LOG_ERR, "pid too large for ruleset name");
			goto die;
		}
	}


	/* Make our entry in /var/authpf as /var/authpf/ipaddr */
	n = snprintf(pidfile, sizeof(pidfile), "%s/%s", PATH_PIDFILE, ipsrc);
	if (n < 0 || (u_int)n >= sizeof(pidfile)) {
		syslog(LOG_ERR, "path to pidfile too long");
		goto die;
	}

	/*
	 * If someone else is already using this ip, then this person
	 * wants to switch users - so kill the old process and exit
	 * as well.
	 *
	 * Note, we could print a message and tell them to log out, but the
	 * usual case of this is that someone has left themselves logged in,
	 * with the authenticated connection iconized and someone else walks
	 * up to use and automatically logs in before using. If this just
	 * gets rid of the old one silently, the new user never knows they
	 * could have used someone else's old authentication. If we
	 * tell them to log out before switching users it is an invitation
	 * for abuse.
	 */

	do {
		int	save_errno, otherpid = -1;
		char	otherluser[MAXLOGNAME];

		if ((pidfd = open(pidfile, O_RDWR|O_CREAT, 0644)) == -1 ||
		    (pidfp = fdopen(pidfd, "r+")) == NULL) {
			if (pidfd != -1)
				close(pidfd);
			syslog(LOG_ERR, "cannot open or create %s: %s", pidfile,
			    strerror(errno));
			goto die;
		}

		if (flock(fileno(pidfp), LOCK_EX|LOCK_NB) == 0)
			break;
		save_errno = errno;

		/* Mark our pid, and username to our file. */

		rewind(pidfp);
		/* 31 == MAXLOGNAME - 1 */
		if (fscanf(pidfp, "%d\n%31s\n", &otherpid, otherluser) != 2)
			otherpid = -1;
		syslog(LOG_DEBUG, "tried to lock %s, in use by pid %d: %s",
		    pidfile, otherpid, strerror(save_errno));

		if (otherpid > 0) {
			syslog(LOG_INFO,
			    "killing prior auth (pid %d) of %s by user %s",
			    otherpid, ipsrc, otherluser);
			if (kill((pid_t) otherpid, SIGTERM) == -1) {
				syslog(LOG_INFO,
				    "could not kill process %d: (%m)",
				    otherpid);
			}
		}

		/*
		 * we try to kill the previous process and acquire the lock
		 * for 10 seconds, trying once a second. if we can't after
		 * 10 attempts we log an error and give up
		 */
		if (++lockcnt > 10) {
			syslog(LOG_ERR, "cannot kill previous authpf (pid %d)",
			    otherpid);
			goto dogdeath;
		}
		sleep(1);

		/* re-open, and try again. The previous authpf process
		 * we killed above should unlink the file and release
		 * it's lock, giving us a chance to get it now
		 */
		fclose(pidfp);
	} while (1);

	/* revoke privs */
	seteuid(getuid());
	setuid(getuid());

	openlog("authpf", LOG_PID | LOG_NDELAY, LOG_DAEMON);

	if (!check_luser(PATH_BAN_DIR, luser) || !allowed_luser(luser)) {
		syslog(LOG_INFO, "user %s prohibited", luser);
		do_death(0);
	}

	if (config == NULL || read_config(config)) {
		syslog(LOG_INFO, "bad or nonexistent %s", PATH_CONFFILE);
		do_death(0);
	}

	if (remove_stale_rulesets()) {
		syslog(LOG_INFO, "error removing stale rulesets");
		do_death(0);
	}

	/* We appear to be making headway, so actually mark our pid */
	rewind(pidfp);
	fprintf(pidfp, "%ld\n%s\n", (long)getpid(), luser);
	fflush(pidfp);
	(void) ftruncate(fileno(pidfp), ftell(pidfp));

	if (change_filter(1, luser, ipsrc) == -1) {
		printf("Unable to modify filters\r\n");
		do_death(0);
	}

	signal(SIGTERM, need_death);
	signal(SIGINT, need_death);
	signal(SIGALRM, need_death);
	signal(SIGPIPE, need_death);
	signal(SIGHUP, need_death);
	signal(SIGSTOP, need_death);
	signal(SIGTSTP, need_death);
	while (1) {
		printf("\r\nHello %s, ", luser);
		printf("You are authenticated from host \"%s\"\r\n", ipsrc);
		setproctitle("%s@%s", luser, ipsrc);
		print_message(PATH_MESSAGE);
		while (1) {
			sleep(10);
			if (want_death)
				do_death(1);
		}
	}

	/* NOTREACHED */
dogdeath:
	printf("\r\n\r\nSorry, this service is currently unavailable due to ");
	printf("technical difficulties\r\n\r\n");
	print_message(PATH_PROBLEM);
	printf("\r\nYour authentication process (pid %ld) was unable to run\n",
	    (long)getpid());
	sleep(180); /* them lusers read reaaaaal slow */
die:
	do_death(0);
}

/*
 * reads config file in PATH_CONFFILE to set optional behaviours up
 */
static int
read_config(FILE *f)
{
	char	buf[1024];
	int	i = 0;

	do {
		char	**ap;
		char	 *pair[4], *cp, *tp;
		int	  len;

		if (fgets(buf, sizeof(buf), f) == NULL) {
			fclose(f);
			return (0);
		}
		i++;
		len = strlen(buf);
		if (buf[len - 1] != '\n' && !feof(f)) {
			syslog(LOG_ERR, "line %d too long in %s", i,
			    PATH_CONFFILE);
			return (1);
		}
		buf[len - 1] = '\0';

		for (cp = buf; *cp == ' ' || *cp == '\t'; cp++)
			; /* nothing */

		if (!*cp || *cp == '#' || *cp == '\n')
			continue;

		for (ap = pair; ap < &pair[3] &&
		    (*ap = strsep(&cp, "=")) != NULL; ) {
			if (**ap != '\0')
				ap++;
		}
		if (ap != &pair[2])
			goto parse_error;

		tp = pair[1] + strlen(pair[1]);
		while ((*tp == ' ' || *tp == '\t') && tp >= pair[1])
			*tp-- = '\0';

		if (strcasecmp(pair[0], "anchor") == 0) {
			if (!pair[1][0] || strlcpy(anchorname, pair[1],
			    sizeof(anchorname)) >= sizeof(anchorname))
				goto parse_error;
		}
	} while (!feof(f) && !ferror(f));
	fclose(f);
	return (0);

parse_error:
	fclose(f);
	syslog(LOG_ERR, "parse error, line %d of %s", i, PATH_CONFFILE);
	return (1);
}


/*
 * splatter a file to stdout - max line length of 1024,
 * used for spitting message files at users to tell them
 * they've been bad or we're unavailable.
 */
static void
print_message(const char *filename)
{
	char	 buf[1024];
	FILE	*f;

	if ((f = fopen(filename, "r")) == NULL)
		return; /* fail silently, we don't care if it isn't there */

	do {
		if (fgets(buf, sizeof(buf), f) == NULL) {
			fflush(stdout);
			fclose(f);
			return;
		}
	} while (fputs(buf, stdout) != EOF && !feof(f));
	fflush(stdout);
	fclose(f);
}

/*
 * allowed_luser checks to see if user "luser" is allowed to
 * use this gateway by virtue of being listed in an allowed
 * users file, namely /etc/authpf/authpf.allow .
 *
 * If /etc/authpf/authpf.allow does not exist, then we assume that
 * all users who are allowed in by sshd(8) are permitted to
 * use this gateway. If /etc/authpf/authpf.allow does exist, then a
 * user must be listed if the connection is to continue, else
 * the session terminates in the same manner as being banned.
 */
static int
allowed_luser(const char *user)
{
	char	*buf, *lbuf;
	int	 matched;
	size_t	 len;
	FILE	*f;

	if ((f = fopen(PATH_ALLOWFILE, "r")) == NULL) {
		if (errno == ENOENT) {
			/*
			 * allowfile doesn't exist, thus this gateway
			 * isn't restricted to certain users...
			 */
			return (1);
		}

		/*
		 * user may in fact be allowed, but we can't open
		 * the file even though it's there. probably a config
		 * problem.
		 */
		syslog(LOG_ERR, "cannot open allowed users file %s (%s)",
		    PATH_ALLOWFILE, strerror(errno));
		return (0);
	} else {
		/*
		 * /etc/authpf/authpf.allow exists, thus we do a linear
		 * search to see if they are allowed.
		 * also, if username "*" exists, then this is a
		 * "public" gateway, such as it is, so let
		 * everyone use it.
		 */
		lbuf = NULL;
		while ((buf = fgetln(f, &len))) {
			if (buf[len - 1] == '\n')
				buf[len - 1] = '\0';
			else {
				if ((lbuf = (char *)malloc(len + 1)) == NULL)
					err(1, NULL);
				memcpy(lbuf, buf, len);
				lbuf[len] = '\0';
				buf = lbuf;
			}

			matched = strcmp(user, buf) == 0 || strcmp("*", buf) == 0;

			if (lbuf != NULL) {
				free(lbuf);
				lbuf = NULL;
			}

			if (matched)
				return (1); /* matched an allowed username */
		}
		syslog(LOG_INFO, "denied access to %s: not listed in %s",
		    user, PATH_ALLOWFILE);

		fputs("\n\nSorry, you are not allowed to use this facility!\n",
		      stdout);
	}
	fflush(stdout);
	return (0);
}

/*
 * check_luser checks to see if user "luser" has been banned
 * from using us by virtue of having an file of the same name
 * in the "luserdir" directory.
 *
 * If the user has been banned, we copy the contents of the file
 * to the user's screen. (useful for telling the user what to
 * do to get un-banned, or just to tell them they aren't
 * going to be un-banned.)
 */
static int
check_luser(const char *userdir, const char *user)
{
	FILE	*f;
	int	 n;
	char	 tmp[MAXPATHLEN];

	n = snprintf(tmp, sizeof(tmp), "%s/%s", userdir, user);
	if (n < 0 || (u_int)n >= sizeof(tmp)) {
		syslog(LOG_ERR, "provided banned directory line too long (%s)",
		    userdir);
		return (0);
	}
	if ((f = fopen(tmp, "r")) == NULL) {
		if (errno == ENOENT) {
			/*
			 * file or dir doesn't exist, so therefore
			 * this luser isn't banned..  all is well
			 */
			return (1);
		} else {
			/*
			 * user may in fact be banned, but we can't open the
			 * file even though it's there. probably a config
			 * problem.
			 */
			syslog(LOG_ERR, "cannot open banned file %s (%s)",
			    tmp, strerror(errno));
			return (0);
		}
	} else {
		/*
		 * user is banned - spit the file at them to
		 * tell what they can do and where they can go.
		 */
		syslog(LOG_INFO, "denied access to %s: %s exists",
		    luser, tmp);

		/* reuse tmp */
		strlcpy(tmp, "\n\n-**- Sorry, you have been banned! -**-\n\n",
		    sizeof(tmp));
		while (fputs(tmp, stdout) != EOF && !feof(f)) {
			if (fgets(tmp, sizeof(tmp), f) == NULL) {
				fflush(stdout);
				return (0);
			}
		}
	}
	fflush(stdout);
	return (0);
}

/*
 * Search for rulesets left by other authpf processes (either because they
 * died ungracefully or were terminated) and remove them.
 */
static int
remove_stale_rulesets(void)
{
	struct pfioc_ruleset	 prs;
	const int		 action[PF_RULESET_MAX] = { PF_SCRUB,
				    PF_PASS, PF_NAT, PF_BINAT, PF_RDR };
	u_int32_t		 nr, mnr;

	memset(&prs, 0, sizeof(prs));
	strlcpy(prs.anchor, anchorname, sizeof(prs.anchor));
	if (ioctl(dev_fd, DIOCGETRULESETS, &prs)) {
		if (errno == EINVAL)
			return (0);
		else
			return (1);
	}

	mnr = prs.nr;
	nr = 0;
	while (nr < mnr) {
		char	*s, *t;
		pid_t	 pid;

		prs.nr = nr;
		if (ioctl(dev_fd, DIOCGETRULESET, &prs))
			return (1);
		errno = 0;
		if ((t = strchr(prs.name, '(')) == NULL)
			t = prs.name;
		else
			t++;
		pid = strtoul(t, &s, 10);
		if (!prs.name[0] || errno ||
		    (*s && (t == prs.name || *s != ')')))
			return (1);
		if (kill(pid, 0) && errno != EPERM) {
			int i;

			for (i = 0; i < PF_RULESET_MAX; ++i) {
				struct pfioc_rule pr;

				memset(&pr, 0, sizeof(pr));
				memcpy(pr.anchor, prs.anchor, sizeof(pr.anchor));
				memcpy(pr.ruleset, prs.name, sizeof(pr.ruleset));
				pr.rule.action = action[i];
				if ((ioctl(dev_fd, DIOCBEGINRULES, &pr) ||
				    ioctl(dev_fd, DIOCCOMMITRULES, &pr)) &&
				    errno != EINVAL)
					return (1);
			}
			mnr--;
		} else
			nr++;
	}
	return (0);
}

/*
 * Add/remove filter entries for user "luser" from ip "ipsrc"
 */
static int
change_filter(int add, const char *user, const char *his_ipsrc)
{
	char			 fn[MAXPATHLEN];
	FILE			*f = NULL;
	struct pfctl		 pf;
	struct pfr_buffer	 t;
	int i;

	if (user == NULL || !user[0] || his_ipsrc == NULL || !his_ipsrc[0]) {
		syslog(LOG_ERR, "invalid luser/ipsrc");
		goto error;
	}

	if (add) {
		if ((i = snprintf(fn, sizeof(fn), "%s/%s/authpf.rules",
		    PATH_USER_DIR, user)) < 0 || i >= (int)sizeof(fn)) {
			syslog(LOG_ERR, "user rule path too long");
			goto error;
		}
		if ((f = fopen(fn, "r")) == NULL && errno != ENOENT) {
			syslog(LOG_ERR, "cannot open %s (%m)", fn);
			goto error;
		}
		if (f == NULL) {
			if (strlcpy(fn, PATH_PFRULES, sizeof(fn)) >=
			    sizeof(fn)) {
				syslog(LOG_ERR, "rule path too long");
				goto error;
			}
			if ((f = fopen(fn, "r")) == NULL) {
				syslog(LOG_ERR, "cannot open %s (%m)", fn);
				goto error;
			}
		}
	}

	if (pfctl_load_fingerprints(dev_fd, 0)) {
		syslog(LOG_ERR, "unable to load kernel's OS fingerprints");
		goto error;
	}
	bzero(&t, sizeof(t));
	t.pfrb_type = PFRB_TRANS;
	memset(&pf, 0, sizeof(pf));
	for (i = 0; i < PF_RULESET_MAX; ++i) {
		if (pfctl_add_trans(&t, i, anchorname, rulesetname)) {
			syslog(LOG_ERR, "pfctl_add_trans %m");
			goto error;
		}
	}
	if (pfctl_trans(dev_fd, &t, DIOCXBEGIN, 0)) {
		syslog(LOG_ERR, "DIOCXBEGIN (%s) %m", add?"add":"remove");
		goto error;
	}

	if (add) {
		if (symset("user_ip", his_ipsrc, 0) ||
		    symset("user_id", user, 0)) {
			syslog(LOG_ERR, "symset");
			goto error;
		}

		pf.dev = dev_fd;
		pf.trans = &t;
		pf.anchor = anchorname;
		pf.ruleset = rulesetname;

		infile = fn;
		if (parse_rules(f, &pf) < 0) {
			syslog(LOG_ERR, "syntax error in rule file: "
			    "authpf rules not loaded");
			goto error;
		}

		infile = NULL;
		fclose(f);
		f = NULL;
	}

	if (pfctl_trans(dev_fd, &t, DIOCXCOMMIT, 0)) {
		syslog(LOG_ERR, "DIOCXCOMMIT (%s) %m", add?"add":"remove");
		goto error;
	}

	if (add) {
		gettimeofday(&Tstart, NULL);
		syslog(LOG_INFO, "allowing %s, user %s", his_ipsrc, user);
	} else {
		gettimeofday(&Tend, NULL);
		syslog(LOG_INFO, "removed %s, user %s - duration %ld seconds",
		    his_ipsrc, user, Tend.tv_sec - Tstart.tv_sec);
	}
	return (0);

error:
	if (f != NULL)
		fclose(f);
	if (pfctl_trans(dev_fd, &t, DIOCXROLLBACK, 0))
		syslog(LOG_ERR, "DIOCXROLLBACK (%s) %m", add?"add":"remove");

	infile = NULL;
	return (-1);
}

/*
 * This is to kill off states that would otherwise be left behind stateful
 * rules. This means we don't need to allow in more traffic than we really
 * want to, since we don't have to worry about any luser sessions lasting
 * longer than their ssh session. This function is based on
 * pfctl_kill_states from pfctl.
 */
static void
authpf_kill_states(void)
{
	struct pfioc_state_kill	psk;
	struct in_addr		target;

	memset(&psk, 0, sizeof(psk));
	psk.psk_af = AF_INET;

	inet_pton(AF_INET, ipsrc, &target);

	/* Kill all states from ipsrc */
	psk.psk_src.addr.v.a.addr.v4 = target;
	memset(&psk.psk_src.addr.v.a.mask, 0xff,
	    sizeof(psk.psk_src.addr.v.a.mask));
	if (ioctl(dev_fd, DIOCKILLSTATES, &psk))
		syslog(LOG_ERR, "DIOCKILLSTATES failed (%m)");

	/* Kill all states to ipsrc */
	psk.psk_af = AF_INET;
	memset(&psk.psk_src, 0, sizeof(psk.psk_src));
	psk.psk_dst.addr.v.a.addr.v4 = target;
	memset(&psk.psk_dst.addr.v.a.mask, 0xff,
	    sizeof(psk.psk_dst.addr.v.a.mask));
	if (ioctl(dev_fd, DIOCKILLSTATES, &psk))
		syslog(LOG_ERR, "DIOCKILLSTATES failed (%m)");
}

/* signal handler that makes us go away properly */
static void
need_death(int signo __unused)
{
	want_death = 1;
}

/*
 * function that removes our stuff when we go away.
 */
static __dead void
do_death(int active)
{
	int	ret = 0;

	if (active) {
		change_filter(0, luser, ipsrc);
		authpf_kill_states();
		remove_stale_rulesets();
	}
	if (pidfp)
		ftruncate(fileno(pidfp), 0);
	if (pidfile[0])
		if (unlink(pidfile) == -1)
			syslog(LOG_ERR, "cannot unlink %s (%m)", pidfile);
	exit(ret);
}

/*
 * callbacks for parse_rules(void)
 */

int
pfctl_add_rule(struct pfctl *pf, struct pf_rule *r)
{
	u_int8_t		rs_num;
	struct pfioc_rule	pr;

	switch (r->action) {
	case PF_PASS:
	case PF_DROP:
		rs_num = PF_RULESET_FILTER;
		break;
	case PF_SCRUB:
		rs_num = PF_RULESET_SCRUB;
		break;
	case PF_NAT:
	case PF_NONAT:
		rs_num = PF_RULESET_NAT;
		break;
	case PF_RDR:
	case PF_NORDR:
		rs_num = PF_RULESET_RDR;
		break;
	case PF_BINAT:
	case PF_NOBINAT:
		rs_num = PF_RULESET_BINAT;
		break;
	default:
		syslog(LOG_ERR, "invalid rule action %d", r->action);
		return (1);
	}

	bzero(&pr, sizeof(pr));
	strlcpy(pr.anchor, pf->anchor, sizeof(pr.anchor));
	strlcpy(pr.ruleset, pf->ruleset, sizeof(pr.ruleset));
	if (pfctl_add_pool(pf, &r->rpool, r->af))
		return (1);
	pr.ticket = pfctl_get_ticket(pf->trans, rs_num, pf->anchor,
	    pf->ruleset);
	pr.pool_ticket = pf->paddr.ticket;
	memcpy(&pr.rule, r, sizeof(pr.rule));
	if (ioctl(pf->dev, DIOCADDRULE, &pr)) {
		syslog(LOG_ERR, "DIOCADDRULE %m");
		return (1);
	}
	pfctl_clear_pool(&r->rpool);
	return (0);
}

int
pfctl_add_pool(struct pfctl *pf, struct pf_pool *p, sa_family_t af)
{
	struct pf_pooladdr	*pa;

	if (ioctl(pf->dev, DIOCBEGINADDRS, &pf->paddr)) {
		syslog(LOG_ERR, "DIOCBEGINADDRS %m");
		return (1);
	}
	pf->paddr.af = af;
	TAILQ_FOREACH(pa, &p->list, entries) {
		memcpy(&pf->paddr.addr, pa, sizeof(struct pf_pooladdr));
		if (ioctl(pf->dev, DIOCADDADDR, &pf->paddr)) {
			syslog(LOG_ERR, "DIOCADDADDR %m");
			return (1);
		}
	}
	return (0);
}

void
pfctl_clear_pool(struct pf_pool *pool)
{
	struct pf_pooladdr	*pa;

	while ((pa = TAILQ_FIRST(&pool->list)) != NULL) {
		TAILQ_REMOVE(&pool->list, pa, entries);
		free(pa);
	}
}

int
pfctl_add_altq(struct pfctl *pf __unused, struct pf_altq *a __unused)
{
	fprintf(stderr, "altq rules not supported in authpf\n");
	return (1);
}

int
pfctl_set_optimization(struct pfctl *pf __unused, const char *opt __unused)
{
	fprintf(stderr, "set optimization not supported in authpf\n");
	return (1);
}

int
pfctl_set_logif(struct pfctl *pf __unused, char *ifname __unused)
{
	fprintf(stderr, "set loginterface not supported in authpf\n");
	return (1);
}

int
pfctl_set_hostid(struct pfctl *pf __unused, u_int32_t hostid __unused)
{
	fprintf(stderr, "set hostid not supported in authpf\n");
	return (1);
}

int
pfctl_set_timeout(struct pfctl *pf __unused, const char *opt __unused,
		  int seconds __unused, int quiet __unused)
{
	fprintf(stderr, "set timeout not supported in authpf\n");
	return (1);
}

int
pfctl_set_limit(struct pfctl *pf __unused, const char *opt __unused,
		unsigned int limit __unused)
{
	fprintf(stderr, "set limit not supported in authpf\n");
	return (1);
}

int
pfctl_set_debug(struct pfctl *pf __unused, char *d __unused)
{
	fprintf(stderr, "set debug not supported in authpf\n");
	return (1);
}

int
pfctl_define_table(char *name __unused, int flags __unused, int addrs __unused,
		   const char *anchor __unused, const char *ruleset __unused,
		   struct pfr_buffer *ab __unused, u_int32_t ticket __unused)
{
	fprintf(stderr, "table definitions not yet supported in authpf\n");
	return (1);
}

int
pfctl_rules(int dev __unused, char *filename __unused, int opts __unused,
	    char *my_anchorname __unused, char *my_rulesetname __unused,
	    struct pfr_buffer *t __unused)
{
	/* never called, no anchors inside anchors, but we need the stub */
	fprintf(stderr, "load anchor not supported from authpf\n");
	return (1);
}

void
pfctl_print_title(const char *title __unused)
{
}
