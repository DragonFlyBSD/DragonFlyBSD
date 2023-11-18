/* SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (c) 2022 The FreeBSD Foundation
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <opencrypto/cryptodev.h>

#include "crypto.h"

static crypto_session_t chacha20_poly1305_sid;

static int
crypto_callback(struct cryptop *crp)
{
	return (0);
}

int
chacha20poly1305_encrypt_mbuf(struct mbuf *m, const uint64_t nonce,
			      const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	static const char blank_tag[POLY1305_HASH_LEN];
	struct cryptop crp;
	int ret;

	if (!m_append(m, POLY1305_HASH_LEN, blank_tag))
		return (ENOMEM);
	crypto_initreq(&crp, chacha20_poly1305_sid);
	crp.crp_op = CRYPTO_OP_ENCRYPT | CRYPTO_OP_COMPUTE_DIGEST;
	crp.crp_flags = CRYPTO_F_IV_SEPARATE | CRYPTO_F_CBIMM;
	crypto_use_mbuf(&crp, m);
	crp.crp_payload_length = m->m_pkthdr.len - POLY1305_HASH_LEN;
	crp.crp_digest_start = crp.crp_payload_length;
	le64enc(crp.crp_iv, nonce);
	crp.crp_cipher_key = key;
	crp.crp_callback = crypto_callback;
	ret = crypto_dispatch(&crp);
	crypto_destroyreq(&crp);
	return (ret);
}

int
chacha20poly1305_decrypt_mbuf(struct mbuf *m, const uint64_t nonce,
			      const uint8_t key[CHACHA20POLY1305_KEY_SIZE])
{
	struct cryptop crp;
	int ret;

	if (m->m_pkthdr.len < POLY1305_HASH_LEN)
		return (EMSGSIZE);
	crypto_initreq(&crp, chacha20_poly1305_sid);
	crp.crp_op = CRYPTO_OP_DECRYPT | CRYPTO_OP_VERIFY_DIGEST;
	crp.crp_flags = CRYPTO_F_IV_SEPARATE | CRYPTO_F_CBIMM;
	crypto_use_mbuf(&crp, m);
	crp.crp_payload_length = m->m_pkthdr.len - POLY1305_HASH_LEN;
	crp.crp_digest_start = crp.crp_payload_length;
	le64enc(crp.crp_iv, nonce);
	crp.crp_cipher_key = key;
	crp.crp_callback = crypto_callback;
	ret = crypto_dispatch(&crp);
	crypto_destroyreq(&crp);
	if (ret)
		return (ret);
	m_adj(m, -POLY1305_HASH_LEN);
	return (0);
}

int
crypto_init(void)
{
	struct crypto_session_params csp = {
		.csp_mode = CSP_MODE_AEAD,
		.csp_ivlen = sizeof(uint64_t),
		.csp_cipher_alg = CRYPTO_CHACHA20_POLY1305,
		.csp_cipher_klen = CHACHA20POLY1305_KEY_SIZE,
		.csp_flags = CSP_F_SEPARATE_AAD | CSP_F_SEPARATE_OUTPUT
	};
	int ret = crypto_newsession(&chacha20_poly1305_sid, &csp, CRYPTOCAP_F_SOFTWARE);
	if (ret != 0)
		return (ret);
	return (0);
}

void
crypto_deinit(void)
{
	crypto_freesession(chacha20_poly1305_sid);
}
