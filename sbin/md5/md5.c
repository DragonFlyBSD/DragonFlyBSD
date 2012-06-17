/*
 * Derived from:
 *
 * MDDRIVER.C - test driver for MD2, MD4 and MD5
 *
 * $FreeBSD: src/sbin/md5/md5.c,v 1.35 2006/01/17 15:35:57 phk Exp $
 */

/*
 *  Copyright (C) 1990-2, RSA Data Security, Inc. Created 1990. All
 *  rights reserved.
 *
 *  RSA Data Security, Inc. makes no representations concerning either
 *  the merchantability of this software or the suitability of this
 *  software for any particular purpose. It is provided "as is"
 *  without express or implied warranty of any kind.
 *
 *  These notices must be retained in any copies of any part of this
 *  documentation and/or software.
 */

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <err.h>
#include <sys/mman.h>
#include <md5.h>
#include <ripemd.h>
#include <sha.h>
#include <sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sysexits.h>

/*
 * Length of test block, number of test blocks.
 */
#define TEST_BLOCK_LEN 10000
#define TEST_BLOCK_COUNT 100000
#define MDTESTCOUNT 8

int qflag;
int rflag;
int sflag;

typedef void (DIGEST_Init)(void *);
typedef void (DIGEST_Update)(void *, const unsigned char *, size_t);
typedef char *(DIGEST_End)(void *, char *);

extern const char *MD5TestOutput[MDTESTCOUNT];
extern const char *SHA1_TestOutput[MDTESTCOUNT];
extern const char *SHA256_TestOutput[MDTESTCOUNT];
extern const char *RIPEMD160_TestOutput[MDTESTCOUNT];

typedef struct Algorithm_t {
	const char *progname;
	const char *name;
	const char *(*TestOutput)[MDTESTCOUNT];
	DIGEST_Init *Init;
	DIGEST_Update *Update;
	DIGEST_End *End;
	char *(*Data)(const void *, unsigned int, char *);
	char *(*File)(const char *, char *);
} Algorithm_t;

static void MD5_Update(MD5_CTX *, const unsigned char *, size_t);
static void MDString(Algorithm_t *, const char *);
static void MDTimeTrial(Algorithm_t *);
static void MDTestSuite(Algorithm_t *);
static void MDFilter(Algorithm_t *, int);
static void usage(int excode);

typedef union {
	MD5_CTX md5;
	SHA1_CTX sha1;
	SHA256_CTX sha256;
	RIPEMD160_CTX ripemd160;
} DIGEST_CTX;

/* max(MD5_DIGEST_LENGTH, SHA_DIGEST_LENGTH, 
	SHA256_DIGEST_LENGTH, RIPEMD160_DIGEST_LENGTH)*2+1 */
#define HEX_DIGEST_LENGTH 65

/* algorithm function table */

struct Algorithm_t Algorithm[] = {
	{ "md5", "MD5", &MD5TestOutput, (DIGEST_Init*)&MD5Init,
		(DIGEST_Update*)&MD5_Update, (DIGEST_End*)&MD5End,
		&MD5Data, &MD5File },
	{ "sha1", "SHA1", &SHA1_TestOutput, (DIGEST_Init*)&SHA1_Init,
		(DIGEST_Update*)&SHA1_Update, (DIGEST_End*)&SHA1_End,
		&SHA1_Data, &SHA1_File },
	{ "sha256", "SHA256", &SHA256_TestOutput, (DIGEST_Init*)&SHA256_Init,
		(DIGEST_Update*)&SHA256_Update, (DIGEST_End*)&SHA256_End,
		&SHA256_Data, &SHA256_File },
	{ "rmd160", "RMD160", &RIPEMD160_TestOutput,
		(DIGEST_Init*)&RIPEMD160_Init, (DIGEST_Update*)&RIPEMD160_Update,
		(DIGEST_End*)&RIPEMD160_End, &RIPEMD160_Data, &RIPEMD160_File }
};

static void
MD5_Update(MD5_CTX *c, const unsigned char *data, size_t len)
{
	MD5Update(c, data, len);
}

/*
 * There is no need to use a huge mmap, just pick something
 * reasonable.
 */
#define MAXMMAP	(32*1024*1024)

