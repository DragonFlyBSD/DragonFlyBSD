/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>

#if defined(__DragonFly__)
#include <sys/endian.h>
#elif defined(__linux__)
#include <endian.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include "crc32.h"
#include "tcplay.h"

/* Endianess macros */
#define BE_TO_HOST(n, v) v = be ## n ## toh(v)
#define LE_TO_HOST(n, v) v = le ## n ## toh(v)
#define HOST_TO_BE(n, v) v = htobe ## n (v)
#define HOST_TO_LE(n, v) v = htole ## n (v)

struct tchdr_dec *
decrypt_hdr(struct tchdr_enc *ehdr, struct tc_cipher_chain *cipher_chain,
    unsigned char *key)
{
	struct tchdr_dec *dhdr;
	unsigned char iv[128];
	int error;

	if ((dhdr = alloc_safe_mem(sizeof(struct tchdr_dec))) == NULL) {
		tc_log(1, "Error allocating safe tchdr_dec memory\n");
		return NULL;
	}

	memset(iv, 0, sizeof(iv));

	error = tc_decrypt(cipher_chain, key, iv, ehdr->enc,
	    sizeof(struct tchdr_dec), (unsigned char *)dhdr);
	if (error) {
		tc_log(1, "Header decryption failed\n");
		free_safe_mem(dhdr);
		return NULL;
	}

	BE_TO_HOST(16, dhdr->tc_ver);
	LE_TO_HOST(16, dhdr->tc_min_ver);
	BE_TO_HOST(32, dhdr->crc_keys);
	BE_TO_HOST(64, dhdr->vol_ctime);
	BE_TO_HOST(64, dhdr->hdr_ctime);
	BE_TO_HOST(64, dhdr->sz_hidvol);
	BE_TO_HOST(64, dhdr->sz_vol);
	BE_TO_HOST(64, dhdr->off_mk_scope);
	BE_TO_HOST(64, dhdr->sz_mk_scope);
	BE_TO_HOST(32, dhdr->flags);
	BE_TO_HOST(32, dhdr->sec_sz);
	BE_TO_HOST(32, dhdr->crc_dhdr);

	return dhdr;
}

int
verify_hdr(struct tchdr_dec *hdr)
{
	uint32_t crc;

	if (memcmp(hdr->tc_str, TC_SIG, sizeof(hdr->tc_str)) != 0) {
#ifdef DEBUG
		fprintf(stderr, "Signature mismatch\n");
#endif
		return 0;
	}

	crc = crc32((void *)&hdr->keys, 256);
	if (crc != hdr->crc_keys) {
#ifdef DEBUG
		fprintf(stderr, "CRC32 mismatch (crc_keys)\n");
#endif
		return 0;
	}

	switch(hdr->tc_ver) {
	case 1:
	case 2:
		/* Unsupported header version */
		tc_log(1, "Header version %d unsupported\n", hdr->tc_ver);
		return 0;

	case 3:
	case 4:
		hdr->sec_sz = 512;
		break;
	}

	return 1;
}

