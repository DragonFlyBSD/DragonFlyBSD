#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <db.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sys/sys/dsched.h"


int main(void)
{
	struct dsched_stats	dsched_stats;
	size_t n = sizeof(struct dsched_stats);

	if (sysctlbyname("dsched.stats", &dsched_stats, &n, NULL, 0) != 0)
		err(1, "sysctlbyname");

	printf( "Allocations\n"
		"thread_ctx:\t%d\n"
		"thread_io:\t%d\n"
		"disk_ctx:\t%d\n"
		"---------------------------------------------\n"
		"Procs/Threads tracked\n"
		"procs:\t\t%d\n"
		"threads:\t%d\n"
		"---------------------------------------------\n"
		"Transactions\n"
		"w/o thread_ctx:\t%d\n",

		dsched_stats.tdctx_allocations,
		dsched_stats.tdio_allocations,
		dsched_stats.diskctx_allocations,

		dsched_stats.nprocs,
		dsched_stats.nthreads,

		dsched_stats.no_tdctx
		);


	return 0;
}
