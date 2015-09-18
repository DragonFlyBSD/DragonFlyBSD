#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_FILENAME	"/tmp/passdesc"

static void
test_send_desc(int s, int fd)
{
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cm;
		uint8_t data[CMSG_SPACE(sizeof(int))];
	} ctrl;
	struct cmsghdr *cm;
	int n;

	iov.iov_base = &fd;
	iov.iov_len = sizeof(fd);

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ctrl.data;
	msg.msg_controllen = sizeof(ctrl.data);

	memset(&ctrl, 0, sizeof(ctrl));
	cm = CMSG_FIRSTHDR(&msg);
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	*((int *)CMSG_DATA(cm)) = fd;

	n = sendmsg(s, &msg, 0);
	if (n < 0)
		err(1, "sendmsg failed");
	else if (n != sizeof(fd))
		errx(1, "sendmsg sent %d", n);
	close(fd);
}

static void
test_recv_desc(int s)
{
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cm;
		uint8_t data[CMSG_SPACE(sizeof(int))];
	} ctrl;
	struct cmsghdr *cm;
	int buf, n, fd;
	char data[16];

	iov.iov_base = &buf;
	iov.iov_len = sizeof(buf);

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ctrl.data;
	msg.msg_controllen = sizeof(ctrl.data);

	n = recvmsg(s, &msg, 0);
	if (n < 0)
		err(1, "recvmsg failed");
	else if (n != sizeof(buf))
		errx(1, "recvmsg received %d", n);

	cm = CMSG_FIRSTHDR(&msg);
	if (cm == NULL)
		errx(1, "no cmsg");
	if (cm->cmsg_len != CMSG_LEN(sizeof(int)))
		errx(1, "cmsg len mismatch");
	if (cm->cmsg_level != SOL_SOCKET)
		errx(1, "cmsg level mismatch");
	if (cm->cmsg_type != SCM_RIGHTS)
		errx(1, "cmsg type mismatch");

	fd = *((int *)CMSG_DATA(cm));

	n = read(fd, data, sizeof(data) - 1);
	if (n < 0)
		err(1, "read failed");
	data[n] = '\0';

	fprintf(stderr, "fd content: %s\n", data);
}

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s [-d]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int s[2], fd, status, n, discard;
	int opt;
	off_t ofs;

	discard = 0;
	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
		case 'd':
			discard = 1;
			break;

		default:
			usage(argv[0]);
		}
	}

	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, s) < 0)
		err(1, "socketpair(LOCAL, STREAM) failed");

	if (fork() == 0) {
		close(s[0]);
		if (!discard)
			test_recv_desc(s[1]);
		else
			sleep(5);
		exit(0);
	}
	close(s[1]);

	fd = open(TEST_FILENAME, O_RDWR | O_TRUNC | O_CREAT,
	    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if (fd < 0)
		err(1, "open " TEST_FILENAME " failed");

	n = write(fd, TEST_FILENAME, strlen(TEST_FILENAME));
	if (n < 0)
		err(1, "write failed");
	else if (n != strlen(TEST_FILENAME))
		errx(1, "write %d", n);

	ofs = lseek(fd, 0, SEEK_SET);
	if (ofs < 0)
		err(1, "lseek failed");
	else if (ofs != 0)
		errx(1, "lseek offset %jd", (intmax_t)ofs);

	test_send_desc(s[0], fd);

	wait(&status);
	exit(0);
}
