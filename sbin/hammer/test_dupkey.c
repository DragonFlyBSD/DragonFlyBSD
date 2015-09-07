/*
 * This is a really simple stupid standalone program which will find two
 * filenames with the same CRC, used to test the directory iterator.
 *
 * cc -I /usr/src/sys test_dupkey.c /usr/src/sys/libkern/crc32.c -o test_dupkey
 *
 * $DragonFly: src/sbin/hammer/test_dupkey.c,v 1.1 2008/06/26 04:07:57 dillon Exp $
 */

#include "hammer_util.h"

static u_int32_t namekey(const char *name);
static void randomname(char *name);

u_int32_t bitmap[0x80000000U / 32];

int
main(int ac, char **av)
{
	char name[32];
	u_int32_t key;
	u_int32_t *ptr;
	u_int32_t mask;
	u_int32_t count;
	u_int32_t saved;

	srandom(0);	/* reproducable random sequence number */
	count = 0;
	for (;;) {
		randomname(name);
		key = namekey(name);
		ptr = &bitmap[key / 32];
		mask = 1 << (key & 31);
		if (*ptr & mask) {
			break;
		}
		*ptr |= mask;
		++count;
	}
	printf("duplicate found at count %d key %08x\n", count, key);
	printf("'%s' and", name);
	saved = key;

	srandom(0);
	count = 0;
	for (;;) {
		randomname(name);
		key = namekey(name);
		if (saved == key)
			break;
		++count;
	}
	printf(" '%s'\n", name);
}

static
u_int32_t
namekey(const char *name)
{
	u_int32_t key;

	key = crc32(name, strlen(name)) & 0x7FFFFFFF;
	if (key == 0)
		key = 1;
	return(key);
}

static
void
randomname(char *name)
{
	int len = random() % 16 + 8;
	int i;

	for (i = 0; i < len; ++i)
		name[i] = random() % 26 + 'a';
	name[i] = 0;
}


