
/*
 * cc -Wall fdpassing.c -o /tmp/fdp
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

void sendfd(int s, int fd);
int recvfd(int s);

int
main(int ac, char **av)
{
    struct stat st;
    int fds[2];
    int fd;
    int i;
    int dummy;

    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fds) < 0) {
	perror("socketpair");
	exit(1);
    }
    fcntl(fds[0], F_SETFL, 0);
    fcntl(fds[1], F_SETFL, 0);
    if (fork() == 0) {
	close(fds[1]);
	while ((fd = recvfd(fds[0])) >= 0) {
		fflush(stdout);
		usleep(1000000 / 10);
		if (fstat(fd, &st) < 0)
		    printf("B");
		else if (read(fd, &dummy, sizeof(dummy)) != sizeof(dummy))
		    printf("b");
		else
		    printf("r");
		close(fd);
		usleep(1000000 / 10);
	}
	printf("X\n");
	_exit(0);
    }
    close(fds[0]);

    for (;;) {
	for (i = 1; i < ac; ++i) {
	    if ((fd = open(av[i], O_RDONLY, 0666)) < 0) {
		fprintf(stderr, "unable to open %s\n", av[i]);
	    } else {
		sendfd(fds[1], fd);
		printf("w");
		fflush(stdout);
		close(fd);
	    }
	}
	usleep(1000000 / 5 - 10000);
    }
    printf("DONE\n");
    close(fds[1]);
    while (wait(NULL) < 0)
	;
    return(0);
}

void
sendfd(int s, int fd)
{
    struct msghdr msg;
    struct {
	struct cmsghdr cmsg;
	int fd;
    } cmsg;
    struct iovec iov;

    bzero(&msg, sizeof(msg));
    bzero(&cmsg, sizeof(cmsg));

    iov.iov_base = NULL;
    iov.iov_len = 0;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = sizeof(cmsg);

    cmsg.cmsg.cmsg_level = SOL_SOCKET;
    cmsg.cmsg.cmsg_type = SCM_RIGHTS;
    cmsg.cmsg.cmsg_len = sizeof(cmsg);
    cmsg.fd = fd;

    if (sendmsg(s, &msg, 0) < 0) {
	printf("sendfd: failed %s\n", strerror(errno));
	exit(1);
    }
}

int
recvfd(int s)
{
    struct msghdr msg;
    struct {
	struct cmsghdr cmsg;
	int fd;
    } cmsg;
    struct iovec iov;
    int r;
    int dummy;

    bzero(&msg, sizeof(msg));
    bzero(&cmsg, sizeof(cmsg));

    iov.iov_base = &dummy;
    iov.iov_len = sizeof(dummy);

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = sizeof(cmsg);

#if 0
    cmsg.cmsg.cmsg_level = SOL_SOCKET;
    cmsg.cmsg.cmsg_type = SCM_RIGHTS;
#endif
    cmsg.cmsg.cmsg_len = sizeof(cmsg);
    cmsg.fd = -1;

    if ((r = recvmsg(s, &msg, MSG_EOR)) < 0) {
	printf("recvmsg: failed %s\n", strerror(errno));
	return(-1);
    }
    return(cmsg.fd);
}
