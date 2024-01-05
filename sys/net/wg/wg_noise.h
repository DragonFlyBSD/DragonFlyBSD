/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2021 Matt Dunwoodie <ncon@noconroy.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NET_WG_NOISE_H_
#define _NET_WG_NOISE_H_

#include <crypto/chachapoly.h>
#include <crypto/blake2/blake2s.h>
#include <crypto/curve25519/curve25519.h>

#define NOISE_PUBLIC_KEY_LEN	CURVE25519_KEY_SIZE
#define NOISE_SYMMETRIC_KEY_LEN	CHACHA20POLY1305_KEY_SIZE
#define NOISE_TIMESTAMP_LEN	(sizeof(uint64_t) + sizeof(uint32_t))
#define NOISE_AUTHTAG_LEN	CHACHA20POLY1305_AUTHTAG_SIZE
#define NOISE_HASH_LEN		BLAKE2S_HASH_SIZE

#define REJECT_AFTER_TIME	180
#define REKEY_TIMEOUT		5
#define KEEPALIVE_TIMEOUT	10

struct noise_local;
struct noise_remote;
struct noise_keypair;

/* Local configuration */
struct noise_local *
	noise_local_alloc(void);
void	noise_local_free(struct noise_local *);

bool	noise_local_set_private(struct noise_local *,
	    const uint8_t[NOISE_PUBLIC_KEY_LEN]);
bool	noise_local_keys(struct noise_local *,
	    uint8_t[NOISE_PUBLIC_KEY_LEN],
	    uint8_t[NOISE_PUBLIC_KEY_LEN]);

/* Remote configuration */
struct noise_remote *
	noise_remote_alloc(struct noise_local *, void *,
	    const uint8_t[NOISE_PUBLIC_KEY_LEN]);
int	noise_remote_enable(struct noise_remote *);
void	noise_remote_disable(struct noise_remote *);
struct noise_remote *
	noise_remote_lookup(struct noise_local *, const uint8_t[NOISE_PUBLIC_KEY_LEN]);
struct noise_remote *
	noise_remote_index(struct noise_local *, uint32_t);
struct noise_remote *
	noise_remote_ref(struct noise_remote *);
void	noise_remote_put(struct noise_remote *);
void	noise_remote_free(struct noise_remote *);
void *	noise_remote_arg(struct noise_remote *);

void	noise_remote_set_psk(struct noise_remote *,
	    const uint8_t[NOISE_SYMMETRIC_KEY_LEN]);
bool	noise_remote_keys(struct noise_remote *,
	    uint8_t[NOISE_PUBLIC_KEY_LEN],
	    uint8_t[NOISE_SYMMETRIC_KEY_LEN]);
bool	noise_remote_initiation_expired(struct noise_remote *);
void	noise_remote_handshake_clear(struct noise_remote *);
void	noise_remote_keypairs_clear(struct noise_remote *);

/* Keypair functions */
struct noise_keypair *
	noise_keypair_lookup(struct noise_local *, uint32_t);
struct noise_keypair *
	noise_keypair_current(struct noise_remote *);
struct noise_keypair *
	noise_keypair_ref(struct noise_keypair *);
void	noise_keypair_put(struct noise_keypair *);
bool	noise_keypair_received_with(struct noise_keypair *);

struct noise_remote *
	noise_keypair_remote(struct noise_keypair *);

bool	noise_keypair_counter_next(struct noise_keypair *, uint64_t *);
bool	noise_keypair_counter_check(struct noise_keypair *, uint64_t);

bool	noise_keep_key_fresh_send(struct noise_remote *);
bool	noise_keep_key_fresh_recv(struct noise_remote *);

int	noise_keypair_encrypt(
	    struct noise_keypair *,
	    uint32_t *r_idx,
	    uint64_t counter,
	    struct mbuf *);
int	noise_keypair_decrypt(
	    struct noise_keypair *,
	    uint64_t counter,
	    struct mbuf *);

/* Handshake functions */
int	noise_create_initiation(
	    struct noise_remote *,
	    uint32_t *s_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
	    uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN]);

int	noise_consume_initiation(
	    struct noise_local *,
	    struct noise_remote **,
	    uint32_t s_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
	    uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN]);

int	noise_create_response(
	    struct noise_remote *,
	    uint32_t *s_idx,
	    uint32_t *r_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t en[0 + NOISE_AUTHTAG_LEN]);

int	noise_consume_response(
	    struct noise_local *,
	    struct noise_remote **,
	    uint32_t s_idx,
	    uint32_t r_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t en[0 + NOISE_AUTHTAG_LEN]);

#ifdef WG_SELFTESTS
bool	noise_counter_selftest(void);
#endif /* WG_SELFTESTS */

#endif /* _NET_WG_NOISE_H_ */
