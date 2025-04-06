/*-
 * Copyright (c) 2010 Konstantin Belousov <kib@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/crypto/aesni/aesni.h,v 1.2 2010/09/23 11:57:25 pjd Exp $
 */

#ifndef _AESNI_H_
#define _AESNI_H_

#include <sys/types.h>
#include <sys/queue.h>

#include <opencrypto/cryptodev.h>
#include <crypto/aesni/aesni.h>

#if defined(__x86_64__)
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#endif

struct aesni_session {
	uint8_t enc_schedule[AES_SCHED_LEN] __aligned(16);
	uint8_t dec_schedule[AES_SCHED_LEN] __aligned(16);
	uint8_t xts_schedule[AES_SCHED_LEN] __aligned(16);
	uint8_t iv[AES_BLOCK_LEN];
	int algo;
	int rounds;
	/* uint8_t *ses_ictx; */
	/* uint8_t *ses_octx; */
	/* int ses_mlen; */
	int used;
	uint32_t id;
	TAILQ_ENTRY(aesni_session) next;
#if 0
	struct fpu_kern_ctx fpu_ctx;
#endif
};

int aesni_cipher_setup(struct aesni_session *ses,
    struct cryptoini *encini);
int aesni_cipher_process(struct aesni_session *ses,
    struct cryptodesc *enccrd, struct cryptop *crp);
uint8_t *aesni_cipher_alloc(struct cryptodesc *enccrd, struct cryptop *crp,
    int *allocated);

#endif
