/*
 * Copyright (c) 2004-2014 The DragonFly Project. All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * by Alex Hornung <alex@alexhornung.com>
 * by Robin J Carey
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*			   --- NOTES ---
 *
 * Note: The word "entropy" is often incorrectly used to describe
 * random data. The word "entropy" originates from the science of
 * Physics. The correct descriptive definition would be something
 * along the lines of "seed", "unpredictable numbers" or
 * "unpredictable data".
 *
 * Note: Some /dev/[u]random implementations save "seed" between
 * boots which represents a security hazard since an adversary
 * could acquire this data (since it is stored in a file). If
 * the unpredictable data used in the above routines is only
 * generated during Kernel operation, then an adversary can only
 * acquire that data through a Kernel security compromise and/or
 * a cryptographic algorithm failure/cryptanalysis.
 *
 * Note: On FreeBSD-4.11, interrupts have to be manually enabled
 * using the rndcontrol(8) command.
 *
 *		--- DESIGN (FreeBSD-4.11 based) ---
 *
 *   The rnddev module automatically initializes itself the first time
 * it is used (client calls any public rnddev_*() interface routine).
 * Both CSPRNGs are initially seeded from the precise nano[up]time() routines.
 * Tests show this method produces good enough results, suitable for intended
 * use. It is necessary for both CSPRNGs to be completely seeded, initially.
 *
 *   After initialization and during Kernel operation the only suitable
 * unpredictable data available is:
 *
 *	(1) Keyboard scan-codes.
 *	(2) Nanouptime acquired by a Keyboard/Read-Event.
 *	(3) Suitable interrupt source; hard-disk/ATA-device.
 *
 *      (X) Mouse-event (xyz-data unsuitable); NOT IMPLEMENTED.
 *
 *   This data is added to both CSPRNGs in real-time as it happens/
 * becomes-available. Additionally, unpredictable (?) data may be
 * acquired from a true-random number generator if such a device is
 * available to the system (not advisable !).
 *   Nanouptime() acquired by a Read-Event is a very important aspect of
 * this design, since it ensures that unpredictable data is added to
 * the CSPRNGs even if there are no other sources.
 *   The nanouptime() Kernel routine is used since time relative to
 * boot is less adversary-known than time itself.
 *
 *   This design has been thoroughly tested with debug logging
 * and the output from both /dev/random and /dev/urandom has
 * been tested with the DIEHARD test-suite; both pass.
 *
 * MODIFICATIONS MADE TO ORIGINAL "kern_random.c":
 *
 * 6th July 2005:
 *
 * o Changed ReadSeed() function to schedule future read-seed-events
 *   by at least one second. Previous implementation used a randomised
 *   scheduling { 0, 1, 2, 3 seconds }.
 * o Changed SEED_NANOUP() function to use a "previous" accumulator
 *   algorithm similar to ReadSeed(). This ensures that there is no
 *   way that an adversary can tell what number is being added to the
 *   CSPRNGs, since the number added to the CSPRNGs at Event-Time is
 *   the sum of nanouptime()@Event and an unknown/secret number.
 * o Changed rnddev_add_interrupt() function to schedule future
 *   interrupt-events by at least one second. Previous implementation
 *   had no scheduling algorithm which allowed an "interrupt storm"
 *   to occur resulting in skewed data entering into the CSPRNGs.
 *
 *
 * 9th July 2005:
 *
 * o Some small cleanups and change all internal functions to be
 *   static/private.
 * o Removed ReadSeed() since its functionality is already performed
 *   by another function { rnddev_add_interrupt_OR_read() } and remove
 *   the silly rndByte accumulator/feedback-thing (since multipying by
 *   rndByte could yield a value of 0).
 * o Made IBAA/L14 public interface become static/private;
 *   Local to this file (not changed to that in the original C modules).
 *
 * 16th July 2005:
 *
 * o SEED_NANOUP() -> NANOUP_EVENT() function rename.
 * o Make NANOUP_EVENT() handle the time-buffering directly so that all
 *   time-stamp-events use this single time-buffer (including keyboard).
 *   This removes dependancy on "time_second" Kernel variable.
 * o Removed second-time-buffer code in rnddev_add_interrupt_OR_read (void).
 * o Rewrote the time-buffering algorithm in NANOUP_EVENT() to use a
 *   randomised time-delay range.
 *
 * 12th Dec 2005:
 *
 * o Updated to (hopefully final) L15 algorithm.
 *
 * 12th June 2006:
 *
 * o Added missing (u_char *) cast in RnddevRead() function.
 * o Changed copyright to 3-clause BSD license and cleaned up the layout
 *   of this file.
 *
 * For a proper changelog, refer to the version control history of this
 * file.
 *
 * January 2020:
 *
 * o Made the random number generator per-cpu.
 *
 * o Certain entropy sources, such as RDRAND, are per-cpu.
 *
 * o Fixed sources such as entropy saved across reboots is split across
 *   available cpus.  Interrupt and generic sources are dribbled across
 *   available cpus.
 *
 * o In addition, we chain random generator data into the buffer randomness
 *   from cpu to cpu to force the cpus to diverge quickly from initial states.
 *   This ensures that no cpu is starved.  This is done at early-init and also
 *   at regular intervals by rand_thread_loop().
 *
 *   The chaining forces the cpus to diverge quickly and also ensures that
 *   all entropy data ultimately affects all cpus regardless of which cpu
 *   the entropy was injected into.  The combination should be pretty killer.
 */

