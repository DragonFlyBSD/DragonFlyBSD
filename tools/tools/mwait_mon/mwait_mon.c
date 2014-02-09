#include <sys/types.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#define MWAIT_CX_MAX	8

struct cx_stat {
	char		sysctl_name[52];
	char		state_name[4];
};

static void	getstats(uint64_t[]);

static struct cx_stat	cx_stats[MWAIT_CX_MAX];
static int		cx_stats_cnt;

int
main(void)
{
	uint64_t count[MWAIT_CX_MAX], prev_count[MWAIT_CX_MAX];
	int i;

	for (i = 0; i < MWAIT_CX_MAX; ++i) {
		char name[64];
		size_t len;
		int subcnt;

		snprintf(name, sizeof(name), "machdep.mwait.C%d.subcnt", i);

		len = sizeof(subcnt);
		sysctlbyname(name, &subcnt, &len, NULL, 0);
		if (subcnt == 0)
			continue;

		snprintf(cx_stats[cx_stats_cnt].state_name,
		    sizeof(cx_stats[cx_stats_cnt].state_name), "C%d", i);
		snprintf(cx_stats[cx_stats_cnt].sysctl_name,
		    sizeof(cx_stats[cx_stats_cnt].sysctl_name),
		    "machdep.mwait.C%d.entered", i);
		++cx_stats_cnt;
	}

	getstats(prev_count);

	for (;;) {
		getstats(count);

		for (i = 0; i < cx_stats_cnt; ++i) {
			printf("%s %-5ju ", cx_stats[i].state_name,
			    (uintmax_t)(count[i] - prev_count[i]));
			prev_count[i] = count[i];
		}
		printf("\n");
		fflush(stdout);

		sleep(1);
	}
}

static void
getstats(uint64_t count[])
{
	int i;

	for (i = 0; i < cx_stats_cnt; ++i) {
		size_t len;

		len = sizeof(uint64_t);
		sysctlbyname(cx_stats[i].sysctl_name, &count[i], &len, NULL, 0);
	}
}
