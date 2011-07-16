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
#include <machine/inttypes.h>
#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <time.h>
#include <libdm.h>
#include <libutil.h>

#include "crc32.h"
#include "tcplay.h"


/* XXX TODO:
 *  - LRW-benbi support? needs further work in dm-crypt and even opencrypto
 *  - secure buffer review (i.e: is everything that needs it using secure mem?)
 *  - mlockall? (at least MCL_FUTURE, which is the only one we support)
 */

summary_fn_t summary_fn = NULL;
int tc_internal_verbose = 1;
char tc_internal_log_buffer[LOG_BUFFER_SZ];

void
tc_log(int is_err, const char *fmt, ...)
{
	__va_list ap;
	FILE *fp;

	if (is_err)
		fp = stderr;
	else
		fp = stdout;

        __va_start(ap, fmt);

	vsnprintf(tc_internal_log_buffer, LOG_BUFFER_SZ, fmt, ap);

	if (tc_internal_verbose)
                vfprintf(fp, fmt, ap);

	__va_end(ap);
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
	{ "AES-256-XTS",	"aes-xts-plain",	64,	8 },
	{ "TWOFISH-256-XTS",	"twofish-xts-plain",	64,	8 },
	{ "SERPENT-256-XTS",	"serpent-xts-plain",	64,	8 },
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
	struct tc_cipher_chain *cipher_chain;
	int klen = 0;

	printf("PBKDF2 PRF:\t\t%s\n", info->pbkdf_prf->name);
	printf("PBKDF2 iterations:\t%d\n", info->pbkdf_prf->iteration_count);

	printf("Cipher:\t\t\t");
	for (cipher_chain = info->cipher_chain;
	    cipher_chain != NULL;
	    cipher_chain = cipher_chain->next) {
		printf("%s%c", cipher_chain->cipher->name,
		    (cipher_chain->next != NULL) ? ',' : '\n');
		klen += cipher_chain->cipher->klen;
	}

	printf("Key Length:\t\t%d bits\n", klen*8);
	printf("CRC Key Data:\t\t%#x\n", info->hdr->crc_keys);
	printf("Sector size:\t\t%d\n", info->hdr->sec_sz);
	printf("Volume size:\t\t%zu sectors\n", info->size);
}

static
struct tcplay_info *
new_info(const char *dev, struct tc_cipher_chain *cipher_chain,
    struct pbkdf_prf_algo *prf, struct tchdr_dec *hdr, off_t start)
{
	struct tcplay_info *info;
	int i;
	int error;

	if ((info = (struct tcplay_info *)alloc_safe_mem(sizeof(*info))) == NULL) {
		tc_log(1, "could not allocate safe info memory\n");
		return NULL;
	}

	info->dev = dev;
	info->cipher_chain = cipher_chain;
	info->pbkdf_prf = prf;
	info->start = start;
	info->hdr = hdr;
	info->size = hdr->sz_mk_scope / hdr->sec_sz;	/* volume size */
	info->skip = hdr->off_mk_scope / hdr->sec_sz;	/* iv skip */
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

	return info;
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
process_hdr(const char *dev, unsigned char *pass, int passlen,
    struct tchdr_enc *ehdr, struct tcplay_info **pinfo)
{
	struct tchdr_dec *dhdr;
	struct tcplay_info *info;
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
		error = pbkdf2(pass, passlen,
		    ehdr->salt, sizeof(ehdr->salt),
		    pbkdf_prf_algos[i].iteration_count,
		    pbkdf_prf_algos[i].name, MAX_KEYSZ, key);

		if (error) {
			tc_log(1, "pbkdf failed for algorithm %s\n",
			    pbkdf_prf_algos[i].name);
			return EINVAL;
		}

#if 0
		printf("Derived Key: ");
		print_hex(key, 0, MAX_KEYSZ);
#endif

		for (j = 0; !found && tc_cipher_chains[j] != NULL; j++) {
#ifdef DEBUG
			printf("\nTrying cipher chain %d\n", j);
#endif

			dhdr = decrypt_hdr(ehdr, tc_cipher_chains[j], key);
			if (dhdr == NULL) {
				tc_log(1, "hdr decryption failed for cipher "
				    "chain %d\n", j);
				return EINVAL;
			}

			if (verify_hdr(dhdr)) {
#ifdef DEBUG
				printf("tc_str: %.4s, tc_ver: %zd, tc_min_ver: %zd, "
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
			}
		}
	}

