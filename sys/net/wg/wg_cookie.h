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

#ifndef _NET_WG_COOKIE_H_
#define _NET_WG_COOKIE_H_

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#endif

#include <crypto/chachapoly.h>

#define COOKIE_MAC_SIZE		16
#define COOKIE_COOKIE_SIZE	16
#define COOKIE_INPUT_SIZE	32
#define COOKIE_NONCE_SIZE	XCHACHA20POLY1305_NONCE_SIZE
#define COOKIE_ENCRYPTED_SIZE	(COOKIE_COOKIE_SIZE + COOKIE_MAC_SIZE)

struct cookie_macs {
	uint8_t	mac1[COOKIE_MAC_SIZE];
	uint8_t	mac2[COOKIE_MAC_SIZE];
};

struct cookie_maker;
struct cookie_checker;

int	cookie_init(void);
void	cookie_deinit(void);

struct cookie_checker *
	cookie_checker_alloc(void);
void	cookie_checker_free(struct cookie_checker *);
void	cookie_checker_update(struct cookie_checker *,
			      const uint8_t[COOKIE_INPUT_SIZE]);
void	cookie_checker_create_payload(struct cookie_checker *,
				      const struct cookie_macs *,
				      uint8_t[COOKIE_NONCE_SIZE],
				      uint8_t[COOKIE_ENCRYPTED_SIZE],
				      const struct sockaddr *);
int	cookie_checker_validate_macs(struct cookie_checker *,
				     const struct cookie_macs *, const void *,
				     size_t, bool, const struct sockaddr *);

struct cookie_maker *
	cookie_maker_alloc(const uint8_t[COOKIE_INPUT_SIZE]);
void	cookie_maker_free(struct cookie_maker *);
int	cookie_maker_consume_payload(struct cookie_maker *,
				     const uint8_t[COOKIE_NONCE_SIZE],
				     const uint8_t[COOKIE_ENCRYPTED_SIZE]);
void	cookie_maker_mac(struct cookie_maker *, struct cookie_macs *,
			 const void *, size_t);

#ifdef WG_SELFTESTS
bool	cookie_selftest(void);
#endif /* WG_SELFTESTS */

#endif /* _NET_WG_COOKIE_H_ */
