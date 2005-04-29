/*
 * DO NOT EDIT
 * $DragonFly: src/usr.bin/make/Attic/directive_hash.c,v 1.2 2005/04/29 03:46:32 okumoto Exp $
 * $DragonFly: src/usr.bin/make/Attic/directive_hash.c,v 1.2 2005/04/29 03:46:32 okumoto Exp $
 * auto-generated from /usr/home/okumoto/Work/make/dfly-src/make/parse.c
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
/*
 * d=2
 * n=69
 * m=33
 * c=2.09
 * maxlen=1
 * minklen=4
 * maxklen=12
 * minchar=46
 * maxchar=95
 * loop=0
 * numiter=8
 * seed=
 */

static const signed char keyword_g[] = {
	-1, 18, 17, 0, -1, -1, -1, -1, 24, 19,
	2, -1, -1, 26, 29, 1, 0, 15, 18, -1,
	-1, 14, 19, 3, -1, 14, -1, 0, 1, -1,
	12, 15, 0, 9, 15, 19, 0, -1, -1, 23,
	-1, 28, -1, 0, -1, 9, -1, -1, -1, 22,
	3, 26, 0, 0, 0, -1, -1, 7, 1, 20,
	-1, 20, -1, 24, -1, 15, -1, 0, 0, 
};

static const u_char keyword_T0[] = {
	8, 30, 55, 61, 14, 13, 48, 1, 18, 12,
	0, 52, 1, 40, 44, 52, 33, 58, 29, 29,
	3, 30, 26, 42, 1, 49, 10, 26, 5, 45,
	65, 13, 6, 22, 45, 61, 7, 25, 62, 65,
	8, 34, 48, 50, 5, 63, 33, 38, 52, 33,
};

static const u_char keyword_T1[] = {
	44, 18, 49, 61, 56, 13, 1, 54, 1, 47,
	46, 17, 22, 36, 25, 66, 14, 36, 58, 51,
	60, 22, 61, 19, 43, 37, 5, 18, 50, 58,
	32, 65, 47, 12, 28, 34, 65, 29, 59, 67,
	48, 36, 15, 41, 44, 11, 39, 29, 18, 68,
};


int
keyword_hash(const u_char *key, size_t len)
{
	unsigned f0, f1;
	const u_char *kp = key;

	if (len < 4 || len > 12)
		return -1;

	for (f0=f1=0; *kp; ++kp) {
		if (*kp < 46 || *kp > 95)
			return -1;
		f0 += keyword_T0[-46 + *kp];
		f1 += keyword_T1[-46 + *kp];
	}

	f0 %= 69;
	f1 %= 69;

	return (keyword_g[f0] + keyword_g[f1]) % 33;
}
