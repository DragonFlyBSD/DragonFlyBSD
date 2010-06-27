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

#include "../../sys/kern/dsched/fq/fq.h"


int main(void)
{
	struct dsched_fq_stats	fq_stats;
	size_t n = sizeof(struct dsched_fq_stats);

	if (sysctlbyname("dsched.fq.stats", &fq_stats, &n, NULL, 0) != 0)
		err(1, "sysctlbyname");

	printf( "Proccesses\n"
		"Rate limited:\t%d\n"
		"---------------------------------------------\n"
		"Transactions\n"
		"Issued:\t\t%d\n"
		"Completed:\t%d\n"
		"Cancelled:\t%d\n",

		fq_stats.procs_limited,

		fq_stats.transactions,
		fq_stats.transactions_completed,
		fq_stats.cancelled
		);


	return 0;
}
