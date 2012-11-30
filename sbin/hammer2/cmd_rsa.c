/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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

#include "hammer2.h"

#include <openssl/rsa.h>	/* public/private key functions */
#include <openssl/pem.h>	/* public/private key file load */
#include <openssl/err.h>

/*
 * Should be run as root.  Creates /etc/hammer2/rsa.{pub,prv} using
 * an openssl command.
 */
int
cmd_rsainit(const char *dir_path)
{
	struct stat st;
	int ecode;
	char *str1;
	char *str2;
	char *cmd;
	mode_t old_umask;

	/*
	 * Create the directory if necessary
	 */
	if (stat(dir_path, &st) < 0) {
		str1 = strdup(dir_path);
		str2 = str1 - 1;

		while ((str2 = strchr(str2 + 1, '/')) != NULL) {
			*str2 = 0;
			mkdir(str1, 0755);
			*str2 = '/';
		}
		mkdir(str1, 0700);
		free(str1);
	}
	asprintf(&str1, "%s/rsa.prv", dir_path);
	asprintf(&str2, "%s/rsa.pub", dir_path);

	old_umask = umask(077);
	if (stat(str1, &st) < 0) {
		asprintf(&cmd, "openssl genrsa -out %s 2048", str1);
		ecode = system(cmd);
		free(cmd);
		chmod(str1, 0400);
		if (ecode) {
			fprintf(stderr,
				"hammer2 rsainit: private key gen failed\n");
			free(str2);
			free(str1);
			umask(old_umask);
			return 1;
		}
		printf("hammer2 rsainit: created %s\n", str1);
		remove(str2);
	} else {
		printf("hammer2 rsainit: Using existing private key in %s\n",
		       str1);
	}
	if (stat(str2, &st) < 0) {
		asprintf(&cmd, "openssl rsa -in %s -out %s -pubout",
			 str1, str2);
		ecode = system(cmd);
		free(cmd);
		if (ecode) {
			fprintf(stderr,
				"hammer2 rsainit: public key gen failed\n");
			free(str2);
			free(str1);
			umask(old_umask);
			return 1;
		}
		printf("hammer2 rsainit: created %s\n", str2);
	} else {
		printf("hammer2 rsainit: both keys already exist\n");
	}
	umask(old_umask);
	free(str2);
	free(str1);

	return 0;
}

int
cmd_rsaenc(const char **keyfiles, int nkeys)
{
	RSA **keys = calloc(nkeys, sizeof(RSA *));
	int *ispub = calloc(nkeys, sizeof(int));
	int ecode = 0;
	int blksize = 0;
	int i;
	int off;
	int n;
	unsigned char *data_in;
	unsigned char *data_out;

	for (i = 0; i < nkeys; ++i) {
		FILE *fp;
		const char *sfx;

		sfx = strrchr(keyfiles[i], '.');
		if (sfx && strcmp(sfx, ".pub") == 0) {
			fp = fopen(keyfiles[i], "r");
			if (fp == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"open %s\n", keyfiles[i]);
				ecode = 1;
				goto done;
			}
			keys[i] = PEM_read_RSA_PUBKEY(fp, NULL, NULL, NULL);
			ispub[i] = 1;
			fclose(fp);
			if (keys[i] == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"parse public key from %s\n",
						keyfiles[i]);
				ecode = 1;
				goto done;
			}
		} else if (sfx && strcmp(sfx, ".prv") == 0) {
			fp = fopen(keyfiles[i], "r");
			if (fp == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"open %s\n", keyfiles[i]);
				ecode = 1;
				goto done;
			}
			keys[i] = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
			if (keys[i] == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"parse private key from %s\n",
						keyfiles[i]);
				ecode = 1;
				goto done;
			}
		} else {
			fprintf(stderr, "hammer2: rsaenc: key files must end "
					"in .pub or .prv\n");
			ecode = 1;
			goto done;
		}
		if (i == 0)
			blksize = RSA_size(keys[i]);
		else
			assert(blksize == RSA_size(keys[i]));
	}
	fprintf(stderr, "blksize %d\n", blksize);

	/*
	 *
	 */
	data_in = malloc(blksize);
	data_out = malloc(blksize);
	off = 0;
	while ((n = read(0, data_in + off, blksize - off)) > 0) {
		off += n;
		if (off == blksize) {
			for (i = 0; i < nkeys; ++i) {
				if (ispub[i])
					RSA_public_encrypt(blksize,
							   data_in, data_out,
							   keys[i],
							   RSA_NO_PADDING);
				else
					RSA_private_encrypt(blksize,
							   data_in, data_out,
							   keys[i],
							   RSA_NO_PADDING);
				if (i + 1 != nkeys)
					bcopy(data_out, data_in, blksize);
			}
			if (write(1, data_out, blksize) != blksize) {
				perror("write");
				ecode = 1;
				break;
			}
			off = 0;
		}
	}
	if (off && ecode == 0) {
		if (off < blksize)
			bzero(data_in + off, blksize - off);
		for (i = 0; i < nkeys; ++i) {
			if (ispub[i])
				RSA_public_encrypt(blksize,
						   data_in, data_out,
						   keys[i],
						   RSA_NO_PADDING);
			else
				RSA_private_encrypt(blksize,
						   data_in, data_out,
						   keys[i],
						   RSA_NO_PADDING);
			if (i + 1 != nkeys)
				bcopy(data_out, data_in, blksize);
		}
		if (write(1, data_out, blksize) != blksize) {
			perror("write");
			ecode = 1;
		}
	}
	if (n < 0) {
		perror("read");
		ecode = 1;
	}
	free(data_out);
	free(data_in);
