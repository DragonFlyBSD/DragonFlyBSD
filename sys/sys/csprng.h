#ifndef _SYS_CSPRNG_H_
#define _SYS_CSPRNG_H_

#include <crypto/sha2/sha2.h>
#include <crypto/chacha/chacha.h>

#include <sys/callout.h>
#include <sys/spinlock.h>
#include <sys/time.h>

/* Flags for various calls */
#define CSPRNG_TRYLOCK		0x0001
#define CSPRNG_UNLIMITED	0x0002

struct csprng_pool {
	uint64_t	bytes;
	SHA256_CTX	hash_ctx;

	struct spinlock	lock;
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

	struct spinlock	lock;
	struct callout	reseed_callout;
	uint32_t	failed_reseeds;
	int		callout_based_reseed;
	struct timeval  last_reseed;
};

int csprng_init(struct csprng_state *state);
int csprng_init_reseed(struct csprng_state *state);
int csprng_get_random(struct csprng_state *state, uint8_t *out, int bytes,
    int flags);
int csprng_add_entropy(struct csprng_state *state, int src_id,
    const uint8_t *entropy, size_t bytes, int flags);

#endif