#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/poll.h>
#include <sys/event.h>
#include <sys/random.h>
#include <sys/systimer.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/sysmsg.h>
#include <sys/spinlock.h>
#include <sys/csprng.h>
#include <sys/malloc.h>
#include <machine/atomic.h>
#include <machine/clock.h>

#include <sys/spinlock2.h>
#include <sys/signal2.h>

static struct csprng_state *csprng_pcpu;
static struct csprng_state csprng_boot;

static MALLOC_DEFINE(M_NRANDOM, "nrandom", "csprng");

static int add_buffer_randomness_state(struct csprng_state *state,
			    const char *buf, int bytes, int srcid);

static
struct csprng_state *
iterate_csprng_state(int bytes __unused)
{
	static unsigned int csprng_iterator;
	unsigned int n;

	if (csprng_pcpu) {
		n = csprng_iterator++ % ncpus;
		return &csprng_pcpu[n];
	}
	return NULL;
}

/*
 * Portability note: The u_char/unsigned char type is used where
 * uint8_t from <stdint.h> or u_int8_t from <sys/types.h> should really
 * be being used. On FreeBSD, it is safe to make the assumption that these
 * different types are equivalent (on all architectures).
 * The FreeBSD <sys/crypto/rc4> module also makes this assumption.
 */

/*------------------------------ IBAA ----------------------------------*/

/*-------------------------- IBAA CSPRNG -------------------------------*/

/*
 * NOTE: The original source code from which this source code (IBAA)
 *       was taken has no copyright/license. The algorithm has no patent
 *       and is freely/publicly available from:
 *
 *           http://www.burtleburtle.net/bob/rand/isaac.html
 */

typedef	u_int32_t	u4;   /* unsigned four bytes, 32 bits */

static void IBAA
(
	u4 *m,		/* Memory: array of SIZE ALPHA-bit terms */
	u4 *r,		/* Results: the sequence, same size as m */
	u4 *aa,		/* Accumulator: a single value */
	u4 *bb,		/* the previous result */
	u4 *counter	/* counter */
)
{
	u4 a, b, x, y, i;
 
	a = *aa;
	b = *bb + *counter;
	++*counter;
	for (i = 0; i < SIZE; ++i) {
		x = m[i];  
		a = barrel(a) + m[ind(i + (SIZE / 2))];	/* set a */
		m[i] = y = m[ind(x)] + a + b;		/* set m */
		r[i] = b = m[ind(y >> ALPHA)] + x;	/* set r */
	}
	*bb = b; *aa = a;
}

/*-------------------------- IBAA CSPRNG -------------------------------*/

static void	IBAA_Init(struct ibaa_state *ibaa);
static void	IBAA_Call(struct ibaa_state *ibaa);
static void	IBAA_Seed(struct ibaa_state *ibaa, u_int32_t val);
static u_char	IBAA_Byte(struct ibaa_state *ibaa);

