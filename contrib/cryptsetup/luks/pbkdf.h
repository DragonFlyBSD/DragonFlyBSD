#ifndef INCLUDED_CRYPTSETUP_LUKS_PBKDF_H
#define INCLUDED_CRYPTSETUP_LUKS_PBKDF_H

#include <stddef.h>

/* */

int PBKDF2_HMAC(const char *hash,
		const char *password, size_t passwordLen,
		const char *salt, size_t saltLen, unsigned int iterations,
		char *dKey, size_t dKeyLen);


int PBKDF2_performance_check(const char *hash, uint64_t *iter);
int PBKDF2_HMAC_ready(const char *hash);

#endif
