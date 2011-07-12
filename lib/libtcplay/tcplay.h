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

/* Version of tcplay */
#define MAJ_VER			0
#define MIN_VER			8


#define MAX_BLKSZ		4096
#define MAX_KEYSZ		192
#define HDRSZ			512
#define HDR_OFFSET_SYS		31744	/* 512 * (63 -1) */
#define TC_SIG			"TRUE"
#define MAX_PASSSZ		64
#define KPOOL_SZ		64
#define MAX_KFILE_SZ		1048576	/* 1 MB */
#define MAX_KEYFILES		256
#define HDR_OFFSET_HIDDEN	65536
#define SALT_LEN		64
#define MIN_VOL_BLOCKS		256
#define MAX_CIPHER_CHAINS	64
#define DEFAULT_RETRIES		3
#define ERASE_BUFFER_SIZE	4*1024*1024 /* 4 MB */

/* TrueCrypt Volume flags */
#define TC_VOLFLAG_SYSTEM	0x01	/* system encryption */
#define TC_VOLFLAG_INPLACE	0x02	/* non-system in-place-encrypted volume */

#define LOG_BUFFER_SZ		1024
#if 0
#define DEBUG 1
#endif

#include <uuid.h>

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
	uint64_t	sz_hidvol;	/*  Size of hidden volume (set to zero
					    in non-hidden volumes) */
	uint64_t	sz_vol;		/*  Size of volume */
	uint64_t	off_mk_scope;	/*  Byte offset of the start of the
					    master key scope */
	uint64_t	sz_mk_scope;	/*  Size of the encrypted area within
					    the master key scope */
	uint32_t	flags;		/*  Flag bits
					    (bit 0: system encryption;
					    bit 1: non-system in-place-encrypted volume;
					    bits 2–31 are reserved) */
	uint32_t	sec_sz;		/*  Sector size (in bytes) */
	unsigned char	unused3[120];
	uint32_t	crc_dhdr;	/* CRC32 of dec. header (except keys) */
	unsigned char	keys[256];
} __attribute__((__packed__));

struct tcplay_info {
	const char *dev;
	struct tchdr_dec *hdr;
	struct tc_cipher_chain *cipher_chain;
	struct pbkdf_prf_algo *pbkdf_prf;
	char key[MAX_KEYSZ*2 + 1];
	off_t start;	/* Logical volume offset in table */
	size_t size;	/* Volume size */

	off_t skip;	/* IV offset */
	off_t offset;	/* Block offset */

	/* Populated by dm_setup */
	uuid_t uuid;
};

void *read_to_safe_mem(const char *file, off_t offset, size_t *sz);
int get_random(unsigned char *buf, size_t len);
int secure_erase(const char *dev, size_t bytes, size_t blksz);
int get_disk_info(const char *dev, size_t *blocks, size_t *bsize);
int write_mem(const char *dev, off_t offset, size_t blksz, void *mem, size_t bytes);
int read_passphrase(const char *prompt, char *pass, size_t passlen,
    time_t timeout);

int tc_crypto_init(void);
int tc_cipher_chain_populate_keys(struct tc_cipher_chain *cipher_chain,
    unsigned char *key);
int tc_encrypt(struct tc_cipher_chain *cipher_chain, unsigned char *key,
    unsigned char *iv,
    unsigned char *in, int in_len, unsigned char *out);
int tc_decrypt(struct tc_cipher_chain *cipher_chain, unsigned char *key,
    unsigned char *iv,
    unsigned char *in, int in_len, unsigned char *out);
int pbkdf2(const char *pass, int passlen, const unsigned char *salt, int saltlen,
    int iter, const char *hash_name, int keylen, unsigned char *out);
int apply_keyfiles(unsigned char *pass, size_t pass_memsz, const char *keyfiles[],
    int nkeyfiles);

struct tchdr_enc *create_hdr(unsigned char *pass, int passlen,
    struct pbkdf_prf_algo *prf_algo, struct tc_cipher_chain *cipher_chain,
    size_t sec_sz, size_t total_blocks,
    off_t offset, size_t blocks, int hidden);
struct tchdr_dec *decrypt_hdr(struct tchdr_enc *ehdr,
    struct tc_cipher_chain *cipher_chain, unsigned char *key);
int verify_hdr(struct tchdr_dec *hdr);

void *_alloc_safe_mem(size_t req_sz, const char *file, int line);
void _free_safe_mem(void *mem, const char *file, int line);
void check_and_purge_safe_mem(void);

struct tc_crypto_algo *check_cipher(const char *cipher, int quiet);
struct tc_cipher_chain *check_cipher_chain(char *cipher_chain, int quiet);
struct pbkdf_prf_algo *check_prf_algo(char *algo, int quiet);

int tc_play_init(void);
void tc_log(int err, const char *fmt, ...);
void print_info(struct tcplay_info *info);
int adjust_info(struct tcplay_info *info, struct tcplay_info *hinfo);
int process_hdr(const char *dev, unsigned char *pass, int passlen,
    struct tchdr_enc *ehdr, struct tcplay_info **pinfo);
int create_volume(const char *dev, int hidden, const char *keyfiles[],
    int nkeyfiles, const char *h_keyfiles[], int n_hkeyfiles,
    struct pbkdf_prf_algo *prf_algo, struct tc_cipher_chain *cipher_chain,
    struct pbkdf_prf_algo *h_prf_algo, struct tc_cipher_chain *h_cipher_chain,
    char *passphrase, char *h_passphrase, size_t hidden_blocks_in,
    int interactive);
int info_volume(const char *device, int sflag, const char *sys_dev,
    int protect_hidden, const char *keyfiles[], int nkeyfiles,
    const char *h_keyfiles[], int n_hkeyfiles,
    char *passphrase, char *passphrase_hidden, int interactive, int retries,
    time_t timeout);
int map_volume(const char *map_name, const char *device, int sflag,
    const char *sys_dev, int protect_hidden, const char *keyfiles[],
    int nkeyfiles, const char *h_keyfiles[], int n_hkeyfiles,
    char *passphrase, char *passphrase_hidden, int interactive, int retries,
    time_t timeout);
int dm_setup(const char *mapname, struct tcplay_info *info);
int dm_teardown(const char *mapname, const char *device);

typedef void(*summary_fn_t)(void);

extern int tc_internal_verbose;
extern char tc_internal_log_buffer[];
extern summary_fn_t summary_fn;

#define alloc_safe_mem(x) \
	_alloc_safe_mem(x, __FILE__, __LINE__)

#define free_safe_mem(x) \
	_free_safe_mem(x, __FILE__, __LINE__)
