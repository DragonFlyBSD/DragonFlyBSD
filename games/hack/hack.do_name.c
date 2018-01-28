/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.do_name.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.do_name.c,v 1.5 1999/11/16 10:26:36 marcel Exp $ */

#include "hack.h"

static void do_oname(struct obj *);
static char *xmonnam(struct monst *, int);
static char *lmonnam(struct monst *);
static char *visctrl(char);

coord
getpos(int force, const char *goal)
{
	int cx, cy, i, c;
	coord cc;

	pline("(For instructions type a ?)");
	cx = u.ux;
	cy = u.uy;
	curs(cx, cy + 2);
	while ((c = readchar()) != '.') {
		for (i = 0; i < 8; i++)
			if (sdir[i] == c) {
				if (1 <= cx + xdir[i] && cx + xdir[i] <= COLNO)
					cx += xdir[i];
				if (0 <= cy + ydir[i] && cy + ydir[i] <= ROWNO - 1)
					cy += ydir[i];
				goto nxtc;
			}
		if (c == '?') {
			pline("Use [hjkl] to move the cursor to %s.", goal);
			pline("Type a . when you are at the right place.");
		} else {
			pline("Unknown direction: '%s' (%s).",
			      visctrl(c),
			      force ? "use hjkl or ." : "aborted");
			if (force)
				goto nxtc;
			cc.x = -1;
			cc.y = 0;
			return (cc);
		}
nxtc:
		curs(cx, cy + 2);
	}
	cc.x = cx;
	cc.y = cy;
	return (cc);
}

int
do_mname(void)
{
	char buf[BUFSZ];
	coord cc;
	int cx, cy, lth, i;
	struct monst *mtmp, *mtmp2;

	cc = getpos(0, "the monster you want to name");
	cx = cc.x;
	cy = cc.y;
	if (cx < 0)
		return (0);
	mtmp = m_at(cx, cy);
	if (!mtmp) {
		if (cx == u.ux && cy == u.uy)
			pline("This ugly monster is called %s and cannot be renamed.",
			    plname);
		else
			pline("There is no monster there.");
		return (1);
	}
	if (mtmp->mimic) {
		pline("I see no monster there.");
		return (1);
	}
	if (!cansee(cx, cy)) {
		pline("I cannot see a monster there.");
		return (1);
	}
	pline("What do you want to call %s? ", lmonnam(mtmp));
	getlin(buf);
	clrlin();
	if (!*buf || *buf == '\033')
		return (1);
	lth = strlen(buf) + 1;
	if (lth > 63) {
		buf[62] = 0;
		lth = 63;
	}
	mtmp2 = newmonst(mtmp->mxlth + lth);
	*mtmp2 = *mtmp;
	for (i = 0; (unsigned)i < mtmp->mxlth; i++)
		((char *)mtmp2->mextra)[i] = ((char *)mtmp->mextra)[i];
	mtmp2->mnamelth = lth;
	strcpy(NAME(mtmp2), buf);
	replmon(mtmp, mtmp2);
	return (1);
}

/*
 * This routine changes the address of  obj . Be careful not to call it
 * when there might be pointers around in unknown places. For now: only
 * when  obj  is in the inventory.
 */
static void
do_oname(struct obj *obj)
{
	struct obj *otmp, *otmp2;
	int lth;
	char buf[BUFSZ];

	pline("What do you want to name %s? ", doname(obj));
	getlin(buf);
	clrlin();
	if (!*buf || *buf == '\033')
		return;
	lth = strlen(buf) + 1;
	if (lth > 63) {
		buf[62] = 0;
		lth = 63;
	}
	otmp2 = newobj(lth);
	*otmp2 = *obj;
	otmp2->onamelth = lth;
	strcpy(ONAME(otmp2), buf);

	setworn(NULL, obj->owornmask);
	setworn(otmp2, otmp2->owornmask);

	/*
	 * do freeinv(obj); etc. by hand in order to preserve the position of
	 * this object in the inventory
	 */
	if (obj == invent)
		invent = otmp2;
	else
		for (otmp = invent;; otmp = otmp->nobj) {
			if (!otmp)
				panic("Do_oname: cannot find obj.");
			if (otmp->nobj == obj) {
				otmp->nobj = otmp2;
				break;
			}
		}
	/*obfree(obj, otmp2);*/	/* now unnecessary: no pointers on bill */
	free(obj);		/* let us hope nobody else saved a pointer */
}

