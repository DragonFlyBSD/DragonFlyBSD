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

#define _BSD_SOURCE
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__DragonFly__)
#include <sys/param.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <time.h>
#if defined(__linux__)
#include <libdevmapper.h>
#include <uuid/uuid.h>
#elif defined(__DragonFly__)
#include <libdm.h>
#include <uuid.h>
#endif

#include <dirent.h>

#include "crc32.h"
#include "tcplay.h"
#include "humanize.h"


/* XXX TODO:
 *  - LRW-benbi support? needs further work in dm-crypt and even opencrypto
 *  - secure buffer review (i.e: is everything that needs it using secure mem?)
 *  - mlockall? (at least MCL_FUTURE, which is the only one we support)
 */

summary_fn_t summary_fn = NULL;
int tc_internal_verbose = 1;
char tc_internal_log_buffer[LOG_BUFFER_SZ];
int tc_internal_state = STATE_UNKNOWN;

void
tc_log(int is_err, const char *fmt, ...)
{
	va_list ap;
	FILE *fp;

	if (is_err)
		fp = stderr;
	else
		fp = stdout;

        va_start(ap, fmt);

	vsnprintf(tc_internal_log_buffer, LOG_BUFFER_SZ, fmt, ap);

	va_end(ap);

	if (tc_internal_verbose)
	    fprintf(fp, "%s", tc_internal_log_buffer);
}

/* Supported algorithms */
struct pbkdf_prf_algo pbkdf_prf_algos[] = {
	{ "RIPEMD160",	2000 }, /* needs to come before the other RIPEMD160 */
	{ "RIPEMD160",	1000 },
	{ "SHA512",	1000 },
	{ "whirlpool",	1000 },
	{ NULL,		0    }
};

struct tc_crypto_algo tc_crypto_algos[] = {
#if 0
	/* XXX: turns out TC doesn't support AES-128-XTS */
	{ "AES-128-XTS",	"aes-xts-plain",	32,	8 },
	{ "TWOFISH-128-XTS",	"twofish-xts-plain",	32,	8 },
	{ "SERPENT-128-XTS",	"serpent-xts-plain",	32,	8 },
#endif
	{ "AES-256-XTS",	"aes-xts-plain64",	64,	8 },
	{ "TWOFISH-256-XTS",	"twofish-xts-plain64",	64,	8 },
	{ "SERPENT-256-XTS",	"serpent-xts-plain64",	64,	8 },
	{ NULL,			NULL,			0,	0 }
};

const char *valid_cipher_chains[][MAX_CIPHER_CHAINS] = {
	{ "AES-256-XTS", NULL },
	{ "TWOFISH-256-XTS", NULL },
	{ "SERPENT-256-XTS", NULL },
	{ "AES-256-XTS", "TWOFISH-256-XTS", "SERPENT-256-XTS", NULL },
	{ "SERPENT-256-XTS", "TWOFISH-256-XTS", "AES-256-XTS", NULL },
#if 0
	/* It seems that all the two-way cascades are the other way round... */
	{ "AES-256-XTS", "TWOFISH-256-XTS", NULL },
	{ "SERPENT-256-XTS", "AES-256-XTS", NULL },
	{ "TWOFISH-256-XTS", "SERPENT-256-XTS", NULL },

#endif
	{ "TWOFISH-256-XTS", "AES-256-XTS", NULL },
	{ "AES-256-XTS", "SERPENT-256-XTS", NULL },
	{ "SERPENT-256-XTS", "TWOFISH-256-XTS", NULL },
	{ NULL }
};

struct tc_cipher_chain *tc_cipher_chains[MAX_CIPHER_CHAINS];

static
int
tc_build_cipher_chains(void)
{
	struct tc_cipher_chain *chain, *elem, *prev;
	int i = 0;
	int k;

	while (valid_cipher_chains[i][0] != NULL) {
		chain = NULL;
		prev = NULL;
		k = 0;

		while (valid_cipher_chains[i][k] != NULL) {
			if ((elem = alloc_safe_mem(sizeof(*elem))) == NULL) {
				tc_log(1, "Error allocating memory for "
				   "cipher chain\n");
				return -1;
			}

			/* Initialize first element of chain */
			if (chain == NULL) {
				chain = elem;
				elem->prev = NULL;
			}

			/* Populate previous element */
			if (prev != NULL) {
				prev->next = elem;
				elem->prev = prev;
			}

			/* Assume we are the last element in the chain */
			elem->next = NULL;

			/* Initialize other fields */
			elem->cipher = check_cipher(valid_cipher_chains[i][k], 0);
			if (elem->cipher == NULL)
				return -1;

			elem->key = NULL;

			prev = elem;
			++k;
		}

		/* Store cipher chain */
		tc_cipher_chains[i++] = chain;

		/* Integrity check */
		if (i >= MAX_CIPHER_CHAINS) {
			tc_log(1, "FATAL: tc_cipher_chains is full!!\n");
			return -1;
		}

		/* Make sure array is NULL terminated */
		tc_cipher_chains[i] = NULL;
	}

	return 0;
}

static
struct tc_cipher_chain *
tc_dup_cipher_chain(struct tc_cipher_chain *src)
{
	struct tc_cipher_chain *first = NULL, *prev = NULL, *elem;

	for (; src != NULL; src = src->next) {
		if ((elem = alloc_safe_mem(sizeof(*elem))) == NULL) {
			tc_log(1, "Error allocating memory for "
			    "duplicate cipher chain\n");
			return NULL;
		}

		memcpy(elem, src, sizeof(*elem));

		if (src->key != NULL) {
			if ((elem->key = alloc_safe_mem(src->cipher->klen)) == NULL) {
				tc_log(1, "Error allocating memory for "
				    "duplicate key in cipher chain\n");
				return NULL;
			}

			memcpy(elem->key, src->key, src->cipher->klen);
		}

		if (first == NULL)
			first = elem;

		elem->next = NULL;
		elem->prev = prev;

		if (prev != NULL)
			prev->next = elem;

		prev = elem;
	}

	return first;
}

static
int
tc_free_cipher_chain(struct tc_cipher_chain *chain)
{
	struct tc_cipher_chain *next = chain;

	while ((chain = next) != NULL) {
		next = chain->next;

		if (chain->key != NULL)
			free_safe_mem(chain->key);
		free_safe_mem(chain);
	}

	return 0;
}

int
tc_cipher_chain_length(struct tc_cipher_chain *chain)
{
	int len = 0;

	for (; chain != NULL; chain = chain->next)
		++len;

	return len;
}

int
tc_cipher_chain_klen(struct tc_cipher_chain *chain)
{
	int klen_bytes = 0;

	for (; chain != NULL; chain = chain->next) {
		klen_bytes += chain->cipher->klen;
	}

	return klen_bytes;
}

char *
tc_cipher_chain_sprint(char *buf, size_t bufsz, struct tc_cipher_chain *chain)
{
	static char sbuf[256];
	int n = 0;

	if (buf == NULL) {
		buf = sbuf;
		bufsz = sizeof(sbuf);
	}

	for (; chain != NULL; chain = chain->next) {
		n += snprintf(buf+n, bufsz-n, "%s%s", chain->cipher->name,
		    (chain->next != NULL) ? "," : "\0");
	}

	return buf;
}

#ifdef DEBUG
static void
print_hex(unsigned char *buf, off_t start, size_t len)
{
	size_t i;

	for (i = start; i < start+len; i++)
		printf("%02x", buf[i]);

	printf("\n");
}
#endif

