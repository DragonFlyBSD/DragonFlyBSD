/*
 * kern_random.c -- A strong random number generator
 *
 * $FreeBSD: src/sys/kern/kern_random.c,v 1.36.2.4 2002/09/17 17:11:57 sam Exp $
 * $DragonFly: src/sys/kern/Attic/kern_random.c,v 1.10.2.2 2006/04/18 18:31:40 dillon Exp $
 *
 * Version 0.95, last modified 18-Oct-95
 * 
 * Copyright Theodore Ts'o, 1994, 1995.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 * 
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/md5.h>
#include <sys/poll.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/systm.h>
#include <sys/systimer.h>
#include <sys/thread2.h>
#include <sys/lock.h>
#include <machine/clock.h>

#ifdef __i386__
#include <i386/icu/icu.h>
#endif

#define MAX_BLKDEV 4

/*
 * The pool is stirred with a primitive polynomial of degree 128
 * over GF(2), namely x^128 + x^99 + x^59 + x^31 + x^9 + x^7 + 1.
 * For a pool of size 64, try x^64+x^62+x^38+x^10+x^6+x+1.
 */
#define POOLWORDS 128    /* Power of 2 - note that this is 32-bit words */
#define POOLBITS (POOLWORDS*32)

#if POOLWORDS == 128
#define TAP1    99     /* The polynomial taps */
#define TAP2    59
#define TAP3    31
#define TAP4    9
#define TAP5    7
#elif POOLWORDS == 64
#define TAP1    62      /* The polynomial taps */
#define TAP2    38
#define TAP3    10
#define TAP4    6
#define TAP5    1
#else
#error No primitive polynomial available for chosen POOLWORDS
#endif

#define WRITEBUFFER	512 /* size in bytes */

/*
 * This is the number of bits represented by entropy_count.  A value of
 * 8 would represent an ultra-conservative value, while a value of 2
 * would be ultra-liberal.  Use a value of 4.
 */
#define STATE_BITS	4
#define MIN_BITS	(STATE_BITS*4)

/* There is actually only one of these, globally. */
struct random_bucket {
	u_int	add_ptr;
	u_int	entropy_count;
	int	input_rotate;
	u_int32_t *pool;
	struct	selinfo rsel;
};

/* There is one of these per entropy source */
struct timer_rand_state {
	u_long	last_time;
	int 	last_delta;
	int 	nbits;
};

static struct random_bucket random_state;
static u_int32_t random_pool[POOLWORDS];
static struct random_bucket urandom_state;
static u_int32_t urandom_pool[POOLWORDS];
static struct timer_rand_state keyboard_timer_state;
static struct timer_rand_state keyboard_utimer_state;
static struct timer_rand_state extract_timer_state;
static struct timer_rand_state irq_timer_state[MAX_INTS];
static struct timer_rand_state irq_utimer_state[MAX_INTS];
#ifdef notyet
static struct timer_rand_state blkdev_timer_state[MAX_BLKDEV];
#endif
static thread_t rand_td;
static int	rand_td_slot;

static void add_timer_randomness(struct random_bucket *r, 
				 struct timer_rand_state *state, u_int num);

/*
 * Called from early boot
 */
void
rand_initialize(void)
{
	random_state.pool = random_pool;
	urandom_state.pool = urandom_pool;
}

/*
 * Random number generator helper thread.
 *
 * Note that rand_td_slot is initially 0, which means nothing will try
 * to schedule our thread until we reset it to -1.  This also prevents
 * any attempt to schedule the thread before it has been initialized.
 */
static
void
rand_thread_loop(void *dummy)
{
	int slot;
	int count;

	for (;;) {
		if ((slot = rand_td_slot) >= 0) {
			add_timer_randomness(&urandom_state,
					     &irq_timer_state[slot],
					     slot);
			add_timer_randomness(&random_state,
					     &irq_utimer_state[slot],
					     slot);
		}

		/*
		 * The fewer bits we have, the shorter we sleep, up to a
		 * point.  We use an interrupt to trigger the thread once
		 * we have slept the calculated amount of time.  This limits
		 * the wakeup rate AND gives us a good interrupt-based
		 * timer value at the same time.
		 *
		 * Use /dev/random's entropy count rather than /dev/urandom's.
		 * Things like the stacksmash code use /dev/urandom on program
		 * exec all the time, and it is not likely to ever have
		 * good entropy.
		 */
		count = random_state.entropy_count * hz / POOLBITS;
		if (count < hz / 25)
			count = hz / 25;
		if (count > hz)
			count = hz;
		tsleep(rand_td, 0, "rwait", count);
		lwkt_deschedule_self(rand_td);
		rand_td_slot = -1;
		lwkt_switch();
	}
}

