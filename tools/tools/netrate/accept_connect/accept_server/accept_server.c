#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	mainloop(int, const struct sockaddr_in *);

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -p port [-i n_instance] [-r]\n", cmd);
	exit(1);
}

static int
create_socket(const struct sockaddr_in *in, int reuseport)
{
	int serv_s, on;

	serv_s = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_s < 0)
		err(1, "socket failed");

	on = 1;
	if (!reuseport) {
		if (setsockopt(serv_s, SOL_SOCKET, SO_REUSEADDR,
		    &on, sizeof(on)) < 0)
			err(1, "setsockopt(REUSEADDR) failed");
	} else {
		if (setsockopt(serv_s, SOL_SOCKET, SO_REUSEPORT,
		    &on, sizeof(on)) < 0)
			err(1, "setsockopt(REUSEPORT) failed");
	}

	if (bind(serv_s, (const struct sockaddr *)in, sizeof(*in)) < 0)
		err(1, "bind failed");

	if (listen(serv_s, -1) < 0)
		err(1, "listen failed");
	return serv_s;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int opt, ninst, serv_s, i, reuseport;
	size_t prm_len;

	prm_len = sizeof(ninst);
	if (sysctlbyname("hw.ncpu", &ninst, &prm_len, NULL, 0) != 0)
		err(2, "sysctl hw.ncpu failed");

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_addr.s_addr = INADDR_ANY;

	reuseport = 0;

	while ((opt = getopt(argc, argv, "p:i:r")) != -1) {
		switch (opt) {
		case 'p':
			in.sin_port = htons(atoi(optarg));
			break;

		case 'i':
			ninst = atoi(optarg);
			break;

		case 'r':
			reuseport = 1;
			break;

		default:
			usage(argv[0]);
		}
	}

	if (ninst < 1 || in.sin_port == 0)
		usage(argv[0]);

	serv_s = -1;
	if (!reuseport)
		serv_s = create_socket(&in, 0);

	for (i = 1; i < ninst; ++i) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			mainloop(serv_s, &in);
			exit(0);
		} else if (pid < 0) {
			err(1, "fork failed");
		}
	}

	mainloop(serv_s, &in);
	exit(0);
}

static void
mainloop(int serv_s, const struct sockaddr_in *in)
{
	if (serv_s < 0)
		serv_s = create_socket(in, 1);

	for (;;) {
		int s;

		s = accept(serv_s, NULL, NULL);
		if (s >= 0)
			close(s);
	}
	/* NEVER REACHED */
}
