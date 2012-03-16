/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.save.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.save.c,v 1.4 1999/11/16 10:26:37 marcel Exp $ */

#include "hack.h"

extern char SAVEF[], nul[];

static bool dosave0(int);

int
dosave(void)
{
	if (dosave0(0)) {
		settty("Be seeing you ...\n");
		exit(0);
	}
	return (0);
}

#ifndef NOSAVEONHANGUP
void
hangup(int n __unused)
{
	dosave0(1);
	exit(1);
}
#endif /* NOSAVEONHANGUP */

/* returns 1 if save successful */
static bool
dosave0(int hu)
{
	int fd, ofd;
	int tmp;	/* not ! */

	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	if ((fd = creat(SAVEF, FMASK)) < 0) {
		if (!hu)
			pline("Cannot open save file. (Continue or Quit)");
		unlink(SAVEF);	/* ab@unido */
		return (0);
	}
	if (flags.moonphase == FULL_MOON)	/* ut-sally!fletcher */
		u.uluck--;			/* and unido!ab */
	savelev(fd, dlevel);
	saveobjchn(fd, invent);
	saveobjchn(fd, fcobj);
	savemonchn(fd, fallen_down);
	tmp = getuid();
	bwrite(fd, (char *)&tmp, sizeof(tmp));
	bwrite(fd, (char *)&flags, sizeof(struct flag));
	bwrite(fd, (char *)&dlevel, sizeof(dlevel));
	bwrite(fd, (char *)&maxdlevel, sizeof(maxdlevel));
	bwrite(fd, (char *)&moves, sizeof(moves));
	bwrite(fd, (char *)&u, sizeof(struct you));
	if (u.ustuck)
		bwrite(fd, (char *)&(u.ustuck->m_id), sizeof(u.ustuck->m_id));
	bwrite(fd, (char *)pl_character, sizeof(pl_character));
	bwrite(fd, (char *)genocided, sizeof(genocided));
	bwrite(fd, (char *)fut_geno, sizeof(fut_geno));
	savenames(fd);
	for (tmp = 1; tmp <= maxdlevel; tmp++) {
		if (tmp == dlevel || !level_exists[tmp])
			continue;
		glo(tmp);
		if ((ofd = open(lock, O_RDONLY)) < 0) {
			if (!hu)
				pline("Error while saving: cannot read %s.", lock);
			close(fd);
			unlink(SAVEF);
			if (!hu)
				done("tricked");
			return (0);
		}
		getlev(ofd, hackpid, tmp);
		close(ofd);
		bwrite(fd, (char *)&tmp, sizeof(tmp));	/* level number */
		savelev(fd, tmp);			/* actual level */
		unlink(lock);
	}
	close(fd);
	glo(dlevel);
	unlink(lock);	/* get rid of current level --jgm */
	glo(0);
	unlink(lock);
	return (1);
}

