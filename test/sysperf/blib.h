/*
 * BLIB.C
 *
 * Simple benchmarking library
 *
 * $DragonFly: src/test/sysperf/blib.h,v 1.3 2004/02/09 18:08:57 dillon Exp $
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

void start_timing(void);
int stop_timing(long long count, const char *ctl, ...);
int stop_timing2(long long count, long long us, const char *ctl, ...);
long long get_timing(void);