static char *
digestfile(const char *fname, char *buf, const Algorithm_t *alg,
    off_t *beginp, off_t *endp)
{
	int		 fd;
	struct stat	 st;
	size_t		 size;
	char		*result = NULL;
	void		*map;
	DIGEST_CTX	 context;
	off_t		 end = *endp, begin = *beginp;
	size_t		 pagesize;

	fd = open(fname, O_RDONLY);
	if (fd == -1) {
		warn("can't open %s", fname);
		return NULL;
	}

	if (fstat(fd, &st) == -1) {
		warn("can't fstat %s after opening", fname);
		goto cleanup;
	}

	/* Non-positive end means, it has to be counted from the back:	*/
	if (end <= 0)
		end += st.st_size;
	/* Negative begin means, it has to be counted from the back:	*/
	if (begin < 0)
		begin += st.st_size;

	if (begin < 0 || end < 0 || begin > st.st_size || end > st.st_size) {
		warnx("%s is %jd bytes long, not large enough for the "
		    "specified offsets [%jd-%jd]", fname,
		    (intmax_t)st.st_size,
		    (intmax_t)*beginp, (intmax_t)*endp);
		goto cleanup;
	}
	if (begin > end) {
		warnx("%s is %jd bytes long. Begin-offset %jd (%jd) is "
		    "larger than end-offset %jd (%jd)",
		    fname, (intmax_t)st.st_size,
		    (intmax_t)begin, (intmax_t)*beginp,
		    (intmax_t)end, (intmax_t)*endp);
		goto cleanup;
	}

	if (*endp <= 0)
		*endp = end;
	if (*beginp < 0)
		*beginp = begin;

	pagesize = getpagesize();

	alg->Init(&context);

	do {
		if (end - begin > MAXMMAP)
			size = MAXMMAP;
		else
			size = end - begin;

		map = mmap(NULL, size, PROT_READ, MAP_NOCORE, fd, begin);
		if (map == MAP_FAILED) {
			warn("mmaping of %s between %jd and %jd ",
			    fname, (intmax_t)begin, (intmax_t)begin + size);
			goto cleanup;
		}
		/*
		 * Try to give kernel a hint. Not that it
		 * cares at the time of this writing :-(
		 */
		if (size > pagesize)
			madvise(map, size, MADV_SEQUENTIAL);
		alg->Update(&context, map, size);
		munmap(map, size);
		begin += size;
	} while (begin < end);

	result = alg->End(&context, buf);

cleanup:
	close(fd);
	return result;
}

static off_t
parseint(const char *arg)
{
	double	 result; /* Use double to allow things like 0.5Kb */
	char	*endp;

	result = strtod(arg, &endp);
	switch (endp[0]) {
	case 'T':
	case 't':
		result *= 1024;	/* FALLTHROUGH */
	case 'M':
	case 'm':
		result *= 1024;	/* FALLTHROUGH */
	case 'K':
	case 'k':
		endp++;
		if (endp[1] == 'b' || endp[1] == 'B')
			endp++;
		result *= 1024;	/* FALLTHROUGH */
	case '\0':
		break;
	default:
		warnx("%c (%d): unrecognized suffix", endp[0], (int)endp[0]);
		goto badnumber;
	}

	if (endp[0] == '\0')
		return result;

badnumber:
	errx(EX_USAGE, "`%s' is not a valid offset.", arg);
}

/* Main driver.

Arguments (may be any combination):
  -sstring - digests string
  -t       - runs time trial
  -x       - runs test script
  filename - digests file
  (none)   - digests standard input
 */
