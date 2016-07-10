/*
 * pipe2.c
 *
 * $DragonFly: src/test/sysperf/pipe2.c,v 1.5 2004/04/29 16:05:21 dillon Exp $
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "blib.h"

#define PAGE_SIZE	4096
#define PAGE_MASK	(PAGE_SIZE - 1)

int
main(int ac, char **av)
{
    long long count = 0;
    long long max;
    char c;
    int j;
    int loops;
    int bytes;
    int ppri = 999;
    int fds[2];
    char *buf;
    char *ptr;
    char *msg = "datarate";

    if (ac == 1) {
	fprintf(stderr, "%s blocksize[k,m] [pipe_writer_pri] [msg]\n", av[0]);
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
    if (ac >= 4)
	msg = av[3];

    buf = mmap(NULL, bytes * 2 + PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
    if (buf == MAP_FAILED) {
	perror("mmap/buffer");
	exit(1);
    }

    bzero(buf, bytes * 2 + PAGE_SIZE);

    printf("tests one-way pipe using direct-write buffer\n");
    if (pipe(fds)) {
	perror("pipe");
	exit(1);
    }
    if (fork() == 0) {
	/*
	 * child process
	 */
	int n;
	int i;

	close(fds[0]);
	buf += (bytes + PAGE_MASK) & ~PAGE_MASK;
	i = 0;
	for (;;) {
	    n = read(fds[1], buf + i, bytes - i);
	    if (n <= 0)
		break;
	    if (n + i == bytes)
		i = 0;
	    else
		i += n;
	}
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

	/*
	 * Figure out how many loops it takes for 1 second's worth.
	 */
	start_timing();
	for (j = 0; ; ++j) {
	    if (write(fds[0], buf, bytes) != bytes) {
		perror("write");
		exit(1);
	    }
	    if ((j & 31) == 0 && stop_timing(0, NULL))
		break;
	}
	loops = j * 2 + 1;
	loops *= 2;
	usleep(1000000 / 10);
	start_timing();

	for (j = loops; j; --j) {
	    if (write(fds[0], buf, bytes) != bytes) {
		perror("write");
		exit(1);
	    }
	}
	close(fds[0]);
	while(wait(NULL) >= 0)
	    ;
	stop_timing(loops, "full duplex pipe / %dK bufs:", bytes / 1024);
	printf("%s: blkSize %d %5.2f MBytes/sec\n",
		msg,
		bytes,
		(double)loops * bytes * 1000000.0 / 
		(1024.0 * 1024.0 * get_timing()));
    }
    return(0);
}

