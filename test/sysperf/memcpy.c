/*
 * memcpy.c
 *
 * $DragonFly: src/test/sysperf/memcpy.c,v 1.1 2004/04/29 16:14:53 dillon Exp $
 */

#include "blib.h"

int glob[16384];

void test_using(const char *ctl, char *buf, int bytes, void (*copyf)(const void *s1, void *d, size_t bytes));

#if 0
extern void docopy1(const void *s, void *d, size_t bytes);
extern void docopy2(const void *s, void *d, size_t bytes);
extern void docopy3(const void *s, void *d, size_t bytes);
extern void docopy4(const void *s, void *d, size_t bytes);
extern void docopy5(const void *s, void *d, size_t bytes);
extern void docopy6(const void *s, void *d, size_t bytes);
extern void docopy7(const void *s, void *d, size_t bytes);
extern void fpcleanup(void);
#endif

int
main(int ac, char **av)
{
    int bytes;
    char *ptr;
    char *buf;

    if (ac == 1) {
	fprintf(stderr, "%s bytes\n", av[0]);
	exit(1);
    }

    bytes = strtol(av[1], &ptr, 0);
    switch(*ptr) {
    case 'k':
    case 'K':
	bytes *= 1024;
	break;
    case 'm':
    case 'M':
	bytes *= 1024 * 1024;
	break;
    case 'g':
    case 'G':
	bytes *= 1024 * 1024 * 1024;
	break;
    case 0:
	break;
    default:
	fprintf(stderr, "suffix '%s' not understood\n");
	exit(1);
    }
    if (bytes <= 0 && (bytes & 127)) {
	fprintf(stderr, "# of bytes must be a multiple of 128\n");
	exit(1);
    }
    buf = mmap(NULL, bytes * 2, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
    if (buf == MAP_FAILED) {
	perror("mmap/buffer");
	exit(1);
    }
    bzero(buf, bytes * 2);

    test_using("bcopy", buf, bytes, bcopy);
#if 0
    test_using("docopy1", buf, bytes, docopy1);
    test_using("docopy2", buf, bytes, docopy2);
    test_using("docopy3", buf, bytes, docopy3);
    test_using("docopy4", buf, bytes, docopy4);
    test_using("docopy5", buf, bytes, docopy5);
    test_using("docopy6", buf, bytes, docopy6);
    test_using("docopy7", buf, bytes, docopy7);
#endif
    return(0);
}

void
test_using(const char *ctl, char *buf, int bytes, void (*copyf)(const void *s1, void *d, size_t bytes))
{
    int i;
    int loops;
    long long us;

    start_timing();
    for (i = 0; (i & 31) || stop_timing(0, NULL) == 0; ++i) {
	copyf(buf, buf + bytes, bytes);
    }

    loops = i * 2;
    start_timing();
    for (i = loops - 1; i >= 0; --i) {
	copyf(buf, buf + bytes, bytes);
    }
#if 0
    fpcleanup();
#endif
    stop_timing(loops, ctl);
    us = get_timing();
    printf("%s %d %5.2f MBytes/sec\n", ctl, bytes, 
	(double)loops * (double)bytes / (double)us);
}

