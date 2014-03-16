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

/* Version of tcplay specified during build (CMakeLists.txt, Makefile.classic) */

#ifndef _TCPLAY_H
#define _TCPLAY_H

#define MAX_BLKSZ		4096
#define MAX_KEYSZ		192
#define HDRSZ			512
#define HDR_OFFSET_SYS		31744	/* 512 * (63 -1) */
#define TC_SIG			"TRUE"
#define MAX_PASSSZ		64
#define PASS_BUFSZ		256
#define KPOOL_SZ		64
#define MAX_KFILE_SZ		1048576	/* 1 MB */
#define MAX_KEYFILES		256
#define HDR_OFFSET_HIDDEN	65536
#define BACKUP_HDR_HIDDEN_OFFSET_END	65536
#define BACKUP_HDR_OFFSET_END	131072
#define SALT_LEN		64
#define VOL_RSVD_BYTES_START	(256*512) /* Reserved bytes at vol. start */
#define VOL_RSVD_BYTES_END	(256*512) /* Reserved bytes at vol. end */
#define MIN_VOL_BYTES		(VOL_RSVD_BYTES_START + VOL_RSVD_BYTES_END)

#define MAX_CIPHER_CHAINS	64
#define DEFAULT_RETRIES		3
#define ERASE_BUFFER_SIZE	4*1024*1024 /* 4 MB */

/* TrueCrypt Volume flags */
#define TC_VOLFLAG_SYSTEM	0x01	/* system encryption */
#define TC_VOLFLAG_INPLACE	0x02	/* non-system in-place-encrypted volume */

#define TC_VOLFLAG_SET(f, x)	((f & TC_VOLFLAG_##x) == TC_VOLFLAG_##x)

#define LOG_BUFFER_SZ		1024
#if 0
#define DEBUG 1
#endif

#define TC_FLAG_SYS		0x0001
#define TC_FLAG_FDE		0x0002
#define TC_FLAG_BACKUP		0x0004
#define TC_FLAG_ONLY_RESTORE	0x0008
#define TC_FLAG_ALLOW_TRIM	0x0010
#define TC_FLAG_SAVE_TO_FILE	0x0020
#define TC_FLAG_HDR_FROM_FILE	0x0040
#define TC_FLAG_H_HDR_FROM_FILE	0x0080

#define TC_FLAG_SET(f, x)	((f & TC_FLAG_##x) == TC_FLAG_##x)

#include <limits.h>
#include <inttypes.h>

#if defined(__DragonFly__)
#include <uuid.h>
#elif defined(__linux__)
#include <uuid/uuid.h>
#endif


typedef uint64_t disksz_t;
#define DISKSZ_FMT PRIu64


struct pbkdf_prf_algo {
	const char *name;
	int iteration_count;
};

struct tc_crypto_algo {
	const char *name;
	const char *dm_crypt_str;
	int klen;
	int ivlen;
};

struct tc_cipher_chain {
	struct tc_crypto_algo *cipher;
	unsigned char *key;
	char dm_key[MAX_KEYSZ*2 + 1];

	struct tc_cipher_chain *prev;
	struct tc_cipher_chain *next;
};

struct tchdr_enc {
	unsigned char salt[SALT_LEN];	/* Salt for PBKDF */
	unsigned char enc[448];		/* Encrypted part of the header */
} __attribute__((__packed__));

struct tchdr_dec {
	char		tc_str[4];	/* ASCII string "TRUE" */
	uint16_t	tc_ver;		/* Volume header format version */
	uint16_t	tc_min_ver;
	uint32_t	crc_keys;	/* CRC32 of the key section */
	uint64_t	vol_ctime;	/* Volume creation time */
	uint64_t	hdr_ctime;	/* Header creation time */
	uint64_t	sz_hidvol;	/* Size of hidden volume (set to zero
					   in non-hidden volumes) */
	uint64_t	sz_vol;		/* Size of volume */
	uint64_t	off_mk_scope;	/* Byte offset of the start of the
					   master key scope */
	uint64_t	sz_mk_scope;	/* Size of the encrypted area within
					   the master key scope */
	uint32_t	flags;		/* Flag bits
					   (bit 0: system encryption;
					   bit 1: non-system in-place-encrypted volume;
					   bits 2–31 are reserved) */
	uint32_t	sec_sz;		/* Sector size (in bytes) */
	unsigned char	unused3[120];
	uint32_t	crc_dhdr;	/* CRC32 of dec. header (except keys) */
	unsigned char	keys[256];
} __attribute__((__packed__));

