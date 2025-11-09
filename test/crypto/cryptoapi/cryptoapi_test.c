/**
 * Test encryption / decryption using cryptoapi.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

struct test_fixture;

struct test_fixture
{
	int valid;
	int lineno;
	char *raw_line;
	char *cipher_name;
	size_t key_len;
	size_t iv_len;
	size_t data_len;
	uint8_t *key;
	uint8_t *iv;
	uint8_t *pt;
	uint8_t *ct;
	struct test_fixture *next;
};

static void usage(void);
static const char *get_test_fixture_header(void);
static void print_test_fixture_header(FILE *out);
static void
print_test_fixture(FILE *out, struct test_fixture *fixture, uint8_t *result);
static int
read_test_fixture(char *line, int lineno, struct test_fixture *fixture);
static struct test_fixture *read_fixtures_from_file(const char *filename);

static void free_test_fixture(struct test_fixture *fixture);
static void print_hex(FILE *out, const uint8_t *bytes, size_t len);
static void read_hex(const char *str, uint8_t *bytes, size_t len);
static int parse_hex_digit(char ch);
static char to_hex(int n);

typedef int(crypt_impl_t)(
    const char *cipher_name, unsigned char *key, size_t klen, unsigned char *iv,
    unsigned char *in, unsigned char *out, size_t len, int do_encrypt,
    int repetitions);

crypt_impl_t syscrypt_cryptoapi;

static uint8_t *safe_syscrypt(
    const char *cipher_name, crypt_impl_t *impl, const uint8_t *key,
    size_t klen, const uint8_t *iv, size_t ivlen, const uint8_t *data,
    size_t datalen, int do_encrypt, int repetitions);

const char *get_test_fixture_header(void)
{
	return "cipher;key_len;iv_len;data_len;key;iv;pt;ct\n";
}

void print_test_fixture_header(FILE *out)
{
	fprintf(out, "%s", get_test_fixture_header());
}

void print_test_fixture(
    FILE *out, struct test_fixture *fixture, uint8_t *result)
{
	fprintf(
	    out, "%s;%ld;%ld;%ld;", fixture->cipher_name, fixture->key_len,
	    fixture->iv_len, fixture->data_len);
	print_hex(out, fixture->key, fixture->key_len);
	fprintf(out, ";");
	print_hex(out, fixture->iv, fixture->iv_len);
	fprintf(out, ";");
	print_hex(out, fixture->pt, fixture->data_len);
	fprintf(out, ";");
	print_hex(out, result ? result : fixture->ct, fixture->data_len);
	fprintf(out, "\n");
}

int read_test_fixture(char *line, int lineno, struct test_fixture *fixture)
{
	char *token;

	bzero(fixture, sizeof(*fixture));

	fixture->valid = 0;
	fixture->lineno = lineno;
	fixture->raw_line = strdup(line);

	if (line[0] == '\n' || line[0] == '#')
		return (0);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->cipher_name = strdup(token);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->key_len = atoi(token);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->iv_len = atoi(token);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->data_len = atoi(token);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->key = malloc(fixture->key_len);
	bzero(fixture->key, fixture->key_len);
	read_hex(token, fixture->key, fixture->key_len);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->iv = malloc(fixture->iv_len);
	bzero(fixture->iv, fixture->iv_len);
	read_hex(token, fixture->iv, fixture->iv_len);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->pt = malloc(fixture->data_len);
	bzero(fixture->pt, fixture->data_len);
	read_hex(token, fixture->pt, fixture->data_len);

	token = strsep(&line, ";");
	if (token == NULL)
		return (1);
	fixture->ct = malloc(fixture->data_len);
	bzero(fixture->ct, fixture->data_len);
	read_hex(token, fixture->ct, fixture->data_len);

	fixture->valid = 1;
	return (0);
}

void free_test_fixture(struct test_fixture *fixture)
{
	if (fixture->raw_line)
		free(fixture->raw_line);
	if (fixture->cipher_name)
		free(fixture->cipher_name);
	if (fixture->key)
		free(fixture->key);
	if (fixture->iv)
		free(fixture->iv);
	if (fixture->pt)
		free(fixture->pt);
	if (fixture->ct)
		free(fixture->ct);
	free(fixture);
}

void print_hex(FILE *out, const uint8_t *bytes, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		fputc(to_hex(bytes[i] / 16), out);
		fputc(to_hex(bytes[i] % 16), out);
	}
}

int parse_hex_digit(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	if (ch >= 'a' && ch <= 'f')
		return (10 + ch - 'a');
	if (ch >= 'A' && ch <= 'F')
		return (10 + ch - 'A');

	return (-1);
}

char to_hex(int n)
{
	if (n >= 0 && n <= 9)
		return ('0' + n);
	if (n >= 10 && n <= 15)
		return ('a' + n - 10);
	return ('X');
}

void read_hex(const char *str, uint8_t *bytes, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		int digit0 = parse_hex_digit(str[2 * i]);
		if (digit0 < 0) {
			fprintf(
			    stderr,
			    "ERROR: Failed to parse hex string %s (near: %s)\n",
			    str, &str[2 * i]);
			exit(1);
		}
		int digit1 = parse_hex_digit(str[2 * i + 1]);
		if (digit1 < 0) {
			fprintf(
			    stderr,
			    "ERROR: Failed to parse hex string %s (near: %s)\n",
			    str, &str[2 * i + 1]);
			exit(1);
		}
		*(bytes++) = digit0 * 16 + digit1;
	}
}

/**
 * On success, returns the encyrypted/decrypted data (allocated).
 */