struct tchdr_enc *
create_hdr(unsigned char *pass, int passlen, struct pbkdf_prf_algo *prf_algo,
    struct tc_cipher_chain *cipher_chain, size_t sec_sz,
    disksz_t total_blocks __unused,
    off_t offset, disksz_t blocks, int hidden, int weak, struct tchdr_enc **backup_hdr)
{
	struct tchdr_enc *ehdr, *ehdr_backup;
	struct tchdr_dec *dhdr;
	unsigned char *key, *key_backup;
	unsigned char iv[128];
	int error;

	key = key_backup = NULL;
	dhdr = NULL;
	ehdr = ehdr_backup = NULL;

	if (backup_hdr != NULL)
		*backup_hdr = NULL;

	if ((dhdr = (struct tchdr_dec *)alloc_safe_mem(sizeof(*dhdr))) == NULL) {
		tc_log(1, "could not allocate safe dhdr memory\n");
		goto error;
	}

	if ((ehdr = (struct tchdr_enc *)alloc_safe_mem(sizeof(*ehdr))) == NULL) {
		tc_log(1, "could not allocate safe ehdr memory\n");
		goto error;
	}

	if ((ehdr_backup = (struct tchdr_enc *)alloc_safe_mem
	    (sizeof(*ehdr_backup))) == NULL) {
		tc_log(1, "could not allocate safe ehdr_backup memory\n");
		goto error;
	}

	if ((key = alloc_safe_mem(MAX_KEYSZ)) == NULL) {
		tc_log(1, "could not allocate safe key memory\n");
		goto error;
	}

	if ((key_backup = alloc_safe_mem(MAX_KEYSZ)) == NULL) {
		tc_log(1, "could not allocate safe backup key memory\n");
		goto error;
	}

	if ((error = get_random(ehdr->salt, sizeof(ehdr->salt), weak)) != 0) {
		tc_log(1, "could not get salt\n");
		goto error;
	}

	if ((error = get_random(ehdr_backup->salt, sizeof(ehdr_backup->salt), weak))
	    != 0) {
		tc_log(1, "could not get salt for backup header\n");
		goto error;
	}

	error = pbkdf2(prf_algo, (char *)pass, passlen,
	    ehdr->salt, sizeof(ehdr->salt),
	    MAX_KEYSZ, key);
	if (error) {
		tc_log(1, "could not derive key\n");
		goto error;
	}

	error = pbkdf2(prf_algo, (char *)pass, passlen,
	    ehdr_backup->salt, sizeof(ehdr_backup->salt),
	    MAX_KEYSZ, key_backup);
	if (error) {
		tc_log(1, "could not derive backup key\n");
		goto error;
	}

	memset(dhdr, 0, sizeof(*dhdr));

	if ((error = get_random(dhdr->keys, sizeof(dhdr->keys), weak)) != 0) {
		tc_log(1, "could not get key random bits\n");
		goto error;
	}

	memcpy(dhdr->tc_str, "TRUE", 4);
	dhdr->tc_ver = 5;
	dhdr->tc_min_ver = 7;
	dhdr->crc_keys = crc32((void *)&dhdr->keys, 256);
	dhdr->sz_vol = blocks * sec_sz;
	if (hidden)
		dhdr->sz_hidvol = dhdr->sz_vol;
	dhdr->off_mk_scope = offset * sec_sz;
	dhdr->sz_mk_scope = blocks * sec_sz;
	dhdr->sec_sz = sec_sz;
	dhdr->flags = 0;

	HOST_TO_BE(16, dhdr->tc_ver);
	HOST_TO_LE(16, dhdr->tc_min_ver);
	HOST_TO_BE(32, dhdr->crc_keys);
	HOST_TO_BE(64, dhdr->sz_vol);
	HOST_TO_BE(64, dhdr->sz_hidvol);
	HOST_TO_BE(64, dhdr->off_mk_scope);
	HOST_TO_BE(64, dhdr->sz_mk_scope);
	HOST_TO_BE(32, dhdr->sec_sz);
	HOST_TO_BE(32, dhdr->flags);

	dhdr->crc_dhdr = crc32((void *)dhdr, 188);
	HOST_TO_BE(32, dhdr->crc_dhdr);

	memset(iv, 0, sizeof(iv));
	error = tc_encrypt(cipher_chain, key, iv, (unsigned char *)dhdr,
	    sizeof(struct tchdr_dec), ehdr->enc);
	if (error) {
		tc_log(1, "Header encryption failed\n");
		goto error;
	}

	memset(iv, 0, sizeof(iv));
	error = tc_encrypt(cipher_chain, key_backup, iv,
	    (unsigned char *)dhdr,
	    sizeof(struct tchdr_dec), ehdr_backup->enc);
	if (error) {
		tc_log(1, "Backup header encryption failed\n");
		goto error;
	}

	free_safe_mem(key);
	free_safe_mem(key_backup);
	free_safe_mem(dhdr);

	if (backup_hdr != NULL)
		*backup_hdr = ehdr_backup;
	else
		free_safe_mem(ehdr_backup);

	return ehdr;
	/* NOT REACHED */

error:
	if (key)
		free_safe_mem(key);
	if (key_backup)
		free_safe_mem(key_backup);
	if (dhdr)
		free_safe_mem(dhdr);
	if (ehdr)
		free_safe_mem(ehdr);
	if (ehdr_backup)
		free_safe_mem(ehdr_backup);

	return NULL;
}

