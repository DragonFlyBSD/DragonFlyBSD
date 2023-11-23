/* $OpenBSD: chacha.h,v 1.4 2016/08/27 04:04:56 guenther Exp $ */

/*
chacha-merged.c version 20080118
D. J. Bernstein
Public domain.
*/

#ifndef CHACHA_H
#define CHACHA_H

#include <sys/types.h>

#include "_chacha.h"

#define CHACHA_MINKEYLEN 	16
#define CHACHA_NONCELEN		8
#define CHACHA_CTRLEN		8
#define CHACHA_STATELEN		(CHACHA_NONCELEN + CHACHA_CTRLEN)
#define CHACHA_BLOCKLEN		64

#ifdef CHACHA_EMBED
#define LOCAL static
#else
#define LOCAL
#endif

/*
 * Initialize the context with key <k> of length <kbits> bit.
 * The recommended key length is 256 bits, although 128-bit keys are
 * also supported.
 */
LOCAL void chacha_keysetup(struct chacha_ctx *x, const uint8_t *k,
    uint32_t kbits);

/*
 * Setup the context with a 64-bit IV <iv> and a 64-bit counter
 * <counter>.  If <counter> is NULL, then the value zero (0) is used.
 *
 * If CHACHA_NONCE0_CTR128 is defined, then the IV <iv> is ignored
 * and the counter <counter> must be 128 bits.
 */
LOCAL void chacha_ivsetup(struct chacha_ctx *x, const uint8_t *iv,
    const uint8_t *counter);

/*
 * Encrypt/decrypt the byte string <m> of length <bytes>, write the
 * result to <c>.
 *
 * The output buffer <c> may point to the same area as the input <m>.
 * In that case, the encryption/decryption is performed in-place.
 *
 * If KEYSTREAM_ONLY is defined, then this function only generates a
 * key stream of the requested length <bytes> and writes to <c>.
 * The input <m> is unused and should be NULL in this case.
 */
LOCAL void chacha_encrypt_bytes(struct chacha_ctx *x, const uint8_t *m,
    uint8_t *c, uint32_t bytes);

#ifdef CHACHA_NONCE0_CTR128
/*
 * Save the current value of the 128-bit counter, which can be restored
 * by calling chacha_ivsetup(x, NULL, counter).
 */
LOCAL void chacha_ctrsave(const struct chacha_ctx *x, uint8_t *counter)
    __unused; /* maybe unused */
#endif

/*
 * HChaCha20 is an intermediary block to build XChaCha20, similar to
 * the HSalsa20 defined for XSalsa20.
 */
LOCAL void hchacha20(uint8_t derived_key[32], const uint8_t nonce[16],
    const uint8_t key[32]) __unused; /* maybe unused */

#endif	/* CHACHA_H */
