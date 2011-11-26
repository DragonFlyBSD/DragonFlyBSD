/* rnd.c - version 1.0.2 */
/* $FreeBSD: src/games/hack/rnd.c,v 1.5 1999/11/16 10:26:38 marcel Exp $ */
/* $DragonFly: src/games/hack/rnd.c,v 1.3 2006/08/21 19:45:32 pavalos Exp $ */

#include "hack.h"

#define	RND(x)	(random() % x)

int
rn1(int x, int y)
{
	return (RND(x) + y);
}

int
rn2(int x)
{
	return (RND(x));
}

int
rnd(int x)
{
	return (RND(x) + 1);
}

int
d(int n, int x)
{
	int tmp = n;

	while (n--)
		tmp += RND(x);
	return (tmp);
}
