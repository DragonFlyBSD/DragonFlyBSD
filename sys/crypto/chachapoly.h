/*
 * Copyright (c) 2015 Mike Belopuhov
 * Copyright (c) 2023 Aaron LI <aly@aaronly.me>
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

#ifndef _CHACHAPOLY_H_
#define _CHACHAPOLY_H_

#define CHACHA20POLY1305_KEY_SIZE	32
#define CHACHA20POLY1305_AUTHTAG_SIZE	16
#define CHACHA20POLY1305_NONCE_SIZE	12
#define XCHACHA20POLY1305_NONCE_SIZE	24

/*
 * ChaCha20-Poly1305 AEAD cipher (RFC 8439)
 *
 * NOTE: Support in-place encryption/decryption; i.e., the output buffer
 *       points to the same location as the input.
 *
 * NOTE: The output buffer may be NULL when to decrypt a message of empty
 *       plaintext.  This is used by WireGuard.
 */
void chacha20poly1305_encrypt(uint8_t *, const uint8_t *, size_t,
			      const uint8_t *, size_t,
			      const uint8_t[CHACHA20POLY1305_NONCE_SIZE],
			      const uint8_t[CHACHA20POLY1305_KEY_SIZE]);
bool chacha20poly1305_decrypt(uint8_t *, const uint8_t *, size_t,
			      const uint8_t *, size_t,
			      const uint8_t[CHACHA20POLY1305_NONCE_SIZE],
			      const uint8_t[CHACHA20POLY1305_KEY_SIZE]);

/*
 * XChaCha20-Poly1305 AEAD cipher
 * (extended nonce size from 96 bits to 192 bits)
 *
 * NOTE: Support in-place encryption/decryption, as above.
 */
void xchacha20poly1305_encrypt(uint8_t *, const uint8_t *, size_t,
			       const uint8_t *, size_t,
			       const uint8_t[XCHACHA20POLY1305_NONCE_SIZE],
			       const uint8_t[CHACHA20POLY1305_KEY_SIZE]);
bool xchacha20poly1305_decrypt(uint8_t *, const uint8_t *, size_t,
			       const uint8_t *, size_t,
			       const uint8_t[XCHACHA20POLY1305_NONCE_SIZE],
			       const uint8_t[CHACHA20POLY1305_KEY_SIZE]);

/*
 * Perform in-place encryption/decryption for data in an mbuf chain.
 */
struct mbuf;
int chacha20poly1305_encrypt_mbuf(struct mbuf *, const uint8_t *, size_t,
				  const uint8_t[CHACHA20POLY1305_NONCE_SIZE],
				  const uint8_t[CHACHA20POLY1305_KEY_SIZE]);
int chacha20poly1305_decrypt_mbuf(struct mbuf *, const uint8_t *, size_t,
				  const uint8_t[CHACHA20POLY1305_NONCE_SIZE],
				  const uint8_t[CHACHA20POLY1305_KEY_SIZE]);

#endif	/* _CHACHAPOLY_H_ */
