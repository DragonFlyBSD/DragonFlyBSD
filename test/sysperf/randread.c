
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <machine/atomic.h>
#include "blib.h"

int
main(int ac, char **av)
{
    char *buf;
    size_t bytes = 512;
    off_t limit;
    int fd;
    int i;
    int nprocs = 32;
    double range = 90.0;
    volatile int *counterp;
    int clast;
    int cnext;
    int cdelta;

    if (ac < 2 || ac > 5) {
	fprintf(stderr, "%s <device> [bufsize:512 [range%:90 [nprocs:32]]]\n",
		av[0]);
	exit (1);
    }

    if (ac >= 3) {
	bytes = (size_t)strtoul(av[2], NULL, 0);
	if (bytes < 512 || (bytes ^ (bytes - 1)) != ((bytes << 1) - 1)) {
	    fprintf(stderr, "bytes must be a power of 2 >= 512\n");
	    exit (1);
	}
    }
    buf = malloc(bytes);

    if (ac >= 4) {
	range = strtod(av[3], NULL);
    }

    if (ac >= 5) {
	nprocs = strtol(av[4], NULL, 0);
	if (nprocs < 0 || nprocs > 512) {
	    fprintf(stderr, "absurd nprocs (%d)\n", nprocs);
	    exit(1);
	}
    }

    fd = open(av[1], O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, "open %s: %s\n", av[1], strerror(errno));
	exit (1);
    }

    lseek(fd, 0L, 2);
    limit = lseek(fd, 0L, 1);
    limit = (off_t)((double)limit * range / 100.0);
    limit &= ~(off_t)(bytes - 1);
    printf("device %s bufsize %zd limit %4.3fGB nprocs %d\n",
	av[1], bytes, (double)limit / (1024.0*1024.0*1024.0), nprocs);

    counterp = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE,
		    MAP_SHARED|MAP_ANON, -1, 0);

    for (i = 0; i < nprocs; ++i) {
	if (fork() == 0) {
	    srandomdev();
	    for (;;) {
		long pos = (random() % limit) & ~(off_t)(bytes - 1);
		lseek(fd, pos, 0);
		read(fd, buf, bytes);
		atomic_add_int(counterp, 1);
	    }
	}
    }
    start_timing();
    sleep(1);
    start_timing();
    clast = *counterp;

    for (;;) {
	sleep(1);
	cnext = *counterp;
	cdelta = cnext - clast;
	clast = cnext;
	stop_timing(cdelta, "randread");
    }
    return 0;
}
