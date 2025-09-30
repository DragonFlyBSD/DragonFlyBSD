/**
 * Test encryption / decryption using different crypto backends
 * (cryptodev and cryptoapi).
 */

#include <sys/param.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	char *cipher_name;
	int do_encrypt;
	size_t klen;
	size_t datalen;
	size_t ivlen;
	uint8_t *key;
	uint8_t *data;
	uint8_t *iv;
} test_fixture;

#define panic_if(cond)                                                         \
	if (cond) {                                                            \
		fprintf(stderr, "PANIC: %d\n", __LINE__);                      \
		exit(1);                                                       \
	}

static const char *get_test_fixture_header(void);
static void print_test_fixture_header(FILE *out);
static void print_test_fixture(FILE *out, test_fixture *fix);
static void read_test_fixture(char *line, test_fixture *fix);
static void free_test_fixture(test_fixture *fix);
static void print_hex(FILE *out, const uint8_t *bytes, size_t len);
static void read_hex(const char *str, uint8_t *bytes, size_t len);

typedef int(crypt_impl_t)(
    const char *cipher_name, unsigned char *key, size_t klen, unsigned char *iv,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt);

crypt_impl_t syscrypt_cryptodev;
crypt_impl_t syscrypt_cryptoapi;

static uint8_t *safe_syscrypt(
    const char *cipher_name, crypt_impl_t *impl, const uint8_t *key,
    size_t klen, const uint8_t *iv, size_t ivlen, const uint8_t *data,
    size_t datalen, int do_encrypt);

const char *get_test_fixture_header(void)
{
	return "cipher;do_encrypt;klen;datalen;ivlen;key;data;iv\n";
}

void print_test_fixture_header(FILE *out)
{
	fprintf(out, "%s", get_test_fixture_header());
}

void print_test_fixture(FILE *out, test_fixture *fix)
{
	fprintf(
	    out, "%s;%d;%ld;%ld;%ld;", fix->cipher_name, fix->do_encrypt,
	    fix->klen, fix->datalen, fix->ivlen);
	print_hex(out, fix->key, fix->klen);
	fprintf(out, ";");
	print_hex(out, fix->data, fix->datalen);
	fprintf(out, ";");
	print_hex(out, fix->iv, fix->ivlen);
	fprintf(out, "\n");
}

void read_test_fixture(char *line, test_fixture *fix)
{
	char *token;
	bzero(fix, sizeof(*fix));

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->cipher_name = strdup(token);

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->do_encrypt = atoi(token);

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->klen = atoi(token);

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->datalen = atoi(token);

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->ivlen = atoi(token);

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->key = malloc(fix->klen);
	bzero(fix->key, fix->klen);
	read_hex(token, fix->key, fix->klen);

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->data = malloc(fix->datalen);
	bzero(fix->data, fix->datalen);
	read_hex(token, fix->data, fix->datalen);

	token = strsep(&line, ";");
	panic_if(token == NULL);
	fix->iv = malloc(fix->ivlen);
	bzero(fix->iv, fix->ivlen);
	read_hex(token, fix->iv, fix->ivlen);
}

void free_test_fixture(test_fixture *fix)
{
	free(fix->cipher_name);
	free(fix->key);
	free(fix->data);
	free(fix->iv);
}

void print_hex(FILE *out, const uint8_t *bytes, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		fprintf(out, "%02x", bytes[i]);
	}
}

void read_hex(const char *str, uint8_t *bytes, size_t len)
{
	char hex[3];
	int byte;

	for (; len > 0; --len) {
		hex[0] = *(str++);
		hex[1] = *(str++);
		hex[2] = '\0';

		panic_if(sscanf(hex, "%x", &byte) != 1);
		*(bytes++) = byte;
	}
}

/**
 * On success, returns the encyrypted/decrypted data (allocated).
 */
static uint8_t *safe_syscrypt(
    const char *cipher_name, crypt_impl_t *impl, const uint8_t *key,
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

	int error = impl(
	    cipher_name, keycopy, klen, ivcopy, inbuf, outbuf, datalen,
	    do_encrypt);

	if (error) {
		printf("syscrypt impl failed with: %d\n", error);
		free(inbuf);
		free(outbuf);
		return NULL;
	}

	free(inbuf);
	return outbuf;
}

int main(int argc, char **argv)
{
	char line[1024];
	test_fixture fix;
	char *out;
	FILE *f;
	crypt_impl_t *crypt_impl;
	char *filename;

	--argc;
	if (argc != 2)
		exit(1);

	filename = argv[1];

	if (strcmp(argv[2], "cryptoapi") == 0)
		crypt_impl = &syscrypt_cryptoapi;
	else if (strcmp(argv[2], "cryptodev") == 0)
		crypt_impl = &syscrypt_cryptodev;
	else
		exit(1);

	f = fopen(filename, "r");
	panic_if(f == NULL);

	fgets(line, sizeof(line), f);
	panic_if(strcmp(line, get_test_fixture_header()) != 0);

	printf("# Test Results\n\n");

	for (int i = 1; fgets(line, sizeof(line), f); ++i) {
		read_test_fixture(line, &fix);

		printf("## Test %d\n\n", i);

		printf("Fixture:\n\n");
		printf("```\n");
		print_test_fixture_header(stdout);
		print_test_fixture(stdout, &fix);
		printf("```\n\n");

		out = safe_syscrypt(
		    fix.cipher_name, crypt_impl, fix.key, fix.klen, fix.iv,
		    fix.ivlen, fix.data, fix.datalen, fix.do_encrypt);

		printf("Result:\n\n");

		if (out == NULL) {
			printf("FAILED\n");
		} else {
			printf("```\n");
			print_hex(stdout, out, fix.datalen);
			printf("\n```\n\n");
		}

		free_test_fixture(&fix);
	}

	fclose(f);

	return 0;
}