static uint8_t *safe_syscrypt(
    const char *cipher_name, crypt_impl_t *impl, const uint8_t *key,
    size_t klen, const uint8_t *iv, size_t ivlen, const uint8_t *data,
    size_t datalen, int do_encrypt, int repetitions)
{
	char keycopy[64];
	char ivcopy[256];
	char *inbuf, *outbuf;
	int error;

	inbuf = outbuf = NULL;

	if (klen > sizeof(keycopy)) {
		fprintf(stderr, "ERROR: Invalid klen\n");
		exit(1);
	}

	if (ivlen > sizeof(ivcopy)) {
		fprintf(stderr, "ERROR: Invalid iv\n");
		exit(1);
	}

	inbuf = malloc(datalen);
	if (inbuf == NULL) {
		fprintf(stderr, "ERROR: No memory\n");
		exit(1);
	}

	outbuf = malloc(datalen);
	if (outbuf == NULL) {
		fprintf(stderr, "ERROR: No memory\n");
		free(inbuf);
		exit(1);
	}

	bzero(inbuf, datalen);
	bzero(outbuf, datalen);
	memcpy(inbuf, data, datalen);

	bzero(keycopy, sizeof(keycopy));
	memcpy(keycopy, key, klen);

	bzero(ivcopy, sizeof(ivcopy));
	memcpy(ivcopy, iv, ivlen);

	error = impl(
	    cipher_name, keycopy, klen, ivcopy, inbuf, outbuf, datalen,
	    do_encrypt, repetitions);

	if (error) {
		fprintf(
		    stderr, "ERROR: syscrypt impl failed with: %d\n", error);
		free(inbuf);
		free(outbuf);
		return (NULL);
	}

	free(inbuf);
	return outbuf;
}

void usage(void)
{
	printf("Usage: cryptoapi_test [OPTIONS]\n\n");
	printf("Options:\n");
	printf(
	    "    -t CRYPTOIMPL       Crypto implementation to test [default: "
	    "cryptoapi]\n");
	printf(
	    "    -f FIXTURE          Test fixture [default: fixtures.csv]\n");
	printf("    -o FILE             Write results to FILE\n");
	printf(
	    "    -b REPETITIONS      Run benchmark with specified number of "
	    "repetitions\n");
	printf("    -h                  Print this help message\n");
}

struct test_fixture *read_fixtures_from_file(const char *filename)
{
	static char linebuf[4 * 4096];
	FILE *f;
	struct test_fixture *head = NULL;
	struct test_fixture *prev = NULL;
	struct test_fixture *fixture = NULL;
	int error;

	f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "ERROR: Failed to open file %s\n", filename);
		exit(1);
	}

	fgets(linebuf, sizeof(linebuf), f);
	if (strcmp(linebuf, get_test_fixture_header()) != 0) {
		fprintf(stderr, "ERROR: invalid test fixture header\n");
		exit(1);
	}

	for (int lineno = 1; fgets(linebuf, sizeof(linebuf), f); ++lineno) {
		fixture = malloc(sizeof(struct test_fixture));

		error = read_test_fixture(linebuf, lineno, fixture);
		if (error) {
			fprintf(
			    stderr, "Invalid test fixture in line %d. Line: %s",
			    lineno, fixture->raw_line);
			exit(1);
		}

		fixture->next = NULL;
		if (head == NULL)
			head = fixture;
		if (prev)
			prev->next = fixture;

		prev = fixture;
	}

	fclose(f);

	return (head);
}

