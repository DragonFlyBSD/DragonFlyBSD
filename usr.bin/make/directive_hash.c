/*
 * DO NOT EDIT
 * $DragonFly: src/usr.bin/make/Attic/directive_hash.c,v 1.1 2005/04/15 21:13:05 okumoto Exp $
 * auto-generated from
 * DragonFly: src/usr.bin/make/parse.c,v 1.71 2005/04/15 21:09:15 okumoto Exp 
 * DO NOT EDIT
 */
#include <sys/types.h>

#include "directive_hash.h"

/*
 * d=2
 * n=40
 * m=19
 * c=2.09
 * maxlen=1
 * minklen=2
 * maxklen=9
 * minchar=97
 * maxchar=119
 * loop=0
 * numiter=1
 * seed=
 */

static const signed char directive_g[] = {
	8, 0, 0, 5, 6, -1, 17, 15, 10, 6,
	-1, -1, 0, 0, 2, -1, 16, 2, 3, 0,
	7, -1, -1, -1, 0, 14, -1, -1, 11, 7,
	-1, -1, 0, -1, 0, 0, 17, 0, -1, 1,
};

static const u_char directive_T0[] = {
	26, 14, 19, 35, 10, 34, 18, 27, 1, 17,
	22, 37, 12, 12, 36, 21, 0, 6, 1, 25,
	9, 4, 19, 
};

static const u_char directive_T1[] = {
	25, 22, 19, 0, 2, 18, 33, 18, 30, 4,
	30, 9, 21, 19, 16, 12, 35, 34, 4, 19,
	9, 33, 16, 
};


int
directive_hash(const u_char *key, size_t len)
{
	unsigned f0, f1;
	const u_char *kp = key;

	if (len < 2 || len > 9)
		return -1;

	for (f0=f1=0; kp < key + len; ++kp) {
		if (*kp < 97 || *kp > 119)
			return -1;
		f0 += directive_T0[-97 + *kp];
		f1 += directive_T1[-97 + *kp];
	}

	f0 %= 40;
	f1 %= 40;

	return (directive_g[f0] + directive_g[f1]) % 19;
}
