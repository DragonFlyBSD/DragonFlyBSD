/*-
 * Copyright (c) 1991, 1993
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
 * @(#) Copyright (c) 1991, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)id.c	8.2 (Berkeley) 2/16/94
 * $FreeBSD: src/usr.bin/id/id.c,v 1.12.2.3 2001/12/20 12:09:03 ru Exp $
 */

#include <sys/param.h>

#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	current(void);
static void	pline(struct passwd *);
static void	pretty(struct passwd *);
static void	group(struct passwd *, int);
static void	usage(void);
static void	user(struct passwd *);
static struct passwd *
		who(char *);

int isgroups, iswhoami;

int
main(int argc, char **argv)
{
	struct group *gr;
	struct passwd *pw;
	int Gflag, Pflag, ch, gflag, id, nflag, pflag, rflag, uflag;
	const char *myname;

	Gflag = Pflag = gflag = nflag = pflag = rflag = uflag = 0;

	myname = strrchr(argv[0], '/');
	myname = (myname != NULL) ? myname + 1 : argv[0];
	if (strcmp(myname, "groups") == 0) {
		isgroups = 1;
		Gflag = nflag = 1;
	}
	else if (strcmp(myname, "whoami") == 0) {
		iswhoami = 1;
		uflag = nflag = 1;
	}

	while ((ch = getopt(argc, argv,
	    (isgroups || iswhoami) ? "" : "PGgnpru")) != -1)
		switch(ch) {
		case 'G':
			Gflag = 1;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 'g':
			gflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'u':
			uflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (iswhoami && argc > 0)
		usage();

	switch(Gflag + Pflag + gflag + pflag + uflag) {
	case 1:
		break;
	case 0:
		if (!nflag && !rflag)
			break;
		/* FALLTHROUGH */
	default:
		usage();
	}

	pw = *argv ? who(*argv) : NULL;

	if (gflag) {
		id = pw ? pw->pw_gid : rflag ? getgid() : getegid();
		if (nflag && (gr = getgrgid(id)))
			printf("%s\n", gr->gr_name);
		else
			printf("%u\n", id);
		exit(0);
	}

	if (uflag) {
		id = pw ? pw->pw_uid : rflag ? getuid() : geteuid();
		if (nflag && (pw = getpwuid(id)))
			printf("%s\n", pw->pw_name);
		else
			printf("%u\n", id);
		exit(0);
	}

	if (Gflag) {
		group(pw, nflag);
		exit(0);
	}

	if (Pflag) {
		pline(pw);
		exit(0);
	}

	if (pflag) {
		pretty(pw);
		exit(0);
	}

	if (pw)
		user(pw);
	else
		current();
	exit(0);
}

static void
pretty(struct passwd *pw)
{
	struct group *gr;
	u_int eid, rid;
	char *login;

	if (pw) {
		printf("uid\t%s\n", pw->pw_name);
		printf("groups\t");
		group(pw, 1);
	} else {
		if ((login = getlogin()) == NULL)
			err(1, "getlogin");

		pw = getpwuid(rid = getuid());
		if (pw == NULL || strcmp(login, pw->pw_name))
			printf("login\t%s\n", login);
		if (pw)
			printf("uid\t%s\n", pw->pw_name);
		else
			printf("uid\t%u\n", rid);

		if ((eid = geteuid()) != rid) {
			if ((pw = getpwuid(eid)))
				printf("euid\t%s\n", pw->pw_name);
			else
				printf("euid\t%u\n", eid);
		}
		if ((rid = getgid()) != (eid = getegid())) {
			if ((gr = getgrgid(rid)))
				printf("rgid\t%s\n", gr->gr_name);
			else
				printf("rgid\t%u\n", rid);
		}
		printf("groups\t");
		group(NULL, 1);
	}
}

static void
current(void)
{
	struct group *gr;
	struct passwd *pw;
	int cnt, id, eid, lastid, ngroups;
	gid_t groups[NGROUPS];
	const char *fmt;

	id = getuid();
	printf("uid=%u", id);
	if ((pw = getpwuid(id)))
		printf("(%s)", pw->pw_name);
	if ((eid = geteuid()) != id) {
		printf(" euid=%u", eid);
		if ((pw = getpwuid(eid)))
			printf("(%s)", pw->pw_name);
	}
	id = getgid();
	printf(" gid=%u", id);
	if ((gr = getgrgid(id)))
		printf("(%s)", gr->gr_name);
	if ((eid = getegid()) != id) {
		printf(" egid=%u", eid);
		if ((gr = getgrgid(eid)))
			printf("(%s)", gr->gr_name);
	}
	if ((ngroups = getgroups(NGROUPS, groups))) {
		for (fmt = " groups=%u", lastid = -1, cnt = 0; cnt < ngroups;
		    fmt = ", %u", lastid = id) {
			id = groups[cnt++];
			if (lastid == id)
				continue;
			printf(fmt, id);
			if ((gr = getgrgid(id)))
				printf("(%s)", gr->gr_name);
		}
	}
	printf("\n");
}

static void
user(struct passwd *pw)
{
	struct group *gr;
	const char *fmt;
	int cnt, ngroups;
	gid_t gid, lastgid, groups[NGROUPS + 1];

	printf("uid=%u(%s)", pw->pw_uid, pw->pw_name);
	gid = pw->pw_gid;
	printf(" gid=%u", gid);
	if ((gr = getgrgid(gid)))
		printf("(%s)", gr->gr_name);
	ngroups = NGROUPS + 1;
	getgrouplist(pw->pw_name, gid, groups, &ngroups);
	fmt = " groups=%u";
	for (lastgid = -1, cnt = 0; cnt < ngroups; ++cnt) {
		if (lastgid == (gid = groups[cnt]))
			continue;
		printf(fmt, gid);
		fmt = ", %u";
		if ((gr = getgrgid(gid)))
			printf("(%s)", gr->gr_name);
		lastgid = gid;
	}
	printf("\n");
}

static void
group(struct passwd *pw, int nflag)
{
	struct group *gr;
	int cnt, id, lastid, ngroups;
	gid_t groups[NGROUPS + 1];
	const char *fmt;

	if (pw) {
		ngroups = NGROUPS + 1;
		getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups);
	} else {
		groups[0] = getgid();
		ngroups = getgroups(NGROUPS, groups + 1) + 1;
	}
	fmt = nflag ? "%s" : "%u";
	for (lastid = -1, cnt = 0; cnt < ngroups; ++cnt) {
		if (lastid == (id = groups[cnt]))
			continue;
		if (nflag) {
			if ((gr = getgrgid(id)))
				printf(fmt, gr->gr_name);
			else
				printf(*fmt == ' ' ? " %u" : "%u",
				    id);
			fmt = " %s";
		} else {
			printf(fmt, id);
			fmt = " %u";
		}
		lastid = id;
	}
	printf("\n");
}

