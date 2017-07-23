/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.unix.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.unix.c,v 1.8 1999/11/16 02:57:13 billf Exp $ */

/* This file collects some Unix dependencies; hack.pager.c contains some more */

/*
 * The time is used for:
 *	- seed for random()
 *	- year on tombstone and yymmdd in record file
 *	- phase of the moon (various monsters react to NEW_MOON or FULL_MOON)
 *	- night and midnight (the undead are dangerous at midnight)
 *	- determination of what files are "very old"
 */

#include <errno.h>
#include "hack.h"

#include <sys/types.h>	/* for time_t and stat */
#include <sys/stat.h>
#include <time.h>

static struct tm *getlt(void);
static bool  veryold(int);
#ifdef MAIL
static void  newmail(void);
static void  mdrush(struct monst *, bool);
#endif

void
setrandom(void)
{
	srandomdev();
}

static struct tm *
getlt(void)
{
	time_t date;

	time(&date);
	return (localtime(&date));
}

int
getyear(void)
{
	return (1900 + getlt()->tm_year);
}

char *
getdate(void)
{
	static char datestr[7];
	struct tm *lt = getlt();

	snprintf(datestr, sizeof(datestr), "%02d%02d%02d",
	    lt->tm_year % 100, lt->tm_mon + 1, lt->tm_mday);
	return (datestr);
}

int
phase_of_the_moon(void)			/* 0-7, with 0: new, 4: full */
{					/* moon period: 29.5306 days */
					/* year: 365.2422 days */
	struct tm *lt = getlt();
	int epact, diy, golden;

	diy = lt->tm_yday;
	golden = (lt->tm_year % 19) + 1;
	epact = (11 * golden + 18) % 30;
	if ((epact == 25 && golden > 11) || epact == 24)
		epact++;

	return ((((((diy + epact) * 6) + 11) % 177) / 22) & 7);
}

bool
night(void)
{
	int hour = getlt()->tm_hour;

	return (hour < 6 || hour > 21);
}

bool
midnight(void)
{
	return (getlt()->tm_hour == 0);
}

struct stat buf, hbuf;

void
gethdate(const char *name)
{
/* old version - for people short of space */
	char *np;

	name = "/usr/games/hide/hack";
	if (stat(name, &hbuf))
		error("Cannot get status of %s.",
		      (np = strrchr(name, '/')) ? np + 1 : name);
}

bool
uptodate(int fd)
{
	if (fstat(fd, &buf)) {
		pline("Cannot get status of saved level? ");
		return (0);
	}
	if (buf.st_mtime < hbuf.st_mtime) {
		pline("Saved level is out of date. ");
		return (0);
	}
	return (1);
}

/* see whether we should throw away this xlock file */
static bool
veryold(int fd)
{
	int i;
	time_t date;

	if (fstat(fd, &buf))			/* cannot get status */
		return (0);
	if (buf.st_size != sizeof(int))		/* not an xlock file */
		return (0);
	time(&date);
	if (date - buf.st_mtime < 3L * 24L * 60L * 60L) {	/* recent */
		int lockedpid;	/* should be the same size as hackpid */

		if (read(fd, (char *)&lockedpid, sizeof(lockedpid)) !=
		    sizeof(lockedpid))
			/* strange ... */
			return (0);

		/* From: Rick Adams <seismo!rick> */
		/* This will work on 4.1cbsd, 4.2bsd and system 3? & 5. */
		/* It will do nothing on V7 or 4.1bsd. */
		if (!(kill(lockedpid, 0) == -1 && errno == ESRCH))
			return (0);
	}
	close(fd);
	for (i = 1; i <= MAXLEVEL; i++) {	/* try to remove all */
		glo(i);
		unlink(lock);
	}
	glo(0);
	if (unlink(lock))			/* cannot remove it */
		return (0);
	return (1);				/* success! */
}

