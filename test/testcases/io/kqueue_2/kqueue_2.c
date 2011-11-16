#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

int
main(int argc, char *argv[])
{
    struct sockaddr_in sa_local, sa_remote;
    socklen_t sin_size = sizeof(struct sockaddr_in);
    struct timespec timeout;
    struct kevent eventlist[1], changelist[1];
    int kq, fd_l, fd_c, fd_n, i;

    if ((fd_l = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        err(EX_OSERR, "socket(2) failure");

    if ((fd_c = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        err(EX_OSERR, "socket(2) failure");

    sa_local.sin_family = AF_INET;
    sa_local.sin_port = 0;
    sa_local.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(&(sa_local.sin_zero), 0, sizeof(sa_local.sin_zero));

    if (bind(fd_l, (struct sockaddr *)&sa_local, sizeof(struct sockaddr)) == -1)
        err(EX_OSERR, "bind(2) failure");

    if (getsockname(fd_l, (struct sockaddr *)&sa_local, &sin_size) == -1)
        err(EX_OSERR, "getsockname(2) failure");

    if (listen(fd_l, 1) == -1)
        err(EX_OSERR, "listen(2) failure");

    if (connect(fd_c, (struct sockaddr *)&sa_local, sizeof(struct sockaddr)) == -1)
        err(EX_OSERR, "connect(2) failure");

    fd_n = accept(fd_l, (struct sockaddr *)&sa_remote, &sin_size);

    if ((kq = kqueue()) == -1)
        err(EX_OSERR, "kqueue(2) failure");

    if (send(fd_c, "x", 1, MSG_OOB) == -1)
        err(EX_OSERR, "send(2) failure");

    EV_SET(&changelist[0], fd_n, EVFILT_EXCEPT, EV_ADD|EV_ENABLE, NOTE_OOB, 0, NULL);

    memset(&timeout, 0, sizeof(timeout));
    if ((i = kevent(kq, &changelist[0], 1, &eventlist[0], 1, &timeout)) == -1)
        err(EX_OSERR, "kevent(2) failure");

    if (i == 1 && eventlist[0].fflags & NOTE_OOB) {
        printf("ok\n");
    }

    return (0);
}