/*
 * Initialize IBAA. 
 */
static void
IBAA_Init(struct ibaa_state *ibaa)
{
	size_t	i;

	for (i = 0; i < SIZE; ++i) {
		ibaa->IBAA_memory[i] = i;
	}
	ibaa->memIndex = 0;
	ibaa->IBAA_aa = 0;
	ibaa->IBAA_bb = 0;
	ibaa->IBAA_counter = 0;
	/* force IBAA_Call() */
	ibaa->IBAA_byte_index = sizeof(ibaa->IBAA_results);
}

/*
 * PRIVATE: Call IBAA to produce 256 32-bit u4 results.
 */
static void
IBAA_Call (struct ibaa_state *ibaa)
{
	IBAA(ibaa->IBAA_memory, ibaa->IBAA_results,
	     &ibaa->IBAA_aa, &ibaa->IBAA_bb,
	     &ibaa->IBAA_counter);
	ibaa->IBAA_byte_index = 0;
}

/*
 * Add a 32-bit u4 seed value into IBAAs memory.  Mix the low 4 bits 
 * with 4 bits of PNG data to reduce the possibility of a seeding-based
 * attack.
 */
static void
IBAA_Seed (struct ibaa_state *ibaa, const u_int32_t val)
{
	u4 *iptr;

	iptr = &ibaa->IBAA_memory[ibaa->memIndex & MASK];
	*iptr = ((*iptr << 3) | (*iptr >> 29)) + (val ^ (IBAA_Byte(ibaa) & 15));
	++ibaa->memIndex;
}

static void
IBAA_Vector (struct ibaa_state *ibaa, const char *buf, int bytes)
{
	int i;

	while (bytes >= sizeof(int)) {
		IBAA_Seed(ibaa, *(const int *)buf);
		buf += sizeof(int);
		bytes -= sizeof(int);
	}

	/*
	 * Warm up the generator to get rid of weak initial states.
	 */
	for (i = 0; i < 10; ++i)
		IBAA_Call(ibaa);
}

/*
 * Extract a byte from IBAAs 256 32-bit u4 results array. 
 *
 * NOTE: This code is designed to prevent MP races from taking
 * IBAA_byte_index out of bounds.
 */
static u_char
IBAA_Byte(struct ibaa_state *ibaa)
{
	u_char result;
	int index;

	index = ibaa->IBAA_byte_index;
	if (index == sizeof(ibaa->IBAA_results)) {
		IBAA_Call(ibaa);
		index = 0;
	}
	result = ((u_char *)ibaa->IBAA_results)[index];
	ibaa->IBAA_byte_index = index + 1;

	return result;
}

/*------------------------------ IBAA ----------------------------------*/


/*------------------------------- L15 ----------------------------------*/

/*
 * IMPORTANT NOTE: LByteType must be exactly 8-bits in size or this software
 * will not function correctly.
 */
typedef unsigned char	LByteType;

/*
 * PRIVATE FUNCS:
 */

static void		L15_Swap(struct l15_state *l15,const LByteType pos1, const LByteType pos2);
static void		L15_InitState(struct l15_state *l15);
static void		L15_KSA(struct l15_state *l15,
				const LByteType * const key,
				const size_t keyLen);
static void		L15_Discard(struct l15_state *l15,
				const LByteType numCalls);

/*
 * PUBLIC INTERFACE:
 */
static void		L15_Init(struct l15_state *l15,
				const LByteType * const key,
				const size_t keyLen);
static LByteType	L15_Byte(struct l15_state *l15);
static void		L15_Vector(struct l15_state *l15,
				const LByteType * const key,
				const size_t keyLen);

static __inline void
L15_Swap(struct l15_state *l15, const LByteType pos1, const LByteType pos2)
{
	LByteType save1;

	save1 = l15->L15_state[pos1];
	l15->L15_state[pos1] = l15->L15_state[pos2];
	l15->L15_state[pos2] = save1;
}

static void
L15_InitState (struct l15_state *l15)
{
	int i;

	for (i = 0; i < L15_STATE_SIZE; ++i)
		l15->L15_state[i] = i;
}

