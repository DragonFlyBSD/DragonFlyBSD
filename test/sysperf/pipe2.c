/*
 * pipe2.c
 *
 * $DragonFly: src/test/sysperf/pipe2.c,v 1.3 2004/04/01 01:47:44 dillon Exp $
 */

#include "blib.h"
#include <sys/resource.h>

#define LOOPS	((int)(1000000LL * 16384 / bytes / divisor))

int
main(int ac, char **av)
{
    long long count = 0;
    long long max;
    char c;
    int j;
    int bytes;
    int divisor;
    int ppri = 999;
    int fds[2];
    char *buf;
    char *ptr;

    if (ac == 1) {
	fprintf(stderr, "%s blocksize[k,m] [pipe_writer_pri]\n", av[0]);
	exit(1);
    }
    bytes = strtol(av[1], &ptr, 0);
    if (*ptr == 'k' || *ptr == 'K') {
	bytes *= 1024;
    } else if (*ptr == 'm' || *ptr == 'M') {
	bytes *= 1024 * 1024;
    } else if (*ptr) {
	fprintf(stderr, "Illegal numerical suffix: %s\n", ptr);
	exit(1);
    }
    if (bytes <= 0) {
	fprintf(stderr, "I can't handle %d sized buffers\n", bytes);
	exit(1);
    }
    if (ac >= 3)
	ppri = strtol(av[2], NULL, 0);

    /*
     * Tiny block sizes, try to take into account overhead.
     */
    if (bytes < 4096)
	divisor = 4096 / bytes;
    else if (bytes > 1024 * 1024)
	divisor = 2;
    else
	divisor = 1;

    if ((buf = malloc(bytes)) == NULL) {
	perror("malloc");
	exit(1);
    }

    bzero(buf, bytes);

    printf("tests one-way pipe using direct-write buffer\n");
    if (pipe(fds)) {
	perror("pipe");
	exit(1);
    }
    if (fork() == 0) {
	/*
	 * child process
	 */
	close(fds[0]);
	while (read(fds[1], buf, bytes) > 0)
		;
	_exit(0);
    } else {
	/* 
	 * parent process.
	 */
	if (ppri != 999) {
	    if (setpriority(PRIO_PROCESS, getpid(), ppri) < 0) {
		perror("setpriority");
		exit(1);
	    }
	}
	close(fds[1]);
	write(fds[0], buf, bytes);	/* prime the caches */
	start_timing();
	for (j = LOOPS; j; --j) {
	    if (write(fds[0], buf, bytes) != bytes) {
		perror("write");
		exit(1);
	    }
	}
	close(fds[0]);
	while(wait(NULL) >= 0);
	stop_timing(LOOPS, "full duplex pipe / %dK bufs:", bytes / 1024);
	printf("datarate: %5.2f MBytes/sec\n",
		(double)LOOPS * bytes * 1000000.0 / 
		(1024.0 * 1024.0 * get_timing()));
    }
    return(0);
}