static struct passwd *
who(char *u)
{
	struct passwd *pw;
	long id;
	char *ep;

	/*
	 * Translate user argument into a pw pointer.  First, try to
	 * get it as specified.  If that fails, try it as a number.
	 */
	if ((pw = getpwnam(u)))
		return(pw);
	id = strtol(u, &ep, 10);
	if (*u && !*ep && (pw = getpwuid(id)))
		return(pw);
	errx(1, "%s: no such user", u);
	/* NOTREACHED */
}

static void
pline(struct passwd *pw)
{
	u_int rid;

	if (!pw) {
		if ((pw = getpwuid(rid = getuid())) == NULL)
			err(1, "getpwuid");
	}

	printf("%s:%s:%d:%d:%s:%ld:%ld:%s:%s:%s\n", pw->pw_name,
			pw->pw_passwd, pw->pw_uid, pw->pw_gid, pw->pw_class,
			(long)pw->pw_change, (long)pw->pw_expire, pw->pw_gecos,
			pw->pw_dir, pw->pw_shell);
}


static void
usage(void)
{

	if (isgroups)
		fprintf(stderr, "usage: groups [user]\n");
	else if (iswhoami)
		fprintf(stderr, "usage: whoami\n");
	else
		fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n",
		    "usage: id [user]",
		    "       id -G [-n] [user]",
		    "       id -P [user]",
		    "       id -g [-nr] [user]",
		    "       id -p [user]",
		    "       id -u [-nr] [user]");
	exit(1);
}
