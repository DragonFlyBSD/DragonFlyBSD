#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -4 ip4 -p port [-t tos [-c]]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int s, opt, n, tos, cmsg_tos;
	uint8_t buf[18];
	struct msghdr msg;
	struct iovec iov;
	struct cmsghdr *cm;
	union {
		struct cmsghdr cm;
		uint8_t data[CMSG_SPACE(sizeof(u_char))];
	} ctrl;

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	tos = -1;
	cmsg_tos = 0;

	while ((opt = getopt(argc, argv, "4:cp:t:")) != -1) {
		switch (opt) {
		case '4':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0)
				usage(argv[0]);
			break;

		case 'c':
			cmsg_tos = 1;
			break;

		case 'p':
			in.sin_port = strtol(optarg, NULL, 10);
			in.sin_port = htons(in.sin_port);
			break;

		case 't':
			tos = strtol(optarg, NULL, 10);
			break;

		default:
			usage(argv[0]);
		}
	}

	if (in.sin_addr.s_addr == INADDR_ANY || in.sin_port == 0)
		usage(argv[0]);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	if (tos >= 0) {
		if (!cmsg_tos) {
			if (setsockopt(s, IPPROTO_IP, IP_TOS,
			    &tos, sizeof(tos)) < 0)
				err(2, "setsockopt IP_TOS %d failed", tos);

			if (sendto(s, buf, sizeof(buf), 0,
			    (const struct sockaddr *)&in, sizeof(in)) < 0)
				err(2, "sendto failed");
		} else {
			iov.iov_base = buf;
			iov.iov_len = sizeof(buf);

			memset(&msg, 0, sizeof(msg));
			msg.msg_name = &in;
			msg.msg_namelen = sizeof(in);
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;
			msg.msg_control = ctrl.data;
			msg.msg_controllen = sizeof(ctrl.data);

			memset(&ctrl, 0, sizeof(ctrl));
			cm = CMSG_FIRSTHDR(&msg);
			cm->cmsg_len = CMSG_LEN(sizeof(u_char));
			cm->cmsg_level = IPPROTO_IP;
			cm->cmsg_type = IP_TOS;
			*((u_char *)CMSG_DATA(cm)) = tos;

			fprintf(stderr, "sendmsg tos %d\n", tos);
			if (sendmsg(s, &msg, MSG_SYNC) < 0)
				err(2, "sendmsg failed");
		}
	} else {
		const int on = 1;

		if (bind(s, (const struct sockaddr *)&in, sizeof(in)) < 0)
			err(2, "bind failed");

		if (setsockopt(s, IPPROTO_IP, IP_RECVTOS, &on, sizeof(on)) < 0)
			err(2, "setsockopt IP_RECVTOS failed");

		iov.iov_base = buf;
		iov.iov_len = sizeof(buf);

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ctrl.data;
		msg.msg_controllen = sizeof(ctrl.data);

		n = recvmsg(s, &msg, MSG_WAITALL);
		if (n < 0)
			err(1, "recvmsg failed");
		else if (n != sizeof(buf))
			errx(1, "recvmsg received %d", n);

		cm = CMSG_FIRSTHDR(&msg);
		if (cm == NULL)
			errx(1, "no cmsg");
		if (cm->cmsg_len != CMSG_LEN(sizeof(u_char)))
			errx(1, "cmsg len mismatch");
		if (cm->cmsg_level != IPPROTO_IP)
			errx(1, "cmsg level mismatch");
		if (cm->cmsg_type != IP_RECVTOS)
			errx(1, "cmsg type mismatch");

		tos = *((u_char *)CMSG_DATA(cm));

		fprintf(stderr, "TOS: %d\n", tos);
	}

	exit(0);
}
