/*
 * BLIB.C
 *
 * Simple benchmarking library
 *
 * $DragonFly: src/test/sysperf/blib.c,v 1.4 2004/03/20 02:02:20 dillon Exp $
 */

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static struct timeval tv1;
static struct timeval tv2;

void
start_timing(void)
{
    gettimeofday(&tv1, NULL);
}

int
stop_timing(long long count, const char *ctl, ...)
{
    long long us;
    va_list va;

    gettimeofday(&tv2, NULL);
    us = (tv2.tv_usec - tv1.tv_usec) + (tv2.tv_sec - tv1.tv_sec) * 1000000LL;
    if (ctl == NULL) 	/* dummy call to pre-cache */
	return(us > 1000000);

    va_start(va, ctl);
    vprintf(ctl, va);
    va_end(va);

    printf(" %6.3fs %lld loops = %6.3fuS/loop\n",
	(double)us / 1000000.0,
	count,
	(double)us / (double)count
    );
    return(0);
}

int
stop_timing2(long long count, long long us, const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vprintf(ctl, va);
    va_end(va);

    printf(" %6.3fs %lld loops = %6.3fnS/loop\n",
	(double)us / 1000000.0,
	count,
	(double)us * 1000.0 / (double)count
    );
    return(0);
}

long long
get_timing(void)
{
    long long us;

    gettimeofday(&tv2, NULL);
    us = (tv2.tv_usec - tv1.tv_usec) + (tv2.tv_sec - tv1.tv_sec) * 1000000LL;
    return(us);
}

