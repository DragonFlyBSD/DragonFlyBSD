#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>

#define TESTSIZE	(128L*1024*1024)

char *TestBuf;
char *FlagBuf;

int
main(int argc, char **argv)
{
	size_t beg;
	size_t len;
	size_t fbeg;
	size_t flen;
	size_t i;
	long counter = 0;

	TestBuf = calloc(1, TESTSIZE);
	FlagBuf = calloc(1, TESTSIZE / 4096);

	for (;;) {
		beg = random() & (TESTSIZE - 1) & ~4095L;
		len = random() & (TESTSIZE - 1) & ~4095L;
		if (beg + len > TESTSIZE)
			len = TESTSIZE - beg;
		fbeg = beg / 4096;
		flen = len / 4096;
		if (len == 0)
			continue;

		if (random() & 1) {
			if (mlock(TestBuf + beg, len) < 0) {
				printf("%ld   mlock: %016lx-%016lx: ERROR %s\n",
					counter, beg, beg + len,
					strerror(errno));
			} else {
				memset(FlagBuf + fbeg, 1, flen);
				printf("%ld   mlock: %016lx-%016lx: OK\n",
					counter, beg, beg + len);
			}
		} else {
			for (i = 0; i < flen; ++i) {
				if (FlagBuf[fbeg+i] == 0)
					break;
			}
			if (i == flen) {
				memset(FlagBuf + fbeg, 0, flen);
				if (munlock(TestBuf + beg, len) < 0) {
					printf("%ld munlock: %016lx-%016lx: "
						"ERROR %s\n",
						counter, beg, beg + len,
						strerror(errno));
				} else {
					printf("%ld munlock: %016lx-%016lx: OK\n",
						counter, beg, beg + len);
				}
			} else {
				if (munlock(TestBuf + beg, len) == 0) {
					printf("%ld munlock: %016lx-%016lx: ERROR "
						"should have failed "
						"at %016lx\n",
						counter, beg, beg + len,
						(fbeg + i) * 4096L);
				} else {
					printf("%ld munlock: %016lx-%016lx: OK "
						"(intentional failure)\n",
						counter, beg, beg + len);
				}
			}
		}
#if 0
		printf("%ld\r", counter);
#endif
		fflush(stdout);
		++counter;
	}
        return (0);
}
