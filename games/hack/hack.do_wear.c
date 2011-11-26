/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.do_wear.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.do_wear.c,v 1.3 1999/11/16 02:57:03 billf Exp $ */
/* $DragonFly: src/games/hack/hack.do_wear.c,v 1.6 2008/04/20 13:44:24 swildner Exp $ */

#include "hack.h"
extern char quitchars[];

static void off_msg(struct obj *);
static int dorr(struct obj *);
static bool cursed(struct obj *);

static void
off_msg(struct obj *otmp)
{
	pline("You were wearing %s.", doname(otmp));
}

int
doremarm(void)
{
	struct obj *otmp;

	if (!uarm && !uarmh && !uarms && !uarmg) {
		pline("Not wearing any armor.");
		return (0);
	}
	otmp = (!uarmh && !uarms && !uarmg) ? uarm :
	    (!uarms && !uarm && !uarmg) ? uarmh :
	    (!uarmh && !uarm && !uarmg) ? uarms :
	    (!uarmh && !uarm && !uarms) ? uarmg :
	    getobj("[", "take off");
	if (!otmp)
		return (0);
	if (!(otmp->owornmask & (W_ARMOR - W_ARM2))) {
		pline("You can't take that off.");
		return (0);
	}
	if (otmp == uarmg && uwep && uwep->cursed) {	/* myers@uwmacc */
		pline("You seem not able to take off the gloves while holding your weapon.");
		return (0);
	}
	armoroff(otmp);
	return (1);
}

int
doremring(void)
{
	if (!uleft && !uright) {
		pline("Not wearing any ring.");
		return (0);
	}
	if (!uleft)
		return (dorr(uright));
	if (!uright)
		return (dorr(uleft));
	if (uleft && uright)
		for (;;) {
			char answer;

			pline("What ring, Right or Left? [ rl?]");
			if (strchr(quitchars, (answer = readchar())))
				return (0);
			switch (answer) {
			case 'l':
			case 'L':
				return (dorr(uleft));
			case 'r':
			case 'R':
				return (dorr(uright));
			case '?':
				doprring();
				/* might look at morc here %% */
			}
		}
	/* NOTREACHED */
	return (0);
}

static int
dorr(struct obj *otmp)
{
	if (cursed(otmp))
		return (0);
	ringoff(otmp);
	off_msg(otmp);
	return (1);
}

static bool
cursed(struct obj *otmp)
{
	if (otmp->cursed) {
		pline("You can't. It appears to be cursed.");
		return (1);
	}
	return (0);
}

bool
armoroff(struct obj *otmp)
{
	int delay = -objects[otmp->otyp].oc_delay;

	if (cursed(otmp))
		return (0);
	setworn(NULL, otmp->owornmask & W_ARMOR);
	if (delay) {
		nomul(delay);
		switch (otmp->otyp) {
		case HELMET:
			nomovemsg = "You finished taking off your helmet.";
			break;
		case PAIR_OF_GLOVES:
			nomovemsg = "You finished taking off your gloves";
			break;
		default:
			nomovemsg = "You finished taking off your suit.";
		}
	} else
		off_msg(otmp);
	return (1);
}

int
doweararm(void)
{
	struct obj *otmp;
	int delay;
	int err = 0;
	long mask = 0;

	otmp = getobj("[", "wear");
	if (!otmp)
		return (0);
	if (otmp->owornmask & W_ARMOR) {
		pline("You are already wearing that!");
		return (0);
	}
	if (otmp->otyp == HELMET) {
		if (uarmh) {
			pline("You are already wearing a helmet.");
			err++;
		} else
			mask = W_ARMH;
	} else if (otmp->otyp == SHIELD) {
		if (uarms)
			pline("You are already wearing a shield."), err++;
		if (uwep && uwep->otyp == TWO_HANDED_SWORD) {
			pline("You cannot wear a shield and wield a two-handed sword.");
			err++;
		}
		if (!err)
			mask = W_ARMS;
	} else if (otmp->otyp == PAIR_OF_GLOVES) {
		if (uarmg) {
			pline("You are already wearing gloves.");
			err++;
		} else if (uwep && uwep->cursed) {
			pline("You cannot wear gloves over your weapon.");
			err++;
		} else
			mask = W_ARMG;
	} else {
		if (uarm) {
			if (otmp->otyp != ELVEN_CLOAK || uarm2) {
				pline("You are already wearing some armor.");
				err++;
			}
		}
		if (!err)
			mask = W_ARM;
	}
	if (otmp == uwep && uwep->cursed) {
		if (!err++)
			pline("%s is welded to your hand.", Doname(uwep));
	}
	if (err)
		return (0);
	setworn(otmp, mask);
	if (otmp == uwep)
		setuwep(NULL);
	delay = -objects[otmp->otyp].oc_delay;
	if (delay) {
		nomul(delay);
		nomovemsg = "You finished your dressing manoeuvre.";
	}
	otmp->known = 1;
	return (1);
}