#define  L_SCHEDULE(xx)						\
								\
for (i = 0; i < L15_STATE_SIZE; ++i) {				\
    L15_Swap(l15, i, (l15->stateIndex += (l15->L15_state[i] + (xx))));	\
}

static void
L15_KSA (struct l15_state *l15, const LByteType * const key,
	 const size_t keyLen)
{
	size_t	i, keyIndex;

	for (keyIndex = 0; keyIndex < keyLen; ++keyIndex) {
		L_SCHEDULE(key[keyIndex]);
	}
	L_SCHEDULE(keyLen);
}

static void
L15_Discard(struct l15_state *l15, const LByteType numCalls)
{
	LByteType i;
	for (i = 0; i < numCalls; ++i) {
		(void)L15_Byte(l15);
	}
}


/*
 * PUBLIC INTERFACE:
 */
static void
L15_Init(struct l15_state *l15, const LByteType *key,
	 const size_t keyLen)
{
	l15->stateIndex = 0;
	l15->L15_x = 0;
	l15->L15_start_x = 0;
	l15->L15_y = L15_STATE_SIZE - 1;
	L15_InitState(l15);
	L15_KSA(l15, key, keyLen);
	L15_Discard(l15, L15_Byte(l15));
}

static LByteType
L15_Byte(struct l15_state *l15)
{
	LByteType z;

	L15_Swap(l15, l15->L15_state[l15->L15_x], l15->L15_y);
	z = (l15->L15_state [l15->L15_x++] + l15->L15_state[l15->L15_y--]);
	if (l15->L15_x == l15->L15_start_x) {
		--l15->L15_y;
	}
	return (l15->L15_state[z]);
}

static void
L15_Vector(struct l15_state *l15, const LByteType * const key,
	   const size_t keyLen)
{
	L15_KSA(l15, key, keyLen);
}

/*------------------------------- L15 ----------------------------------*/

/************************************************************************
 *				KERNEL INTERFACE			*
 ************************************************************************
 *
 * By Robin J Carey, Matthew Dillon and Alex Hornung.
 */

static int rand_thread_value;
static void NANOUP_EVENT(struct timespec *last, struct csprng_state *state);
static thread_t rand_td;

static int sysctl_kern_random(SYSCTL_HANDLER_ARGS);

static int rand_mode = 2;
static struct systimer systimer_rand;

static int sysctl_kern_rand_mode(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_kern, OID_AUTO, random, CTLFLAG_RD | CTLFLAG_ANYBODY, 0, 0,
		sysctl_kern_random, "I", "Acquire random data");
SYSCTL_PROC(_kern, OID_AUTO, rand_mode, CTLTYPE_STRING | CTLFLAG_RW, NULL, 0,
    sysctl_kern_rand_mode, "A", "RNG mode (csprng, ibaa or mixed)");


/*
 * Called twice.  Once very early on when ncpus is still 1 (before
 * kmalloc or much of anything else is available), and then again later
 * after SMP has been heated up.
 *
 * The early initialization is needed so various subsystems early in the
 * boot have some source of pseudo random bytes.
 */
