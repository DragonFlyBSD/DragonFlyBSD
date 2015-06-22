/*
 * syscall2.c
 *
 * All-threads getuid timing test.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <time.h>
#include <stdio.h>

#include <machine/atomic.h>

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
	int cpuno;
	int ncpu;
	int *done;
	size_t ncpu_size;

	done = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
		     MAP_SHARED|MAP_ANON, -1, 0);

	/*
	 * How many cpu threads are there?
	 */
	ncpu = 0;
	ncpu_size = sizeof(ncpu);
	if (sysctlbyname("hw.ncpu", &ncpu, &ncpu_size, NULL, 0) < 0) {
		perror("sysctl hw.ncpu");
		exit(1);
	}
	printf("timing standard getuid() syscall, %d threads\n", ncpu);
	printf("if using powerd, run several times\n");
	*done = 0;

	/*
	 * Approximate timing run length
	 */
	start_timing();
	while (stop_timing(0, NULL) == 0) {
		for (j = 0; j < 100; ++j)
			getuid();
		count += 100;
	}
	max = count;

	/*
	 * Run same length on all threads.
	 */
	for (cpuno = 0; cpuno < ncpu; ++cpuno) {
		if (fork() == 0) {
			/*
			 * Give scheduler time to move threads around
			 */
			start_timing();
			while (stop_timing(0, NULL) == 0) {
				for (j = 0; j < 100; ++j)
					getuid();
			}

			/*
			 * Actual timing test is here.
			 */
			start_timing();
			for (count = 0; count < max; count += 100) {
				for (j = 0; j < 100; ++j)
					getuid();
			}
			stop_timing(count, "getuid() sysmsg");

			/*
			 * Don't unbusy the cpu until the other threads are
			 * done.
			 */
			atomic_add_int(done, 1);
			while (*done < ncpu)	/* wait for other threads */
				getuid();
			exit(0);
		}
	}
	while (wait3(NULL, 0, NULL) > 0 || errno == EINTR)
		;
	return 0;
}
