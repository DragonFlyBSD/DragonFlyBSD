/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.mkobj.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.mkobj.c,v 1.5 1999/11/16 10:26:37 marcel Exp $ */
/* $DragonFly: src/games/hack/hack.mkobj.c,v 1.4 2006/08/21 19:45:32 pavalos Exp $ */

#include "hack.h"

char mkobjstr[] = "))[[!!!!????%%%%/=**))[[!!!!????%%%%/=**(%";

struct obj *
mkobj_at(int let, int x, int y)
{
	struct obj *otmp = mkobj(let);

	otmp->ox = x;
	otmp->oy = y;
	otmp->nobj = fobj;
	fobj = otmp;
	return (otmp);
}

void
mksobj_at(int otyp, int x, int y)
{
	struct obj *otmp = mksobj(otyp);

	otmp->ox = x;
	otmp->oy = y;
	otmp->nobj = fobj;
	fobj = otmp;
}

struct obj *
mkobj(int let)
{
	if (!let)
		let = mkobjstr[rn2(sizeof(mkobjstr) - 1)];
	return (
		mksobj(
		       letter(let) ?
		       CORPSE +
		       ((let > 'Z') ? (let - 'a' + 'Z' - '@' +
				       1) : (let - '@'))
		       :   probtype(let)
		       )
		);
}

struct obj zeroobj;

struct obj *
mksobj(int otyp)
{
	struct obj *otmp;
	char let = objects[otyp].oc_olet;

	otmp = newobj(0);
	*otmp = zeroobj;
	otmp->age = moves;
	otmp->o_id = flags.ident++;
	otmp->quan = 1;
	otmp->olet = let;
	otmp->otyp = otyp;
	otmp->dknown = strchr("/=!?*", let) ? 0 : 1;
	switch (let) {
	case WEAPON_SYM:
		otmp->quan = (otmp->otyp <= ROCK) ? rn1(6, 6) : 1;
		if (!rn2(11))
			otmp->spe = rnd(3);
		else if (!rn2(10)) {
			otmp->cursed = 1;
			otmp->spe = -rnd(3);
		}
		break;
	case FOOD_SYM:
		if (otmp->otyp >= CORPSE)
			break;
#ifdef NOT_YET_IMPLEMENTED
		/* if tins are to be identified, need to adapt doname() etc */
		if (otmp->otyp == TIN)
			otmp->spe = rnd(...);
#endif /* NOT_YET_IMPLEMENTED */
		/* FALLTHROUGH */
	case GEM_SYM:
		otmp->quan = rn2(6) ? 1 : 2;
	case TOOL_SYM:
	case CHAIN_SYM:
	case BALL_SYM:
	case ROCK_SYM:
	case POTION_SYM:
	case SCROLL_SYM:
	case AMULET_SYM:
		break;
	case ARMOR_SYM:
		if (!rn2(8))
			otmp->cursed = 1;
		if (!rn2(10))
			otmp->spe = rnd(3);
		else if (!rn2(9)) {
			otmp->spe = -rnd(3);
			otmp->cursed = 1;
		}
		break;
	case WAND_SYM:
		if (otmp->otyp == WAN_WISHING)
			otmp->spe = 3;
		else
			otmp->spe = rn1(5,
			    (objects[otmp->otyp].bits & NODIR) ? 11 : 4);
		break;
	case RING_SYM:
		if (objects[otmp->otyp].bits & SPEC) {
			if (!rn2(3)) {
				otmp->cursed = 1;
				otmp->spe = -rnd(2);
			} else
				otmp->spe = rnd(2);
		} else if (otmp->otyp == RIN_TELEPORTATION ||
			   otmp->otyp == RIN_AGGRAVATE_MONSTER ||
			   otmp->otyp == RIN_HUNGER || !rn2(9))
			otmp->cursed = 1;
		break;
	default:
		panic("impossible mkobj");
	}
	otmp->owt = weight(otmp);
	return (otmp);
}

bool
letter(char c)
{
	return (('@' <= c && c <= 'Z') || ('a' <= c && c <= 'z'));
}

int
weight(struct obj *obj)
{
	int wt = objects[obj->otyp].oc_weight;
	return (wt ? wt * obj->quan : (obj->quan + 1) / 2);
}

void
mkgold(long num, int x, int y)
{
	struct gold *gold;
	long amount = (num ? num : 1 + (rnd(dlevel + 2) * rnd(30)));

	if ((gold = g_at(x, y)) != NULL)
		gold->amount += amount;
	else {
		gold = newgold();
		gold->ngold = fgold;
		gold->gx = x;
		gold->gy = y;
		gold->amount = amount;
		fgold = gold;
		/* do sth with display? */
	}
}
