/**
 * Tests encryption / decryption by comparing
 * output between cryptodev and cryptoapi.
 */

#include <sys/param.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct test_param {
	const char *cipher_name;
	size_t klen;
};

static const struct test_param test_params[] = {

	{ .cipher_name = "AES-128-XTS", .klen = 32 },
	{ .cipher_name = "AES-128-XTS", .klen = 64 },
	{ .cipher_name = "AES-256-XTS", .klen = 32 },
	{ .cipher_name = "AES-256-XTS", .klen = 64 },

	{ .cipher_name = "TWOFISH-128-XTS", .klen = 32 },
	{ .cipher_name = "TWOFISH-128-XTS", .klen = 64 },

	{ .cipher_name = "TWOFISH-256-XTS", .klen = 32 },
	{ .cipher_name = "TWOFISH-256-XTS", .klen = 64 },

	{ .cipher_name = "SERPENT-128-XTS", .klen = 32 },
	{ .cipher_name = "SERPENT-128-XTS", .klen = 64 },

	{ .cipher_name = "SERPENT-256-XTS", .klen = 32 },
	{ .cipher_name = "SERPENT-256-XTS", .klen = 64 }

};

int syscrypt_cryptodev(const char *cipher_name, unsigned char *key, size_t klen,
    unsigned char *iv, unsigned char *in, unsigned char *out, size_t len,
    int do_encrypt);

int syscrypt_cryptoapi(const char *cipher_name, unsigned char *key, size_t klen,
    unsigned char *iv, unsigned char *in, unsigned char *out, size_t len,
    int do_encrypt);

typedef int (*syscrypt_impl)(const char *cipher_name, unsigned char *key,
    size_t klen, unsigned char *iv, unsigned char *in, unsigned char *out,
    size_t len, int do_encrypt);

static uint8_t *safe_syscrypt(const char *cipher_name, syscrypt_impl impl,
    const uint8_t *key, size_t klen, const uint8_t *iv, size_t ivlen,
    const uint8_t *data, size_t datalen, int do_encrypt);

int testcase(const char *cipher_name, const char *key, size_t klen,
    size_t datalen, int do_encrypt);

/**
 * On success, returns the encyrypted/decrypted data (allocated).
 */
static uint8_t *
safe_syscrypt(const char *cipher_name, syscrypt_impl impl, const uint8_t *key,
    size_t klen, const uint8_t *iv, size_t ivlen, const uint8_t *data,
    size_t datalen, int do_encrypt)
{
	char keycopy[64];
	char ivcopy[256];
	char *inbuf, *outbuf;

	inbuf = outbuf = NULL;

	if (klen > sizeof(keycopy)) {
		printf("Invalid klen\n");
		return NULL;
	}

	if (ivlen > sizeof(ivcopy)) {
		printf("Invalid iv\n");
		return NULL;
	}

	inbuf = malloc(datalen);
	if (inbuf == NULL) {
		printf("No memory\n");
		return NULL;
	}

	outbuf = malloc(datalen);
	if (outbuf == NULL) {
		printf("No memory\n");
		free(inbuf);
		return NULL;
	}

	memset(inbuf, 0, datalen);
	memset(outbuf, 0, datalen);
	memcpy(inbuf, data, datalen);

	memset(keycopy, 0, sizeof(keycopy));
	memcpy(keycopy, key, klen);

	memset(ivcopy, 0, sizeof(ivcopy));
	memcpy(ivcopy, iv, ivlen);

	int error = impl(cipher_name, keycopy, klen, ivcopy, inbuf, outbuf,
	    datalen, do_encrypt);

	if (error) {
		printf("syscrypt impl failed with: %d\n", error);
		free(inbuf);
		free(outbuf);
		return NULL;
	}

	free(inbuf);
	return outbuf;
}

#define AES_BLOCK_LEN 16

int
testcase(const char *cipher_name, const char *key, size_t klen, size_t datalen,
    int do_encrypt)
{
	char *data;
	char *out_cryptoapi;
	char *out_cryptodev;
	const char *errmsg;
	uint8_t iv[AES_BLOCK_LEN];

	data = NULL;
	out_cryptoapi = NULL;
	out_cryptodev = NULL;

	printf("TC\tc:%s\tklen:%ld\tdatalen:%ld\tencdec:%d\t", cipher_name,
	    klen, datalen, do_encrypt);

	data = malloc(datalen);
	if (data == NULL) {
		errmsg = "No memory";
		goto err;
	}

	arc4random_buf(data, datalen);
	arc4random_buf(iv, sizeof(iv));

	out_cryptoapi = safe_syscrypt(cipher_name, syscrypt_cryptoapi, key,
	    klen, iv, sizeof(iv), data, datalen, do_encrypt);

	if (out_cryptoapi == NULL) {
		errmsg = "cryptoapi failed";
		goto err;
	}

	out_cryptodev = safe_syscrypt(cipher_name, syscrypt_cryptodev, key,
	    klen, iv, sizeof(iv), data, datalen, do_encrypt);

	if (out_cryptodev == NULL) {
		errmsg = "cryptodev failed";
		goto err;
	}

	if (memcmp(out_cryptoapi, out_cryptodev, datalen) != 0) {
		errmsg = "wrong result";
		goto err;
	}

	printf("OK\n");
	free(data);
	free(out_cryptoapi);
	free(out_cryptodev);
	return (0);
err:

	if (data)
		free(data);
	if (out_cryptoapi)
		free(out_cryptoapi);
	if (out_cryptodev)
		free(out_cryptodev);

	printf("FAILED (%s)\n", errmsg);
	return (1);
}

int
main(int argc __unused, char **argv __unused)
{
	char key[64];
	int total, failed;

	arc4random_buf(key, sizeof(key));

	total = 0;
	failed = 0;

	for (size_t i = 0; i < nitems(test_params); ++i)
		for (int do_encrypt = 0; do_encrypt <= 1; ++do_encrypt)
			for (int block_cnt = 1; block_cnt <= 10; ++block_cnt) {
				++total;
				failed += testcase(test_params[i].cipher_name,
				    key, test_params[i].klen,
				    block_cnt * AES_BLOCK_LEN, do_encrypt);
			}

	printf("Total test cases: %d, failed: %d\n", total, failed);
	if (failed > 0) {
		return 1;
	}
	return 0;
}