void
rand_initialize(void)
{
	struct csprng_state *state;
	struct timespec	now;
	globaldata_t rgd;
	char buf[64];
	int i;
	int n;

	if (ncpus == 1) {
		csprng_pcpu = &csprng_boot;
	} else {
		csprng_pcpu = kmalloc(ncpus * sizeof(*csprng_pcpu),
				      M_NRANDOM, M_WAITOK | M_ZERO);
	}

	for (n = 0; n < ncpus; ++n) {
		state = &csprng_pcpu[n];
		rgd = globaldata_find(n);

		/* CSPRNG */
		csprng_init(state);

		/* Initialize IBAA. */
		IBAA_Init(&state->ibaa);

		/* Initialize L15. */
		nanouptime(&now);
		L15_Init(&state->l15,
		    (const LByteType *)&now.tv_nsec, sizeof(now.tv_nsec));

		for (i = 0; i < (SIZE / 2); ++i) {
			nanotime(&now);
			state->inject_counter[RAND_SRC_TIMING] = 0;
			add_buffer_randomness_state(state,
						(const uint8_t *)&now.tv_nsec,
						sizeof(now.tv_nsec),
						RAND_SRC_TIMING);

			nanouptime(&now);
			state->inject_counter[RAND_SRC_TIMING] = 0;
			add_buffer_randomness_state(state,
						(const uint8_t *)&now.tv_nsec,
						sizeof(now.tv_nsec),
						RAND_SRC_TIMING);
		}

		/*
		 * In the second call the globaldata structure has enough
		 * differentiation to give us decent initial divergence
		 * between cpus.  It isn't really all that random but its
		 * better than nothing.
		 */
		state->inject_counter[RAND_SRC_THREAD2] = 0;
		add_buffer_randomness_state(state,
					    (void *)rgd,
					    sizeof(*rgd),
					    RAND_SRC_THREAD2);

		/*
		 * Warm up the generator to get rid of weak initial states.
		 */
		for (i = 0; i < 10; ++i)
			IBAA_Call(&state->ibaa);

		/*
		 * Chain to next cpu to create as much divergence as
		 * possible.
		 */
		state->inject_counter[RAND_SRC_TIMING] = 0;
		add_buffer_randomness_state(state, buf, sizeof(buf),
					    RAND_SRC_TIMING);
		read_random(buf, sizeof(buf), 1);
	}
}

SYSINIT(rand1, SI_BOOT2_POST_SMP, SI_ORDER_SECOND, rand_initialize, 0);

/*
 * Keyboard events
 */
void
add_keyboard_randomness(u_char scancode)
{
	struct csprng_state *state;

	state = iterate_csprng_state(1);
	if (state) {
		spin_lock(&state->spin);
		L15_Vector(&state->l15,
			   (const LByteType *)&scancode, sizeof (scancode));
		++state->nrandevents;
		++state->nrandseed;
		spin_unlock(&state->spin);
		add_interrupt_randomness(0);
	}
}

/*
 * Interrupt events.  This is SMP safe and allowed to race.
 *
 * This adjusts rand_thread_value which will be incorporated into the next
 * time-buffered seed.  It does not effect the seeding period per-say.
 */
void
add_interrupt_randomness(int intr)
{
	if (tsc_present) {
		rand_thread_value = (rand_thread_value << 4) ^ 1 ^
		((int)rdtsc() % 151);
	}
	++rand_thread_value;				/* ~1 bit */
}

/*
 * Add entropy to our rng.  Half the time we add the entropy to both
 * csprngs and the other half of the time we add the entropy to one
 * or the other (so both don't get generated from the same entropy).
 */
static int
add_buffer_randomness_state(struct csprng_state *state,
			    const char *buf, int bytes, int srcid)
{
	uint8_t ic;

	if (state) {
		spin_lock(&state->spin);
		ic = ++state->inject_counter[srcid & 255];
		if (ic & 1) {
			L15_Vector(&state->l15, (const LByteType *)buf, bytes);
			IBAA_Vector(&state->ibaa, buf, bytes);
			csprng_add_entropy(state, srcid & RAND_SRC_MASK,
					   (const uint8_t *)buf, bytes, 0);
		} else if (ic & 2) {
			L15_Vector(&state->l15, (const LByteType *)buf, bytes);
			IBAA_Vector(&state->ibaa, buf, bytes);
		} else {
			csprng_add_entropy(state, srcid & RAND_SRC_MASK,
					   (const uint8_t *)buf, bytes, 0);
		}
		++state->nrandevents;
		state->nrandseed += bytes;
		spin_unlock(&state->spin);
		wakeup(state);
	}

	return 0;
}



/*
 * Add buffer randomness from miscellaneous sources.  Large amounts of
 * generic random data will be split across available cpus.
 */
int
add_buffer_randomness(const char *buf, int bytes)
{
	struct csprng_state *state;

	state = iterate_csprng_state(bytes);
	return add_buffer_randomness_state(state, buf, bytes, RAND_SRC_UNKNOWN);
}