static
void
rand_thread_init(void)
{
	lwkt_create(rand_thread_loop, NULL, &rand_td, NULL, 0, 0, "random");
}

SYSINIT(rand, SI_SUB_HELPER_THREADS, SI_ORDER_ANY, rand_thread_init, 0);

/*
 * This function adds an int into the entropy "pool".  It does not
 * update the entropy estimate.  The caller must do this if appropriate.
 *
 * The pool is stirred with a primitive polynomial of degree 128
 * over GF(2), namely x^128 + x^99 + x^59 + x^31 + x^9 + x^7 + 1.
 * For a pool of size 64, try x^64+x^62+x^38+x^10+x^6+x+1.
 * 
 * We rotate the input word by a changing number of bits, to help
 * assure that all bits in the entropy get toggled.  Otherwise, if we
 * consistently feed the entropy pool small numbers (like ticks and
 * scancodes, for example), the upper bits of the entropy pool don't
 * get affected. --- TYT, 10/11/95
 */
static __inline void
add_entropy_word(struct random_bucket *r, const u_int32_t input)
{
	u_int i;
	u_int32_t w;

	w = (input << r->input_rotate) | (input >> (32 - r->input_rotate));
	i = r->add_ptr = (r->add_ptr - 1) & (POOLWORDS-1);
	if (i)
		r->input_rotate = (r->input_rotate + 7) & 31;
	else
		/*
		 * At the beginning of the pool, add an extra 7 bits
		 * rotation, so that successive passes spread the
		 * input bits across the pool evenly.
		 */
		r->input_rotate = (r->input_rotate + 14) & 31;

	/* XOR in the various taps */
	w ^= r->pool[(i+TAP1)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP2)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP3)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP4)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP5)&(POOLWORDS-1)];
	w ^= r->pool[i];
	/* Rotate w left 1 bit (stolen from SHA) and store */
	r->pool[i] = (w << 1) | (w >> 31);
}

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays.  It uses the timer_rand_state structure to make an estimate
 * of how  any bits of entropy this call has added to the pool.
 *
 * The number "num" is also added to the pool - it should somehow describe
 * the type of event which just happened.  This is currently 0-255 for
 * keyboard scan codes, and 256 upwards for interrupts.
 * On the i386, this is assumed to be at most 16 bits, and the high bits
 * are used for a high-resolution timer.
 */
static void
add_timer_randomness(struct random_bucket *r, struct timer_rand_state *state,
		     u_int num)
{
	int		delta, delta2;
	u_int		nbits;
	u_int32_t	time;
	int		count;

	num ^= sys_cputimer->count() << 16;
	if (tsc_present)
		num ^= ~(u_int)rdtsc();
	count = r->entropy_count + 2;
	if (count > POOLBITS)
		count = POOLBITS;
	r->entropy_count = count;
		
	time = ticks;

	add_entropy_word(r, (u_int32_t) num);
	add_entropy_word(r, time);

	/*
	 * Calculate number of bits of randomness we probably
	 * added.  We take into account the first and second order
	 * deltas in order to make our estimate.
	 */
	delta = time - state->last_time;
	state->last_time = time;

	delta2 = delta - state->last_delta;
	state->last_delta = delta;

	if (delta < 0) delta = -delta;
	if (delta2 < 0) delta2 = -delta2;
	delta = MIN(delta, delta2) >> 1;
	for (nbits = 0; delta; nbits++)
		delta >>= 1;

	/* Prevent overflow */
	count = r->entropy_count + nbits;
	if (count > POOLBITS)
		count = POOLBITS;
	cpu_sfence();
	r->entropy_count = count;

	if (count >= MIN_BITS && try_mplock()) {
		selwakeup(&r->rsel);
		rel_mplock();
	}
}

void
add_keyboard_randomness(u_char scancode)
{
	add_timer_randomness(&random_state, &keyboard_timer_state, scancode);
	add_timer_randomness(&urandom_state, &keyboard_utimer_state, scancode);
}

/*
 * This routine is called from an interrupt and must be very efficient.
 */
void
add_interrupt_randomness(int intr)
{
	if (rand_td_slot < 0) {
		rand_td_slot = intr;
		lwkt_schedule(rand_td);
	}
}

