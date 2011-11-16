#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>

main(int argc, char *argv[])
{
    int result, fd;
    fd_set rset;
    char tmpstr[] = "seltest.XXXX";

    if ((fd = mkstemp(tmpstr)) < 0) {
        printf ("Error %d opening temp file\n", errno);
        exit (-1);
    }

    lseek(fd, 0, SEEK_END);

    FD_ZERO(&rset);
    FD_SET(fd, &rset);

    result = select(fd + 1, &rset, 0, 0, 0);
    if (result == 1)
        printf("OK\n");
    else
        printf("FAIL\n");

    unlink(tmpstr);

    return !result;
}