int
add_buffer_randomness_src(const char *buf, int bytes, int srcid)
{
	struct csprng_state *state;
	int n;

	while (csprng_pcpu && bytes) {
		n = bytes;
		if (srcid & RAND_SRCF_PCPU) {
			state = &csprng_pcpu[mycpu->gd_cpuid];
		} else {
			state = iterate_csprng_state(bytes);
			if (n > 256)
				n = 256;
		}
		add_buffer_randomness_state(state, buf, bytes, srcid);
		bytes -= n;
		buf += n;
	}
	return 0;
}

/*
 * Kqueue filter (always succeeds)
 */
int
random_filter_read(struct knote *kn, long hint)
{
	return (1);
}

/*
 * Heavy weight random number generator.  May return less then the
 * requested number of bytes.
 *
 * Instead of stopping early,
 */
u_int
read_random(void *buf, u_int nbytes, int unlimited)
{
	struct csprng_state *state;
	int i, j;

	if (csprng_pcpu == NULL) {
		kprintf("read_random: csprng not yet ready\n");
		return 0;
	}
	state = &csprng_pcpu[mycpu->gd_cpuid];

	spin_lock(&state->spin);
	if (rand_mode == 0) {
		/* Only use CSPRNG */
		i = csprng_get_random(state, buf, nbytes, 0, unlimited);
	} else if (rand_mode == 1) {
		/* Only use IBAA */
		for (i = 0; i < nbytes; i++)
			((u_char *)buf)[i] = IBAA_Byte(&state->ibaa);
	} else {
		/* Mix both CSPRNG and IBAA */
		i = csprng_get_random(state, buf, nbytes, 0, unlimited);
		for (j = 0; j < i; j++)
			((u_char *)buf)[j] ^= IBAA_Byte(&state->ibaa);
	}
	spin_unlock(&state->spin);
	add_interrupt_randomness(0);

	return (i > 0) ? i : 0;
}

/*
 * Read random data via sysctl().
 */
static
int
sysctl_kern_random(SYSCTL_HANDLER_ARGS)
{
	char buf[256];
	size_t n;
	size_t r;
	int error = 0;

	n = req->oldlen;
	if (n > 1024 * 1024)
		n = 1024 * 1024;
	while (n > 0) {
		if ((r = n) > sizeof(buf))
			r = sizeof(buf);
		read_random(buf, r, 1);
		error = SYSCTL_OUT(req, buf, r);
		if (error)
			break;
		n -= r;
	}
	return(error);
}

int
sys_getrandom(struct sysmsg *sysmsg, const struct getrandom_args *uap)
{
	char buf[256];
	ssize_t bytes;
	ssize_t r;
	ssize_t n;
	int error;
	int sigcnt;

	bytes = (ssize_t)uap->len;
	if (bytes < 0)
		return EINVAL;

	r = 0;
	error = 0;
	sigcnt = 0;

	while (r < bytes) {
		n = (ssize_t)sizeof(buf);
		if (n > bytes - r)
			n = bytes - r;
		read_random(buf, n, 1);
		error = copyout(buf, (char *)uap->buf + r, n);
		if (error)
			break;
		r += n;
		lwkt_user_yield();
		if (++sigcnt == 128) {
			sigcnt = 0;
			if (CURSIG_NOBLOCK(curthread->td_lwp) != 0) {
				error = EINTR;
				break;
			}
		}
	}
	if (error == 0)
		sysmsg->sysmsg_szresult = r;

	return error;
}

/*
 * Change the random mode via sysctl().
 */
static
const char *
rand_mode_to_str(int mode)
{
	switch (mode) {
	case 0:
		return "csprng";
	case 1:
		return "ibaa";
	case 2:
		return "mixed";
	default:
		return "unknown";
	}
}

static
int
sysctl_kern_rand_mode(SYSCTL_HANDLER_ARGS)
{
	char mode[32];
	int error;

	strncpy(mode, rand_mode_to_str(rand_mode), sizeof(mode)-1);
	error = sysctl_handle_string(oidp, mode, sizeof(mode), req);
	if (error || req->newptr == NULL)
	    return error;

	if ((strncmp(mode, "csprng", sizeof(mode))) == 0)
		rand_mode = 0;
	else if ((strncmp(mode, "ibaa", sizeof(mode))) == 0)
		rand_mode = 1;
	else if ((strncmp(mode, "mixed", sizeof(mode))) == 0)
		rand_mode = 2;
	else
		error = EINVAL;

	return error;
}

