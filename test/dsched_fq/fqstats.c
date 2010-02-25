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
		"FQMP:\t%d\n"
		"FQP:\t%d\n"
		"DPRIV:\t%d\n"
		"---------------------------------------------\n"
		"Proccesses\n"
		"Rate limited:\t%d\n"
		"---------------------------------------------\n"
		"Transactions\n"
		"Issued:\t\t%d\n"
		"Completed:\t%d\n"
		"without FQMP:\t%d\n",

		fq_stats.fqmp_allocations,
		fq_stats.fqp_allocations,
		fq_stats.dpriv_allocations,

		fq_stats.procs_limited,

		fq_stats.transactions,
		fq_stats.transactions_completed,
		fq_stats.no_fqmp
		);


	return 0;
}
