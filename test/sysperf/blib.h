/*
 * BLIB.C
 *
 * Simple benchmarking library
 *
 * $DragonFly: src/test/sysperf/blib.h,v 1.2 2003/11/19 00:59:19 dillon Exp $
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
long long get_timing(void);

