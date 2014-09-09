#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	mainloop(const struct sockaddr_in *, int, int, long, u_long *,
		    int);

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -4 inet4 [-4 inet4_1] -p port "
	    "[-i n_instance] [-c conn_max] [-l duration] [-u]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in *in, *tmp;
	int opt, ninst, nconn, i, in_max, in_cnt, do_udp;
	long dur;
	u_long *result, sum;
	u_short port;
	size_t prm_len;

	prm_len = sizeof(ninst);
	if (sysctlbyname("hw.ncpu", &ninst, &prm_len, NULL, 0) != 0)
		err(2, "sysctl hw.ncpu failed");

	nconn = 8;
	dur = 10;
	port = 0;
	do_udp = 0;

	in_max = 8;
	in = calloc(in_max, sizeof(struct sockaddr_in));
	if (in == NULL)
		err(1, "calloc failed");
	in_cnt = 0;

	while ((opt = getopt(argc, argv, "4:p:i:l:c:u")) != -1) {
		switch (opt) {
		case '4':
			if (in_cnt >= in_max) {
				struct sockaddr_in *old_in = in;
				int old_in_max = in_max;

				in_max <<= 1;
				in = calloc(in_max, sizeof(struct sockaddr_in));
				if (in == NULL)
					err(1, "calloc failed");

				memcpy(in, old_in,
				    old_in_max * sizeof(struct sockaddr_in));
				free(old_in);
			}

			tmp = &in[in_cnt];
			if (inet_pton(AF_INET, optarg, &tmp->sin_addr) <= 0) {
				fprintf(stderr, "invalid inet address %s\n",
				    optarg);
				usage(argv[0]);
			}
			++in_cnt;
			break;

		case 'p':
			port = htons(atoi(optarg));
			break;

		case 'i':
			ninst = atoi(optarg);
			break;

		case 'c':
			nconn = atoi(optarg);
			break;

		case 'l':
			dur = strtol(optarg, NULL, 10);
			break;

		case 'u':
			do_udp = 1;
			break;

		default:
			usage(argv[0]);
		}
	}

	if (ninst < 1 || dur < 1 || nconn < 1 || port == 0 || in_cnt == 0)
		usage(argv[0]);

	for (i = 0; i < in_cnt; ++i) {
		tmp = &in[i];
		tmp->sin_family = AF_INET;
		tmp->sin_port = port;
	}

	result = mmap(NULL, ninst * sizeof(u_long), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED, -1, 0);
	if (result == MAP_FAILED)
		err(1, "mmap failed");
	memset(result, 0, ninst * sizeof(u_long));

	for (i = 0; i < ninst; ++i) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			mainloop(in, in_cnt, nconn, dur, &result[i], do_udp);
			exit(0);
		} else if (pid < 0) {
			err(1, "fork failed");
		}
	}

	for (i = 0; i < ninst; ++i) {
		pid_t pid;

		pid = waitpid(-1, NULL, 0);
		if (pid < 0)
			err(1, "waitpid failed");
	}

	sum = 0;
	for (i = 0; i < ninst; ++i)
		sum += result[i];
	printf("%.2f\n", (double)sum / (double)dur);

	exit(0);
}

static void
udp_send(const struct sockaddr_in *in)
{
	uint8_t d[18];
	int s;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		warn("socket DGRAM failed");
		return;
	}

	if (connect(s, (const struct sockaddr *)in, sizeof(*in)) < 0) {
		warn("connect DGRAM failed");
		goto done;
	}

	write(s, d, sizeof(d));
done:
	close(s);
}

static void
mainloop(const struct sockaddr_in *in, int in_cnt, int nconn_max,
    long dur, u_long *res, int do_udp)
{
	struct kevent *evt_change0, *evt;
	int kq, nchange = 0, nconn = 0, nevt_max;
	u_long count = 0;
	u_int in_idx = 0;
	int nblock = 1;

	kq = kqueue();
	if (kq < 0)
		err(1, "kqueue failed");

	nevt_max = nconn_max + 1; /* timer */

	evt_change0 = malloc(nevt_max * sizeof(struct kevent));
	if (evt_change0 == NULL)
		err(1, "malloc evt_change failed");

	evt = malloc(nevt_max * sizeof(struct kevent));
	if (evt == NULL)
		err(1, "malloc evt failed");

	EV_SET(&evt_change0[0], 0, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0,
	    dur * 1000L, NULL);
	nchange = 1;

	for (;;) {
		struct kevent *evt_change = NULL;
		int n, i, done = 0;

		while (nconn < nconn_max) {
			const struct sockaddr_in *tmp;
			int s;

			tmp = &in[in_idx % in_cnt];
			++in_idx;

			if (do_udp)
				udp_send(tmp);

			s = socket(AF_INET, SOCK_STREAM, 0);
			if (s < 0)
				err(1, "socket failed");

			if (ioctl(s, FIONBIO, &nblock, sizeof(nblock)) < 0)
				err(1, "ioctl failed");

			n = connect(s, (const struct sockaddr *)tmp,
			    sizeof(*tmp));
			if (n == 0) {
				++count;
				close(s);
				continue;
			} else {
			    	int error = errno;

				if (error != EINPROGRESS)
					errc(1, error, "connect failed");
			}
			++nconn;

			if (nchange >= nevt_max) {
				fprintf(stderr, "invalid nchange %d, max %d\n",
				    nchange, nevt_max);
				abort();
			}
			EV_SET(&evt_change0[nchange], s, EVFILT_WRITE, EV_ADD,
			    0, 0, NULL);
			++nchange;
		}

		if (nchange)
			evt_change = evt_change0;

		n = kevent(kq, evt_change, nchange, evt, nevt_max, NULL);
		if (n < 0)
			err(1, "kevent failed");
		nchange = 0;

		for (i = 0; i < n; ++i) {
			struct kevent *e = &evt[i];

			if (e->filter == EVFILT_TIMER) {
				done = 1;
				continue;
			}

			if ((e->flags & EV_EOF) && e->fflags) {
				/* Error, don't count */
			} else {
				++count;
			}
			close(e->ident);
			--nconn;
		}
		if (done)
			break;
	}
	*res = count;
}