#ifdef notused
void
add_blkdev_randomness(int major)
{
	if (major >= MAX_BLKDEV)
		return;

	add_timer_randomness(&random_state, &blkdev_timer_state[major],
			     0x200+major);
}
#endif /* notused */

#if POOLWORDS % 16
#error extract_entropy() assumes that POOLWORDS is a multiple of 16 words.
#endif
/*
 * This function extracts randomness from the "entropy pool", and
 * returns it in a buffer.  This function computes how many remaining
 * bits of entropy are left in the pool, but it does not restrict the
 * number of bytes that are actually obtained.
 */
static __inline int
extract_entropy(struct random_bucket *r, char *buf, int nbytes)
{
	int ret, i;
	u_int32_t tmp[4];
	
	add_timer_randomness(r, &extract_timer_state, nbytes);
	
	/* Redundant, but just in case... */
	if (r->entropy_count > POOLBITS) 
		r->entropy_count = POOLBITS;
	/* Why is this here?  Left in from Ted Ts'o.  Perhaps to limit time. */
	if (nbytes > 32768)
		nbytes = 32768;

	ret = nbytes;
	if (r->entropy_count > nbytes * STATE_BITS)
		r->entropy_count -= nbytes * STATE_BITS;
	else
		r->entropy_count = 0;

	while (nbytes) {
		/* Hash the pool to get the output */
		tmp[0] = 0x67452301;
		tmp[1] = 0xefcdab89;
		tmp[2] = 0x98badcfe;
		tmp[3] = 0x10325476;
		for (i = 0; i < POOLWORDS; i += 16)
			MD5Transform(tmp, (char *)(r->pool+i));
		/* Modify pool so next hash will produce different results */
		add_entropy_word(r, tmp[0]);
		add_entropy_word(r, tmp[1]);
		add_entropy_word(r, tmp[2]);
		add_entropy_word(r, tmp[3]);
		/*
		 * Run the MD5 Transform one more time, since we want
		 * to add at least minimal obscuring of the inputs to
		 * add_entropy_word().  --- TYT
		 */
		MD5Transform(tmp, (char *)(r->pool));
		
		/* Copy data to destination buffer */
		i = MIN(nbytes, 16);
		bcopy(tmp, buf, i);
		nbytes -= i;
		buf += i;
	}

	/* Wipe data from memory */
	bzero(tmp, sizeof(tmp));
	
	return ret;
}

#ifdef notused /* XXX NOT the exported kernel interface */
/*
 * This function is the exported kernel interface.  It returns some
 * number of good random numbers, suitable for seeding TCP sequence
 * numbers, etc.
 */
void
get_random_bytes(void *buf, u_int nbytes)
{
	extract_entropy(&random_state, (char *) buf, nbytes);
}
#endif /* notused */

u_int
read_random(void *buf, u_int nbytes)
{
	if (random_state.entropy_count < MIN_BITS)
		return 0;
	if ((nbytes * STATE_BITS) > random_state.entropy_count)
		nbytes = random_state.entropy_count / STATE_BITS;
	
	return extract_entropy(&random_state, (char *)buf, nbytes);
}

u_int
read_random_unlimited(void *buf, u_int nbytes)
{
	return extract_entropy(&urandom_state, (char *)buf, nbytes);
}

#ifdef notused
u_int
write_random(const char *buf, u_int nbytes)
{
	u_int i;
	u_int32_t word, *p;

	for (i = nbytes, p = (u_int32_t *)buf;
	     i >= sizeof(u_int32_t);
	     i-= sizeof(u_int32_t), p++)
		add_entropy_word(&random_state, *p);
	if (i) {
		word = 0;
		bcopy(p, &word, i);
		add_entropy_word(&random_state, word);
	}
	return nbytes;
}
#endif /* notused */

void
add_true_randomness(int val)
{
	int count;

	add_entropy_word(&random_state, val);
	count = random_state.entropy_count + 8 *sizeof(val);
	if (count > POOLBITS)
		count = POOLBITS;
	random_state.entropy_count = count;
	selwakeup(&random_state.rsel);
}

/*
 * Returns whether /dev/random has data or not.  entropy_count must be 
 * at least STATE_BITS.
 */
int
random_poll(dev_t dev, int events, struct thread *td)
{
	int revents = 0;

	crit_enter();
	if (events & (POLLIN | POLLRDNORM)) {
		if (random_state.entropy_count >= MIN_BITS)
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &random_state.rsel);
	}
	crit_exit();
	if (events & (POLLOUT | POLLWRNORM))
		revents |= events & (POLLOUT | POLLWRNORM);	/* heh */

	return (revents);
}