	free_safe_mem(key);

	if (!found)
		return EINVAL;

	if ((info = new_info(dev, tc_cipher_chains[j-1], &pbkdf_prf_algos[i-1],
	    dhdr, 0)) == NULL) {
		return ENOMEM;
	}

	*pinfo = info;
	return 0;
}

int
create_volume(const char *dev, int hidden, const char *keyfiles[], int nkeyfiles,
    const char *h_keyfiles[], int n_hkeyfiles, struct pbkdf_prf_algo *prf_algo,
    struct tc_cipher_chain *cipher_chain, struct pbkdf_prf_algo *h_prf_algo,
    struct tc_cipher_chain *h_cipher_chain, char *passphrase,
    char *h_passphrase, size_t hidden_blocks_in, int interactive)
{
	char *pass, *pass_again;
	char *h_pass = NULL;
	char buf[1024];
	size_t blocks, blksz, hidden_blocks = 0;
	struct tchdr_enc *ehdr, *hehdr = NULL;
	int64_t tmp;
	int error, r;

	if (cipher_chain == NULL)
		cipher_chain = tc_cipher_chains[0];
	if (prf_algo == NULL)
		prf_algo = &pbkdf_prf_algos[0];
	if (h_cipher_chain == NULL)
		h_cipher_chain = cipher_chain;
	if (h_prf_algo == NULL)
		h_prf_algo = prf_algo;

	if ((error = get_disk_info(dev, &blocks, &blksz)) != 0) {
		tc_log(1, "could not get disk info\n");
		return -1;
	}

	if (blocks <= MIN_VOL_BLOCKS) {
		tc_log(1, "Cannot create volumes on devices with less "
		    "than %d blocks/sectors\n", MIN_VOL_BLOCKS);
		return -1;
	}

	if (interactive) {
		if (((pass = alloc_safe_mem(MAX_PASSSZ)) == NULL) ||
		   ((pass_again = alloc_safe_mem(MAX_PASSSZ)) == NULL)) {
			tc_log(1, "could not allocate safe passphrase memory\n");
			return -1;
		}

		if ((error = read_passphrase("Passphrase: ", pass, MAX_PASSSZ, 0) ||
		   (read_passphrase("Repeat passphrase: ", pass_again,
		   MAX_PASSSZ, 0)))) {
			tc_log(1, "could not read passphrase\n");
			return -1;
		}

		if (strcmp(pass, pass_again) != 0) {
			tc_log(1, "Passphrases don't match\n");
			return -1;
		}

		free_safe_mem(pass_again);
	} else {
		/* In batch mode, use provided passphrase */
		if ((pass = alloc_safe_mem(MAX_PASSSZ)) == NULL) {
			tc_log(1, "could not allocate safe "
			    "passphrase memory");
			return -1;
		}

		if (passphrase != NULL)
			strcpy(pass, passphrase);
	}

	if (nkeyfiles > 0) {
		/* Apply keyfiles to 'pass' */
		if ((error = apply_keyfiles(pass, MAX_PASSSZ, keyfiles,
		    nkeyfiles))) {
			tc_log(1, "could not apply keyfiles\n");
		}
	}

	if (hidden) {
		if (interactive) {
			if (((h_pass = alloc_safe_mem(MAX_PASSSZ)) == NULL) ||
			   ((pass_again = alloc_safe_mem(MAX_PASSSZ)) == NULL)) {
				tc_log(1, "could not allocate safe "
				    "passphrase memory\n");
				return -1;
			}

			if ((error = read_passphrase("Passphrase for hidden volume: ",
			   h_pass, MAX_PASSSZ, 0) ||
			   (read_passphrase("Repeat passphrase: ", pass_again,
			   MAX_PASSSZ, 0)))) {
				tc_log(1, "could not read passphrase\n");
				return -1;
			}

			if (strcmp(h_pass, pass_again) != 0) {
				tc_log(1, "Passphrases for hidden volume don't "
				    "match\n");
				return -1;
			}

			free_safe_mem(pass_again);
		} else {
			/* In batch mode, use provided passphrase */
			if ((h_pass = alloc_safe_mem(MAX_PASSSZ)) == NULL) {
				tc_log(1, "could not allocate safe "
				    "passphrase memory");
				return -1;
			}

			if (h_passphrase != NULL)
				strcpy(h_pass, h_passphrase);
		}

		if (n_hkeyfiles > 0) {
			/* Apply keyfiles to 'h_pass' */
			if ((error = apply_keyfiles(h_pass, MAX_PASSSZ, h_keyfiles,
			n_hkeyfiles))) {
				tc_log(1, "could not apply keyfiles\n");
				return -1;
			}
		}

		if (interactive) {
			hidden_blocks = 0;
		} else {
			hidden_blocks = hidden_blocks_in;
			if (hidden_blocks == 0) {
				tc_log(1, "hidden_blocks to create volume "
				    "cannot be zero!\n");
				return -1;
			}
		}

		/* This only happens in interactive mode */
		while (hidden_blocks == 0) {
			if ((r = humanize_number(buf, strlen("XXX MB"),
			    (int64_t)(blocks * blksz), "B", 0, 0)) < 0) {
				sprintf(buf, "%zu bytes", (blocks * blksz));
			}

			printf("The total volume size of %s is %s (bytes)\n", dev, buf);
			memset(buf, 0, sizeof(buf));
			printf("Size of hidden volume (e.g. 127M): ");
			fflush(stdout);

			if ((fgets(buf, sizeof(buf), stdin)) == NULL) {
				tc_log(1, "Could not read from stdin\n");
				return -1;
			}

			/* get rid of trailing newline */
			buf[strlen(buf)-1] = '\0';
			if ((error = dehumanize_number(buf,
			    &tmp)) != 0) {
				tc_log(1, "Could not interpret input: %s\n", buf);
				return -1;
			}

			hidden_blocks = (size_t)tmp;
			hidden_blocks /= blksz;
			if (hidden_blocks >= blocks - MIN_VOL_BLOCKS) {
				tc_log(1, "Hidden volume needs to be "
				    "smaller than the outer volume\n");
				hidden_blocks = 0;
				continue;
			}
		}
	}

	if (interactive) {
		/* Show summary and ask for confirmation */
		printf("Summary of actions:\n");
		printf(" - Completely erase *EVERYTHING* on %s\n", dev);
		printf(" - Create %svolume on %s\n", hidden?("outer "):"", dev);
		if (hidden) {
			printf(" - Create hidden volume of %zu bytes at end of "
			    "outer volume\n",
			    hidden_blocks * blksz);
		}

		printf("\n Are you sure you want to proceed? (y/n) ");
		fflush(stdout);
		if ((fgets(buf, sizeof(buf), stdin)) == NULL) {
			tc_log(1, "Could not read from stdin\n");
			return -1;
		}

		if ((buf[0] != 'y') && (buf[0] != 'Y')) {
			tc_log(1, "User cancelled action(s)\n");
			return -1;
		}
	}

	/* erase volume */
	if ((error = secure_erase(dev, blocks * blksz, blksz)) != 0) {
		tc_log(1, "could not securely erase device %s\n", dev);
		return -1;
	}

	/* create encrypted headers */
	ehdr = create_hdr(pass, (nkeyfiles > 0)?MAX_PASSSZ:strlen(pass),
	    prf_algo, cipher_chain, blksz, blocks, MIN_VOL_BLOCKS,
	    blocks-MIN_VOL_BLOCKS, 0);
	if (ehdr == NULL) {
		tc_log(1, "Could not create header\n");
		return -1;
	}

	if (hidden) {
		hehdr = create_hdr(h_pass,
		    (n_hkeyfiles > 0)?MAX_PASSSZ:strlen(h_pass), h_prf_algo,
		    h_cipher_chain,
		    blksz, blocks, blocks - hidden_blocks, hidden_blocks, 1);
		if (hehdr == NULL) {
			tc_log(1, "Could not create hidden volume header\n");
			return -1;
		}
	}

	if ((error = write_mem(dev, 0, blksz, ehdr, sizeof(*ehdr))) != 0) {
		tc_log(1, "Could not write volume header to device\n");
		return -1;
	}

	if (hidden) {
		if ((error = write_mem(dev, HDR_OFFSET_HIDDEN, blksz, hehdr,
		    sizeof(*hehdr))) != 0) {
			tc_log(1, "Could not write hidden volume header to "
			    "device\n");
			return -1;
		}
	}

	return 0;
}

static
struct tcplay_info *
info_map_common(const char *dev, int sflag, const char *sys_dev,
    int protect_hidden, const char *keyfiles[], int nkeyfiles,
    const char *h_keyfiles[], int n_hkeyfiles, char *passphrase,
    char *passphrase_hidden, int interactive, int retries, time_t timeout)
{
	struct tchdr_enc *ehdr, *hehdr = NULL;
	struct tcplay_info *info, *hinfo = NULL;
	char *pass;
	char *h_pass;
	int error, error2 = 0;
	size_t sz;

	info = NULL;
	if (retries < 1)
		retries = 1;

	while ((info == NULL) && retries-- > 0)
	{
		h_pass = NULL;
		ehdr = NULL;
		hehdr = NULL;

		if ((pass = alloc_safe_mem(MAX_PASSSZ)) == NULL) {
			tc_log(1, "could not allocate safe passphrase memory\n");
			return NULL;
		}

		if (interactive) {
		        if ((error = read_passphrase("Passphrase: ", pass,
			    MAX_PASSSZ, timeout))) {
				tc_log(1, "could not read passphrase\n");
				return NULL;
			}
		} else {
			/* In batch mode, use provided passphrase */
			if (passphrase != NULL)
				strcpy(pass, passphrase);
		}

		if (nkeyfiles > 0) {
			/* Apply keyfiles to 'pass' */
			if ((error = apply_keyfiles(pass, MAX_PASSSZ, keyfiles,
			    nkeyfiles))) {
				tc_log(1, "could not apply keyfiles");
				return NULL;
			}
		}

		if (protect_hidden) {
			if ((h_pass = alloc_safe_mem(MAX_PASSSZ)) == NULL) {
				tc_log(1, "could not allocate safe passphrase memory\n");
				return NULL;
			}

			if (interactive) {
			        if ((error = read_passphrase(
				    "Passphrase for hidden volume: ", h_pass,
				    MAX_PASSSZ, timeout))) {
					tc_log(1, "could not read passphrase\n");
					return NULL;
				}
			} else {
				/* In batch mode, use provided passphrase */
				if (passphrase_hidden != NULL)
					strcpy(h_pass, passphrase_hidden);
			}

			if (n_hkeyfiles > 0) {
				/* Apply keyfiles to 'pass' */
				if ((error = apply_keyfiles(h_pass, MAX_PASSSZ, h_keyfiles,
				    n_hkeyfiles))) {
					tc_log(1, "could not apply keyfiles");
					return NULL;
				}
			}
		}

		sz = HDRSZ;
		ehdr = (struct tchdr_enc *)read_to_safe_mem((sflag) ? sys_dev : dev,
		    (sflag) ? HDR_OFFSET_SYS : 0, &sz);
		if (ehdr == NULL) {
			tc_log(1, "error read hdr_enc: %s", dev);
			return NULL;
		}

		if (!sflag) {
			sz = HDRSZ;
			hehdr = (struct tchdr_enc *)read_to_safe_mem(dev,
			    HDR_OFFSET_HIDDEN, &sz);
			if (hehdr == NULL) {
				tc_log(1, "error read hdr_enc: %s", dev);
				return NULL;
			}
		} else {
			hehdr = NULL;
		}

		error = process_hdr(dev, pass, (nkeyfiles > 0)?MAX_PASSSZ:strlen(pass),
		    ehdr, &info);

		/*
		 * Try to process hidden header if we have to protect the hidden
		 * volume, or the decryption/verification of the main header
		 * failed.
		 */
		if (hehdr && (error || protect_hidden)) {
			if (error) {
				error2 = process_hdr(dev, pass,
				    (nkeyfiles > 0)?MAX_PASSSZ:strlen(pass), hehdr,
				    &info);
			} else if (protect_hidden) {
				error2 = process_hdr(dev, h_pass,
				    (n_hkeyfiles > 0)?MAX_PASSSZ:strlen(h_pass), hehdr,
				    &hinfo);
			}
		}

		/* We need both to protect a hidden volume */
		if ((protect_hidden && (error || error2)) ||
		    (error && error2)) {
			tc_log(1, "Incorrect password or not a TrueCrypt volume\n");
			info = NULL;
			hinfo = NULL;

			/* Try again (or finish) */
			free_safe_mem(pass);
			if (h_pass)
				free_safe_mem(h_pass);
			if (ehdr)
				free_safe_mem(ehdr);
			if (hehdr)
				free_safe_mem(hehdr);
			continue;
		}

		if (protect_hidden) {
			if (adjust_info(info, hinfo) != 0) {
				tc_log(1, "Could not protect hidden volume\n");
				return NULL;
			}
		}
        }

	return info;
}

int
info_volume(const char *device, int sflag, const char *sys_dev,
    int protect_hidden, const char *keyfiles[], int nkeyfiles,
    const char *h_keyfiles[], int n_hkeyfiles,
    char *passphrase, char *passphrase_hidden, int interactive, int retries,
    time_t timeout)
{
	struct tcplay_info *info;

	info = info_map_common(device, sflag, sys_dev, protect_hidden,
	    keyfiles, nkeyfiles, h_keyfiles, n_hkeyfiles,
	    passphrase, passphrase_hidden, interactive, retries, timeout);

	if (info != NULL) {
		if (interactive)
			print_info(info);
	}

	return (info != NULL) ? 0 : -1;
}

int
map_volume(const char *map_name, const char *device, int sflag,
    const char *sys_dev, int protect_hidden, const char *keyfiles[],
    int nkeyfiles, const char *h_keyfiles[], int n_hkeyfiles,
    char *passphrase, char *passphrase_hidden, int interactive, int retries,
    time_t timeout)

{
	struct tcplay_info *info;
	int error;

	info = info_map_common(device, sflag, sys_dev, protect_hidden,
	    keyfiles, nkeyfiles, h_keyfiles, n_hkeyfiles,
	    passphrase, passphrase_hidden, interactive, retries, timeout);

	if (info == NULL)
		return -1;

	if ((error = dm_setup(map_name, info)) != 0) {
		tc_log(1, "Could not set up mapping %s\n", map_name);
		return -1;
	}

	if (interactive)
		printf("All ok!");

	return 0;
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
	char *uu;
	char *uu_stack[64];
	int uu_stack_idx;
	uint32_t status;
	int ret = 0;
	int j;
	off_t start, offset;
	char dev[PATH_MAX];
	char map[PATH_MAX];

	if ((params = alloc_safe_mem(512)) == NULL) {
		tc_log(1, "could not allocate safe parameters memory");
		return ENOMEM;
	}

	strcpy(dev, info->dev);
	start = info->start;
	offset = info->offset;
	uu_stack_idx = 0;

	/* Get to the end of the chain */
	for (cipher_chain = info->cipher_chain; cipher_chain->next != NULL;
	    cipher_chain = cipher_chain->next)
		;

	for (j= 0; cipher_chain != NULL;
	    cipher_chain = cipher_chain->prev, j++) {
		/* aes-cbc-essiv:sha256 7997f8af... 0 /dev/ad0s0a 8 */
		/*			   iv off---^  block off--^ */
		snprintf(params, 512, "%s %s %"PRIu64 " %s %"PRIu64,
		    cipher_chain->cipher->dm_crypt_str, cipher_chain->dm_key,
		    info->skip, dev, offset);
#ifdef DEBUG
		printf("Params: %s\n", params);
#endif

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

		uuid_create(&info->uuid, &status);
		if (status != uuid_s_ok) {
			tc_log(1, "uuid_create failed\n");
			ret = -1;
			goto out;
		}

		uuid_to_string(&info->uuid, &uu, &status);
		if (uu == NULL) {
			tc_log(1, "uuid_to_string failed\n");
			ret = -1;
			goto out;
		}

		if ((dm_task_set_uuid(dmt, uu)) == 0) {
			free(uu);
			tc_log(1, "dm_task_set_uuid failed\n");
			ret = -1;
			goto out;
		}

		free(uu);

		if ((dm_task_add_target(dmt, start, info->size, "crypt", params)) == 0) {
			tc_log(1, "dm_task_add_target failed\n");
			ret = -1;
			goto out;
		}

		if ((dm_task_run(dmt)) == 0) {
			tc_log(1, "dm_task_task_run failed\n");
			ret = -1;
			goto out;
		}

		if ((dm_task_get_info(dmt, &dmi)) == 0) {
			tc_log(1, "dm_task_get info failed\n");
			/* XXX: probably do more than just erroring out... */
			ret = -1;
			goto out;
		}

		asprintf(&uu_stack[uu_stack_idx++], "%s", map);

		offset = 0;
		start = 0;
		sprintf(dev, "/dev/mapper/%s.%d", mapname, j);

		dm_task_destroy(dmt);
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
			if ((ret = dm_remove_device(uu_stack[--j])) != 0) {
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
	for (i = 2; i >= 0; i--) {
		sprintf(map, "%s.%d", mapname, i);
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
check_cipher_chain(char *cipher_chain, int quiet)
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
check_prf_algo(char *algo, int quiet)
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
