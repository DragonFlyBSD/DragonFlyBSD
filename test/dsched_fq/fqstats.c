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

#include "../../sys/dsched/fq/dsched_fq.h"


int main(void)
{
	struct dsched_fq_stats	fq_stats;
	size_t n = sizeof(struct dsched_fq_stats);

	if (sysctlbyname("kern.fq_stats", &fq_stats, &n, NULL, 0) != 0)
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
		"Proccesses\n"
		"Rate limited:\t%d\n"
		"---------------------------------------------\n"
		"Transactions\n"
		"Issued:\t\t%d\n"
		"Completed:\t%d\n"
		"without thread_ctx:\t%d\n",

		fq_stats.tdctx_allocations,
		fq_stats.tdio_allocations,
		fq_stats.diskctx_allocations,

		fq_stats.nprocs,
		fq_stats.nthreads,

		fq_stats.procs_limited,

		fq_stats.transactions,
		fq_stats.transactions_completed,
		fq_stats.no_tdctx
		);


	return 0;
}
