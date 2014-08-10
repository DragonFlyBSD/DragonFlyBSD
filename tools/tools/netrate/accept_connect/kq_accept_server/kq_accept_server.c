#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EVENT_MAX	128

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
	if (serv_s < 0) {
		fprintf(stderr, "socket failed: %d\n", errno);
		exit(1);
	}

	on = 1;
	if (!reuseport) {
		if (setsockopt(serv_s, SOL_SOCKET, SO_REUSEADDR,
		    &on, sizeof(on)) < 0) {
			fprintf(stderr, "setsockopt(REUSEADDR) failed: %d\n",
			    errno);
			exit(1);
		}
	} else {
		if (setsockopt(serv_s, SOL_SOCKET, SO_REUSEPORT,
		    &on, sizeof(on)) < 0) {
			fprintf(stderr, "setsockopt(REUSEPORT) failed: %d\n",
			    errno);
			exit(1);
		}
	}

	on = 1;
	if (ioctl(serv_s, FIONBIO, &on, sizeof(on)) < 0) {
		fprintf(stderr, "ioctl(FIONBIO) failed: %d\n", errno);
		exit(1);
	}

	if (bind(serv_s, (const struct sockaddr *)in, sizeof(*in)) < 0) {
		fprintf(stderr, "bind failed: %d\n", errno);
		exit(1);
	}

	if (listen(serv_s, -1) < 0) {
		fprintf(stderr, "listen failed: %d\n", errno);
		exit(1);
	}
	return serv_s;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int opt, ninst, serv_s, i, reuseport;

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_addr.s_addr = INADDR_ANY;

	ninst = 1;
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
			fprintf(stderr, "fork failed: %d\n", errno);
		}
	}

	mainloop(serv_s, &in);
	exit(0);
}

static void
mainloop(int serv_s, const struct sockaddr_in *in)
{
	struct kevent change_evt0[EVENT_MAX];
	int kq, nchange;

	if (serv_s < 0)
		serv_s = create_socket(in, 1);

	kq = kqueue();
	if (kq < 0) {
		fprintf(stderr, "kqueue failed: %d\n", errno);
		exit(1);
	}

	EV_SET(&change_evt0[0], serv_s, EVFILT_READ, EV_ADD, 0, 0, NULL);
	nchange = 1;

	for (;;) {
		const struct kevent *change_evt = NULL;
		struct kevent evt[EVENT_MAX];
		int i, nevt;

		if (nchange > 0)
			change_evt = change_evt0;

		nevt = kevent(kq, change_evt, nchange, evt, EVENT_MAX, NULL);
		if (nevt < 0) {
			fprintf(stderr, "kevent failed: %d\n", errno);
			exit(1);
		}
		nchange = 0;

		for (i = 0; i < nevt; ++i) {
			if (evt[i].ident == (u_int)serv_s) {
				while (nchange < EVENT_MAX) {
					int s;

					s = accept(serv_s, NULL, NULL);
					if (s < 0)
						break;
					EV_SET(&change_evt0[nchange], s,
					    EVFILT_READ, EV_ADD, 0, 0, NULL);
					++nchange;
				}
			} else {
				close(evt[i].ident);
			}
		}
	}
	/* NEVER REACHED */
}
