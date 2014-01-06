/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.rumors.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.rumors.c,v 1.3 1999/11/16 02:57:10 billf Exp $ */

#include "hack.h"			/* for RUMORFILE and BSD (index) */
#define	CHARSZ	8			/* number of bits in a char */
int n_rumors = 0;
int n_used_rumors = -1;
char *usedbits;

static void init_rumors(FILE *);
static bool skipline(FILE *);
static void outline(FILE *);
static bool used(int);

static void
init_rumors(FILE *rumf)
{
	int i;

	n_used_rumors = 0;
	while (skipline(rumf))
		n_rumors++;
	rewind(rumf);
	i = n_rumors / CHARSZ;
	usedbits = alloc((unsigned)(i + 1));
	for (; i >= 0; i--)
		usedbits[i] = 0;
}

static bool
skipline(FILE *rumf)
{
	char line[COLNO];

	for (;;) {
		if (!fgets(line, sizeof(line), rumf))
			return (0);
		if (strchr(line, '\n'))
			return (1);
	}
}

static void
outline(FILE *rumf)
{
	char line[COLNO];
	char *ep;

	if (!fgets(line, sizeof(line), rumf))
		return;
	if ((ep = strchr(line, '\n')) != NULL)
		*ep = 0;
	pline("This cookie has a scrap of paper inside! It reads: ");
	pline("%s", line);
}

void
outrumor(void)
{
	int rn, i;
	FILE *rumf;

	if (n_rumors <= n_used_rumors ||
	    (rumf = fopen(RUMORFILE, "r")) == NULL)
		return;
	if (n_used_rumors < 0)
		init_rumors(rumf);
	if (!n_rumors)
		goto none;
	rn = rn2(n_rumors - n_used_rumors);
	i = 0;
	while (rn || used(i)) {
		skipline(rumf);
		if (!used(i))
			rn--;
		i++;
	}
	usedbits[i / CHARSZ] |= (1 << (i % CHARSZ));
	n_used_rumors++;
	outline(rumf);
none:
	fclose(rumf);
}

static bool
used(int i)
{
	return (usedbits[i / CHARSZ] & (1 << (i % CHARSZ)));
}