struct tcplay_info {
	char dev[PATH_MAX];
	struct tchdr_dec *hdr;
	struct tc_cipher_chain *cipher_chain;
	struct pbkdf_prf_algo *pbkdf_prf;
	char key[MAX_KEYSZ*2 + 1];

	int flags;
	int volflags;

	uint32_t blk_sz;

	off_t start;	/* Logical volume offset in table (in blk_sz blocks) */
	disksz_t size;	/* Volume size (in blk_sz blocks) */

	off_t skip;	/* IV offset (in blk_sz blocks) */
	off_t offset;	/* Block offset (in blk_sz blocks) */

	/* Populated by dm_setup */
	uuid_t uuid;

	int hidden;
};

#define INFO_TO_DM_BLOCKS(info, memb) \
    (((info)->memb * (uint64_t)((info)->blk_sz))/512)

struct tcplay_dm_table {
	char device[PATH_MAX];	/* Underlying device */
	char target[256];	/* DM Target type */
	off_t start;		/* Logical volume offset in table */
	disksz_t size;		/* Volume size */

	char cipher[256];	/* Cipher */
	off_t skip;		/* IV offset */
	off_t offset;		/* Block offset */
};


typedef int (*tc_state_change_fn)(void *, const char *, int);

struct tcplay_opts {
	/* (Mostly) common options */
	const char	*dev;
	const char	*keyfiles[MAX_KEYFILES];
	int		nkeyfiles;
	const char	*h_keyfiles[MAX_KEYFILES];
	int		n_hkeyfiles;
	struct pbkdf_prf_algo	*prf_algo;
	struct tc_cipher_chain	*cipher_chain;
	struct pbkdf_prf_algo	*h_prf_algo;
	struct tc_cipher_chain	*h_cipher_chain;
	const char	*passphrase;
	const char	*h_passphrase;
	int		interactive;
	int		weak_keys_and_salt;

	/* Options for create */
	int		hidden;
	disksz_t	hidden_size_bytes;
	int		secure_erase; /* XXX: default to 1! */

	/* Options for map, info_mapped */
	const char	*map_name;

	/* Options for info, map, modify */
	int		flags;
	const char	*sys_dev;
	int		protect_hidden;
	int		retries;	/* XXX: default to DEFAULT_RETRIES */
	time_t		timeout;

	const char	*hdr_file_in;
	const char	*h_hdr_file_in;

	/* Options for modify only */
	struct pbkdf_prf_algo	*new_prf_algo;
	const char	*new_passphrase;
	const char	*hdr_file_out;
	const char	*new_keyfiles[MAX_KEYFILES];
	int		n_newkeyfiles;

	void		*api_ctx;
	tc_state_change_fn	state_change_fn;
};


struct tcplay_opts *opts_init(void);
int opts_add_keyfile(struct tcplay_opts *opts, const char *keyfile);
int opts_add_keyfile_hidden(struct tcplay_opts *opts, const char *keyfile);
int opts_add_keyfile_new(struct tcplay_opts *opts, const char *keyfile);
void opts_free(struct tcplay_opts *opts);
void opts_clear_keyfile(struct tcplay_opts *opts);
void opts_clear_keyfile_hidden(struct tcplay_opts *opts);
void opts_clear_keyfile_new(struct tcplay_opts *opts);

void *read_to_safe_mem(const char *file, off_t offset, size_t *sz);
int get_random(unsigned char *buf, size_t len, int weak);
int secure_erase(const char *dev, disksz_t bytes, size_t blksz);
int get_disk_info(const char *dev, disksz_t *blocks, size_t *bsize);
int write_to_disk(const char *dev, off_t offset, size_t blksz, void *mem,
    size_t bytes);
int write_to_file(const char *file, void *mem, size_t bytes);
int read_passphrase(const char *prompt, char *pass, size_t passlen,
    size_t bufsz, time_t timeout);
float get_random_read_progress(void);
float get_secure_erase_progress(void);


int tc_crypto_init(void);
int tc_cipher_chain_populate_keys(struct tc_cipher_chain *cipher_chain,
    unsigned char *key);
int tc_cipher_chain_free_keys(struct tc_cipher_chain *cipher_chain);
int tc_encrypt(struct tc_cipher_chain *cipher_chain, unsigned char *key,
    unsigned char *iv,
    unsigned char *in, int in_len, unsigned char *out);
