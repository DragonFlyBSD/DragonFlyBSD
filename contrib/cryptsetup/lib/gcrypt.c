#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <gcrypt.h>

#include "libcryptsetup.h"
#include "internal.h"

#define MAX_DIGESTS		64
#define GCRYPT_REQ_VERSION	"1.1.42"

int init_crypto(void)
{
	if (!gcry_control (GCRYCTL_INITIALIZATION_FINISHED_P)) {
		if (!gcry_check_version (GCRYPT_REQ_VERSION))
			return -ENOSYS;

/* FIXME: If gcrypt compiled to support POSIX 1003.1e capabilities,
 * it drops all privileges during secure memory initialisation.
 * For now, the only workaround is to disable secure memory in gcrypt.
 * cryptsetup always need at least cap_sys_admin privilege for dm-ioctl
 * and it locks its memory space anyway.
 */
#if 0
		log_dbg("Initializing crypto backend (secure memory disabled).");
		gcry_control (GCRYCTL_DISABLE_SECMEM);
#else
		log_dbg("Initializing crypto backend (using secure memory).");
		gcry_control (GCRYCTL_SUSPEND_SECMEM_WARN);
		gcry_control (GCRYCTL_INIT_SECMEM, 16384, 0);
		gcry_control (GCRYCTL_RESUME_SECMEM_WARN);
#endif
		gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
	}

	return 0;
}

static int gcrypt_hash(void *data, int size, char *key,
                       int sizep, const char *passphrase)
{
	gcry_md_hd_t md;
	int algo = *((int *)data);
	int len = gcry_md_get_algo_dlen(algo);
	int round, i;

	if (gcry_md_open(&md, algo, 0))
		return -1;

	for(round = 0; size; round++) {
		/* hack from hashalot to avoid null bytes in key */
		for(i = 0; i < round; i++)
			gcry_md_write(md, "A", 1);

		gcry_md_write(md, passphrase, sizep);

		if (len > size)
			len = size;
		memcpy(key, gcry_md_read(md, algo), len);

		key += len;
		size -= len;
		if (size)
			gcry_md_reset(md);
	}

	gcry_md_close(md);
	return 0;
}

static struct hash_type *gcrypt_get_hashes(void)
{
	struct hash_type *hashes;
	int size = MAX_DIGESTS;
	int *list;
	int i;
	gcry_error_t r;

	if (!gcry_check_version(GCRYPT_REQ_VERSION))
		return NULL;

	list = (int *)malloc(sizeof(*list) * size);
	if (!list)
		return NULL;

	r = gcry_md_list(list, &size);
	if (r || !size) {
		free(list);
		return NULL;
	}

	hashes = malloc(sizeof(*hashes) * (size + 1));
	if (!hashes) {
		free(list);
		return NULL;
	}

	for(i = 0; i < size; i++) {
		hashes[i].name = NULL;
		hashes[i].private = NULL;
	}

	for(i = 0; i < size; i++) {
		char *p;

		hashes[i].name = strdup(gcry_md_algo_name(list[i]));
		if(!hashes[i].name)
			goto err;
		for(p = (char *)hashes[i].name; *p; p++)
			*p = tolower(*p);
		hashes[i].private = malloc(sizeof(int));
		if(!hashes[i].private)
			goto err;
		*((int *)hashes[i].private) = list[i];
		hashes[i].fn = gcrypt_hash;
	}
	hashes[i].name = NULL;
	hashes[i].private = NULL;
	hashes[i].fn = NULL;

	free(list);

	return hashes;

err:
	free(list);
	for(i = 0; i < size; i++) {
		free(hashes[i].name);
		free(hashes[i].private);
	}
	free(hashes);
	return NULL;
}

static void gcrypt_free_hashes(struct hash_type *hashes)
{
	struct hash_type *hash;

	for(hash = hashes; hash->name; hash++) {
		free(hash->name);
		free(hash->private);
	}

	free(hashes);
}

struct hash_backend hash_gcrypt_backend = {
	.name = "libgcrypt",
	.get_hashes = gcrypt_get_hashes,
	.free_hashes = gcrypt_free_hashes
};