int main(int argc, char *argv[])
{
	struct timespec start, stop, tv;
	int ch;

	const char *crypt_impl_name = "cryptoapi";
	const char *fixtures_filename = "fixtures.csv";
	crypt_impl_t *crypt_impl = NULL;
	int repetitions = 1;
	bool benchmark = false;
	const char *result_filename = NULL;
	FILE *result_file = NULL;

	while ((ch = getopt(argc, argv, "t:f:b:o:h")) != -1) {
		switch (ch) {
		case 't':
			crypt_impl_name = optarg;
			break;
		case 'f':
			fixtures_filename = optarg;
			break;
		case 'b':
			benchmark = true;
			repetitions = atoi(optarg);
			break;
		case 'o':
			result_filename = optarg;
			break;
		case 'h':
			/* fall-through */
			usage();
			exit(0);
			break;

		default:
			usage();
			exit(1);
			break;
		}
	}

	if (strcmp(crypt_impl_name, "cryptoapi") == 0)
		crypt_impl = &syscrypt_cryptoapi;
	else {
		usage();
		exit(2);
	}

	struct test_fixture *fixtures =
	    read_fixtures_from_file(fixtures_filename);

	int total = 0;
	int failures = 0;
	int ok = 0;

	if (result_filename)
		result_file = fopen(result_filename, "w");

	if (result_file)
		print_test_fixture_header(result_file);

	if (benchmark) {
		fprintf(
		    stderr, "--------------------------------------------\n");
		fprintf(
		    stderr, "%-22s %21s\n",
		    "crypto implementation:", crypt_impl_name);
		fprintf(stderr, "%-22s %21s\n", "fixtures:", fixtures_filename);
		fprintf(stderr, "%-22s %21d\n", "repetitions:", repetitions);
		fprintf(
		    stderr, "--------------------------------------------\n");
		fprintf(
		    stderr, "%15s %5s %9s %5s %6s\n", "Cipher", "Mode",
		    "Blocklen", "Time", "MiB/s");
	}

	for (struct test_fixture *fixture = fixtures; fixture;
	     fixture = fixture->next) {
		if (!fixture->valid) {
			if (result_file)
				fprintf(result_file, "%s", fixture->raw_line);
			continue;
		}

		for (int do_encrypt = 0; do_encrypt <= 1; ++do_encrypt) {
			clock_gettime(CLOCK_REALTIME, &start);
			uint8_t *data = do_encrypt ? fixture->pt : fixture->ct;
			uint8_t *expected =
			    do_encrypt ? fixture->ct : fixture->pt;
			uint8_t *result = safe_syscrypt(
			    fixture->cipher_name, crypt_impl, fixture->key,
			    fixture->key_len, fixture->iv, fixture->iv_len,
			    data, fixture->data_len, do_encrypt, repetitions);
			clock_gettime(CLOCK_REALTIME, &stop);
			timespecsub(&stop, &start, &tv);
			double secs = (double)tv.tv_sec +
				      (double)tv.tv_nsec / 1000000000.0;
			double mib_per_second =
			    ((double)fixture->data_len * repetitions * 2) /
			    (1024.0 * 1024.0) / secs;
			if (benchmark)
				fprintf(
				    stderr, "%15s %5s %9ld %5.2f %6.1f\n",
				    fixture->cipher_name,
				    (do_encrypt ? "ENC" : "DEC"),
				    fixture->data_len, secs, mib_per_second);

			++total;
			if (result &&
			    memcmp(result, expected, fixture->data_len) == 0) {
				if (!benchmark) {
					fprintf(stderr, ".");
					fflush(stderr);
				}
				++ok;
			} else {
				++failures;
				if (!benchmark) {
					fprintf(
					    stderr, "\nFAILED %s line %d\n",
					    do_encrypt ? "ENC" : "DEC",
					    fixture->lineno);
					fprintf(stderr, "Expected: \n");
					print_hex(
					    stderr, expected,
					    fixture->data_len);
					fprintf(stderr, "\nActual: \n");
					print_hex(
					    stderr, result, fixture->data_len);
					fprintf(stderr, "\n");
				}
			}

			if (result_file && do_encrypt)
				print_test_fixture(
				    result_file, fixture, result);

			if (result)
				free(result);
		}
	}

	if (result_file)
		fclose(result_file);

	while (fixtures) {
		struct test_fixture *next = fixtures->next;
		free_test_fixture(fixtures);
		fixtures = next;
	}

	fprintf(stderr, "\n");
	fprintf(stderr, "--------------------------------------------\n");
	fprintf(
	    stderr, "Total: %d, OK: %d, Failures: %d\n", total, ok, failures);
	fprintf(stderr, "--------------------------------------------\n");

	if (!benchmark) {
		if (failures > 0 || ok != total)
			exit(1);
	}

	return 0;
}
