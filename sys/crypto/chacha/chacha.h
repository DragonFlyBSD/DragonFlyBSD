#ifndef _CRYPTO_CHACHA_H_
#define _CRYPTO_CHACHA_H_

#define CHACHA_NONCELEN		8
#define CHACHA_CTRLEN		8
#define CHACHA_STATELEN		(CHACHA_NONCELEN + CHACHA_CTRLEN)
#define CHACHA_BLOCKLEN		64

typedef struct chacha_ctx
{
  uint32_t input[16]; /* could be compressed */
} chacha_ctx;

void chacha_keysetup(chacha_ctx *x, const uint8_t *k, uint32_t kbits);
void chacha_ivsetup(chacha_ctx *x, const uint8_t *iv);
void chacha_encrypt_bytes(chacha_ctx *x, const uint8_t *m, uint8_t *c, uint32_t bytes);
void chacha_decrypt_bytes(chacha_ctx *x, const uint8_t *c, uint8_t *m, uint32_t bytes);
void chacha_keystream_bytes(chacha_ctx *x, uint8_t *stream, uint32_t bytes);
int chacha_incr_counter(chacha_ctx *x);
int chacha_check_counter(chacha_ctx *x);

#endif