void
print_info(struct tcplay_info *info)
{
	printf("Device:\t\t\t%s\n", info->dev);

	if (info->pbkdf_prf != NULL) {
		printf("PBKDF2 PRF:\t\t%s\n", info->pbkdf_prf->name);
		printf("PBKDF2 iterations:\t%d\n",
		    info->pbkdf_prf->iteration_count);
	}

	printf("Cipher:\t\t\t%s\n",
	    tc_cipher_chain_sprint(NULL, 0, info->cipher_chain));

	printf("Key Length:\t\t%d bits\n",
	    8*tc_cipher_chain_klen(info->cipher_chain));

	if (info->hdr != NULL) {
		printf("CRC Key Data:\t\t%#x\n", info->hdr->crc_keys);
		printf("Sector size:\t\t%d\n", info->hdr->sec_sz);
	} else {
		printf("Sector size:\t\t512\n");
	}
	printf("Volume size:\t\t%"DISKSZ_FMT" sectors\n", info->size);
#if 0
	/* Don't print this; it's always 0 and is rather confusing */
	printf("Volume offset:\t\t%"PRIu64"\n", (uint64_t)info->start);
#endif

#ifdef DEBUG
	printf("Vol Flags:\t\t%d\n", info->volflags);
#endif

	printf("IV offset:\t\t%"PRIu64" sectors\n",
	    (uint64_t)info->skip);
	printf("Block offset:\t\t%"PRIu64" sectors\n",
	    (uint64_t)info->offset);
}

static
struct tcplay_info *
new_info(const char *dev, int flags, struct tc_cipher_chain *cipher_chain,
    struct pbkdf_prf_algo *prf, struct tchdr_dec *hdr, off_t start)
{
	struct tc_cipher_chain *chain_start;
	struct tcplay_info *info;
	int i;
	int error;

	chain_start = cipher_chain;

	if ((info = (struct tcplay_info *)alloc_safe_mem(sizeof(*info))) == NULL) {
		tc_log(1, "could not allocate safe info memory\n");
		return NULL;
	}

	strncpy(info->dev, dev, sizeof(info->dev));
	info->cipher_chain = cipher_chain;
	info->pbkdf_prf = prf;
	info->start = start;
	info->hdr = hdr;
	info->blk_sz = hdr->sec_sz;
	info->size = hdr->sz_mk_scope / hdr->sec_sz;	/* volume size */
	info->skip = hdr->off_mk_scope / hdr->sec_sz;	/* iv skip */

	info->volflags = hdr->flags;
	info->flags = flags;

	if (TC_FLAG_SET(flags, SYS))
		info->offset = 0; /* offset is 0 for system volumes */
	else
		info->offset = hdr->off_mk_scope / hdr->sec_sz;	/* block offset */

	/* Associate a key out of the key pool with each cipher in the chain */
	error = tc_cipher_chain_populate_keys(cipher_chain, hdr->keys);
	if (error) {
		tc_log(1, "could not populate keys in cipher chain\n");
		return NULL;
	}

	for (; cipher_chain != NULL; cipher_chain = cipher_chain->next) {
		for (i = 0; i < cipher_chain->cipher->klen; i++)
			sprintf(&cipher_chain->dm_key[i*2], "%02x",
			    cipher_chain->key[i]);
	}

	tc_cipher_chain_free_keys(chain_start);

	return info;
}

int
free_info(struct tcplay_info *info)
{
	if (info->cipher_chain)
		tc_free_cipher_chain(info->cipher_chain);
	if (info->hdr)
		free_safe_mem(info->hdr);

	free_safe_mem(info);

	return 0;
}

int
adjust_info(struct tcplay_info *info, struct tcplay_info *hinfo)
{
	if (hinfo->hdr->sz_hidvol == 0)
		return 1;

	info->size -= hinfo->hdr->sz_hidvol / hinfo->hdr->sec_sz;
	return 0;
}

int
process_hdr(const char *dev, int flags, unsigned char *pass, int passlen,
    struct tchdr_enc *ehdr, struct tcplay_info **pinfo)
{
	struct tchdr_dec *dhdr;
	struct tcplay_info *info;
	struct tc_cipher_chain *cipher_chain = NULL;
	unsigned char *key;
	int i, j, found, error;

	*pinfo = NULL;

	if ((key = alloc_safe_mem(MAX_KEYSZ)) == NULL) {
		tc_log(1, "could not allocate safe key memory\n");
		return ENOMEM;
	}

	/* Start search for correct algorithm combination */
	found = 0;
	for (i = 0; !found && pbkdf_prf_algos[i].name != NULL; i++) {
#ifdef DEBUG
		printf("\nTrying PRF algo %s (%d)\n", pbkdf_prf_algos[i].name,
		    pbkdf_prf_algos[i].iteration_count);
		printf("Salt: ");
		print_hex(ehdr->salt, 0, sizeof(ehdr->salt));
#endif
		error = pbkdf2(&pbkdf_prf_algos[i], (char *)pass, passlen,
		    ehdr->salt, sizeof(ehdr->salt),
		    MAX_KEYSZ, key);

		if (error) {
			tc_log(1, "pbkdf failed for algorithm %s\n",
			    pbkdf_prf_algos[i].name);
			free_safe_mem(key);
			return EINVAL;
		}

#if 0
		printf("Derived Key: ");
		print_hex(key, 0, MAX_KEYSZ);
#endif

		for (j = 0; !found && tc_cipher_chains[j] != NULL; j++) {
			cipher_chain = tc_dup_cipher_chain(tc_cipher_chains[j]);
#ifdef DEBUG
			printf("\nTrying cipher chain %d\n", j);
#endif

			dhdr = decrypt_hdr(ehdr, cipher_chain, key);
			if (dhdr == NULL) {
				tc_log(1, "hdr decryption failed for cipher "
				    "chain %d\n", j);
				free_safe_mem(key);
				return EINVAL;
			}

			if (verify_hdr(dhdr)) {
#ifdef DEBUG
				printf("tc_str: %.4s, tc_ver: %d, tc_min_ver: %d, "
				    "crc_keys: %d, sz_vol: %"PRIu64", "
				    "off_mk_scope: %"PRIu64", sz_mk_scope: %"PRIu64", "
				    "flags: %d, sec_sz: %d crc_dhdr: %d\n",
				    dhdr->tc_str, dhdr->tc_ver, dhdr->tc_min_ver,
				    dhdr->crc_keys, dhdr->sz_vol, dhdr->off_mk_scope,
				    dhdr->sz_mk_scope, dhdr->flags, dhdr->sec_sz,
				    dhdr->crc_dhdr);
#endif
				found = 1;
			} else {
				free_safe_mem(dhdr);
				tc_free_cipher_chain(cipher_chain);
			}
		}
	}

	free_safe_mem(key);

	if (!found)
		return EINVAL;

	if ((info = new_info(dev, flags, cipher_chain,
	    &pbkdf_prf_algos[i-1], dhdr, 0)) == NULL) {
		free_safe_mem(dhdr);
		return ENOMEM;
	}

	*pinfo = info;

	return 0;
}

