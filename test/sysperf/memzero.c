/*
 * memzero.c
 *
 * $DragonFly: src/test/sysperf/memzero.c,v 1.1 2004/04/29 16:14:53 dillon Exp $
 */

#include "blib.h"

int glob[16384];

void test_using(const char *ctl, char *buf, int bytes, void (*zerof)(void *d, size_t bytes));

extern void dozero1(void *d, size_t bytes);
extern void dozero2(void *d, size_t bytes);
extern void dozero3(void *d, size_t bytes);
extern void dozero4(void *d, size_t bytes);
extern void dozero5(void *d, size_t bytes);
extern void dozero6(void *d, size_t bytes);
extern void dozero7(void *d, size_t bytes);
extern void fpcleanup(void);

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

    test_using("bzero", buf, bytes, (void *)bzero);
#if 0
    test_using("dozero1", buf, bytes, dozero1);
    test_using("dozero2", buf, bytes, dozero2);
    test_using("dozero3", buf, bytes, dozero3);
    test_using("dozero4", buf, bytes, dozero4);
    test_using("dozero5", buf, bytes, dozero5);
    test_using("dozero6", buf, bytes, dozero6);
    test_using("dozero7", buf, bytes, dozero7);
#endif
    return(0);
}

void
test_using(const char *ctl, char *buf, int bytes, void (*zerof)(void *d, size_t bytes))
{
    int i;
    int loops;
    long long us;

    start_timing();
    for (i = 0; (i & 31) || stop_timing(0, NULL) == 0; ++i) {
	zerof(buf, bytes);
    }

    loops = i * 2;
    start_timing();
    for (i = loops - 1; i >= 0; --i) {
	zerof(buf, bytes);
    }
#if 0
    fpcleanup();
#endif
    stop_timing(loops, ctl);
    us = get_timing();
    printf("%s %d %5.2f MBytes/sec\n", ctl, bytes, 
	(double)loops * (double)bytes / (double)us);
}