int tc_decrypt(struct tc_cipher_chain *cipher_chain, unsigned char *key,
    unsigned char *iv,
    unsigned char *in, int in_len, unsigned char *out);

/* The following two are platform dependent */
int syscrypt(struct tc_crypto_algo *cipher, unsigned char *key, size_t klen,
    unsigned char *iv, unsigned char *in, unsigned char *out, size_t len,
    int do_encrypt);
int pbkdf2(struct pbkdf_prf_algo *hash, const char *pass, int passlen,
    const unsigned char *salt, int saltlen,
    int keylen, unsigned char *out);

int apply_keyfiles(unsigned char *pass, size_t pass_memsz, const char *keyfiles[],
    int nkeyfiles);

struct tchdr_enc *create_hdr(unsigned char *pass, int passlen,
    struct pbkdf_prf_algo *prf_algo, struct tc_cipher_chain *cipher_chain,
    size_t sec_sz, disksz_t total_blocks,
    off_t offset, disksz_t blocks, int hidden, int weak,
    struct tchdr_enc **backup_hdr);
struct tchdr_dec *decrypt_hdr(struct tchdr_enc *ehdr,
    struct tc_cipher_chain *cipher_chain, unsigned char *key);
int verify_hdr(struct tchdr_dec *hdr);
struct tchdr_enc *copy_reencrypt_hdr(unsigned char *pass, int passlen,
    struct pbkdf_prf_algo *prf_algo, int weak, struct tcplay_info *info,
    struct tchdr_enc **backup_hdr);

void *_alloc_safe_mem(size_t req_sz, const char *file, int line);
void *_strdup_safe_mem(const char *in, const char *file, int line);
void _free_safe_mem(void *mem, const char *file, int line);
void check_and_purge_safe_mem(void);

struct tc_crypto_algo *check_cipher(const char *cipher, int quiet);
struct tc_cipher_chain *check_cipher_chain(const char *cipher_chain, int quiet);
struct pbkdf_prf_algo *check_prf_algo(const char *algo, int quiet);

int tc_play_init(void);
void tc_log(int err, const char *fmt, ...);
int tc_cipher_chain_klen(struct tc_cipher_chain *chain);
int tc_cipher_chain_length(struct tc_cipher_chain *chain);
char *tc_cipher_chain_sprint(char *buf, size_t bufsz,
    struct tc_cipher_chain *chain);
int free_info(struct tcplay_info *info);
void print_info(struct tcplay_info *info);
int adjust_info(struct tcplay_info *info, struct tcplay_info *hinfo);
int process_hdr(const char *dev, int flags, unsigned char *pass, int passlen,
    struct tchdr_enc *ehdr, struct tcplay_info **pinfo);
int create_volume(struct tcplay_opts *opts);
struct tcplay_info *info_map_common(struct tcplay_opts *opts,
    char *passphrase_out);
int info_mapped_volume(struct tcplay_opts *opts);
int info_volume(struct tcplay_opts *opts);
int map_volume(struct tcplay_opts *opts);
int modify_volume(struct tcplay_opts *opts);
int dm_setup(const char *mapname, struct tcplay_info *info);
int dm_teardown(const char *mapname, const char *device);
struct tcplay_info *dm_info_map(const char *map_name);

typedef void(*summary_fn_t)(void);

extern int tc_internal_verbose;
extern char tc_internal_log_buffer[];
extern summary_fn_t summary_fn;
extern struct pbkdf_prf_algo pbkdf_prf_algos[];
extern struct tc_cipher_chain *tc_cipher_chains[MAX_CIPHER_CHAINS];

#define STATE_UNKNOWN		0
#define STATE_GET_RANDOM	1
#define STATE_ERASE		2

extern int tc_internal_state;
#ifndef	__DECONST
#define	__DECONST(type, var)	((type)(uintptr_t)(const void *)(var))
#endif

#define alloc_safe_mem(x) \
	_alloc_safe_mem(x, __FILE__, __LINE__)

#define strdup_safe_mem(x) \
	_strdup_safe_mem(x, __FILE__, __LINE__)

#define free_safe_mem(x) \
	_free_safe_mem(__DECONST(void *, x), __FILE__, __LINE__)

#define __unused       __attribute__((__unused__))

#endif
