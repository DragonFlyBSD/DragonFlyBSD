/*
 * cc -Wall revoke.c -o /tmp/revoke
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

int
main(int ac, char **av)
{
    int i;

    for (i = 1; i < ac; ++i) {
	if (revoke(av[i]) < 0) {
	    fprintf(stderr, "revoke %s failed: %s\n",
		    av[i], strerror(errno));
	}
    }
    return(0);
}
