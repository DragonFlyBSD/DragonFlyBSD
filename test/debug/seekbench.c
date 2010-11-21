/*
 * SEEKBENCH.C
 *
 * cc seekbench.c -o /tmp/sb
 *
 * This intentionally performs a sequence of seek/reads with ever
 * growing distances in a way that should be essentially uncached.
 * It attempts to defeat both OS caches and the HDs zone cache.
 *
 * The average read-latency per seek/read is calculated.
 *
 * This can heat up the hard drive so be careful.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <libutil.h>

#define BLKSIZE		((intmax_t)1024)
#define BLKMASK		(BLKSIZE - 1)

char Buf[BLKSIZE];

int
main(int ac, char **av)
{
	struct timeval tv1;
	struct timeval tv2;
	off_t bytes;
	off_t base;
	off_t skip;
	intmax_t us;
	int fd;
	int count;

	if (ac == 1) {
		fprintf(stderr, "seekbench <blockdevice>\n");
		exit(1);
	}
	fd = open(av[1], O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}
	lseek(fd, 0L, 2);
	bytes = lseek(fd, 0L, 1);
	printf("%s: %jdMB\n", av[1], (intmax_t)bytes / 1024 / 1024);
	printf("distance avg-seek\n");
	skip = BLKSIZE;
	while (skip < bytes) {
		count = 0;
		gettimeofday(&tv1, NULL);
		for (base = skip; base < bytes && count < 100; base += skip) {
			lseek(fd, base, 0);
			read(fd, Buf, BLKSIZE);
			++count;
		}
		gettimeofday(&tv2, NULL);
		us = (tv2.tv_usec + 1000000 - tv1.tv_usec) +
		     (tv2.tv_sec - 1 - tv1.tv_sec) * 1000000;
		if (skip < 100*1024*1024)
			printf("%5jdKB %6.3fms\n",
				(intmax_t)skip / 1024,
				(double)us / 1000.0 / count);
		else
			printf("%5jdMB %6.3fms\n",
				(intmax_t)skip / 1024 / 1024,
				(double)us / 1000.0 / count);
		skip += BLKSIZE;
		if (skip < BLKSIZE * 25)
			skip += BLKSIZE;
		else
			skip += (skip / 25 + BLKMASK) & ~BLKMASK;
	}
}
