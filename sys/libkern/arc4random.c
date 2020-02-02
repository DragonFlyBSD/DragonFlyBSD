/*-
 * THE BEER-WARE LICENSE
 *
 * <dan@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff.  If we meet some day, and you
 * think this stuff is worth it, you can buy me a beer in return.
 *
 * Dan Moschuk
 *
 * $FreeBSD: src/sys/libkern/arc4random.c,v 1.3.2.2 2001/09/17 07:06:50 silby Exp $
 * $DragonFly: src/sys/libkern/arc4random.c,v 1.3 2006/09/03 17:31:55 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/random.h>
#include <sys/libkern.h>
#include <sys/time.h>

#include <vm/vm_extern.h>

#define	ARC4_MAXRUNS		16384
#define	ARC4_RESEED_SECONDS	300
#define	ARC4_KEYBYTES		32	/* 256 bit key */

struct arc4_data {
	uint8_t			arc4_i;
	uint8_t			arc4_j;
	int			arc4_numruns;
	time_t			arc4_nextreseed;
	uint8_t			arc4_sbox[256];
};

static struct arc4_data		*arc4_data_pcpu[MAXCPU];

static uint8_t			arc4_randbyte(struct arc4_data *);

static __inline void
arc4_swap(uint8_t *a, uint8_t *b)
{
	uint8_t c;

	c = *a;
	*a = *b;
	*b = c;
}	

/*
 * Stir our S-box.
 */
static void
arc4_randomstir(struct arc4_data *d)
{
	uint8_t key[256];
	int r, n;

	/*
	 * XXX read_random() returns unsafe numbers if the entropy
	 * device is not loaded -- MarkM.
	 */
	r = read_random(key, ARC4_KEYBYTES, 1);
	/* If r == 0 || -1, just use what was on the stack. */
	if (r > 0) {
		for (n = r; n < sizeof(key); n++)
			key[n] = key[n % r];
	}

	for (n = 0; n < 256; n++) {
		d->arc4_j = (d->arc4_j + d->arc4_sbox[n] + key[n]) % 256;
		arc4_swap(&d->arc4_sbox[n], &d->arc4_sbox[d->arc4_j]);
	}

	/*
	 * Discard early keystream, as per recommendations in:
	 * "(Not So) Random Shuffles of RC4" by Ilya Mironov.
	 */
	for (n = 0; n < 768 * 4; n++)
		arc4_randbyte(d);

	/* Reset for next reseed cycle. */
	d->arc4_nextreseed = time_uptime + ARC4_RESEED_SECONDS;
	d->arc4_numruns = 0;
}

/*
 * Generate a random byte.
 */
static uint8_t
arc4_randbyte(struct arc4_data *d)
{
	uint8_t arc4_t;

	d->arc4_i = (d->arc4_i + 1) % 256;
	d->arc4_j = (d->arc4_j + d->arc4_sbox[d->arc4_i]) % 256;

	arc4_swap(&d->arc4_sbox[d->arc4_i], &d->arc4_sbox[d->arc4_j]);

	arc4_t = (d->arc4_sbox[d->arc4_i] + d->arc4_sbox[d->arc4_j]) % 256;
	return d->arc4_sbox[arc4_t];
}

uint32_t
karc4random(void)
{
	struct arc4_data *d = arc4_data_pcpu[mycpuid];
	uint32_t ret;

	if (++(d->arc4_numruns) > ARC4_MAXRUNS ||
	    time_uptime > d->arc4_nextreseed)
		arc4_randomstir(d);

	ret = arc4_randbyte(d);
	ret |= arc4_randbyte(d) << 8;
	ret |= arc4_randbyte(d) << 16;
	ret |= arc4_randbyte(d) << 24;

	return ret;
}

uint64_t
karc4random64(void)
{
	struct arc4_data *d = arc4_data_pcpu[mycpuid];
	uint64_t ret;

	if (++(d->arc4_numruns) > ARC4_MAXRUNS ||
	    time_uptime > d->arc4_nextreseed)
		arc4_randomstir(d);

	ret = arc4_randbyte(d);
	ret |= arc4_randbyte(d) << 8;
	ret |= arc4_randbyte(d) << 16;
	ret |= arc4_randbyte(d) << 24;
	ret |= (uint64_t)arc4_randbyte(d) << 32;
	ret |= (uint64_t)arc4_randbyte(d) << 40;
	ret |= (uint64_t)arc4_randbyte(d) << 48;
	ret |= (uint64_t)arc4_randbyte(d) << 56;

	return ret;
}

void
karc4rand(void *ptr, size_t len)
{
	struct arc4_data *d = arc4_data_pcpu[mycpuid];
	uint8_t *p = ptr;

#if 0
	/* No one call this function in ISR/ithread. */
	crit_enter();
#endif

	if (++(d->arc4_numruns) > ARC4_MAXRUNS ||
	    time_uptime > d->arc4_nextreseed)
		arc4_randomstir(d);

	while (len--)
		*p++ = arc4_randbyte(d);

#if 0
	crit_exit();
#endif
}

/*
 * Initialize our S-box to its beginning defaults.
 */
void
arc4_init_pcpu(int cpuid)
{
	struct arc4_data *d;
	int n;

	KASSERT(arc4_data_pcpu[cpuid] == NULL,
	    ("arc4 was initialized on cpu%d", cpuid));

	d = (void *)kmem_alloc3(&kernel_map, sizeof(*d), VM_SUBSYS_GD,
	    KM_CPU(cpuid));
	memset(d, 0, sizeof(*d));

	for (n = 0; n < 256; n++)
		d->arc4_sbox[n] = (uint8_t)n;

	arc4_randomstir(d);

	/*
	 * Discard early keystream, as per recommendations in:
	 * "(Not So) Random Shuffles of RC4" by Ilya Mironov.
	 */
	for (n = 0; n < 768 * 4; n++)
		arc4_randbyte(d);

	arc4_data_pcpu[cpuid] = d;
}
