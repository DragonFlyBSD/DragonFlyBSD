#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kq_sendrecv_proto.h"

#define RECV_EVENT_MAX		64
#define RECV_BUFLEN		(128 * 1024)

struct recv_thrctx {
	int			t_id;
	struct sockaddr_in	t_in;

	pthread_mutex_t		t_lock;
	pthread_cond_t		t_cond;

	pthread_t		t_tid;
};

static void	*recv_thread(void *);

static int	recv_buflen = RECV_BUFLEN;

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s [-4 addr4] [-p port] [-t nthreads] [-D] "
	    "[-b buflen]\n", cmd);
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct recv_thrctx *ctx_arr;
	struct recv_info *info;
	struct sockaddr_in in;
	sigset_t sigset;
	int opt, s, on, nthr, i, info_sz, do_daemon;
	size_t sz;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGPIPE);
	if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0)
		err(1, "sigprocmask failed");

	sz = sizeof(nthr);
	if (sysctlbyname("hw.ncpu", &nthr, &sz, NULL, 0) < 0)
		err(1, "sysctl hw.ncpu failed");

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_addr.s_addr = htonl(INADDR_ANY);
	in.sin_port = htons(RECV_PORT);

	do_daemon = 1;

	while ((opt = getopt(argc, argv, "4:Db:p:t:")) != -1) {
		switch (opt) {
		case '4':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0)
				errx(1, "inet_pton failed %s", optarg);
			break;

		case 'D':
			do_daemon = 0;
			break;

		case 'b':
			recv_buflen = strtol(optarg, NULL, 10);
			if (recv_buflen <= 0)
				errx(1, "invalid -b");
			break;

		case 'p':
			in.sin_port = htons(strtoul(optarg, NULL, 10));
			break;

		case 't':
			nthr = strtol(optarg, NULL, 10);
			if (nthr <= 0)
				errx(1, "invalid -t");
			break;

		default:
			usage(argv[0]);
		}
	}

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		err(1, "socket failed");

	on = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		err(1, "setsockopt(REUSEPADDR) failed");

	if (bind(s, (const struct sockaddr *)&in, sizeof(in)) < 0)
		err(1, "bind failed");

	if (listen(s, -1) < 0)
		err(1, "listen failed");

	ctx_arr = calloc(nthr, sizeof(struct recv_thrctx));
	if (ctx_arr == NULL)
		err(1, "calloc failed");

	info_sz = __offsetof(struct recv_info, dport[nthr]);
	info = calloc(1, info_sz);
	if (info == NULL)
		err(1, "calloc failed");
	info->ndport = nthr;

	if (do_daemon)
		daemon(0, 0);

	pthread_set_name_np(pthread_self(), "main");

	for (i = 0; i < nthr; ++i) {
		struct recv_thrctx *ctx = &ctx_arr[i];
		int error;

		ctx->t_in = in;
		ctx->t_in.sin_port = 0;

		ctx->t_id = i;
		pthread_mutex_init(&ctx->t_lock, NULL);
		pthread_cond_init(&ctx->t_cond, NULL);

		/* Start receiver */
		error = pthread_create(&ctx->t_tid, NULL, recv_thread, ctx);
		if (error)
			errc(1, error, "pthread_create %d failed", i);

		/*
		 * Wait for the receiver to select a proper data port
		 * and start a listen socket on the data port.
		 */
		pthread_mutex_lock(&ctx->t_lock);
		while (ctx->t_in.sin_port == 0)
			pthread_cond_wait(&ctx->t_cond, &ctx->t_lock);
		pthread_mutex_unlock(&ctx->t_lock);

		info->dport[i] = ctx->t_in.sin_port;
	}

	/*
	 * Send information, e.g. data ports, back to the clients.
	 */
	for (;;) {
		int s1;

		s1 = accept(s, NULL, NULL);
		if (s1 < 0)
			continue;
		write(s1, info, info_sz);
		close(s1);
	}

	/* NEVER REACHED */
	exit(0);
}

static void *
recv_thread(void *xctx)
{
	struct recv_thrctx *ctx = xctx;
	struct kevent change_evt0[RECV_EVENT_MAX];
	struct conn_ack ack;
	uint8_t *buf;
	char name[32];
	u_short port;
	int s, kq, nchange;

	/*
	 * Select a proper data port and create a listen socket on it.
	 */
	port = RECV_PORT + ctx->t_id;
	for (;;) {
		struct sockaddr_in in = ctx->t_in;
		int on;

		++port;
		if (port < RECV_PORT)
			errx(1, "failed to find a data port");
		in.sin_port = htons(port);

		s = socket(AF_INET, SOCK_STREAM, 0);
		if (s < 0)
			err(1, "socket failed");

		on = 1;
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
			err(1, "setsockopt(REUSEADDR) failed");

		on = 1;
		if (ioctl(s, FIONBIO, &on, sizeof(on)) < 0)
			err(1, "ioctl(FIONBIO) failed");

		if (bind(s, (const struct sockaddr *)&in, sizeof(in)) < 0) {
			close(s);
			continue;
		}

		if (listen(s, -1) < 0)
			err(1, "listen failed");

		break;
	}

	kq = kqueue();
	if (kq < 0)
		err(1, "kqueue failed");

	buf = malloc(recv_buflen);
	if (buf == NULL)
		err(1, "malloc %d failed", recv_buflen);

	memset(&ack, 0, sizeof(ack));

	snprintf(name, sizeof(name), "rcv%d %d", ctx->t_id, port);
	pthread_set_name_np(pthread_self(), name);

	/*
	 * Inform the main thread that we are ready.
	 */
	pthread_mutex_lock(&ctx->t_lock);
	ctx->t_in.sin_port = htons(port);
	pthread_mutex_unlock(&ctx->t_lock);
	pthread_cond_signal(&ctx->t_cond);

	EV_SET(&change_evt0[0], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
	nchange = 1;

	for (;;) {
		const struct kevent *change_evt = NULL;
		struct kevent evt[RECV_EVENT_MAX];
		int nevt, i;

		if (nchange > 0)
			change_evt = change_evt0;

		nevt = kevent(kq, change_evt, nchange, evt, RECV_EVENT_MAX,
		    NULL);
		if (nevt < 0)
			err(1, "kevent failed");
		nchange = 0;

		for (i = 0; i < nevt; ++i) {
			int n;

			if (evt[i].ident == (u_int)s) {
				while (nchange < RECV_EVENT_MAX) {
					int s1;

					s1 = accept(s, NULL, NULL);
					if (s1 < 0)
						break;

					/* TODO: keepalive */

					n = write(s1, &ack, sizeof(ack));
					if (n != sizeof(ack)) {
						close(s1);
						continue;
					}

					EV_SET(&change_evt0[nchange], s1,
					    EVFILT_READ, EV_ADD, 0, 0, NULL);
					++nchange;
				}
			} else {
				n = read(evt[i].ident, buf, recv_buflen);
				if (n <= 0) {
					if (n == 0 || errno != EAGAIN)
						close(evt[i].ident);
				}
			}
		}
	}

	/* NEVER REACHED */
	return NULL;
}