done:
	for (i = 0; i < nkeys; ++i) {
		if (keys[i])
			RSA_free(keys[i]);
	}
	free(keys);
	free(ispub);
	return (ecode);
}

int
cmd_rsadec(const char **keyfiles, int nkeys)
{
	RSA **keys = calloc(nkeys, sizeof(RSA *));
	int *ispub = calloc(nkeys, sizeof(int));
	int ecode = 0;
	int blksize = 0;
	int i;
	int off;
	int n;
	unsigned char *data_in;
	unsigned char *data_out;

	for (i = 0; i < nkeys; ++i) {
		FILE *fp;
		const char *sfx;

		sfx = strrchr(keyfiles[i], '.');
		if (sfx && strcmp(sfx, ".pub") == 0) {
			fp = fopen(keyfiles[i], "r");
			if (fp == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"open %s\n", keyfiles[i]);
				ecode = 1;
				goto done;
			}
			keys[i] = PEM_read_RSA_PUBKEY(fp, NULL, NULL, NULL);
			ispub[i] = 1;
			fclose(fp);
			if (keys[i] == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"parse public key from %s\n",
						keyfiles[i]);
				ecode = 1;
				goto done;
			}
		} else if (sfx && strcmp(sfx, ".prv") == 0) {
			fp = fopen(keyfiles[i], "r");
			if (fp == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"open %s\n", keyfiles[i]);
				ecode = 1;
				goto done;
			}
			keys[i] = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
			if (keys[i] == NULL) {
				fprintf(stderr, "hammer2 rsaenc: unable to "
						"parse private key from %s\n",
						keyfiles[i]);
				ecode = 1;
				goto done;
			}
		} else {
			fprintf(stderr, "hammer2: rsaenc: key files must end "
					"in .pub or .prv\n");
			ecode = 1;
			goto done;
		}
		if (i == 0)
			blksize = RSA_size(keys[i]);
		else
			assert(blksize == RSA_size(keys[i]));
	}

	/*
	 *
	 */
	data_in = malloc(blksize);
	data_out = malloc(blksize);
	off = 0;
	while ((n = read(0, data_in + off, blksize - off)) > 0) {
		off += n;
		if (off == blksize) {
			for (i = 0; i < nkeys; ++i) {
				if (ispub[i])
					RSA_public_decrypt(blksize,
							   data_in, data_out,
							   keys[i],
							   RSA_NO_PADDING);
				else
					RSA_private_decrypt(blksize,
							   data_in, data_out,
							   keys[i],
							   RSA_NO_PADDING);
				if (i + 1 != nkeys)
					bcopy(data_out, data_in, blksize);
			}
			if (write(1, data_out, blksize) != blksize) {
				perror("write");
				ecode = 1;
				break;
			}
			off = 0;
		}
	}
	if (off) {
		if (off < blksize)
			bzero(data_in + off, blksize - off);
		for (i = 0; i < nkeys; ++i) {
			if (ispub[i])
				RSA_public_decrypt(blksize,
						   data_in, data_out,
						   keys[i],
						   RSA_NO_PADDING);
			else
				RSA_private_decrypt(blksize,
						   data_in, data_out,
						   keys[i],
						   RSA_NO_PADDING);
			if (i + 1 != nkeys)
				bcopy(data_out, data_in, blksize);
		}
		if (write(1, data_out, blksize) != blksize) {
			perror("write");
			ecode = 1;
		}
	}
	if (n < 0) {
		perror("read");
		ecode = 1;
	}
	free(data_out);
	free(data_in);
done:
	for (i = 0; i < nkeys; ++i) {
		if (keys[i])
			RSA_free(keys[i]);
	}
	free(keys);
	free(ispub);
	return (ecode);
}