bool
dorecover(int fd)
{
	int nfd;
	int tmp;		/* not a ! */
	unsigned mid;		/* idem */
	struct obj *otmp;

	restoring = TRUE;
	getlev(fd, 0, 0);
	invent = restobjchn(fd);
	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (otmp->owornmask)
			setworn(otmp, otmp->owornmask);
	fcobj = restobjchn(fd);
	fallen_down = restmonchn(fd);
	mread(fd, (char *)&tmp, sizeof(tmp));
	if (tmp != (int)getuid()) {	/* strange ... */
		close(fd);
		unlink(SAVEF);
		puts("Saved game was not yours.");
		restoring = FALSE;
		return (0);
	}
	mread(fd, (char *)&flags, sizeof(struct flag));
	mread(fd, (char *)&dlevel, sizeof(dlevel));
	mread(fd, (char *)&maxdlevel, sizeof(maxdlevel));
	mread(fd, (char *)&moves, sizeof(moves));
	mread(fd, (char *)&u, sizeof(struct you));
	if (u.ustuck)
		mread(fd, (char *)&mid, sizeof(mid));
	mread(fd, (char *)pl_character, sizeof(pl_character));
	mread(fd, (char *)genocided, sizeof(genocided));
	mread(fd, (char *)fut_geno, sizeof(fut_geno));
	restnames(fd);
	for (;;) {
		if (read(fd, (char *)&tmp, sizeof(tmp)) != sizeof(tmp))
			break;
		getlev(fd, 0, tmp);
		glo(tmp);
		if ((nfd = creat(lock, FMASK)) < 0)
			panic("Cannot open temp file %s!\n", lock);
		savelev(nfd, tmp);
		close(nfd);
	}
	lseek(fd, (off_t)0, SEEK_SET);
	getlev(fd, 0, 0);
	close(fd);
	unlink(SAVEF);
	if (Punished) {
		for (otmp = fobj; otmp; otmp = otmp->nobj)
			if (otmp->olet == CHAIN_SYM)
				goto chainfnd;
		panic("Cannot find the iron chain?");
chainfnd:
		uchain = otmp;
		if (!uball) {
			for (otmp = fobj; otmp; otmp = otmp->nobj)
				if (otmp->olet == BALL_SYM && otmp->spe)
					goto ballfnd;
			panic("Cannot find the iron ball?");
ballfnd:
			uball = otmp;
		}
	}
	if (u.ustuck) {
		struct monst *mtmp;

		for (mtmp = fmon; mtmp; mtmp = mtmp->nmon)
			if (mtmp->m_id == mid)
				goto monfnd;
		panic("Cannot find the monster ustuck.");
monfnd:
		u.ustuck = mtmp;
	}
#ifndef QUEST
	setsee();	/* only to recompute seelx etc. - these weren't saved */
#endif /* QUEST */
	docrt();
	restoring = FALSE;
	return (1);
}

struct obj *
restobjchn(int fd)
{
	struct obj *otmp, *otmp2;
	struct obj *first = NULL;
	int xl;

	/* suppress "used before set" warning from lint */
	otmp2 = NULL;
	for (;;) {
		mread(fd, (char *)&xl, sizeof(xl));
		if (xl == -1)
			break;
		otmp = newobj(xl);
		if (!first)
			first = otmp;
		else
			otmp2->nobj = otmp;
		mread(fd, (char *)otmp, (unsigned)xl + sizeof(struct obj));
		if (!otmp->o_id) otmp->o_id = flags.ident++;
		otmp2 = otmp;
	}
	if (first && otmp2->nobj) {
		impossible("Restobjchn: error reading objchn.");
		otmp2->nobj = 0;
	}
	return (first);
}

struct monst *
restmonchn(int fd)
{
	struct monst *mtmp, *mtmp2;
	struct monst *first = NULL;
	int xl;
	struct permonst *monbegin;
	long differ;

	mread(fd, (char *)&monbegin, sizeof(monbegin));
	differ = (char *)(&mons[0]) - (char *)(monbegin);

	/* suppress "used before set" warning from lint */
	mtmp2 = NULL;
	for (;;) {
		mread(fd, (char *)&xl, sizeof(xl));
		if (xl == -1)
			break;
		mtmp = newmonst(xl);
		if (!first)
			first = mtmp;
		else
			mtmp2->nmon = mtmp;
		mread(fd, (char *)mtmp, (unsigned)xl + sizeof(struct monst));
		if (!mtmp->m_id)
			mtmp->m_id = flags.ident++;
		mtmp->data = (struct permonst *)
		    ((char *)mtmp->data + differ);
		if (mtmp->minvent)
			mtmp->minvent = restobjchn(fd);
		mtmp2 = mtmp;
	}
	if (first && mtmp2->nmon) {
		impossible("Restmonchn: error reading monchn.");
		mtmp2->nmon = 0;
	}
	return (first);
}