int
ddocall(void)
{
	struct obj *obj;

	pline("Do you want to name an individual object? [ny] ");
	switch (readchar()) {
	case '\033':
		break;
	case 'y':
		obj = getobj("#", "name");
		if (obj)
			do_oname(obj);
		break;
	default:
		obj = getobj("?!=/", "call");
		if (obj)
			docall(obj);
	}
	return (0);
}

void
docall(struct obj *obj)
{
	char buf[BUFSZ];
	struct obj otemp;
	char **str1;
	char *str;

	otemp = *obj;
	otemp.quan = 1;
	otemp.onamelth = 0;
	str = xname(&otemp);
	pline("Call %s %s: ", strchr(vowels, *str) ? "an" : "a", str);
	getlin(buf);
	clrlin();
	if (!*buf || *buf == '\033')
		return;
	str = newstring(strlen(buf) + 1);
	strcpy(str, buf);
	str1 = &(objects[obj->otyp].oc_uname);
	if (*str1)
		free(*str1);
	*str1 = str;
}

/* these names should have length < PL_NSIZ */
const char *ghostnames[] = {
	"adri", "andries", "andreas", "bert", "david", "dirk", "emile",
	"frans", "fred", "greg", "hether", "jay", "john", "jon", "kay",
	"kenny", "maud", "michiel", "mike", "peter", "robert", "ron",
	"tom", "wilmar"
};

static char *
xmonnam(struct monst *mtmp, int vb)
{
	static char buf[BUFSZ];		/* %% */

	if (mtmp->mnamelth && !vb) {
		strcpy(buf, NAME(mtmp));
		return (buf);
	}
	switch (mtmp->data->mlet) {
	case ' ':
		{
			const char *gn = (const char *)mtmp->mextra;
			if (!*gn) {	/* might also look in scorefile */
				gn = ghostnames[rn2(SIZE(ghostnames))];
				if (!rn2(2))
					strcpy((char *)mtmp->mextra,
					    !rn2(5) ? plname : gn);
			}
			sprintf(buf, "%s's ghost", gn);
		}
		break;
	case '@':
		if (mtmp->isshk) {
			strcpy(buf, shkname(mtmp));
			break;
		}
		/* FALLTHROUGH */
	default:
		sprintf(buf, "the %s%s",
			mtmp->minvis ? "invisible " : "",
			mtmp->data->mname);
	}
	if (vb && mtmp->mnamelth) {
		strcat(buf, " called ");
		strcat(buf, NAME(mtmp));
	}
	return (buf);
}

static char *
lmonnam(struct monst *mtmp)
{
	return (xmonnam(mtmp, 1));
}

char *
monnam(struct monst *mtmp)
{
	return (xmonnam(mtmp, 0));
}

char *
Monnam(struct monst *mtmp)
{
	char *bp = monnam(mtmp);

	if ('a' <= *bp && *bp <= 'z')
		*bp += ('A' - 'a');
	return (bp);
}

char *
amonnam(struct monst *mtmp, const char *adj)
{
	char *bp = monnam(mtmp);
	static char buf[BUFSZ];		/* %% */

	if (!strncmp(bp, "the ", 4))
		bp += 4;
	sprintf(buf, "the %s %s", adj, bp);
	return (buf);
}

char *
Amonnam(struct monst *mtmp, const char *adj)
{
	char *bp = amonnam(mtmp, adj);

	*bp = 'T';
	return (bp);
}

char *
Xmonnam(struct monst *mtmp)
{
	char *bp = Monnam(mtmp);

	if (!strncmp(bp, "The ", 4)) {
		bp += 2;
		*bp = 'A';
	}
	return (bp);
}

static char *
visctrl(char c)
{
	static char ccc[3];

	if (c < 040) {
		ccc[0] = '^';
		ccc[1] = c + 0100;
		ccc[2] = 0;
	} else {
		ccc[0] = c;
		ccc[1] = 0;
	}
	return (ccc);
}
