/*
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Arc4 random number generator for OpenBSD.
 *
 * This code is derived from section 17.1 of Applied Cryptography,
 * second edition, which describes a stream cipher allegedly
 * compatible with RSA Labs "RC4" cipher (the actual description of
 * which is a trade secret).  The same algorithm is used as a stream
 * cipher called "arcfour" in Tatu Ylonen's ssh package.
 *
 * Here the stream cipher has been modified always to include the time
 * when initializing the state.  That makes it impossible to
 * regenerate the same random sequence twice, so this can't be used
 * for encryption, but will generate good random numbers.
 *
 * RC4 is a registered trademark of RSA Laboratories.
 *
 * $FreeBSD: src/lib/libc/gen/arc4random.c,v 1.25 2008/09/09 09:46:36 ache Exp $
 * $DragonFly: src/lib/libc/gen/arc4random.c,v 1.7 2005/11/13 00:07:42 swildner Exp $
 */

#include "namespace.h"
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "libc_private.h"
#include "un-namespace.h"

/*
 * Misc constants
 */
#define	RANDOMDEV	"/dev/random"
#define KEYSIZE		128
#define	THREAD_LOCK()						\
	do {							\
		if (__isthreaded)				\
			_pthread_mutex_lock(&arc4random_mtx);	\
	} while (0)

#define	THREAD_UNLOCK()						\
	do {							\
		if (__isthreaded)				\
			_pthread_mutex_unlock(&arc4random_mtx);	\
	} while (0)


struct arc4_stream {
	u_int8_t i;
	u_int8_t j;
	u_int8_t s[KEYSIZE * 2];
};

static pthread_mutex_t	arc4random_mtx = PTHREAD_MUTEX_INITIALIZER;

static struct arc4_stream rs;
static int rs_initialized;
static int rs_stired;
static int arc4_count;

static u_int8_t arc4_getbyte(void);
static void arc4_stir(void);

static inline void
arc4_init(void)
{
	int     n;

	for (n = 0; n < KEYSIZE * 2; n++)
		rs.s[n] = n;
	rs.i = 0;
	rs.j = 0;
}

static inline void
arc4_addrandom(u_char *dat, size_t datlen)
{
	size_t n;
	u_int8_t si;

	rs.i--;
	for (n = 0; n < KEYSIZE * 2; n++) {
		rs.i = (rs.i + 1);
		si = rs.s[rs.i];
		rs.j = (rs.j + si + dat[n % datlen]);
		rs.s[rs.i] = rs.s[rs.j];
		rs.s[rs.j] = si;
	}
	rs.j = rs.i;
}

struct pray {
	struct timeval	tv;
	pid_t 		pid;
};

static void
arc4_stir(void)
{
	u_int8_t rnd[KEYSIZE*2];
	size_t n;
	int fd;

	/*
	 * NOTE: Don't assume that the garbage on the stack is actually
	 *	 random.
	 */
	n = 0;
	fd = _open(RANDOMDEV, O_RDONLY, 0);
	if (fd >= 0) {
		n = _read(fd, rnd, sizeof(rnd));
		_close(fd);
		if ((ssize_t)n < 0)
			n = 0;
	}

	/*
	 * Align for added entropy, sysctl back-off for chroots that might
	 * not have access to /dev/random.
	 */
	n = n & ~15;	/* align for added entropy */
	if (n < sizeof(rnd)) {
		size_t r = sizeof(rnd) - n;
		if (sysctlbyname("kern.random", rnd + n, &r, NULL, 0) == 0)
			n += r;
	}

	/*
	 * Pray if this code ever gets triggered.
	 */
	n = n & ~15;
	if (n <= sizeof(rnd) - sizeof(struct pray)) {
		struct pray *pray = (void *)(rnd + n);
		gettimeofday(&pray->tv, NULL);
		pray->pid = getpid();
		n += sizeof(struct pray);
	}
	arc4_addrandom((u_char *)rnd, n);

	/*
	 * Throw away the first N bytes of output, as suggested in the
	 * paper "Weaknesses in the Key Scheduling Algorithm of RC4"
	 * by Fluher, Mantin, and Shamir.  N=1024 is based on
	 * suggestions in the paper "(Not So) Random Shuffles of RC4"
	 * by Ilya Mironov.
	 */
	for (n = 0; n < 1024; n++)
		arc4_getbyte();

	/*
	 * Theoretically we can set arc4_count to 1600000.  Realistically,
	 * it makes no sense to use a number that high.  Use something
	 * reasonable.
	 */
	arc4_count = 65539;
}

