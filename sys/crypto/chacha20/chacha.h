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

LOCAL void chacha_keysetup(struct chacha_ctx *x, const uint8_t *k,
    uint32_t kbits);
LOCAL void chacha_ivsetup(struct chacha_ctx *x, const uint8_t *iv,
    const uint8_t *counter);
LOCAL void chacha_encrypt_bytes(struct chacha_ctx *x, const uint8_t *m,
    uint8_t *c, uint32_t bytes);

#ifdef CHACHA_NONCE0_CTR128
LOCAL void chacha_ctrsave(const struct chacha_ctx *x, uint8_t *counter);
#endif

#endif	/* CHACHA_H */
