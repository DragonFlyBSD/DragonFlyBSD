#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* workaround leaky case */
#ifndef MSG_CMSG_CLOEXEC
#define MSG_CMSG_CLOEXEC 0
#endif

#define CHECKFD_CMD	"checkfd"
#define CHECKFD_PATH	"/usr/local/bin/" CHECKFD_CMD

int sendfd(int s, int fd);
int recvfd(int s);

int main(void)
{
	pid_t pid;
	int s[2], status, ecode;

	socketpair(AF_UNIX, SOCK_STREAM, 0, s);

	pid = fork();
	if (pid < 0) {
		err(1, "fork failed");
	} else if (pid == 0) {
		close(0);
		close(1);
		/* close(2); */

		int mysecretfd = recvfd(s[0]);

		write(mysecretfd, "hello secret\n", 14);

		char fd[8];
		snprintf(fd, sizeof(fd), "%d", mysecretfd);
		if (execl(CHECKFD_PATH, CHECKFD_CMD, fd, NULL) < 0)
			err(3, "execl failed");
	} else {
		sendfd(s[1], 1);
	}

	if (waitpid(pid, &status, 0) < 0)
		err(1, "waitpid failed");

	if (!WIFEXITED(status))
		errx(1, CHECKFD_CMD " did not exit");

	ecode = WEXITSTATUS(status);
	if (ecode != 0) {
		warnx("exit code %d", ecode);
		abort();
	}

	fprintf(stderr, "passed\n");
	exit(0);

}

int
sendfd(int s, int fd)
{
	char buf[1];
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	int n;
	char cms[CMSG_SPACE(sizeof(int))];

	buf[0] = 0;
	iov.iov_base = buf;
	iov.iov_len = 1;

	memset(&msg, 0, sizeof msg);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t)cms;
	msg.msg_controllen = CMSG_LEN(sizeof(int));

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memmove(CMSG_DATA(cmsg), &fd, sizeof(int));

	if ((n=sendmsg(s, &msg, 0)) != (int)iov.iov_len)
		return -1;
	return 0;
}

int
recvfd(int s)
{
	int n;
	int fd;
	char buf[1];
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	char cms[CMSG_SPACE(sizeof(int))];

	iov.iov_base = buf;
	iov.iov_len = 1;

	memset(&msg, 0, sizeof msg);
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = (caddr_t)cms;
	msg.msg_controllen = sizeof cms;

	if ((n=recvmsg(s, &msg, MSG_CMSG_CLOEXEC)) < 0)
		return -1;
	if (n == 0){
		return -1;
	}
	cmsg = CMSG_FIRSTHDR(&msg);
	memmove(&fd, CMSG_DATA(cmsg), sizeof(int));
	return fd;
}
