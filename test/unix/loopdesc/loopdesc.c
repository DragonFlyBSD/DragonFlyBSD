#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
test_sendto_self(int to_s, int s)
{
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cm;
		uint8_t data[CMSG_SPACE(sizeof(int))];
	} ctrl;
	struct cmsghdr *cm;
	int n, buf;

	iov.iov_base = &buf;
	iov.iov_len = sizeof(buf);

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
	*((int *)CMSG_DATA(cm)) = s;

	n = sendmsg(to_s, &msg, 0);
	if (n < 0)
		err(1, "sendmsg failed");
	else if (n != sizeof(buf))
		errx(1, "sendmsg sent %d", n);
}

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s [-x]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int s[2], opt, xref;

	xref = 0;
	while ((opt = getopt(argc, argv, "x")) != -1) {
		switch (opt) {
		case 'x':
			xref = 1;
			break;

		default:
			usage(argv[0]);
		}
	}

	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, s) < 0)
		err(1, "socketpair(LOCAL, STREAM) failed");

	if (xref) {
		fprintf(stderr, "cross reference\n");
		/* Send s[0] to s[1].rcvbuf */
		test_sendto_self(s[0], s[0]);
		/* Send s[1] to s[0].rcvbuf */
		test_sendto_self(s[1], s[1]);
	} else {
		fprintf(stderr, "self reference\n");
		/* Send s[0] to s[0].rcvbuf */
		test_sendto_self(s[1], s[0]);
		/* Send s[1] to s[1].rcvbuf */
		test_sendto_self(s[0], s[1]);
	}
	exit(0);
}
