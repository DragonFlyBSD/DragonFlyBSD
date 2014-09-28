#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/usched.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RCVBUF_SIZE	(256 * 1024)
#define BUFLEN		2048

#define FLAG_NOREPLY	0x0001
#define FLAG_BINDCPU	0x0002

static void	mainloop(struct sockaddr_in *, int, int, uint32_t);

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -4 addr4 -p port [-i ninst] [-r rcvbuf] "
	    "[-N] [-B]\n", cmd);
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int opt, ninst, i, s, rcvbuf, flags;
	size_t prm_len;

	prm_len = sizeof(ninst);
	if (sysctlbyname("hw.ncpu", &ninst, &prm_len, NULL, 0) != 0)
		err(2, "sysctl hw.ncpu failed");

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;

	flags = 0;
	rcvbuf = RCVBUF_SIZE;

	while ((opt = getopt(argc, argv, "4:p:i:r:NB")) != -1) {
		switch (opt) {
		case '4':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0)
				errx(1, "invalid -4 %s\n", optarg);
			break;

		case 'p':
			in.sin_port = strtoul(optarg, NULL, 10);
			in.sin_port = htons(in.sin_port);
			break;

		case 'i':
			ninst = strtoul(optarg, NULL, 10);
			break;

		case 'r':
			rcvbuf = strtoul(optarg, NULL, 10);
			rcvbuf *= 1024;
			break;

		case 'N':
			flags |= FLAG_NOREPLY;
			break;

		case 'B':
			flags |= FLAG_BINDCPU;
			break;

		default:
			usage(argv[0]);
		}
	}

	if (in.sin_port == 0 || in.sin_addr.s_addr == INADDR_ANY)
		usage(argv[0]);

	s = -1;

	for (i = 0; i < ninst - 1; ++i) {
		pid_t pid;

		pid = fork();
		if (pid == 0)
			mainloop(&in, s, rcvbuf, flags);
		else if (pid < 0)
			err(1, "fork %d failed", i);
	}

	mainloop(&in, s, rcvbuf, flags);
	exit(0);
}

static void
mainloop(struct sockaddr_in *in, int s, int rcvbuf, uint32_t flags)
{
	int on;
	void *buf;

	if (s < 0) {
		s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
			err(1, "socket(INET, DGRAM) failed");
	}

	buf = malloc(BUFLEN);
	if (buf == NULL)
		err(1, "malloc buf failed");

	on = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0)
		err(1, "setsockopt(SOCK, REUSEPORT) failed");

	if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0)
		err(1, "setsockopt(SOCK, RCVBUF) failed");

	if (bind(s, (const struct sockaddr *)in, sizeof(*in)) < 0)
		err(1, "bind failed");

	if (flags & FLAG_BINDCPU) {
		socklen_t cpu_len;
		int cpu;

		cpu_len = sizeof(cpu);
		if (getsockopt(s, SOL_SOCKET, SO_CPUHINT, &cpu, &cpu_len) < 0)
			err(1, "getsockopt(SOCK, CPUHINT) failed");
		if (cpu >= 0)
			usched_set(getpid(), USCHED_SET_CPU, &cpu, sizeof(cpu));
	}

	for (;;) {
		struct sockaddr_in cli;
		socklen_t cli_len;
		int n;

		cli_len = sizeof(cli);
		n = recvfrom(s, buf, BUFLEN, 0,
		    (struct sockaddr *)&cli, &cli_len);
		if (n > 0 && (flags & FLAG_NOREPLY) == 0) {
			sendto(s, buf, n, 0,
			   (const struct sockaddr *)&cli, cli_len);
		}
	}
	exit(0);
}
