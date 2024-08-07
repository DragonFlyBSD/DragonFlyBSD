/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <alex@alexhornung.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/csprng.h>

#define CHACHA_EMBED
#define CHACHA_NONCE0_CTR128
#define KEYSTREAM_ONLY
#include <crypto/chacha20/chacha.c>
#include <crypto/sha2/sha2.h>

/*
 * Minimum amount of bytes in pool before we consider it
 * good enough.
 * It's 64 + the hash digest size because we always
 * reinitialize the pools with a hash of the previous chunk
 * of entropy.
 */
#define MIN_POOL_SIZE	(64 + SHA256_DIGEST_LENGTH)

/* Minimum reseed interval */
#define MIN_RESEED_INTERVAL	hz/10

#if 0
static void csprng_reseed_callout(void *arg);
#endif
static int csprng_reseed(struct csprng_state *state);

static struct timeval csprng_reseed_interval = { 0, 100000 };

static
int
csprng_pool_init(struct csprng_pool *pool, uint8_t *buf, size_t len)
{
	pool->bytes = 0;
	SHA256_Init(&pool->hash_ctx);

	if (len > 0)
		SHA256_Update(&pool->hash_ctx, buf, len);

	return 0;
}

int
csprng_init(struct csprng_state *state)
{
	int i, r;

	bzero(state->key, sizeof(state->key));
	bzero(&state->cipher_ctx, sizeof(state->cipher_ctx));
	bzero(state->src_pool_idx, sizeof(state->src_pool_idx));
	bzero(&state->last_reseed, sizeof(state->last_reseed));

	state->reseed_cnt = 0;
	state->failed_reseeds = 0;
	state->callout_based_reseed = 0;

	for (i = 0; i < 32; i++) {
		r = csprng_pool_init(&state->pool[i], NULL, 0);
		if (r != 0)
			break;
	}

	return r;
}

#if 0
int
csprng_init_reseed(struct csprng_state *state)
{
	state->callout_based_reseed = 1;

	callout_init_mp(&state->reseed_callout);
	callout_reset(&state->reseed_callout, MIN_RESEED_INTERVAL,
		      csprng_reseed_callout, state);

	return 0;
}
#endif

/*
 * XXX:
 * Sources don't really a uniquely-allocated src id...
 * another way we could do that is by simply using
 * (uint8_t)__LINE__ as the source id... cheap & cheerful.
 */

/*
 * Called with state->spin held.
 */
int
csprng_get_random(struct csprng_state *state, uint8_t *out, int bytes,
		  int flags)
{
	int cnt;
	int total_bytes = 0;

again:
	if (!state->callout_based_reseed &&
	     ratecheck(&state->last_reseed, &csprng_reseed_interval)) {
		csprng_reseed(state);
	}

	/*
	 * If no reseed has occurred yet, we can't possibly give out
	 * any random data.
	 * If this isn't an unlimited (i.e., /dev/urandom) read, sleep
	 * until entropy is added to the pools (or a callout-based
	 * reseed, if enabled, occurs).
	 */
	if ((flags & CSPRNG_UNLIMITED) == 0 && state->reseed_cnt == 0) {
		ssleep(state, &state->spin, 0, "csprngrsd", 0);
		goto again;
	}

	while (bytes > 0) {
		/* Limit amount of output without rekeying to 2^20 */
		cnt = (bytes > (1 << 20)) ? (1 << 20) : bytes;

		chacha_encrypt_bytes(&state->cipher_ctx, NULL, out, cnt);

		/* Update key and rekey cipher */
		chacha_encrypt_bytes(&state->cipher_ctx, NULL, state->key,
				     sizeof(state->key));
		chacha_keysetup(&state->cipher_ctx, state->key,
				8 * sizeof(state->key));

		out += cnt;
		bytes -= cnt;
		total_bytes += cnt;
	}

	return total_bytes;
}

/*
 * Called with state->spin held.
 */
static
int
csprng_reseed(struct csprng_state *state)
{
	int i;
	struct csprng_pool *pool;
	SHA256_CTX hash_ctx;
	uint8_t digest[SHA256_DIGEST_LENGTH];
	uint8_t counter[16];

	/*
	 * If there's not enough entropy in the first
	 * pool, don't reseed.
	 */
	if (state->pool[0].bytes < MIN_POOL_SIZE) {
		++state->failed_reseeds;
		return 1;
	}

	SHA256_Init(&hash_ctx);

	/*
	 * Update hash that will result in new key with the
	 * old key.
	 */
	SHA256_Update(&hash_ctx, state->key, sizeof(state->key));

	state->reseed_cnt++;

	for (i = 0; i < 32; i++) {
		if ((state->reseed_cnt % (1 << i)) != 0)
			break;

		pool = &state->pool[i];

		/*
		 * Finalize hash of the entropy in this pool.
		 */
		SHA256_Final(digest, &pool->hash_ctx);

		/*
		 * Reinitialize pool with a hash of the old pool digest.
		 * This is a slight deviation from Fortuna as per reference,
		 * but is in line with other Fortuna implementations.
		 */
		csprng_pool_init(pool, digest, sizeof(digest));

		/*
		 * Update hash that will result in new key with this
		 * pool's hashed entropy.
		 */
		SHA256_Update(&hash_ctx, digest, sizeof(digest));
	}

	SHA256_Final(state->key, &hash_ctx);

	/* Update key and rekey cipher */
	chacha_keysetup(&state->cipher_ctx, state->key, 8*sizeof(state->key));

	/* No IV but a 128-bit counter, should never overflow */
	bzero(counter, sizeof(counter));
	chacha_ivsetup(&state->cipher_ctx, NULL, counter);

	return 0;
}

#if 0
static
void
csprng_reseed_callout(void *arg)
{
	struct csprng_state *state = (struct csprng_state *)arg;
	int reseed_interval = MIN_RESEED_INTERVAL;

	spin_lock(&state->spin);
	csprng_reseed(arg);
	spin_unlock(&state->spin);
	wakeup(state);

	callout_reset(&state->reseed_callout, reseed_interval,
		      csprng_reseed_callout, state);
}
#endif

/*
 * Called with state->spin held
 */
int
csprng_add_entropy(struct csprng_state *state, int src_id,
		   const uint8_t *entropy, size_t bytes, int flags)
{
	struct csprng_pool *pool;
	int pool_id;

	/*
	 * Pick the next pool for this source on a round-robin
	 * basis.
	 */
	src_id &= 0xff;
	pool_id = state->src_pool_idx[src_id]++ & 0x1f;
	pool = &state->pool[pool_id];

	SHA256_Update(&pool->hash_ctx, (const uint8_t *)&src_id,
		      sizeof(src_id));
	SHA256_Update(&pool->hash_ctx, (const uint8_t *)&bytes,
		      sizeof(bytes));
	SHA256_Update(&pool->hash_ctx, entropy, bytes);

	pool->bytes += bytes;

	return 0;
}