int
create_volume(struct tcplay_opts *opts)
{
	char *pass, *pass_again;
	char *h_pass = NULL;
	char buf[1024];
	disksz_t blocks, hidden_blocks = 0;
	size_t blksz;
	struct tchdr_enc *ehdr, *hehdr;
	struct tchdr_enc *ehdr_backup, *hehdr_backup;
	uint64_t tmp;
	int error, r, ret;

	pass = h_pass = pass_again = NULL;
	ehdr = hehdr = NULL;
	ehdr_backup = hehdr_backup = NULL;
	ret = -1; /* Default to returning error */

	if (opts->cipher_chain == NULL)
		opts->cipher_chain = tc_cipher_chains[0];
	if (opts->prf_algo == NULL)
		opts->prf_algo = &pbkdf_prf_algos[0];
	if (opts->h_cipher_chain == NULL)
		opts->h_cipher_chain = opts->cipher_chain;
	if (opts->h_prf_algo == NULL)
		opts->h_prf_algo = opts->prf_algo;

	if ((error = get_disk_info(opts->dev, &blocks, &blksz)) != 0) {
		tc_log(1, "could not get disk info\n");
		return -1;
	}

	if ((blocks*blksz) <= MIN_VOL_BYTES) {
		tc_log(1, "Cannot create volumes on devices with less "
		    "than %d bytes\n", MIN_VOL_BYTES);
		return -1;
	}

	if (opts->interactive) {
		if (((pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) ||
		   ((pass_again = alloc_safe_mem(PASS_BUFSZ)) == NULL)) {
			tc_log(1, "could not allocate safe passphrase memory\n");
			goto out;
		}

		if ((error = read_passphrase("Passphrase: ", pass, MAX_PASSSZ,
		    PASS_BUFSZ, 0) ||
		    (read_passphrase("Repeat passphrase: ", pass_again,
		    MAX_PASSSZ, PASS_BUFSZ, 0)))) {
			tc_log(1, "could not read passphrase\n");
			goto out;
		}

		if (strcmp(pass, pass_again) != 0) {
			tc_log(1, "Passphrases don't match\n");
			goto out;
		}

		free_safe_mem(pass_again);
		pass_again = NULL;
	} else {
		/* In batch mode, use provided passphrase */
		if ((pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) {
			tc_log(1, "could not allocate safe "
			    "passphrase memory");
			goto out;
		}

		if (opts->passphrase != NULL) {
			strncpy(pass, opts->passphrase, MAX_PASSSZ);
			pass[MAX_PASSSZ] = '\0';
		}
	}

	if (opts->nkeyfiles > 0) {
		/* Apply keyfiles to 'pass' */
		if ((error = apply_keyfiles((unsigned char *)pass, PASS_BUFSZ,
		    opts->keyfiles, opts->nkeyfiles))) {
			tc_log(1, "could not apply keyfiles\n");
			goto out;
		}
	}

	if (opts->hidden) {
		if (opts->interactive) {
			if (((h_pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) ||
			   ((pass_again = alloc_safe_mem(PASS_BUFSZ)) == NULL)) {
				tc_log(1, "could not allocate safe "
				    "passphrase memory\n");
				goto out;
			}

			if ((error = read_passphrase("Passphrase for hidden volume: ",
			   h_pass, MAX_PASSSZ, PASS_BUFSZ, 0) ||
			   (read_passphrase("Repeat passphrase: ", pass_again,
			   MAX_PASSSZ, PASS_BUFSZ, 0)))) {
				tc_log(1, "could not read passphrase\n");
				goto out;
			}

			if (strcmp(h_pass, pass_again) != 0) {
				tc_log(1, "Passphrases for hidden volume don't "
				    "match\n");
				goto out;
			}

			free_safe_mem(pass_again);
			pass_again = NULL;
		} else {
			/* In batch mode, use provided passphrase */
			if ((h_pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) {
				tc_log(1, "could not allocate safe "
				    "passphrase memory");
				goto out;
			}

			if (opts->h_passphrase != NULL) {
				strncpy(h_pass, opts->h_passphrase, MAX_PASSSZ);
				h_pass[MAX_PASSSZ] = '\0';
			}
		}

		if (opts->n_hkeyfiles > 0) {
			/* Apply keyfiles to 'h_pass' */
			if ((error = apply_keyfiles((unsigned char *)h_pass,
			    PASS_BUFSZ, opts->h_keyfiles, opts->n_hkeyfiles))) {
				tc_log(1, "could not apply keyfiles\n");
				goto out;
			}
		}

		if (opts->interactive) {
			hidden_blocks = 0;
		} else {
			hidden_blocks = opts->hidden_size_bytes/blksz;
			if (hidden_blocks == 0) {
				tc_log(1, "hidden_blocks to create volume "
				    "cannot be zero!\n");
				goto out;
			}

			if (opts->hidden_size_bytes >=
			    (blocks*blksz) - MIN_VOL_BYTES) {
				tc_log(1, "Hidden volume needs to be "
				    "smaller than the outer volume\n");
				goto out;
			}
		}

		/* This only happens in interactive mode */
		while (hidden_blocks == 0) {
			if ((r = _humanize_number(buf, sizeof(buf),
			    (uint64_t)(blocks * blksz))) < 0) {
				sprintf(buf, "%"DISKSZ_FMT" bytes", (blocks * blksz));
			}

			printf("The total volume size of %s is %s (bytes)\n", opts->dev, buf);
			memset(buf, 0, sizeof(buf));
			printf("Size of hidden volume (e.g. 127M):  ");
			fflush(stdout);

			if ((fgets(buf, sizeof(buf), stdin)) == NULL) {
				tc_log(1, "Could not read from stdin\n");
				goto out;
			}

			/* get rid of trailing newline */
			buf[strlen(buf)-1] = '\0';
			if ((error = _dehumanize_number(buf,
			    &tmp)) != 0) {
				tc_log(1, "Could not interpret input: %s\n", buf);
				continue;
			}

			if (tmp >= (blocks*blksz) - MIN_VOL_BYTES) {
				tc_log(1, "Hidden volume needs to be "
				    "smaller than the outer volume\n");
				hidden_blocks = 0;
				continue;
			}

			hidden_blocks = (size_t)tmp;
			hidden_blocks /= blksz;
		}
	}

	if (opts->interactive) {
		/* Show summary and ask for confirmation */
		printf("Summary of actions:\n");
		if (opts->secure_erase)
			printf(" - Completely erase *EVERYTHING* on %s\n", opts->dev);
		printf(" - Create %svolume on %s\n", opts->hidden?("outer "):"", opts->dev);
		if (opts->hidden) {
			printf(" - Create hidden volume of %"DISKSZ_FMT" bytes at end of "
			    "outer volume\n",
			    hidden_blocks * blksz);
		}

		printf("\n Are you sure you want to proceed? (y/n) ");
		fflush(stdout);
		if ((fgets(buf, sizeof(buf), stdin)) == NULL) {
			tc_log(1, "Could not read from stdin\n");
			goto out;
		}

		if ((buf[0] != 'y') && (buf[0] != 'Y')) {
			tc_log(1, "User cancelled action(s)\n");
			goto out;
		}
	}

	/* erase volume */
	if (opts->secure_erase) {
		tc_log(0, "Securely erasing the volume...\nThis process may take "
		    "some time depending on the size of the volume\n");

		if (opts->state_change_fn)
			opts->state_change_fn(opts->api_ctx, "secure_erase", 1);

		if ((error = secure_erase(opts->dev, blocks * blksz, blksz)) != 0) {
			tc_log(1, "could not securely erase device %s\n", opts->dev);
			goto out;
		}

		if (opts->state_change_fn)
			opts->state_change_fn(opts->api_ctx, "secure_erase", 0);
	}

	tc_log(0, "Creating volume headers...\nDepending on your system, this "
	    "process may take a few minutes as it uses true random data which "
	    "might take a while to refill\n");

	if (opts->weak_keys_and_salt) {
		tc_log(0, "WARNING: Using a weak random generator to get "
		    "entropy for the key material. Odds are this is NOT "
		    "what you want.\n");
	}

	if (opts->state_change_fn)
		opts->state_change_fn(opts->api_ctx, "create_header", 1);

	/* create encrypted headers */
	ehdr = create_hdr((unsigned char *)pass,
	    (opts->nkeyfiles > 0)?MAX_PASSSZ:strlen(pass),
	    opts->prf_algo, opts->cipher_chain, blksz, blocks, VOL_RSVD_BYTES_START/blksz,
	    blocks - (MIN_VOL_BYTES/blksz), 0, opts->weak_keys_and_salt, &ehdr_backup);
	if (ehdr == NULL) {
		tc_log(1, "Could not create header\n");
		goto out;
	}

	if (opts->hidden) {
		hehdr = create_hdr((unsigned char *)h_pass,
		    (opts->n_hkeyfiles > 0)?MAX_PASSSZ:strlen(h_pass), opts->h_prf_algo,
		    opts->h_cipher_chain,
		    blksz, blocks,
		    blocks - (VOL_RSVD_BYTES_END/blksz) - hidden_blocks,
		    hidden_blocks, 1, opts->weak_keys_and_salt, &hehdr_backup);
		if (hehdr == NULL) {
			tc_log(1, "Could not create hidden volume header\n");
			goto out;
		}
	}

	if (opts->state_change_fn)
		opts->state_change_fn(opts->api_ctx, "create_header", 0);

	tc_log(0, "Writing volume headers to disk...\n");

	if ((error = write_to_disk(opts->dev, 0, blksz, ehdr, sizeof(*ehdr))) != 0) {
		tc_log(1, "Could not write volume header to device\n");
		goto out;
	}

	/* Write backup header; it's offset is relative to the end */
	if ((error = write_to_disk(opts->dev, (blocks*blksz - BACKUP_HDR_OFFSET_END),
	    blksz, ehdr_backup, sizeof(*ehdr_backup))) != 0) {
		tc_log(1, "Could not write backup volume header to device\n");
		goto out;
	}

	if (opts->hidden) {
		if ((error = write_to_disk(opts->dev, HDR_OFFSET_HIDDEN, blksz, hehdr,
		    sizeof(*hehdr))) != 0) {
			tc_log(1, "Could not write hidden volume header to "
			    "device\n");
			goto out;
		}

		/* Write backup hidden header; offset is relative to end */
		if ((error = write_to_disk(opts->dev,
		    (blocks*blksz - BACKUP_HDR_HIDDEN_OFFSET_END), blksz,
		    hehdr_backup, sizeof(*hehdr_backup))) != 0) {
			tc_log(1, "Could not write backup hidden volume "
			    "header to device\n");
			goto out;
		}
	}

	/* Everything went ok */
	tc_log(0, "All done!\n");

	ret = 0;

out:
	if (pass)
		free_safe_mem(pass);
	if (h_pass)
		free_safe_mem(h_pass);
	if (pass_again)
		free_safe_mem(pass_again);
	if (ehdr)
		free_safe_mem(ehdr);
	if (hehdr)
		free_safe_mem(hehdr);
	if (ehdr_backup)
		free_safe_mem(ehdr_backup);
	if (hehdr_backup)
		free_safe_mem(hehdr_backup);

	return ret;
}

struct tcplay_info *
info_map_common(struct tcplay_opts *opts, char *passphrase_out)
{
	struct tchdr_enc *ehdr, *hehdr = NULL;
	struct tcplay_info *info, *hinfo = NULL;
	char *pass;
	char *h_pass;
	int error, error2 = 0;
	size_t sz;
	size_t blksz;
	disksz_t blocks;
	int is_hidden = 0;
	int try_empty = 0;
	int retries;

	if ((error = get_disk_info(opts->dev, &blocks, &blksz)) != 0) {
		tc_log(1, "could not get disk information\n");
		return NULL;
	}

	if (opts->retries < 1)
		retries = 1;
	else
		retries = opts->retries;

	/*
	 * Add one retry so we can do a first try without asking for
	 * a password if keyfiles are passed in.
	 */
	if (opts->interactive && (opts->nkeyfiles > 0)) {
		try_empty = 1;
		++retries;
	}

	info = NULL;

	ehdr = NULL;
	pass = h_pass = NULL;

	while ((info == NULL) && retries-- > 0)
	{
		pass = h_pass = NULL;
		ehdr = hehdr = NULL;
		info = hinfo = NULL;

		if ((pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) {
			tc_log(1, "could not allocate safe passphrase memory\n");
			goto out;
		}

		if (try_empty) {
			pass[0] = '\0';
		} else if (opts->interactive) {
		        if ((error = read_passphrase("Passphrase: ", pass,
			    MAX_PASSSZ, PASS_BUFSZ, opts->timeout))) {
				tc_log(1, "could not read passphrase\n");
				/* XXX: handle timeout differently? */
				goto out;
			}
			pass[MAX_PASSSZ] = '\0';
		} else {
			/* In batch mode, use provided passphrase */
			if (opts->passphrase != NULL) {
				strncpy(pass, opts->passphrase, MAX_PASSSZ);
				pass[MAX_PASSSZ] = '\0';
			}
		}

		if (passphrase_out != NULL) {
			strcpy(passphrase_out, pass);
		}

		if (opts->nkeyfiles > 0) {
			/* Apply keyfiles to 'pass' */
			if ((error = apply_keyfiles((unsigned char *)pass, PASS_BUFSZ,
			    opts->keyfiles, opts->nkeyfiles))) {
				tc_log(1, "could not apply keyfiles");
				goto out;
			}
		}

		if (opts->protect_hidden) {
			if ((h_pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) {
				tc_log(1, "could not allocate safe passphrase memory\n");
				goto out;
			}

			if (opts->interactive) {
			        if ((error = read_passphrase(
				    "Passphrase for hidden volume: ", h_pass,
				    MAX_PASSSZ, PASS_BUFSZ, opts->timeout))) {
					tc_log(1, "could not read passphrase\n");
					goto out;
				}
				h_pass[MAX_PASSSZ] = '\0';
			} else {
				/* In batch mode, use provided passphrase */
				if (opts->h_passphrase != NULL) {
					strncpy(h_pass, opts->h_passphrase, MAX_PASSSZ);
					h_pass[MAX_PASSSZ] = '\0';
				}
			}

			if (opts->n_hkeyfiles > 0) {
				/* Apply keyfiles to 'pass' */
				if ((error = apply_keyfiles((unsigned char *)h_pass, PASS_BUFSZ,
				    opts->h_keyfiles, opts->n_hkeyfiles))) {
					tc_log(1, "could not apply keyfiles");
					goto out;
				}
			}
		}

		/* Always read blksz-sized chunks */
		sz = blksz;

		if (TC_FLAG_SET(opts->flags, HDR_FROM_FILE)) {
			ehdr = (struct tchdr_enc *)read_to_safe_mem(
			    opts->hdr_file_in, 0, &sz);
			if (ehdr == NULL) {
				tc_log(1, "error read hdr_enc: %s", opts->hdr_file_in);
				goto out;
			}
		} else {
			ehdr = (struct tchdr_enc *)read_to_safe_mem(
			    (TC_FLAG_SET(opts->flags, SYS)) ? opts->sys_dev : opts->dev,
			    (TC_FLAG_SET(opts->flags, SYS) || TC_FLAG_SET(opts->flags, FDE)) ?
			    HDR_OFFSET_SYS :
			    (!TC_FLAG_SET(opts->flags, BACKUP)) ? 0 : -BACKUP_HDR_OFFSET_END,
			    &sz);
			if (ehdr == NULL) {
				tc_log(1, "error read hdr_enc: %s", opts->dev);
				goto out;
			}
		}

		if (!TC_FLAG_SET(opts->flags, SYS)) {
			/* Always read blksz-sized chunks */
			sz = blksz;

			if (TC_FLAG_SET(opts->flags, H_HDR_FROM_FILE)) {
				hehdr = (struct tchdr_enc *)read_to_safe_mem(
				    opts->h_hdr_file_in, 0, &sz);
				if (hehdr == NULL) {
					tc_log(1, "error read hdr_enc: %s", opts->h_hdr_file_in);
					goto out;
				}
			} else {
				hehdr = (struct tchdr_enc *)read_to_safe_mem(opts->dev,
				    (!TC_FLAG_SET(opts->flags, BACKUP)) ? HDR_OFFSET_HIDDEN :
				    -BACKUP_HDR_HIDDEN_OFFSET_END, &sz);
				if (hehdr == NULL) {
					tc_log(1, "error read hdr_enc: %s", opts->dev);
					goto out;
				}
			}
		} else {
			hehdr = NULL;
		}

		error = process_hdr(opts->dev, opts->flags, (unsigned char *)pass,
		    (opts->nkeyfiles > 0)?MAX_PASSSZ:strlen(pass),
		    ehdr, &info);

		/*
		 * Try to process hidden header if we have to protect the hidden
		 * volume, or the decryption/verification of the main header
		 * failed.
		 */
		if (hehdr && (error || opts->protect_hidden)) {
			if (error) {
				error2 = process_hdr(opts->dev, opts->flags, (unsigned char *)pass,
				    (opts->nkeyfiles > 0)?MAX_PASSSZ:strlen(pass), hehdr,
				    &info);
				is_hidden = !error2;
			} else if (opts->protect_hidden) {
				error2 = process_hdr(opts->dev, opts->flags, (unsigned char *)h_pass,
				    (opts->n_hkeyfiles > 0)?MAX_PASSSZ:strlen(h_pass), hehdr,
				    &hinfo);
			}
		}

		/* We need both to protect a hidden volume */
		if ((opts->protect_hidden && (error || error2)) ||
		    (error && error2)) {
			if (!try_empty)
				tc_log(1, "Incorrect password or not a TrueCrypt volume\n");

			if (info) {
				free_info(info);
				info = NULL;
			}
			if (hinfo) {
				free_info(hinfo);
				hinfo = NULL;
			}

			/* Try again (or finish) */
			free_safe_mem(pass);
			pass = NULL;

			if (h_pass) {
				free_safe_mem(h_pass);
				h_pass = NULL;
			}
			if (ehdr) {
				free_safe_mem(ehdr);
				ehdr = NULL;
			}
			if (hehdr) {
				free_safe_mem(hehdr);
				hehdr = NULL;
			}

			try_empty = 0;
			continue;
		}

		if (opts->protect_hidden) {
			if (adjust_info(info, hinfo) != 0) {
				tc_log(1, "Could not protect hidden volume\n");
				if (info)
					free_info(info);
				info = NULL;

				if (hinfo)
					free_info(hinfo);
				hinfo = NULL;

				goto out;
			}

			if (hinfo) {
				free_info(hinfo);
				hinfo = NULL;
			}
		}
		try_empty = 0;
        }

out:
	if (hinfo)
		free_info(hinfo);
	if (pass)
		free_safe_mem(pass);
	if (h_pass)
		free_safe_mem(h_pass);
	if (ehdr)
		free_safe_mem(ehdr);
	if (hehdr)
		free_safe_mem(hehdr);

	if (info != NULL)
		info->hidden = is_hidden;

	return info;
}

int
info_mapped_volume(struct tcplay_opts *opts)
{
	struct tcplay_info *info;

	info = dm_info_map(opts->map_name);
	if (info != NULL) {
		if (opts->interactive)
			print_info(info);

		free_info(info);

		return 0;
		/* NOT REACHED */
	} else if (opts->interactive) {
		tc_log(1, "Could not retrieve information about mapped "
		    "volume %s. Does it exist?\n", opts->map_name);
	}

	return -1;
}

int
info_volume(struct tcplay_opts *opts)
{
	struct tcplay_info *info;

	info = info_map_common(opts, NULL);

	if (info != NULL) {
		if (opts->interactive)
			print_info(info);

		free_info(info);

		return 0;
		/* NOT REACHED */
	}

	return -1;
}

int
map_volume(struct tcplay_opts *opts)
{
	struct tcplay_info *info;
	int error;

	info = info_map_common(opts, NULL);

	if (info == NULL)
		return -1;

	if ((error = dm_setup(opts->map_name, info)) != 0) {
		tc_log(1, "Could not set up mapping %s\n", opts->map_name);
		free_info(info);
		return -1;
	}

	if (opts->interactive)
		printf("All ok!\n");

	free_info(info);

	return 0;
}

int
modify_volume(struct tcplay_opts *opts)
{
	struct tcplay_info *info;
	struct tchdr_enc *ehdr, *ehdr_backup;
	const char *new_passphrase = opts->new_passphrase;
	const char **new_keyfiles = opts->new_keyfiles;
	struct pbkdf_prf_algo *new_prf_algo = opts->new_prf_algo;
	int n_newkeyfiles = opts->n_newkeyfiles;
	char *pass, *pass_again;
	int ret = -1;
	off_t offset, offset_backup = 0;
	const char *dev;
	size_t blksz;
	disksz_t blocks;
	int error;

	ehdr = ehdr_backup = NULL;
	pass = pass_again = NULL;
	info = NULL;

	if (TC_FLAG_SET(opts->flags, ONLY_RESTORE)) {
		if (opts->interactive) {
			if ((pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) {
				tc_log(1, "could not allocate safe "
				    "passphrase memory");
				goto out;
			}
		} else {
			new_passphrase = opts->passphrase;
		}
		new_keyfiles = opts->keyfiles;
		n_newkeyfiles = opts->nkeyfiles;
		new_prf_algo = NULL;
	}

	info = info_map_common(opts, pass);
	if (info == NULL)
		goto out;

	if (opts->interactive && !TC_FLAG_SET(opts->flags, ONLY_RESTORE)) {
		if (((pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) ||
		   ((pass_again = alloc_safe_mem(PASS_BUFSZ)) == NULL)) {
			tc_log(1, "could not allocate safe passphrase memory\n");
			goto out;
		}

		if ((error = read_passphrase("New passphrase: ", pass, MAX_PASSSZ,
		    PASS_BUFSZ, 0) ||
		    (read_passphrase("Repeat passphrase: ", pass_again,
		    MAX_PASSSZ, PASS_BUFSZ, 0)))) {
			tc_log(1, "could not read passphrase\n");
			goto out;
		}

		if (strcmp(pass, pass_again) != 0) {
			tc_log(1, "Passphrases don't match\n");
			goto out;
		}

		free_safe_mem(pass_again);
		pass_again = NULL;
	} else if (!opts->interactive) {
		/* In batch mode, use provided passphrase */
		if ((pass = alloc_safe_mem(PASS_BUFSZ)) == NULL) {
			tc_log(1, "could not allocate safe "
			    "passphrase memory");
			goto out;
		}

		if (new_passphrase != NULL) {
			strncpy(pass, new_passphrase, MAX_PASSSZ);
			pass[MAX_PASSSZ] = '\0';
		}
	}

	if (n_newkeyfiles > 0) {
		/* Apply keyfiles to 'pass' */
		if ((error = apply_keyfiles((unsigned char *)pass, PASS_BUFSZ,
		    new_keyfiles, n_newkeyfiles))) {
			tc_log(1, "could not apply keyfiles\n");
			goto out;
		}
	}

	ehdr = copy_reencrypt_hdr((unsigned char *)pass,
	    (opts->n_newkeyfiles > 0)?MAX_PASSSZ:strlen(pass),
	    new_prf_algo, opts->weak_keys_and_salt, info, &ehdr_backup);
	if (ehdr == NULL) {
		tc_log(1, "Could not create header\n");
		goto out;
	}

	dev = (TC_FLAG_SET(opts->flags, SYS)) ? opts->sys_dev : opts->dev;
	if (TC_FLAG_SET(opts->flags, SYS) || TC_FLAG_SET(opts->flags, FDE)) {
		/* SYS and FDE don't have backup headers (as far as I understand) */
		if (info->hidden) {
			offset = HDR_OFFSET_HIDDEN;
		} else {
			offset = HDR_OFFSET_SYS;
		}
	} else {
		if (info->hidden) {
			offset = HDR_OFFSET_HIDDEN;
			offset_backup = -BACKUP_HDR_HIDDEN_OFFSET_END;
		} else {
			offset = 0;
			offset_backup = -BACKUP_HDR_OFFSET_END;
		}
	}

	if ((error = get_disk_info(dev, &blocks, &blksz)) != 0) {
		tc_log(1, "could not get disk information\n");
		goto out;
	}

	tc_log(0, "Writing new volume headers to disk/file...\n");

	if (TC_FLAG_SET(opts->flags, SAVE_TO_FILE)) {
		if ((error = write_to_file(opts->hdr_file_out, ehdr, sizeof(*ehdr))) != 0) {
			tc_log(1, "Could not write volume header to file\n");
			goto out;
		}
	} else {
		if ((error = write_to_disk(dev, offset, blksz, ehdr,
		    sizeof(*ehdr))) != 0) {
			tc_log(1, "Could not write volume header to device\n");
			goto out;
		}

		if (!TC_FLAG_SET(opts->flags, SYS) && !TC_FLAG_SET(opts->flags, FDE)) {
			if ((error = write_to_disk(dev, offset_backup, blksz,
			    ehdr_backup, sizeof(*ehdr_backup))) != 0) {
				tc_log(1, "Could not write backup volume header to device\n");
				goto out;
			}
		}
	}

	/* Everything went ok */
	tc_log(0, "All done!\n");

	ret = 0;

out:
	if (pass)
		free_safe_mem(pass);
	if (pass_again)
		free_safe_mem(pass_again);
	if (ehdr)
		free_safe_mem(ehdr);
	if (ehdr_backup)
		free_safe_mem(ehdr_backup);
	if (info)
		free_safe_mem(info);

	return ret;
}

static
int
dm_get_info(const char *name, struct dm_info *dmi)
{
	struct dm_task *dmt = NULL;
	int error = -1;

	if ((dmt = dm_task_create(DM_DEVICE_INFO)) == NULL)
		goto out;

	if ((dm_task_set_name(dmt, name)) == 0)
		goto out;

	if ((dm_task_run(dmt)) == 0)
		goto out;

	if ((dm_task_get_info(dmt, dmi)) == 0)
		goto out;

	error = 0;

out:
	if (dmt)
		dm_task_destroy(dmt);

	return error;
}

#if defined(__DragonFly__)
static
int
xlate_maj_min(const char *start_path __unused, int max_depth __unused,
    char *buf, size_t bufsz, uint32_t maj, uint32_t min)
{
	dev_t dev = makedev(maj, min);

	snprintf(buf, bufsz, "/dev/%s", devname(dev, S_IFCHR));
	return 1;
}
#else
static
int
xlate_maj_min(const char *start_path, int max_depth, char *buf, size_t bufsz,
    uint32_t maj, uint32_t min)
{
	dev_t dev = makedev(maj, min);
	char path[PATH_MAX];
	struct stat sb;
	struct dirent *ent;
	DIR *dirp;
	int found = 0;

	if (max_depth <= 0)
		return -1;

	if ((dirp = opendir(start_path)) == NULL)
		return -1;

	while ((ent = readdir(dirp)) != NULL) {
		/* d_name, d_type, DT_BLK, DT_CHR, DT_DIR, DT_LNK */
		if (ent->d_name[0] == '.')
			continue;

		/* Linux' /dev is littered with junk, so skip over it */
		/*
		 * The dm-<number> devices seem to be the raw DM devices
		 * things in mapper/ link to.
		 */
		if (((strcmp(ent->d_name, "block")) == 0) ||
		    ((strcmp(ent->d_name, "fd")) == 0) ||
                    (((strncmp(ent->d_name, "dm-", 3) == 0) && strlen(ent->d_name) <= 5)))
			continue;

		snprintf(path, PATH_MAX, "%s/%s", start_path, ent->d_name);

		if ((stat(path, &sb)) < 0)
			continue;

		if (S_ISDIR(sb.st_mode)) {
			found = !xlate_maj_min(path, max_depth-1, buf, bufsz, maj, min);
			if (found)
				break;
		}

		if (!S_ISBLK(sb.st_mode))
			continue;

		if (sb.st_rdev != dev)
			continue;

		snprintf(buf, bufsz, "%s", path);
		found = 1;
		break;
	}

	if (dirp)
		closedir(dirp);

	return found ? 0 : -ENOENT;
}
#endif

static
struct tcplay_dm_table *
dm_get_table(const char *name)
{
	struct tcplay_dm_table *tc_table;
	struct dm_task *dmt = NULL;
	void *next = NULL;
	uint64_t start, length;
	char *target_type;
	char *params;
	char *p1;
	int c = 0;
	uint32_t maj, min;

	if ((tc_table = (struct tcplay_dm_table *)alloc_safe_mem(sizeof(*tc_table))) == NULL) {
		tc_log(1, "could not allocate safe tc_table memory\n");
		return NULL;
	}

	if ((dmt = dm_task_create(DM_DEVICE_TABLE)) == NULL)
		goto error;

	if ((dm_task_set_name(dmt, name)) == 0)
		goto error;

	if ((dm_task_run(dmt)) == 0)
		goto error;

	tc_table->start = (off_t)0;
	tc_table->size = (size_t)0;

	do {
		next = dm_get_next_target(dmt, next, &start, &length,
		    &target_type, &params);

		tc_table->size += (size_t)length;
		strncpy(tc_table->target, target_type,
		    sizeof(tc_table->target));

		/* Skip any leading whitespace */
		while (params && *params == ' ')
			params++;

		if (strcmp(target_type, "crypt") == 0) {
			while ((p1 = strsep(&params, " ")) != NULL) {
				/* Skip any whitespace before the next strsep */
				while (params && *params == ' ')
					params++;

				/* Process p1 */
				if (c == 0) {
					/* cipher */
					strncpy(tc_table->cipher, p1,
					    sizeof(tc_table->cipher));
				} else if (c == 2) {
					/* iv offset */
					tc_table->skip = (off_t)strtoll(p1, NULL, 10);
				} else if (c == 3) {
					/* major:minor */
					maj = strtoul(p1, NULL, 10);
					while (*p1 != ':' && *p1 != '\0')
						p1++;
					min = strtoul(++p1, NULL, 10);
					if ((xlate_maj_min("/dev", 2, tc_table->device,
					    sizeof(tc_table->device), maj, min)) != 0)
						snprintf(tc_table->device,
						    sizeof(tc_table->device),
						    "%u:%u", maj, min);
				} else if (c == 4) {
					/* block offset */
					tc_table->offset = (off_t)strtoll(p1,
					    NULL, 10);
				}
				++c;
			}

			if (c < 5) {
				tc_log(1, "could not get all the info required from "
				    "the table\n");
				goto error;
			}
		}
	} while (next != NULL);

	if (dmt)
		dm_task_destroy(dmt);

#ifdef DEBUG
	printf("device: %s\n", tc_table->device);
	printf("target: %s\n", tc_table->target);
	printf("cipher: %s\n", tc_table->cipher);
	printf("size:   %ju\n", tc_table->size);
	printf("offset: %"PRId64"\n", tc_table->offset);
	printf("skip:   %"PRId64"\n", tc_table->skip);
#endif

	return tc_table;

error:
	if (dmt)
		dm_task_destroy(dmt);
	if (tc_table)
		free_safe_mem(tc_table);

	return NULL;
}

struct tcplay_info *
dm_info_map(const char *map_name)
{
	struct dm_task *dmt = NULL;
	struct dm_info dmi[3];
	struct tcplay_dm_table *dm_table[3];
	struct tc_crypto_algo *crypto_algo;
	struct tcplay_info *info;
	char map[PATH_MAX];
	char ciphers[512];
	int i, outermost = -1;

	memset(dm_table, 0, sizeof(dm_table));

	if ((info = (struct tcplay_info *)alloc_safe_mem(sizeof(*info))) == NULL) {
		tc_log(1, "could not allocate safe info memory\n");
		return NULL;
	}

	strncpy(map, map_name, PATH_MAX);
	for (i = 0; i < 3; i++) {
		if ((dm_get_info(map, &dmi[i])) != 0)
			goto error;

		if (dmi[i].exists)
			dm_table[i] = dm_get_table(map);

		snprintf(map, PATH_MAX, "%s.%d", map_name, i);
	}

	if (dmt)
		dm_task_destroy(dmt);

	if (dm_table[0] == NULL)
		goto error;

	/*
	 * Process our dmi, dm_table fun into the info structure.
	 */
	/* First find which cipher chain we are using */
	ciphers[0] = '\0';
	for (i = 0; i < 3; i++) {
		if (dm_table[i] == NULL)
			continue;

		if (outermost < i)
			outermost = i;

		crypto_algo = &tc_crypto_algos[0];
		while ((crypto_algo != NULL) &&
		    (strcmp(dm_table[i]->cipher, crypto_algo->dm_crypt_str) != 0))
			++crypto_algo;
		if (crypto_algo == NULL) {
			tc_log(1, "could not find corresponding cipher\n");
			goto error;
		}
		strcat(ciphers, crypto_algo->name);
		strcat(ciphers, ",");
	}
	ciphers[strlen(ciphers)-1] = '\0';

	info->cipher_chain = check_cipher_chain(ciphers, 1);
	if (info->cipher_chain == NULL) {
		tc_log(1, "could not find cipher chain\n");
		goto error;
	}

	/* Copy over the name */
	strncpy(info->dev, dm_table[outermost]->device, sizeof(info->dev));

	/* Other fields */
	info->hdr = NULL;
	info->pbkdf_prf = NULL;
	info->start = dm_table[outermost]->start;
	info->size = dm_table[0]->size;
	info->skip = dm_table[outermost]->skip;
	info->offset = dm_table[outermost]->offset;
	info->blk_sz = 512;

	return info;

error:
	if (dmt)
		dm_task_destroy(dmt);
	if (info)
		free_safe_mem(info);
	for (i = 0; i < 3; i++)
		if (dm_table[i] != NULL)
			free_safe_mem(dm_table[i]);

	return NULL;
}

static
int
dm_exists_device(const char *name)
{
	struct dm_info dmi;
	int exists = 0;

	if (dm_get_info(name, &dmi) != 0)
		goto out;

	exists = dmi.exists;

out:
	return exists;
}

static
int
dm_remove_device(const char *name)
{
	struct dm_task *dmt = NULL;
	int ret = EINVAL;

	if ((dmt = dm_task_create(DM_DEVICE_REMOVE)) == NULL)
		goto out;

	if ((dm_task_set_name(dmt, name)) == 0)
		goto out;

	if ((dm_task_run(dmt)) == 0)
		goto out;

	ret = 0;
out:
	if (dmt)
		dm_task_destroy(dmt);

	return ret;
}

int
dm_setup(const char *mapname, struct tcplay_info *info)
{
	struct tc_cipher_chain *cipher_chain;
	struct dm_task *dmt = NULL;
	struct dm_info dmi;
	char *params = NULL;
	char *uu, *uu_temp;
	char *uu_stack[64];
	int uu_stack_idx;
#if defined(__DragonFly__)
	uint32_t status;
#endif
	int r, ret = 0;
	int j, len;
	off_t start, offset;
	char dev[PATH_MAX];
	char map[PATH_MAX];
	uint32_t cookie;

	dm_udev_set_sync_support(1);

	if ((params = alloc_safe_mem(512)) == NULL) {
		tc_log(1, "could not allocate safe parameters memory");
		return ENOMEM;
	}

	strcpy(dev, info->dev);

	/*
	 * Device Mapper blocks are always 512-byte blocks, so convert
	 * from the "native" block size to the dm block size here.
	 */
	start = INFO_TO_DM_BLOCKS(info, start);
	offset = INFO_TO_DM_BLOCKS(info, offset);
	uu_stack_idx = 0;

	/*
         * Find length of cipher chain. Could use the for below, but doesn't
         * really matter.
         */
	len = tc_cipher_chain_length(info->cipher_chain);

	/* Get to the end of the chain */
	for (cipher_chain = info->cipher_chain; cipher_chain->next != NULL;
	    cipher_chain = cipher_chain->next)
		;

	/*
         * Start j at len-2, as we want to use .0, and the final one has no
         * suffix.
         */
	for (j = len-2; cipher_chain != NULL;
	    cipher_chain = cipher_chain->prev, j--) {

		cookie = 0;

		if ((dmt = dm_task_create(DM_DEVICE_CREATE)) == NULL) {
			tc_log(1, "dm_task_create failed\n");
			ret = -1;
			goto out;
		}

		/*
		 * If this is the last element in the cipher chain, use the
		 * final map name. Otherwise pick a secondary name...
		 */
		if (cipher_chain->prev == NULL)
			strcpy(map, mapname);
		else
			sprintf(map, "%s.%d", mapname, j);

		if ((dm_task_set_name(dmt, map)) == 0) {
			tc_log(1, "dm_task_set_name failed\n");
			ret = -1;
			goto out;
		}

#if defined(__linux__)
		uuid_generate(info->uuid);
		if ((uu_temp = malloc(1024)) == NULL) {
			tc_log(1, "uuid_unparse memory failed\n");
			ret = -1;
			goto out;
		}
		uuid_unparse(info->uuid, uu_temp);
#elif defined(__DragonFly__)
		uuid_create(&info->uuid, &status);
		if (status != uuid_s_ok) {
			tc_log(1, "uuid_create failed\n");
			ret = -1;
			goto out;
		}

		uuid_to_string(&info->uuid, &uu_temp, &status);
		if (uu_temp == NULL) {
			tc_log(1, "uuid_to_string failed\n");
			ret = -1;
			goto out;
		}
#endif

		if ((uu = malloc(1024)) == NULL) {
			free(uu_temp);
			tc_log(1, "uuid second malloc failed\n");
			ret = -1;
			goto out;
		}

		snprintf(uu, 1024, "CRYPT-TCPLAY-%s", uu_temp);
		free(uu_temp);

		if ((dm_task_set_uuid(dmt, uu)) == 0) {
			free(uu);
			tc_log(1, "dm_task_set_uuid failed\n");
			ret = -1;
			goto out;
		}

		free(uu);

		if (TC_FLAG_SET(info->flags, FDE)) {
			/*
			 * When the full disk encryption (FDE) flag is set,
			 * we map the first N sectors using a linear target
			 * as they aren't encrypted.
			 */

			/*  /dev/ad0s0a              0 */
			/* dev---^       block off --^ */
			snprintf(params, 512, "%s 0", dev);

			if ((dm_task_add_target(dmt, 0,
				INFO_TO_DM_BLOCKS(info, offset),
				"linear", params)) == 0) {
				tc_log(1, "dm_task_add_target failed\n");
				ret = -1;
				goto out;
			}

			start = INFO_TO_DM_BLOCKS(info, offset);
		}

		/* aes-cbc-essiv:sha256 7997f8af... 0 /dev/ad0s0a 8 <opts> */
		/*			   iv off---^  block off--^ <opts> */
		snprintf(params, 512, "%s %s %"PRIu64 " %s %"PRIu64 " %s",
		    cipher_chain->cipher->dm_crypt_str, cipher_chain->dm_key,
		    (uint64_t)INFO_TO_DM_BLOCKS(info, skip), dev,
		    (uint64_t)offset,
		    TC_FLAG_SET(info->flags, ALLOW_TRIM) ? "1 allow_discards" : "");
#ifdef DEBUG
		printf("Params: %s\n", params);
#endif

		if ((dm_task_add_target(dmt, start,
		    INFO_TO_DM_BLOCKS(info, size), "crypt", params)) == 0) {
			tc_log(1, "dm_task_add_target failed\n");
			ret = -1;
			goto out;
		}

		if ((dm_task_set_cookie(dmt, &cookie, 0)) == 0) {
			tc_log(1, "dm_task_set_cookie failed\n");
			ret = -1;
			goto out;
		}

		if ((dm_task_run(dmt)) == 0) {
			dm_udev_wait(cookie);
			tc_log(1, "dm_task_run failed\n");
			ret = -1;
			goto out;
		}

		if ((dm_task_get_info(dmt, &dmi)) == 0) {
			dm_udev_wait(cookie);
			tc_log(1, "dm_task_get info failed\n");
			ret = -1;
			goto out;
		}

		dm_udev_wait(cookie);

		if ((r = asprintf(&uu_stack[uu_stack_idx++], "%s", map)) < 0)
			tc_log(1, "warning, asprintf failed. won't be able to "
			    "unroll changes\n");


		offset = 0;
		start = 0;
		sprintf(dev, "/dev/mapper/%s.%d", mapname, j);

		dm_task_destroy(dmt);
		dm_task_update_nodes();
	}

out:
	/*
	 * If an error occured, try to unroll changes made before it
	 * happened.
	 */
	if (ret) {
		j = uu_stack_idx;
		while (j > 0) {
#ifdef DEBUG
			printf("Unrolling dm changes! j = %d (%s)\n", j-1,
			    uu_stack[j-1]);
#endif
			if ((uu_stack[j-1] == NULL) ||
			    ((r = dm_remove_device(uu_stack[--j])) != 0)) {
				tc_log(1, "Tried to unroll dm changes, "
				    "giving up.\n");
				break;
			}
		}
	}

	while (uu_stack_idx > 0)
		free(uu_stack[--uu_stack_idx]);

	free_safe_mem(params);

	return ret;
}

int
dm_teardown(const char *mapname, const char *device __unused)
{
#if 0
	struct dm_task *dmt = NULL;
	struct dm_info dmi;
#endif
	char map[PATH_MAX];
	int i, error;

	if ((error = dm_remove_device(mapname)) != 0) {
		tc_log(1, "Could not remove mapping %s\n", mapname);
		return error;
	}

	/* Try to remove other cascade devices */
	for (i = 0; i < 2; i++) {
		sprintf(map, "%s.%d", mapname, i);
		if (dm_exists_device(map))
			dm_remove_device(map);
	}

	return 0;
}

struct tc_crypto_algo *
check_cipher(const char *cipher, int quiet)
{
	int i, found = 0;

	for (i = 0; tc_crypto_algos[i].name != NULL; i++) {
		if (strcmp(cipher, tc_crypto_algos[i].name) == 0) {
			found = 1;
			break;
		}
	}

	if (!found && !quiet) {
		fprintf(stderr, "Valid ciphers are: ");
		for (i = 0; tc_crypto_algos[i].name != NULL; i++)
			fprintf(stderr, "%s ", tc_crypto_algos[i].name);
		fprintf(stderr, "\n");
		return NULL;
	}

	return &tc_crypto_algos[i];
}

struct tc_cipher_chain *
check_cipher_chain(const char *cipher_chain, int quiet)
{
	struct tc_cipher_chain *cipher = NULL;
	int i,k, nciphers = 0, mismatch = 0;
	char *ciphers[8];
	char *tmp_chain, *tmp_chain_free;
	char *token;

	if ((tmp_chain = strdup(cipher_chain)) == NULL) {
		tc_log(1, "Could not allocate strdup memory\n");
		return NULL;
	}

	tmp_chain_free = tmp_chain;

	while ((token = strsep(&tmp_chain, ",")) != NULL)
		ciphers[nciphers++] = token;

	cipher = NULL;

	for (i = 0; valid_cipher_chains[i][0] != NULL; i++) {
		mismatch = 0;

		for (k = 0; (valid_cipher_chains[i][k] != NULL); k++) {
			/*
			 * If there are more ciphers in the chain than in the
			 * ciphers[] variable this is not the right chain.
			 */
			if (k == nciphers) {
				mismatch = 1;
				break;
			}

			if (strcmp(ciphers[k], valid_cipher_chains[i][k]) != 0)
				mismatch = 1;
		}

		/*
		 * If all ciphers matched and there are exactly nciphers,
		 * then we found the right cipher chain.
		 */
		if ((k == nciphers) && !mismatch) {
			cipher = tc_cipher_chains[i];
			break;
		}
	}

	if (cipher == NULL) {
		tc_log(1, "Invalid cipher: %s\n", cipher_chain);
		if (!quiet) {
			fprintf(stderr, "Valid cipher chains are:\n");
			for (i = 0; valid_cipher_chains[i][0] != NULL; i++) {
				for (k = 0; valid_cipher_chains[i][k] != NULL;
				    k++) {
					fprintf(stderr, "%s%c",
					    valid_cipher_chains[i][k],
					    (valid_cipher_chains[i][k+1] != NULL) ?
					    ',' : '\0');
				}
				fprintf(stderr, "\n");
			}
		}
	}

	free(tmp_chain_free);
	return cipher;
}

struct pbkdf_prf_algo *
check_prf_algo(const char *algo, int quiet)
{
	int i, found = 0;

	for (i = 0; pbkdf_prf_algos[i].name != NULL; i++) {
		if (strcmp(algo, pbkdf_prf_algos[i].name) == 0) {
			found = 1;
			break;
		}
	}

	if (!found && !quiet) {
		fprintf(stderr, "Valid PBKDF PRF algorithms are: ");
		for (i = 0; pbkdf_prf_algos[i].name != NULL; i++)
			fprintf(stderr, "%s ", pbkdf_prf_algos[i].name);
		fprintf(stderr, "\n");
		return NULL;
	}

	return &pbkdf_prf_algos[i];
}

int
tc_play_init(void)
{
	int error;

	if ((error = tc_build_cipher_chains()) != 0)
		return error;

	if ((error = tc_crypto_init()) != 0)
		return error;

	return 0;
}

struct tcplay_opts *opts_init(void)
{
	struct tcplay_opts *opts;

	if ((opts = (struct tcplay_opts *)alloc_safe_mem(sizeof(*opts))) == NULL) {
		tc_log(1, "could not allocate safe opts memory\n");
		return NULL;
	}

	memset(opts, 0, sizeof(*opts));

	opts->retries = DEFAULT_RETRIES;
	opts->secure_erase = 1;

	return opts;
}

int
opts_add_keyfile(struct tcplay_opts *opts, const char *keyfile)
{
	const char *keyf;

	if (opts->nkeyfiles == MAX_KEYFILES)
		return -1;

	if ((keyf = strdup_safe_mem(keyfile)) == NULL) {
		return -1;
	}

	opts->keyfiles[opts->nkeyfiles++] = keyf;

	return 0;
}

int
opts_add_keyfile_hidden(struct tcplay_opts *opts, const char *keyfile)
{
	const char *keyf;

	if (opts->n_hkeyfiles == MAX_KEYFILES)
		return -1;

	if ((keyf = strdup_safe_mem(keyfile)) == NULL) {
		return -1;
	}

	opts->h_keyfiles[opts->n_hkeyfiles++] = keyf;

	return 0;
}

int
opts_add_keyfile_new(struct tcplay_opts *opts, const char *keyfile)
{
	const char *keyf;

	if (opts->n_newkeyfiles == MAX_KEYFILES)
		return -1;

	if ((keyf = strdup_safe_mem(keyfile)) == NULL) {
		return -1;
	}

	opts->new_keyfiles[opts->n_newkeyfiles++] = keyf;

	return 0;
}

void
opts_clear_keyfile(struct tcplay_opts *opts)
{
	int i;

	for (i = 0; i < opts->nkeyfiles; i++) {
		free_safe_mem(opts->keyfiles[i]);
	}

	opts->nkeyfiles = 0;
}

void
opts_clear_keyfile_hidden(struct tcplay_opts *opts)
{
	int i;

	for (i = 0; i < opts->n_hkeyfiles; i++) {
		free_safe_mem(opts->h_keyfiles[i]);
	}

	opts->n_hkeyfiles = 0;
}


void
opts_clear_keyfile_new(struct tcplay_opts *opts)
{
	int i;

	for (i = 0; i < opts->n_newkeyfiles; i++) {
		free_safe_mem(opts->new_keyfiles[i]);
	}

	opts->n_newkeyfiles = 0;
}


void
opts_free(struct tcplay_opts *opts)
{
	int i;

	for (i = 0; i < opts->nkeyfiles; i++) {
		free_safe_mem(opts->keyfiles[i]);
	}

	for (i = 0; i < opts->n_hkeyfiles; i++) {
		free_safe_mem(opts->h_keyfiles[i]);
	}

	for (i = 0; i < opts->n_newkeyfiles; i++) {
		free_safe_mem(opts->new_keyfiles[i]);
	}

	if (opts->dev)
		free_safe_mem(opts->dev);
	if (opts->passphrase)
		free_safe_mem(opts->passphrase);
	if (opts->h_passphrase)
		free_safe_mem(opts->h_passphrase);
	if (opts->new_passphrase)
		free_safe_mem(opts->new_passphrase);
	if (opts->map_name)
		free_safe_mem(opts->map_name);
	if (opts->sys_dev)
		free_safe_mem(opts->sys_dev);
	if (opts->hdr_file_in)
		free_safe_mem(opts->hdr_file_in);
	if (opts->h_hdr_file_in)
		free_safe_mem(opts->h_hdr_file_in);
	if (opts->hdr_file_out)
		free_safe_mem(opts->hdr_file_out);

	free_safe_mem(opts);
}
