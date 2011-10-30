
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sysexits.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main()
{
    int sock[2];
    struct kevent kc;
    int kq;
    char foo[1024];

    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sock) < 0)
        err(EX_TEMPFAIL, "socketpair");

    if (write(sock[0], foo, sizeof(foo)) < 0)
        err(EX_TEMPFAIL, "write");

    close(sock[0]);

    EV_SET(&kc, sock[1], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);

    kq = kqueue();
    if (kevent(kq, &kc, 1, NULL, 0, NULL) < 0)
        err(EX_TEMPFAIL, "kqueue");

    if (kevent(kq, NULL, 0, &kc, 1, NULL) < 0)
        err(EX_TEMPFAIL, "kevent1");
    printf("kevent1 done, flags: %04x\n", kc.flags);

    if (kc.flags & EV_EOF) {
        printf("kevent1 reported EOF, exiting\n");
        exit(0);
    }

    if (kevent(kq, NULL, 0, &kc, 1, NULL) < 0)
        err(EX_TEMPFAIL, "kevent2");
    printf("kevent2 done, flags: %04x\n", kc.flags);

    exit(0);
}