static u_int8_t
arc4_getbyte(void)
{
	u_int8_t si, sj;

	rs.i = (rs.i + 1);
	si = rs.s[rs.i];
	rs.j = (rs.j + si);
	sj = rs.s[rs.j];
	rs.s[rs.i] = sj;
	rs.s[rs.j] = si;

	return (rs.s[(si + sj) & 0xff]);
}

static u_int32_t
arc4_getword(void)
{
	u_int32_t val;

	val = arc4_getbyte() << 24;
	val |= arc4_getbyte() << 16;
	val |= arc4_getbyte() << 8;
	val |= arc4_getbyte();

	return (val);
}

static void
arc4_check_init(void)
{
	if (!rs_initialized) {
		arc4_init();
		rs_initialized = 1;
	}
}

static inline void
arc4_check_stir(void)
{
	if (!rs_stired || arc4_count <= 0) {
		arc4_stir();
		rs_stired = 1;
	}
}

void
arc4random_stir(void)
{
	THREAD_LOCK();
	arc4_check_init();
	arc4_stir();
	rs_stired = 1;
	THREAD_UNLOCK();
}

void
arc4random_addrandom(uint8_t *dat, size_t datlen)
{
	THREAD_LOCK();
	arc4_check_init();
	arc4_check_stir();
	arc4_addrandom(dat, datlen);
	THREAD_UNLOCK();
}

u_int32_t
arc4random(void)
{
	u_int32_t rnd;

	THREAD_LOCK();
	arc4_check_init();
	arc4_check_stir();
	rnd = arc4_getword();
	arc4_count -= 4;
	THREAD_UNLOCK();

	return (rnd);
}

void
arc4random_buf(void *_buf, size_t n)
{
	u_char *buf = (u_char *)_buf;

	THREAD_LOCK();
	arc4_check_init();
	while (n--) {
		arc4_check_stir();
		buf[n] = arc4_getbyte();
		arc4_count--;
	}
	THREAD_UNLOCK();
}

/*
 * Calculate a uniformly distributed random number less than upper_bound
 * avoiding "modulo bias".
 *
 * Uniformity is achieved by generating new random numbers until the one
 * returned is outside the range [0, 2**32 % upper_bound).  This
 * guarantees the selected random number will be inside
 * [2**32 % upper_bound, 2**32) which maps back to [0, upper_bound)
 * after reduction modulo upper_bound.
 */
u_int32_t
arc4random_uniform(u_int32_t upper_bound)
{
	u_int32_t r, min;

	if (upper_bound < 2)
		return (0);

#if (ULONG_MAX > 0xffffffffUL)
	min = 0x100000000UL % upper_bound;
#else
	/* Calculate (2**32 % upper_bound) avoiding 64-bit math */
	if (upper_bound > 0x80000000)
		min = 1 + ~upper_bound;		/* 2**32 - upper_bound */
	else {
		/* (2**32 - (x * 2)) % x == 2**32 % x when x <= 2**31 */
		min = ((0xffffffff - (upper_bound * 2)) + 1) % upper_bound;
	}
#endif

	/*
	 * This could theoretically loop forever but each retry has
	 * p > 0.5 (worst case, usually far better) of selecting a
	 * number inside the range we need, so it should rarely need
	 * to re-roll.
	 */
	for (;;) {
		r = arc4random();
		if (r >= min)
			break;
	}

	return (r % upper_bound);
}

#if 0
/*-------- Test code for i386 --------*/
#include <stdio.h>
#include <machine/pctr.h>
int
main(int argc, char **argv)
{
	const int iter = 1000000;
	int     i;
	pctrval v;

	v = rdtsc();
	for (i = 0; i < iter; i++)
		arc4random();
	v = rdtsc() - v;
	v /= iter;

	printf("%qd cycles\n", v);
}
#endif
