#ifndef _SYS_CSPRNG_H_
#define _SYS_CSPRNG_H_

#include <crypto/sha2/sha2.h>
#include <crypto/chacha/chacha.h>

#include <sys/callout.h>
#include <sys/spinlock.h>
#include <sys/time.h>
#include <sys/ibaa.h>

/* Flags for various calls */
#define CSPRNG_TRYLOCK		0x0001
#define CSPRNG_UNLIMITED	0x0002

struct csprng_pool {
	uint64_t	bytes;
	SHA256_CTX	hash_ctx;
};

CTASSERT(SHA256_DIGEST_LENGTH == 32);

struct csprng_state {
	uint8_t		key[SHA256_DIGEST_LENGTH];
	uint64_t	nonce;		/* Effectively high 64-bits of ctr */
	uint64_t	ctr;

	uint64_t	reseed_cnt;	/* Times we have reseeded */

	chacha_ctx	cipher_ctx;	/* (Stream) cipher context */

	/* Pools and the per-source round robin pool index */
	struct csprng_pool pool[32];
	uint8_t		src_pool_idx[256];

	struct spinlock	spin;
	struct callout	reseed_callout;
	uint32_t	failed_reseeds;
	int		callout_based_reseed;
	uint8_t		inject_counter[256];
	long		nrandevents;
	long		nrandseed;
	struct timeval  last_reseed;
	struct ibaa_state ibaa;
	struct l15_state l15;
} __cachealign;

int csprng_init(struct csprng_state *state);
int csprng_get_random(struct csprng_state *state, uint8_t *out, int bytes,
    int flags, int unlimited);
int csprng_add_entropy(struct csprng_state *state, int src_id,
    const uint8_t *entropy, size_t bytes, int flags);

#endif