int
dowearring(void)
{
	struct obj *otmp;
	long mask = 0;
	long oldprop;

	if (uleft && uright) {
		pline("There are no more ring-fingers to fill.");
		return (0);
	}
	otmp = getobj("=", "wear");
	if (!otmp)
		return (0);
	if (otmp->owornmask & W_RING) {
		pline("You are already wearing that!");
		return (0);
	}
	if (otmp == uleft || otmp == uright) {
		pline("You are already wearing that.");
		return (0);
	}
	if (otmp == uwep && uwep->cursed) {
		pline("%s is welded to your hand.", Doname(uwep));
		return (0);
	}
	if (uleft)
		mask = RIGHT_RING;
	else if (uright)
		mask = LEFT_RING;
	else
		do {
			char answer;

			pline("What ring-finger, Right or Left? ");
			if (strchr(quitchars, (answer = readchar())))
				return (0);
			switch (answer) {
			case 'l':
			case 'L':
				mask = LEFT_RING;
				break;
			case 'r':
			case 'R':
				mask = RIGHT_RING;
				break;
			}
		} while (!mask);
	setworn(otmp, mask);
	if (otmp == uwep)
		setuwep(NULL);
	oldprop = u.uprops[PROP(otmp->otyp)].p_flgs;
	u.uprops[PROP(otmp->otyp)].p_flgs |= mask;
	switch (otmp->otyp) {
	case RIN_LEVITATION:
		if (!oldprop)
			float_up();
		break;
	case RIN_PROTECTION_FROM_SHAPE_CHANGERS:
		rescham();
		break;
	case RIN_GAIN_STRENGTH:
		u.ustr += otmp->spe;
		u.ustrmax += otmp->spe;
		if (u.ustr > 118)
			u.ustr = 118;
		if (u.ustrmax > 118)
			u.ustrmax = 118;
		flags.botl = 1;
		break;
	case RIN_INCREASE_DAMAGE:
		u.udaminc += otmp->spe;
		break;
	}
	prinv(otmp);
	return (1);
}

void
ringoff(struct obj *obj)
{
	long mask;

	mask = obj->owornmask & W_RING;
	setworn(NULL, obj->owornmask);
	if (!(u.uprops[PROP(obj->otyp)].p_flgs & mask))
		impossible("Strange... I didn't know you had that ring.");
	u.uprops[PROP(obj->otyp)].p_flgs &= ~mask;
	switch (obj->otyp) {
	case RIN_FIRE_RESISTANCE:
		/* Bad luck if the player is in hell... --jgm */
		if (!Fire_resistance && dlevel >= 30) {
			pline("The flames of Hell burn you to a crisp.");
			killer = "stupidity in hell";
			done("burned");
		}
		break;
	case RIN_LEVITATION:
		if (!Levitation)	/* no longer floating */
			float_down();
		break;
	case RIN_GAIN_STRENGTH:
		u.ustr -= obj->spe;
		u.ustrmax -= obj->spe;
		if (u.ustr > 118)
			u.ustr = 118;
		if (u.ustrmax > 118)
			u.ustrmax = 118;
		flags.botl = 1;
		break;
	case RIN_INCREASE_DAMAGE:
		u.udaminc -= obj->spe;
		break;
	}
}

void
find_ac(void)
{
	int uac = 10;

	if (uarm)
		uac -= ARM_BONUS(uarm);
	if (uarm2)
		uac -= ARM_BONUS(uarm2);
	if (uarmh)
		uac -= ARM_BONUS(uarmh);
	if (uarms)
		uac -= ARM_BONUS(uarms);
	if (uarmg)
		uac -= ARM_BONUS(uarmg);
	if (uleft && uleft->otyp == RIN_PROTECTION)
		uac -= uleft->spe;
	if (uright && uright->otyp == RIN_PROTECTION)
		uac -= uright->spe;
	if (uac != u.uac) {
		u.uac = uac;
		flags.botl = 1;
	}
}

void
glibr(void)
{
	struct obj *otmp;
	int xfl = 0;

	if (!uarmg)
		if (uleft || uright) {
			/* Note: at present also cursed rings fall off */
			pline("Your %s off your fingers.",
			    (uleft && uright) ? "rings slip" : "ring slips");
			xfl++;
			if ((otmp = uleft) != NULL) {
				ringoff(uleft);
				dropx(otmp);
			}
			if ((otmp = uright) != NULL) {
				ringoff(uright);
				dropx(otmp);
			}
		}
	if ((otmp = uwep) != NULL) {
		/* Note: at present also cursed weapons fall */
		setuwep(NULL);
		dropx(otmp);
		pline("Your weapon %sslips from your hands.",
		      xfl ? "also " : "");
	}
}

struct obj *
some_armor(void)
{
	struct obj *otmph = uarm;

	if (uarmh && (!otmph || !rn2(4)))
		otmph = uarmh;
	if (uarmg && (!otmph || !rn2(4)))
		otmph = uarmg;
	if (uarms && (!otmph || !rn2(4)))
		otmph = uarms;
	return (otmph);
}

void
corrode_armor(void)
{
	struct obj *otmph = some_armor();

	if (otmph) {
		if (otmph->rustfree ||
		    otmph->otyp == ELVEN_CLOAK ||
		    otmph->otyp == LEATHER_ARMOR ||
		    otmph->otyp == STUDDED_LEATHER_ARMOR) {
			pline("Your %s not affected!",
			      aobjnam(otmph, "are"));
			return;
		}
		pline("Your %s!", aobjnam(otmph, "corrode"));
		otmph->spe--;
	}
}
