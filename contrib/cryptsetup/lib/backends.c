#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <openssl/evp.h>

#include "libcryptsetup.h"
#include "internal.h"

int init_crypto(void)
{
	return 0;
}

int hash(const char *backend_name, const char *hash_name,
         char *result, size_t size,
         const char *passphrase, size_t sizep)
{
	EVP_MD_CTX mdctx;
	const EVP_MD *md;
	size_t pad = 0;
	int r = -ENOENT;

	OpenSSL_add_all_digests();
	md = EVP_get_digestbyname(hash_name);
	if (md == NULL) {
		set_error("Unknown hash type %s", hash_name);
		goto out;
	}

	if (EVP_MD_size(md) > size) {
		set_error("requested hash length (%zd) > key length (%zd)", EVP_MD_size(md), size);
		return -EINVAL;
	}

	pad = size - EVP_MD_size(md);

	EVP_DigestInit(&mdctx, md);
	EVP_DigestUpdate(&mdctx, passphrase, sizep);
	r = !EVP_DigestFinal(&mdctx, result, NULL);

	if (pad) {
		memset(result+size, 0, pad);
	}

out:
	return r;
}