struct tchdr_enc *copy_reencrypt_hdr(unsigned char *pass, int passlen,
    struct pbkdf_prf_algo *prf_algo, int weak, struct tcplay_info *info,
    struct tchdr_enc **backup_hdr)
{
	struct tchdr_enc *ehdr, *ehdr_backup;
	unsigned char *key, *key_backup;
	unsigned char iv[128];
	int error;

	key = key_backup = NULL;
	ehdr = ehdr_backup = NULL;

	/* By default stick to current PRF algo */
	if (prf_algo == NULL)
		prf_algo = info->pbkdf_prf;

	if ((ehdr = (struct tchdr_enc *)alloc_safe_mem(sizeof(*ehdr))) == NULL) {
		tc_log(1, "could not allocate safe ehdr memory\n");
		goto error;
	}

	if ((ehdr_backup = (struct tchdr_enc *)alloc_safe_mem
	    (sizeof(*ehdr_backup))) == NULL) {
		tc_log(1, "could not allocate safe ehdr_backup memory\n");
		goto error;
	}

	if ((key = alloc_safe_mem(MAX_KEYSZ)) == NULL) {
		tc_log(1, "could not allocate safe key memory\n");
		goto error;
	}

	if ((key_backup = alloc_safe_mem(MAX_KEYSZ)) == NULL) {
		tc_log(1, "could not allocate safe backup key memory\n");
		goto error;
	}

	if ((error = get_random(ehdr->salt, sizeof(ehdr->salt), weak)) != 0) {
		tc_log(1, "could not get salt\n");
		goto error;
	}

	if ((error = get_random(ehdr_backup->salt, sizeof(ehdr_backup->salt), weak))
	    != 0) {
		tc_log(1, "could not get salt for backup header\n");
		goto error;
	}

	error = pbkdf2(prf_algo, (char *)pass, passlen,
	    ehdr->salt, sizeof(ehdr->salt),
	    MAX_KEYSZ, key);
	if (error) {
		tc_log(1, "could not derive key\n");
		goto error;
	}

	error = pbkdf2(prf_algo, (char *)pass, passlen,
	    ehdr_backup->salt, sizeof(ehdr_backup->salt),
	    MAX_KEYSZ, key_backup);
	if (error) {
		tc_log(1, "could not derive backup key\n");
		goto error;
	}

	HOST_TO_BE(16, info->hdr->tc_ver);
	HOST_TO_LE(16, info->hdr->tc_min_ver);
	HOST_TO_BE(32, info->hdr->crc_keys);
	HOST_TO_BE(64, info->hdr->vol_ctime);
	HOST_TO_BE(64, info->hdr->hdr_ctime);
	HOST_TO_BE(64, info->hdr->sz_vol);
	HOST_TO_BE(64, info->hdr->sz_hidvol);
	HOST_TO_BE(64, info->hdr->off_mk_scope);
	HOST_TO_BE(64, info->hdr->sz_mk_scope);
	HOST_TO_BE(32, info->hdr->sec_sz);
	HOST_TO_BE(32, info->hdr->flags);
	HOST_TO_BE(32, info->hdr->crc_dhdr);

	memset(iv, 0, sizeof(iv));
	error = tc_encrypt(info->cipher_chain, key, iv,
	    (unsigned char *)info->hdr, sizeof(struct tchdr_dec), ehdr->enc);
	if (error) {
		tc_log(1, "Header encryption failed\n");
		goto error;
	}

	memset(iv, 0, sizeof(iv));
	error = tc_encrypt(info->cipher_chain, key_backup, iv,
	    (unsigned char *)info->hdr,
	    sizeof(struct tchdr_dec), ehdr_backup->enc);
	if (error) {
		tc_log(1, "Backup header encryption failed\n");
		goto error;
	}

	free_safe_mem(key);
	free_safe_mem(key_backup);

	if (backup_hdr != NULL)
		*backup_hdr = ehdr_backup;
	else
		free_safe_mem(ehdr_backup);

	return ehdr;
	/* NOT REACHED */

error:
	if (key)
		free_safe_mem(key);
	if (key_backup)
		free_safe_mem(key_backup);
	if (ehdr)
		free_safe_mem(ehdr);
	if (ehdr_backup)
		free_safe_mem(ehdr_backup);

	return NULL;
}
