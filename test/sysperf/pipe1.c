/*
 * pipe1.c
 *
 * $DragonFly: src/test/sysperf/pipe1.c,v 1.1 2003/08/12 02:29:44 dillon Exp $
 */

#include "blib.h"

int
main(int ac, char **av)
{
    long long count = 0;
    long long max;
    char c;
    int j;
    int fds[2];

    printf("tests full duplex pipe 1write,2read,2write,1read loop\n");
    if (pipe(fds)) {
	perror("pipe");
	exit(1);
    }
    if (fork() == 0) {
	/*
	 * child process
	 */
	close(fds[0]);
	while (read(fds[1], &c, 1) == 1) {
	    write(fds[1], &c, 1);
	}
	_exit(0);
    } else {
	/* 
	 * parent process.
	 */
	close(fds[1]);
	write(fds[0], &c, 1);	/* prime the caches */
	read(fds[0], &c, 1);
	start_timing();
	for (j = 0; j < 100000; ++j) {
	    write(fds[0], &c, 1);
	    if (read(fds[0], &c, 1) != 1) {
		fprintf(stderr, "broken pipe during test\n");
		exit(1);
	    }
	}
	stop_timing(j, "full duplex pipe / 1char:");
	close(fds[0]);
	while(wait(NULL) >= 0);
    }
    return(0);
}

