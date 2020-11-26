#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
test_sendsrcaddr(int s, const struct sockaddr_in *remote,
    const struct in_addr *src)
{
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cm;
		uint8_t data[CMSG_SPACE(sizeof(struct in_addr))];
	} ctrl;
	struct cmsghdr *cm;
	int n;

	iov.iov_base = &n;
	iov.iov_len = sizeof(n);

	memset(&msg, 0, sizeof(msg));
	if (remote != NULL) {
		msg.msg_name = __DECONST(void *, remote);
		msg.msg_namelen = sizeof(*remote);
	}
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ctrl.data;
	msg.msg_controllen = sizeof(ctrl.data);

	memset(&ctrl, 0, sizeof(ctrl));
	cm = CMSG_FIRSTHDR(&msg);
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	cm->cmsg_level = IPPROTO_IP;
	cm->cmsg_type = IP_RECVDSTADDR;
	*((struct in_addr *)CMSG_DATA(cm)) = *src;

	return sendmsg(s, &msg, MSG_SYNC);
}

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -4 ip4 -p port [-s src]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in remote, local;
	struct in_addr src;
	socklen_t local_len;
	char local_str[INET_ADDRSTRLEN];
	int s, opt, s2, on, n;

	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;

	src.s_addr = INADDR_ANY;

	while ((opt = getopt(argc, argv, "4:p:s:")) != -1) {
		switch (opt) {
		case '4':
			if (inet_pton(AF_INET, optarg, &remote.sin_addr) <= 0)
				usage(argv[0]);
			break;

		case 'p':
			remote.sin_port = strtol(optarg, NULL, 10);
			remote.sin_port = htons(remote.sin_port);
			break;

		case 's':
			if (inet_pton(AF_INET, optarg, &src) <= 0)
				usage(argv[0]);
			break;

		default:
			usage(argv[0]);
		}
	}

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket faild");

	/*
	 * inp_laddr == ANY && src valid --> pass.
	 * inp_laddr == ANY && src invalid --> fail.
	 */
	if (test_sendsrcaddr(s, &remote, &src) < 0)
		err(2, "sendsrcaddr failed");

	local_len = sizeof(local);
	if (getsockname(s, (struct sockaddr *)&local, &local_len) < 0)
		err(2, "getsockname failed");

	fprintf(stderr, "wildcard: laddr %s, lport %u\n",
	    inet_ntop(AF_INET, &local.sin_addr, local_str, sizeof(local_str)),
	    ntohs(local.sin_port));
	local.sin_addr = src;

	s2 = socket(AF_INET, SOCK_DGRAM, 0);
	if (s2 < 0)
		err(2, "socket 2 failed");

	on = 1;
	if (setsockopt(s2, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		err(2, "setsockopt(REUSEADDR) failed");

	if (bind(s2, (const struct sockaddr *)&local, sizeof(local)) < 0)
		err(2, "bind src failed");

	/*
	 * inp_laddr != ANY && src == inp_laddr --> pass.
	 */
	if (test_sendsrcaddr(s2, &remote, &src) < 0)
		err(2, "sendsrcaddr 2 failed");

	/*
	 * Duplicated src/lport as s2 --> fail.
	 */
	if (test_sendsrcaddr(s, &remote, &src) > 0)
		errx(2, "sendsrcaddr succeeded!?");

	close(s2);

	if (connect(s, (const struct sockaddr *)&remote, sizeof(remote)) < 0)
		err(2, "connect remote failed");

	local_len = sizeof(local);
	if (getsockname(s, (struct sockaddr *)&local, &local_len) < 0)
		err(2, "getsockname failed");
	fprintf(stderr, "connected: laddr %s, lport %u\n",
	    inet_ntop(AF_INET, &local.sin_addr, local_str, sizeof(local_str)),
	    ntohs(local.sin_port));

	/*
	 * Connected socket:
	 * if inp_laddr == src --> pass.
	 * if inp_laddr != src --> fail.
	 */
	n = test_sendsrcaddr(s, NULL, &src);
	if (local.sin_addr.s_addr == src.s_addr) {
		if (n < 0)
			err(2, "sendsrcaddr 3 failed");
	} else {
		if (n > 0)
			errx(2, "sendsrcaddr 2 succeeded!?");
	}

	s2 = socket(AF_INET, SOCK_DGRAM, 0);
	if (s2 < 0)
		err(2, "socket 3 failed");

	on = 1;
	if (setsockopt(s2, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		err(2, "setsockopt(REUSEADDR) failed");

	local.sin_addr.s_addr = INADDR_ANY;
	if (bind(s2, (const struct sockaddr *)&local, sizeof(local)) < 0)
		err(2, "bind * failed");

	/*
	 * Connected socket, implied laddr/lport bound:
	 * if inp_laddr == src --> pass.
	 * if inp_laddr != src --> fail.
	 *
	 * Wildcard bound above should not matter.
	 */
	local_len = sizeof(local);
	if (getsockname(s, (struct sockaddr *)&local, &local_len) < 0)
		err(2, "getsockname failed");
	n = test_sendsrcaddr(s, NULL, &src);
	if (local.sin_addr.s_addr == src.s_addr) {
		if (n < 0)
			err(2, "sendsrcaddr 4 failed");
	} else {
		if (n > 0)
			errx(2, "sendsrcaddr 3 succeeded!?");
	}

	close(s2);

	exit(0);
}