void
getlock(void)
{
	int i = 0, fd;

	fflush(stdout);

	/* we ignore QUIT and INT at this point */
	if ((link(HLOCK, LLOCK) == -1) && (symlink(HLOCK, LLOCK) == -1)) {
		int errnosv = errno;

		perror(HLOCK);
		printf("Cannot link %s to %s\n", LLOCK, HLOCK);
		switch (errnosv) {
		case ENOENT:
			printf("Perhaps there is no (empty) file %s ?\n", HLOCK);
			break;
		case EACCES:
			printf("It seems you don't have write permission here.\n");
			break;
		case EEXIST:
			printf("(Try again or rm %s.)\n", LLOCK);
			break;
		default:
			printf("I don't know what is wrong.");
		}
		getret();
		error("%s", "");
		/* NOTREACHED */
	}

	regularize(lock);
	glo(0);
	if (locknum > 25)
		locknum = 25;

	do {
		if (locknum)
			lock[0] = 'a' + i++;

		if ((fd = open(lock, O_RDONLY)) == -1) {
			if (errno == ENOENT)	/* no such file */
				goto gotlock;
			perror(lock);
			unlink(LLOCK);
			error("Cannot open %s", lock);
		}

		if (veryold(fd)) /* if true, this closes fd and unlinks lock */
			goto gotlock;
		close(fd);
	} while (i < locknum);

	unlink(LLOCK);
	error(locknum ? "Too many hacks running now."
	      : "There is a game in progress under your name.");
gotlock:
	fd = creat(lock, FMASK);
	if (unlink(LLOCK) == -1)
		error("Cannot unlink %s.", LLOCK);
	if (fd == -1) {
		error("cannot creat lock file.");
	} else {
		if (write(fd, (char *)&hackpid, sizeof(hackpid))
		    != sizeof(hackpid))
			error("cannot write lock");
		if (close(fd) == -1)
			error("cannot close lock");
	}
}

#ifdef MAIL

/*
 * Notify user when new mail has arrived. [Idea from Merlyn Leroy, but
 * I don't know the details of his implementation.]
 * { Later note: he disliked my calling a general mailreader and felt that
 *   hack should do the paging itself. But when I get mail, I want to put it
 *   in some folder, reply, etc. - it would be unreasonable to put all these
 *   functions in hack. }
 * The mail daemon '2' is at present not a real monster, but only a visual
 * effect. Thus, makemon() is superfluous. This might become otherwise,
 * however. The motion of '2' is less restrained than usual: diagonal moves
 * from a DOOR are possible. He might also use SDOOR's. Also, '2' is visible
 * in a ROOM, even when you are Blind.
 * Its path should be longer when you are Telepat-hic and Blind.
 *
 * Interesting side effects:
 *	- You can get rich by sending yourself a lot of mail and selling
 *	  it to the shopkeeper. Unfortunately mail isn't very valuable.
 *	- You might die in case '2' comes along at a critical moment during
 *	  a fight and delivers a scroll the weight of which causes you to
 *	  collapse.
 *
 * Possible extensions:
 *	- Open the file MAIL and do fstat instead of stat for efficiency.
 *	  (But sh uses stat, so this cannot be too bad.)
 *	- Examine the mail and produce a scroll of mail called "From somebody".
 *	- Invoke MAILREADER in such a way that only this single letter is read.
 *
 *	- Make him lose his mail when a Nymph steals the letter.
 *	- Do something to the text when the scroll is enchanted or cancelled.
 */
#include "def.mkroom.h"
static struct stat omstat, nmstat;
static char *mailbox;
static long laststattime;

void
getmailstatus(void)
{
	if (!(mailbox = getenv("MAIL")))
		return;
	if (stat(mailbox, &omstat)) {
#ifdef PERMANENT_MAILBOX
		pline("Cannot get status of MAIL=%s .", mailbox);
		mailbox = NULL;
#else
		omstat.st_mtime = 0;
#endif /* PERMANENT_MAILBOX */
	}
}

