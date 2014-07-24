#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/csprng.h>

/*
 * Minimum amount of bytes in pool before we consider it
 * good enough.
 * It's 64 + the hash digest size because we always
 * reinitialize the pools with a hash of the previous chunk
 * of entropy.
 */
#define MIN_POOL_SIZE	64 + SHA256_DIGEST_LENGTH

/* Minimum reseed interval */
#define MIN_RESEED_INTERVAL	hz/10

/* Lock macros */
#define POOL_LOCK_INIT(pool) \
    spin_init(&(pool)->lock)

#define POOL_LOCK(pool)      \
    spin_lock(&pool->lock)

#define POOL_TRYLOCK(pool)   \
    spin_trylock(&pool->lock)

#define POOL_UNLOCK(pool)    \
    spin_unlock(&pool->lock)


#define STATE_LOCK_INIT(state)  \
    spin_init(&state->lock)

#define STATE_LOCK(state)	\
    spin_lock(&state->lock)

#define STATE_UNLOCK(state)	\
    spin_unlock(&state->lock)

#define STATE_SLEEP(state, wmesg, timo)	\
    ssleep(state, &state->lock, 0, wmesg, timo)

#define STATE_WAKEUP(state)	\
    wakeup(state)

static void csprng_reseed_callout(void *arg);
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

	state->nonce = 0;
	state->ctr   = 0;
	state->reseed_cnt = 0;
	state->failed_reseeds = 0;
	state->callout_based_reseed = 0;

	STATE_LOCK_INIT(state);

	for (i = 0; i < 32; i++) {
		r = csprng_pool_init(&state->pool[i], NULL, 0);
		if (r != 0)
			break;
		POOL_LOCK_INIT(&state->pool[i]);
	}

	return r;
}

int
csprng_init_reseed(struct csprng_state *state)
{
	state->callout_based_reseed = 1;

	callout_init_mp(&state->reseed_callout);
	callout_reset(&state->reseed_callout, MIN_RESEED_INTERVAL,
	    csprng_reseed_callout, state);

	return 0;
}

/*
 * XXX:
 * Sources don't really a uniquely-allocated src id...
 * another way we could do that is by simply using
 * (uint8_t)__LINE__ as the source id... cheap & cheerful.
 */

static
int
encrypt_bytes(struct csprng_state *state, uint8_t *out, uint8_t *in, size_t bytes)
{
	/* Update nonce whenever the counter is about to overflow */
	if (chacha_check_counter(&state->cipher_ctx)) {
		++state->nonce;
		chacha_ivsetup(&state->cipher_ctx, (const uint8_t *)&state->nonce);
	}

	chacha_encrypt_bytes(&state->cipher_ctx, in, out, (uint32_t)bytes);

	return 0;
}

/*
 * XXX: flags is currently unused, but could be used to know whether
 *      it's a /dev/random or /dev/urandom read, and make sure that
 *      enough entropy has been collected recently, etc.
 */
int
csprng_get_random(struct csprng_state *state, uint8_t *out, int bytes,
    int flags __unused)
{
	int cnt;
	int total_bytes = 0;

	/*
	 * XXX: can optimize a bit by digging into chacha_encrypt_bytes
	 *      and removing the xor of the stream with the input - that
	 *      way we don't have to xor the output (which we provide
	 *      as input).
	 */
	bzero(out, bytes);

	STATE_LOCK(state);

again:
	if (!state->callout_based_reseed &&
	     ratecheck(&state->last_reseed, &csprng_reseed_interval)) {
		csprng_reseed(state);
	}

	KKASSERT(state->reseed_cnt >= 0);

	if (state->reseed_cnt == 0) {
		STATE_SLEEP(state, "csprngrsd", 0);
		goto again;
	}

	while (bytes > 0) {
		/* Limit amount of output without rekeying to 2^20 */
		cnt = (bytes > (1 << 20)) ? (1 << 20) : bytes;

		encrypt_bytes(state, out, out, cnt);

		/* Update key and rekey cipher */
		encrypt_bytes(state, state->key, state->key, sizeof(state->key));
		chacha_keysetup(&state->cipher_ctx, state->key,
		    8*sizeof(state->key));

		out += cnt;
		bytes -= cnt;
		total_bytes += cnt;
	}

	STATE_UNLOCK(state);

	return total_bytes;
}

static
int
csprng_reseed(struct csprng_state *state)
{
	int i;
	struct csprng_pool *pool;
	SHA256_CTX hash_ctx;
	uint8_t digest[SHA256_DIGEST_LENGTH];

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
		POOL_LOCK(pool);

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

		POOL_UNLOCK(pool);

		/*
		 * Update hash that will result in new key with this
		 * pool's hashed entropy.
		 */
		SHA256_Update(&hash_ctx, digest, sizeof(digest));
	}

	SHA256_Final(state->key, &hash_ctx);

	/* Update key and rekey cipher */
	chacha_keysetup(&state->cipher_ctx, state->key,
	    8*sizeof(state->key));

	/* Increment the nonce if the counter overflows */
	if (chacha_incr_counter(&state->cipher_ctx)) {
		++state->nonce;
		chacha_ivsetup(&state->cipher_ctx, (const uint8_t *)&state->nonce);
	}

	return 0;
}

static
void
csprng_reseed_callout(void *arg)
{
	struct csprng_state *state = (struct csprng_state *)arg;
	int reseed_interval = MIN_RESEED_INTERVAL;

	STATE_LOCK(state);

	csprng_reseed(arg);

	STATE_WAKEUP(state);
	STATE_UNLOCK(state);

	callout_reset(&state->reseed_callout, reseed_interval,
	    csprng_reseed_callout, state);
}

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

	if (flags & CSPRNG_TRYLOCK) {
		/*
		 * If we are asked to just try the lock instead
		 * of spinning until we get it, return if we
		 * can't get a hold of the lock right now.
		 */
		if (!POOL_TRYLOCK(pool))
			return -1;
	} else {
		POOL_LOCK(pool);
	}

	SHA256_Update(&pool->hash_ctx, (const uint8_t *)&src_id, sizeof(src_id));
	SHA256_Update(&pool->hash_ctx, (const uint8_t *)&bytes, sizeof(bytes));
	SHA256_Update(&pool->hash_ctx, entropy, bytes);

	pool->bytes += bytes;

	POOL_UNLOCK(pool);

	/*
	 * If a wakeup is missed, it doesn't matter too much - it'll get woken
	 * up by the next add_entropy() call.
	 */
	STATE_WAKEUP(state);

	return 0;
}
