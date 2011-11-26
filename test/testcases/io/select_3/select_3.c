#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MANY 80

int
main(int argc, char *argv[])
{
    fd_set write_fds;
    int fd[MANY+3], i, maxfd;

    FD_ZERO(&write_fds);
    for (i = 0; i < MANY; ++i) {
        if ((fd[i] = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
            err(EX_OSERR, "socket(2) failure");

        FD_SET(fd[i], &write_fds);
        maxfd = fd[i];
    }

    i = select(maxfd+1, NULL, &write_fds, NULL, NULL);

    if (i == MANY)
        printf("ok\n");

    return (0);
}
