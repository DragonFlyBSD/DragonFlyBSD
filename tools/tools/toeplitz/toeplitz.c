#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define KEYLEN		40
#define HASHLEN		12

static uint8_t	toeplitz_key[KEYLEN];
static uint32_t	hash_table[HASHLEN][256];

static void	toeplitz_init(uint32_t[][256], int, const uint8_t[], int);
static void	getaddrport(char *, uint32_t *, uint16_t *);

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s [-s s1_hex [-s s2_hex]] [-p] [-m mask]"
	    "addr1.port1 addr2.port2\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	uint32_t saddr, daddr;
	uint16_t sport, dport;
	uint32_t res, mask;

	const char *cmd = argv[0];
	uint8_t seeds[2] = { 0x6d, 0x5a };
	int i, opt, use_port;

	i = 0;
	use_port = 0;
	mask = 0xffffffff;

	while ((opt = getopt(argc, argv, "s:pm:")) != -1) {
		switch (opt) {
		case 's':
			if (i >= 2)
				usage(cmd);
			seeds[i++] = strtoul(optarg, NULL, 16);
			break;

		case 'p':
			use_port = 1;
			break;

		case 'm':
			mask = strtoul(optarg, NULL, 16);
			break;

		default:
			usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage(cmd);

	for (i = 0; i < KEYLEN; ++i) {
		if (i & 1)
			toeplitz_key[i] = seeds[1];
		else
			toeplitz_key[i] = seeds[0];
	}

	getaddrport(argv[0], &saddr, &sport);
	getaddrport(argv[1], &daddr, &dport);

	toeplitz_init(hash_table, HASHLEN, toeplitz_key, KEYLEN);

	res =  hash_table[0][(saddr >> 0) & 0xff];
	res ^= hash_table[1][(saddr >> 8) & 0xff];
	res ^= hash_table[2][(saddr >> 16)  & 0xff];
	res ^= hash_table[3][(saddr >> 24)  & 0xff];
	res ^= hash_table[4][(daddr >> 0) & 0xff];
	res ^= hash_table[5][(daddr >> 8) & 0xff];
	res ^= hash_table[6][(daddr >> 16)  & 0xff];
	res ^= hash_table[7][(daddr >> 24)  & 0xff];
	if (use_port) {
		res ^= hash_table[8][(sport >> 0)  & 0xff];
		res ^= hash_table[9][(sport >> 8)  & 0xff];
		res ^= hash_table[10][(dport >> 0)  & 0xff];
		res ^= hash_table[11][(dport >> 8)  & 0xff];
	}

	printf("%#08x, masked %#8x\n", res, res & mask);
	exit(0);
}

static void
toeplitz_init(uint32_t cache[][256], int cache_len,
    const uint8_t key_str[], int key_strlen)
{
	int i;

	if (key_strlen < cache_len + (int)sizeof(uint32_t))
		exit(1);

	for (i = 0; i < cache_len; ++i) {
		uint32_t key[NBBY];
		int j, b, shift, val;

		bzero(key, sizeof(key));

		/*
		 * Calculate 32bit keys for one byte; one key for each bit.
		 */
		for (b = 0; b < NBBY; ++b) {
			for (j = 0; j < 32; ++j) {
				uint8_t k;
				int bit;

				bit = (i * NBBY) + b + j;

				k = key_str[bit / NBBY];
				shift = NBBY - (bit % NBBY) - 1;
				if (k & (1 << shift))
					key[b] |= 1 << (31 - j);
			}
		}

		/*
		 * Cache the results of all possible bit combination of
		 * one byte.
		 */
		for (val = 0; val < 256; ++val) {
			uint32_t res = 0;

			for (b = 0; b < NBBY; ++b) {
				shift = NBBY - b - 1;
				if (val & (1 << shift))
					res ^= key[b];
			}
			cache[i][val] = res;
		}
	}
}

static void
getaddrport(char *ap_str, uint32_t *addr, uint16_t *port0)
{
	uint16_t port;
	char *p;

	p = strrchr(ap_str, '.');
	if (p == NULL) {
		fprintf(stderr, "invalid addr.port %s\n", ap_str);
		exit(1);
	}

	*p = '\0';
	++p;

	port = strtoul(p, NULL, 10);
	*port0 = htons(port);

	if (inet_pton(AF_INET, ap_str, addr) <= 0) {
		fprintf(stderr, "invalid addr %s\n", ap_str);
		exit(1);
	}
}