void
ckmailstatus(void)
{
	if (!mailbox
#ifdef MAILCKFREQ
	    || moves < laststattime + MAILCKFREQ
#endif /* MAILCKFREQ */
	    )
		return;
	laststattime = moves;
	if (stat(mailbox, &nmstat)) {
#ifdef PERMANENT_MAILBOX
		pline("Cannot get status of MAIL=%s anymore.", mailbox);
		mailbox = NULL;
#else
		nmstat.st_mtime = 0;
#endif /* PERMANENT_MAILBOX */
	} else if (nmstat.st_mtime > omstat.st_mtime) {
		if (nmstat.st_size)
			newmail();
		getmailstatus();	/* might be too late ... */
	}
}

static void
newmail(void)
{
	/* produce a scroll of mail */
	struct obj *obj;
	struct monst *md;
	extern struct permonst pm_mail_daemon;

	obj = mksobj(SCR_MAIL);
	if (md = makemon(&pm_mail_daemon, u.ux, u.uy))	/* always succeeds */
		mdrush(md, 0);

	pline("\"Hello, %s! I have some mail for you.\"", plname);
	if (md) {
		if (dist(md->mx, md->my) > 2)
			pline("\"Catch!\"");
		more();

		/* let him disappear again */
		mdrush(md, 1);
		mondead(md);
	}

	obj = addinv(obj);
	identify(obj);		/* set known and do prinv() */
}

/* make md run through the cave */
static void
mdrush(struct monst *md, bool away)
{
	int uroom = inroom(u.ux, u.uy);

	if (uroom >= 0) {
		int tmp = rooms[uroom].fdoor;
		int cnt = rooms[uroom].doorct;
		int fx = u.ux, fy = u.uy;
		while (cnt--) {
			if (dist(fx, fy) < dist(doors[tmp].x, doors[tmp].y)) {
				fx = doors[tmp].x;
				fy = doors[tmp].y;
			}
			tmp++;
		}
		tmp_at(-1, md->data->mlet);	/* open call */
		if (away) {	/* interchange origin and destination */
			unpmon(md);
			tmp = fx;
			fx = md->mx;
			md->mx = tmp;
			tmp = fy;
			fy = md->my;
			md->my = tmp;
		}
		while (fx != md->mx || fy != md->my) {
			int dx, dy, nfx = fx, nfy = fy, d1, d2;

			tmp_at(fx, fy);
			d1 = DIST(fx, fy, md->mx, md->my);
			for (dx = -1; dx <= 1; dx++)
				for (dy = -1; dy <= 1; dy++)
					if (dx || dy) {
						d2 = DIST(fx + dx, fy + dy, md->mx, md->my);
						if (d2 < d1) {
							d1 = d2;
							nfx = fx + dx;
							nfy = fy + dy;
						}
					}
			if (nfx != fx || nfy != fy) {
				fx = nfx;
				fy = nfy;
			} else {
				if (!away) {
					md->mx = fx;
					md->my = fy;
				}
				break;
			}
		}
		tmp_at(-1, -1);		/* close call */
	}
	if (!away)
		pmon(md);
}

void
readmail(void)
{
#ifdef DEF_MAILREADER		/* This implies that UNIX is defined */
	char *mr = NULL;

	more();
	if (!(mr = getenv("MAILREADER")))
		mr = DEF_MAILREADER;
	if (child(1)) {
		execl(mr, mr, NULL);
		exit(1);
	}
#else /* DEF_MAILREADER */
	page_file(mailbox, FALSE);
#endif /* DEF_MAILREADER */
       /* get new stat; not entirely correct: there is a small time
	* window where we do not see new mail */
	getmailstatus();
}
#endif /* MAIL */

void
regularize(char *s)	/* normalize file name - we don't like ..'s or /'s */
{
	char *lp;

	while ((lp = strchr(s, '.')) || (lp = strchr(s, '/')))
		*lp = '_';
}
