/*
 * MBWTEST.C
 *
 * (c)Copyright 2003 Matthew Dillon.  This code is hereby placed in the public
 * domain.
 *
 *      Attempt to figure out the L1 and L2 cache sizes and measure memory
 *      bandwidth for the L1 and L2 cache and for non-cache memory.
 *
 * $DragonFly: src/test/sysperf/mbwtest.c,v 1.1 2003/11/13 07:10:36 dillon Exp $
 */

#include <sys/file.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAXBYTES	(16*1024*1024)

static int bandwidth_test(char *buf, int loops, int bytes, char *msg);
static void start_timing(void);
static int stop_timing(char *str, long long bytes);

int
main(int ac, char **av)
{
    char *buf;
    int loops;
    int us1;
    int us2;
    long long count1;
    long long count2;
    long long count3;
    int bytes1;
    int bytes2;
    int bytes3;

    buf = malloc(MAXBYTES * 2);
    bzero(buf, MAXBYTES * 2);

    /*
     * Get a baseline for 1/4 second L1 cache timing maximizing the number
     * of loops.  The minimum L1 cache size is 4K.
     */
    start_timing();
    us1 = bandwidth_test(buf, 1000, 4096, NULL);	/* uS per 1000 loops */
    loops = 1000000LL * 1000 / 4 / us1;			/* loops for 1/4 sec */
    count1 = loops * 4096LL;
    start_timing();
    us1 = bandwidth_test(buf, loops, 4096, NULL);	/* best case timing */
    printf("."); fflush(stdout); usleep(1000000 / 4);

    /*
     * Search for the L1 cache size.  Look for a 20% difference in bandwidth
     */
    bzero(buf, 4096);
    start_timing();
    us1 = bandwidth_test(buf, count1 / 4096 + 20, 4096, NULL);
    for (bytes1 = 8192; bytes1 < MAXBYTES; bytes1 <<= 1) {
	start_timing();
	us2 = bandwidth_test(buf, count1 / bytes1 + 20, bytes1, NULL);
	if (us2 > us1 + us1 / 5)
		break;
    }
    bytes1 >>= 1;	/* actual L1 cache size */
    count2 = count1 * us1 / us2;
    printf("."); fflush(stdout); usleep(1000000 / 4);

    bytes2 = bytes1 << 1;
    bzero(buf, bytes2);
    start_timing();
    us1 = bandwidth_test(buf, count2 / bytes2 + 20, bytes2, NULL);
    for (bytes2 <<= 1; bytes2 < MAXBYTES; bytes2 <<= 1) {
	start_timing();
	us2 = bandwidth_test(buf, count2 / bytes2 + 20, bytes2, NULL);
	if (us2 > us1 + us1 / 5)
		break;
    }
    count3 = count2 * us1 / us2;
    bytes2 >>= 1;	/* actual L2 cache size */

    /*
     * Final run to generate output
     */
    printf("\nL1 cache size: %d\n", bytes1);
    if (bytes2 == MAXBYTES)
	printf("L2 cache size: No L2 cache found\n");
    else
	printf("L2 cache size: %d\n", bytes2);
    sleep(1);
    start_timing();
    bandwidth_test(buf, count1 / bytes1 + 20, bytes1, "L1 cache bandwidth");
    if (bytes2 != MAXBYTES) {
	start_timing();
	bandwidth_test(buf, count2 / bytes2 + 20, bytes2,
	    "L2 cache bandwidth");
    }

    /*
     * Set bytes2 to exceed the L2 cache size
     */
    bytes2 <<= 1;
    if (bytes2 < MAXBYTES)
	bytes2 <<= 1;
    start_timing();
    bandwidth_test(buf, count3 / bytes2 + 20, bytes2, "non-cache bandwidth");
    return(0);
}

struct timeval tv1;
struct timeval tv2;

static
int
bandwidth_test(char *buf, int loops, int bytes, char *msg)
{
    register char *bptr;
    register char *lptr;
    register int v;
    int j;
    int us;

    lptr = buf + bytes;
    for (j = 0; j < loops; ++j) {
	for (bptr = buf; bptr < lptr; bptr += 32) {
	    v = *(volatile int *)(bptr + 0);
	    v = *(volatile int *)(bptr + 4);
	    v = *(volatile int *)(bptr + 8);
	    v = *(volatile int *)(bptr + 12);
	    v = *(volatile int *)(bptr + 16);
	    v = *(volatile int *)(bptr + 20);
	    v = *(volatile int *)(bptr + 24);
	    v = *(volatile int *)(bptr + 28);
	}
    }
    us = stop_timing(msg, (long long)bytes * loops);
    return(us);
}

static 
void
start_timing(void)
{
    gettimeofday(&tv1, NULL);
}

static
int
stop_timing(char *str, long long bytes)
{
    int us;

    gettimeofday(&tv2, NULL);

    us = tv2.tv_usec + 1000000 - tv1.tv_usec + 
	(tv2.tv_sec - tv1.tv_sec - 1) * 1000000;
    if (str) {
	printf("%s: %4.2f Mbytes/sec\n",
	    str,
	    (double)bytes * 1000000.0 / ((double)us * 1024.0 * 1024.0));
    }
    return(us);
}

