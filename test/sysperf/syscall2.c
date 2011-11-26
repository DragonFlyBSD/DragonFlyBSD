/*
 * syscall2.c
 *
 * $DragonFly: src/test/sysperf/syscall2.c,v 1.2 2004/10/31 20:19:24 eirikn Exp $
 */

#include <time.h>

#include "blib.h"

extern int getuid_test(void);

int
main(void)
{
	struct timespec ts, ts2;
	int error;
	long long count = 0;
	long long max;
	int j;

	start_timing();
	while (stop_timing(0, NULL) == 0) {
		for (j = 0; j < 100; ++j)
			getuid_msg();
		count += 100;
	}
	max = count;
	start_timing();
	for (count = 0; count < max; count += 100) {
		for (j = 0; j < 100; ++j)
			getuid_msg();
	}
	stop_timing(count, "getuid() sysmsg");
}