int
main(int argc, char *argv[])
{
	int     ch;
	char   *p;
	char	buf[HEX_DIGEST_LENGTH];
	int     failed, useoffsets = 0;
	off_t   begin = 0, end = 0; /* To shut compiler warning */
 	unsigned	digest;
 	const char*	progname;
 
 	if ((progname = strrchr(argv[0], '/')) == NULL)
 		progname = argv[0];
 	else
 		progname++;
 
 	for (digest = 0; digest < sizeof(Algorithm)/sizeof(*Algorithm); digest++)
 		if (strcasecmp(Algorithm[digest].progname, progname) == 0)
 			break;
 
 	if (digest == sizeof(Algorithm)/sizeof(*Algorithm))
 		digest = 0;

	failed = 0;
	while ((ch = getopt(argc, argv, "hb:e:pqrs:tx")) != -1) {
		switch (ch) {
		case 'b':
			begin = parseint(optarg);
			useoffsets = 1;
			break;
		case 'e':
			end = parseint(optarg);
			useoffsets = 1;
			break;
		case 'p':
			MDFilter(&Algorithm[digest], 1);
			break;
		case 'q':
			qflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 's':
			sflag = 1;
			MDString(&Algorithm[digest], optarg);
			break;
		case 't':
			MDTimeTrial(&Algorithm[digest]);
			break;
		case 'x':
			MDTestSuite(&Algorithm[digest]);
			break;
		case 'h':
			usage(EX_OK);
		default:
			usage(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (*argv) {
		do {
			if (useoffsets)
				p = digestfile(*argv, buf, Algorithm + digest,
				    &begin, &end);
			else
				p = Algorithm[digest].File(*argv, buf);
			if (!p) {
				/* digestfile() outputs its own diagnostics */
				if (!useoffsets)
					warn("%s", *argv);
				failed++;
			} else {
				if (qflag) {
					printf("%s\n", p);
				} else if (rflag) {
					if (useoffsets)
						printf("%s %s[%jd-%jd]\n",
						       p, *argv,
						       (intmax_t)begin,
						       (intmax_t)end);
					else
						printf("%s %s\n",
							p, *argv);
				} else if (useoffsets) {
					printf("%s (%s[%jd-%jd]) = %s\n",
					       Algorithm[digest].name, *argv,
					       (intmax_t)begin,
					       (intmax_t)end,
					       p);
				} else {
					printf("%s (%s) = %s\n",
					       Algorithm[digest].name,
					       *argv, p);
				}
			}
		} while (*++argv);
	} else if (!sflag && (optind == 1 || qflag || rflag))
		MDFilter(&Algorithm[digest], 0);

	if (failed != 0)
		return (EX_NOINPUT);
 
	return (0);
}
/*
 * Digests a string and prints the result.
 */
static void
MDString(Algorithm_t *alg, const char *string)
{
	size_t len = strlen(string);
	char buf[HEX_DIGEST_LENGTH];

	if (qflag)
		printf("%s\n", alg->Data(string, len, buf));
	else if (rflag)
		printf("%s \"%s\"\n", alg->Data(string, len, buf), string);
	else
		printf("%s (\"%s\") = %s\n", alg->name, string, alg->Data(string, len, buf));
}
/*
 * Measures the time to digest TEST_BLOCK_COUNT TEST_BLOCK_LEN-byte blocks.
 */
static void
MDTimeTrial(Algorithm_t *alg)
{
	DIGEST_CTX context;
	struct rusage before, after;
	struct timeval total;
	float seconds;
	unsigned char block[TEST_BLOCK_LEN];
	unsigned int i;
	char   *p, buf[HEX_DIGEST_LENGTH];

	printf
	    ("%s time trial. Digesting %d %d-byte blocks ...",
	    alg->name, TEST_BLOCK_COUNT, TEST_BLOCK_LEN);
	fflush(stdout);

	/* Initialize block */
	for (i = 0; i < TEST_BLOCK_LEN; i++)
		block[i] = (unsigned char) (i & 0xff);

	/* Start timer */
	getrusage(0, &before);

	/* Digest blocks */
	alg->Init(&context);
	for (i = 0; i < TEST_BLOCK_COUNT; i++)
		alg->Update(&context, block, TEST_BLOCK_LEN);
	p = alg->End(&context, buf);

	/* Stop timer */
	getrusage(0, &after);
	timersub(&after.ru_utime, &before.ru_utime, &total);
	seconds = total.tv_sec + (float) total.tv_usec / 1000000;

	printf(" done\n");
	printf("Digest = %s", p);
	printf("\nTime = %f seconds\n", seconds);
	printf
	    ("Speed = %f bytes/second\n",
	    (float) TEST_BLOCK_LEN * (float) TEST_BLOCK_COUNT / seconds);
}
/*
 * Digests a reference suite of strings and prints the results.
 */

const char *MDTestInput[MDTESTCOUNT] = {
	"", 
	"a",
	"abc",
	"message digest",
	"abcdefghijklmnopqrstuvwxyz",
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
	"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
	"MD5 has not yet (2001-09-03) been broken, but sufficient attacks have been made \
that its security is in some doubt"
};

const char *MD5TestOutput[MDTESTCOUNT] = {
	"d41d8cd98f00b204e9800998ecf8427e",
	"0cc175b9c0f1b6a831c399e269772661",
	"900150983cd24fb0d6963f7d28e17f72",
	"f96b697d7cb7938d525a2f31aaf161d0",
	"c3fcd3d76192e4007dfb496cca67e13b",
	"d174ab98d277d9f5a5611c2c9f419d9f",
	"57edf4a22be3c955ac49da2e2107b67a",
	"b50663f41d44d92171cb9976bc118538"
};

const char *SHA1_TestOutput[MDTESTCOUNT] = {
	"da39a3ee5e6b4b0d3255bfef95601890afd80709",
	"86f7e437faa5a7fce15d1ddcb9eaeaea377667b8",
	"a9993e364706816aba3e25717850c26c9cd0d89d",
	"c12252ceda8be8994d5fa0290a47231c1d16aae3",
	"32d10c7b8cf96570ca04ce37f2a19d84240d3a89",
	"761c457bf73b14d27e9e9265c46f4b4dda11f940",
	"50abf5706a150990a08b2c5ea40fa0e585554732",
	"18eca4333979c4181199b7b4fab8786d16cf2846"
};

const char *SHA256_TestOutput[MDTESTCOUNT] = {
	"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb",
	"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	"f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650",
	"71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73",
	"db4bfcbd4da0cd85a60c3c37d3fbd8805c77f15fc6b1fdfe614ee0a7c8fdb4c0",
	"f371bc4a311f2b009eef952dd83ca80e2b60026c8e935592d0f9c308453c813e",
	"e6eae09f10ad4122a0e2a4075761d185a272ebd9f5aa489e998ff2f09cbfdd9f"
};

const char *RIPEMD160_TestOutput[MDTESTCOUNT] = {
	"9c1185a5c5e9fc54612808977ee8f548b2258d31",
	"0bdc9d2d256b3ee9daae347be6f4dc835a467ffe",
	"8eb208f7e05d987a9b044a8e98c6b087f15a0bfc",
	"5d0689ef49d2fae572b881b123a85ffa21595f36",
	"f71c27109c692c1b56bbdceb5b9d2865b3708dbc",
	"b0e20b6e3116640286ed3a87a5713079b21f5189",
	"9b752e45573d4b39f4dbd3323cab82bf63326bfb",
	"5feb69c6bf7c29d95715ad55f57d8ac5b2b7dd32"
};

static void
MDTestSuite(Algorithm_t *alg)
{
	int i;
	char buffer[HEX_DIGEST_LENGTH];

	printf("%s test suite:\n", alg->name);
	for (i = 0; i < MDTESTCOUNT; i++) {
		(*alg->Data)(MDTestInput[i], strlen(MDTestInput[i]), buffer);
		printf("%s (\"%s\") = %s", alg->name, MDTestInput[i], buffer);
		if (strcmp(buffer, (*alg->TestOutput)[i]) == 0)
			printf(" - verified correct\n");
		else
			printf(" - INCORRECT RESULT!\n");
	}
}

/*
 * Digests the standard input and prints the result.
 */
static void
MDFilter(Algorithm_t *alg, int tee)
{
	DIGEST_CTX context;
	unsigned int len;
	unsigned char buffer[BUFSIZ];
	char buf[HEX_DIGEST_LENGTH];

	alg->Init(&context);
	while ((len = fread(buffer, 1, BUFSIZ, stdin))) {
		if (tee && len != fwrite(buffer, 1, len, stdout))
			err(1, "stdout");
		alg->Update(&context, buffer, len);
	}
	printf("%s\n", alg->End(&context, buf));
}

static void
usage(int excode)
{
	fprintf(stderr, "usage:\n\t%s [-pqrtx] [-b offset] [-e offset] "
	    "[-s string] [files ...]\n", getprogname());
	exit(excode);
}