/*
 * Random number generator helper thread.  This limits code overhead from
 * high frequency events by delaying the clearing of rand_thread_value.
 *
 * This is a time-buffered loop, with a randomizing delay.  Note that interrupt
 * entropy does not cause the thread to wakeup any faster, but does improve the
 * quality of the entropy produced.
 *
 * In addition, we pull statistics from available cpus.
 */
static
void
rand_thread_loop(void *dummy)
{
	struct csprng_state *state;
	globaldata_t rgd;
	int64_t count;
	char buf[32];
	uint32_t wcpu;
	struct timespec	last;

	wcpu = 0;
	bzero(&last, sizeof(last));

	for (;;) {
		/*
		 * Generate entropy.
		 */
		wcpu = (wcpu + 1) % ncpus;
		state = &csprng_pcpu[wcpu];
		rgd = globaldata_find(wcpu);

		NANOUP_EVENT(&last, state);
		spin_lock(&state->spin);
		count = (uint8_t)L15_Byte(&state->l15);
		spin_unlock(&state->spin);

		/*
		 * Calculate 1/10 of a second to 2/10 of a second, fine-grained
		 * using a L15_Byte() feedback.
		 *
		 * Go faster in the first 1200 seconds after boot.  This effects
		 * the time-after-next interrupt (pipeline delay).
		 */
		count = muldivu64(sys_cputimer->freq, count + 256, 256 * 10);
		if (time_uptime < 120)
			count = count / 10 + 1;
		systimer_rand.periodic = count;

		/*
		 * Force cpus to diverge.  Chained state and per-cpu state
		 * is thrown in.
		 */
		add_buffer_randomness_state(state,
					    buf, sizeof(buf),
					    RAND_SRC_THREAD1);
		add_buffer_randomness_state(state,
					    (void *)&rgd->gd_cnt,
					    sizeof(rgd->gd_cnt),
					    RAND_SRC_THREAD2);
		add_buffer_randomness_state(state,
					    (void *)&rgd->gd_vmtotal,
					    sizeof(rgd->gd_vmtotal),
					    RAND_SRC_THREAD3);

		read_random(buf, sizeof(buf), 1);

		tsleep(rand_td, 0, "rwait", 0);
	}
}

/*
 * Systimer trigger - fine-grained random trigger
 */
static
void
rand_thread_wakeup(struct systimer *timer, int in_ipi, struct intrframe *frame)
{
	wakeup(rand_td);
}

static
void
rand_thread_init(void)
{
	systimer_init_periodic_nq(&systimer_rand, rand_thread_wakeup, NULL, 25);
	lwkt_create(rand_thread_loop, NULL, &rand_td, NULL, 0, 0, "random");
}

SYSINIT(rand2, SI_SUB_HELPER_THREADS, SI_ORDER_ANY, rand_thread_init, 0);

/*
 * Caller is time-buffered.  Incorporate any accumulated interrupt randomness
 * as well as the high frequency bits of the TSC.
 *
 * A delta nanoseconds value is used to remove absolute time from the generated
 * entropy.  Even though we are pushing 32 bits, this entropy is probably only
 * good for one or two bits without any interrupt sources, and possibly
 * 8 bits with.
 */
static void
NANOUP_EVENT(struct timespec *last, struct csprng_state *state)
{
	struct timespec		now;
	int			nsec;

	/*
	 * Delta nanoseconds since last event
	 */
	nanouptime(&now);
	nsec = now.tv_nsec - last->tv_nsec;
	*last = now;

	/*
	 * Interrupt randomness.
	 */
	nsec ^= rand_thread_value;

	/*
	 * The TSC, if present, generally has an even higher
	 * resolution.  Integrate a portion of it into our seed.
	 */
	if (tsc_present)
		nsec ^= (rdtsc() & 255) << 8;

	/* 
	 * Ok.
	 */
	add_buffer_randomness_state(state,
				    (const uint8_t *)&nsec, sizeof(nsec),
				    RAND_SRC_INTR);
}

