
/*
 * UTF8BIN.C
 *
 * cc utf8bin.c -o ~/bin/utf8bin
 * rehash
 * setenv LANG en_US.UTF-8
 * dd if=/dev/urandom bs=32k count=1024 | utf8bin
 *
 * Test round-trip UTF8-B binary escaping functions.
 */
#include <sys/types.h>
#include <sys/file.h>
#include <locale.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <wchar.h>

int
main(int ac, char **av)
{
	char ibuf[1024];
	char obuf[1024];
	wchar_t warray[1024];
	ssize_t r1;
	ssize_t r2;
	ssize_t x;
	ssize_t w1;
	ssize_t w2;
	ssize_t i;
	ssize_t o;
	int failed;
	int flags;

	/*
	 * NOTE: If we use WCSBIN_SURRO the round-trip will not be 8-bit
	 *	 clean.
	 *
	 *	 Typically use either 0 or WCSBIN_LONGCODE, both of which
	 *	 are 8-bit clean, will escape, and cannot error on input
	 *	 (or generate wchars that might error on output) given the
	 *	 same flags).
	 */
	flags = 0;
	flags = WCSBIN_LONGCODES;

	setlocale(LC_ALL, "");
	x = 0;
	while ((flags & WCSBIN_EOF) == 0 &&
	       (r1 = read(0, ibuf + x, sizeof(ibuf) - x)) >= 0) {
		/* allow final loop for loose ends */
		if (r1 == 0)
			flags |= WCSBIN_EOF;
		r1 += x;
		r2 = r1;
		w1 = mbintowcr(warray, ibuf, 1024, &r2, flags);

		/* round-trip, output buffer can be same size as input buffer */
		w2 = w1;
		o = wcrtombin(obuf, warray, sizeof(obuf), &w2, flags);
		fflush(stdout);

		printf("read %4d/%-4d wc=%4d/%-4d write=%-4d\t",
		       r2, r1, w2, w1, o);
		if (r2 == o) {
			if (bcmp(ibuf, obuf, o) == 0) {
				printf("ok%c",
					(flags & WCSBIN_EOF) ? '\n' : '\r');
				fflush(stdout);
			} else {
				printf("compare-fail\n");
			}
		} else if (r2 < o) {
			if (bcmp(ibuf, obuf, r2) == 0)
				printf("len-fail, rest-ok\n");
			else
				printf("len-fail, compare-fail\n");
		} else if (o < r2) {
			if (bcmp(ibuf, obuf, o) == 0)
				printf("len-fail, rest-ok\n");
			else
				printf("len-fail, compare-fail\n");
		}
		for (i = failed = 0; i < r2 && i < o; ++i) {
			if (ibuf[i] != obuf[i]) {
				printf("    @%04x %02x %02x\n",
				       i, (uint8_t)ibuf[i], (uint8_t)obuf[i]);
				failed = 16;
			} else if (failed) {
				--failed;
				printf("    @%04x %02x %02x\n",
				       i, (uint8_t)ibuf[i], (uint8_t)obuf[i]);
			}
		}

		x = r1 - r2;
		if (x)
			bcopy(ibuf + r2, ibuf, x);
	}
}
