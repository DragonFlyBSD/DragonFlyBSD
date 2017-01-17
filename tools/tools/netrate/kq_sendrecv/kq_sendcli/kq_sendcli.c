#include <sys/param.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <machine/atomic.h>
#ifdef __FreeBSD__
#include <machine/cpu.h>
#endif
#include <machine/cpufunc.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kq_sendrecv_proto.h"

/*
 * Note about the sender start synchronization.
 *
 * We apply two stage synchronization.  The first stage uses pthread
 * condition (it sleeps), which waits for the establishment for all
 * connections, which could be slow.  The second stage uses g_nwait
 * of send_globctx; all relevant threads spin on g_nwait.  The main
 * thread spin-waits for all senders to increase g_nwait.  The sender
 * thread increases the g_nwait, then it spin-waits for main thread
 * to reset g_nwait.  In this way, we can make sure that all senders
 * start roughly at the same time.
 */

#ifndef timespecsub
#define timespecsub(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec -= (uvp)->tv_sec;				\
		(vvp)->tv_nsec -= (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)
#endif

#ifndef timespeccmp
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#endif

#if 0
#define SEND_DEBUG
#endif
#if 0
#define SEND_TIME_DEBUG
#endif

#define SEND_DUR		10
#define SEND_EVENT_MAX		64
#define SEND_BUFLEN		(128 * 1024)

/*
 * The successful 3-way handshake on the connection does not mean the
 * remote application can accept(2) this connection.  Even worse, the
 * remote side's network stack may drop the connection silently, i.e.
 * w/o RST.  If this happened, the blocking read(2) would not return,
 * until the keepalive kicked in, which would take quite some time.
 * This is obviously not what we want here, so use synthetic timeout
 * for blocking read(2).  Here, we will retry if a blocking read(2)
 * times out.
 */
#define SEND_READTO_MS		1000		/* unit: ms */

#if defined(__DragonFly__)
#define SEND_CONN_CTX_ALIGN	__VM_CACHELINE_SIZE
#elif defined(__FreeBSD__)
#define SEND_CONN_CTX_ALIGN	CACHE_LINE_SIZE
#else
#define SEND_CONN_CTX_ALIGN	64	/* XXX */
#endif

struct conn_ctx {
	int			c_s;
	int			c_err;
	uint64_t		c_stat;
	struct timespec		c_terr;

	STAILQ_ENTRY(conn_ctx)	c_glob_link;
	STAILQ_ENTRY(conn_ctx)	c_link;
	struct sockaddr_in	c_in;
	int			c_thr_id;
} __aligned(SEND_CONN_CTX_ALIGN);

STAILQ_HEAD(conn_ctx_list, conn_ctx);

struct send_globctx {
	struct conn_ctx_list	g_conn;

	int			g_dur;
	int			g_nconn;
	pthread_mutex_t		g_lock;
	pthread_cond_t		g_cond;

	volatile u_int		g_nwait;
	int			g_readto_ms;	/* unit: ms */
	bool			g_sendfile;
};

struct send_thrctx {
	struct conn_ctx_list	t_conn;
	pthread_mutex_t		t_lock;
	pthread_cond_t		t_cond;

	struct send_globctx	*t_glob;
	struct timespec		t_start;
	struct timespec		t_end;
	double			t_run_us;	/* unit: us */

	pthread_t		t_tid;
	int			t_id;
};

static void	send_build_addrlist(const struct sockaddr_in *, int,
		    const struct sockaddr_in **, int *, int);
static void	*send_thread(void *);

static __inline void
send_spinwait(void)
{
#if defined(__DragonFly__)
	cpu_pause();
#elif defined(__FreeBSD__)
	cpu_spinwait();
#else
	/* XXX nothing */
#endif
}

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -4 addr4 [-4 addr4 ...] [-p port] "
	    "-c conns [-t nthreads] [-l sec] [-r readto_ms] [-S] [-E]\n", cmd);
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct send_globctx glob;
	struct send_thrctx *ctx_arr, *ctx;
	struct sockaddr_in *in_arr, *in;
	const struct sockaddr_in *daddr;
	struct timespec run, end, start;
	double total_run_us, total, conn_min, conn_max;
	double jain, jain_res;
	int jain_cnt;
	struct conn_ctx *conn;
	sigset_t sigset;
	int opt, i;
	int in_arr_cnt, in_arr_sz, ndaddr;
	int nthr, nconn, dur, readto_ms;
	int log_err, err_cnt, has_minmax;
	u_short port = RECV_PORT;
	uint32_t idx;
	size_t sz;
	bool do_sendfile = false;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGPIPE);
	if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0)
		err(1, "sigprocmask failed");

	sz = sizeof(nthr);
	if (sysctlbyname("hw.ncpu", &nthr, &sz, NULL, 0) < 0)
		err(1, "sysctl hw.ncpu failed");

	in_arr_sz = 4;
	in_arr_cnt = 0;
	in_arr = malloc(in_arr_sz * sizeof(struct sockaddr_in));
	if (in_arr == NULL)
		err(1, "malloc failed");

	log_err = 0;
	nconn = 0;
	dur = SEND_DUR;
	readto_ms = SEND_READTO_MS;

	while ((opt = getopt(argc, argv, "4:ESc:l:p:r:t:")) != -1) {
		switch (opt) {
		case '4':
			if (in_arr_cnt == in_arr_sz) {
				in_arr_sz *= 2;
				in_arr = reallocf(in_arr,
				    in_arr_sz * sizeof(struct sockaddr_in));
				if (in_arr == NULL)
					err(1, "reallocf failed");
			}
			in = &in_arr[in_arr_cnt];
			++in_arr_cnt;

			memset(in, 0, sizeof(*in));
			in->sin_family = AF_INET;
			if (inet_pton(AF_INET, optarg, &in->sin_addr) <= 0)
				errx(1, "inet_pton failed %s", optarg);
			break;

		case 'E':
			log_err = 1;
			break;

		case 'S':
			do_sendfile = true;
			break;

		case 'c':
			nconn = strtol(optarg, NULL, 10);
			if (nconn <= 0)
				errx(1, "invalid -c");
			break;

		case 'l':
			dur = strtoul(optarg, NULL, 10);
			if (dur == 0)
				errx(1, "invalid -l");
			break;

		case 'p':
			port = strtoul(optarg, NULL, 10);
			break;

		case 'r':
			readto_ms = strtol(optarg, NULL, 10);
			if (readto_ms <= 0)
				errx(1, "invalid -r");
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
	if (in_arr_cnt == 0 || nconn == 0)
		errx(1, "either -4 or -c are specified");

	if (nthr > nconn)
		nthr = nconn;

	for (i = 0; i < in_arr_cnt; ++i)
		in_arr[i].sin_port = htons(port);

	ctx_arr = calloc(nthr, sizeof(struct send_thrctx));
	if (ctx_arr == NULL)
		err(1, "calloc failed");

	memset(&glob, 0, sizeof(glob));
	STAILQ_INIT(&glob.g_conn);
	glob.g_nconn = nconn;
	glob.g_nwait = 1; /* count self in */
	glob.g_dur = dur;
	glob.g_readto_ms = readto_ms;
	glob.g_sendfile = do_sendfile;
	pthread_mutex_init(&glob.g_lock, NULL);
	pthread_cond_init(&glob.g_cond, NULL);

	pthread_set_name_np(pthread_self(), "main");

	/* Build receiver address list */
	send_build_addrlist(in_arr, in_arr_cnt, &daddr, &ndaddr, readto_ms);

	/*
	 * Start senders.
	 */
	for (i = 0; i < nthr; ++i) {
		int error;

		ctx = &ctx_arr[i];
		STAILQ_INIT(&ctx->t_conn);
		ctx->t_id = i;
		ctx->t_glob = &glob;
		pthread_mutex_init(&ctx->t_lock, NULL);
		pthread_cond_init(&ctx->t_cond, NULL);

		error = pthread_create(&ctx->t_tid, NULL, send_thread, ctx);
		if (error)
			errc(1, error, "pthread_create failed");
	}

	/*
	 * Distribute connections to senders.
	 *
	 * NOTE:
	 * We start from a random position in the address list, so that the
	 * first several receiving servers will not be abused, if the number
	 * of connections is small and there are many clients.
	 */
	idx = arc4random_uniform(ndaddr);
	for (i = 0; i < nconn; ++i) {
		const struct sockaddr_in *da;

		da = &daddr[idx % ndaddr];
		++idx;

		conn = aligned_alloc(SEND_CONN_CTX_ALIGN, sizeof(*conn));
		if (conn == NULL)
			err(1, "aligned_alloc failed");
		memset(conn, 0, sizeof(*conn));
		conn->c_in = *da;
		conn->c_s = -1;

		ctx = &ctx_arr[i % nthr];
		conn->c_thr_id = ctx->t_id;

		pthread_mutex_lock(&ctx->t_lock);
		STAILQ_INSERT_TAIL(&ctx->t_conn, conn, c_link);
		pthread_mutex_unlock(&ctx->t_lock);
		pthread_cond_signal(&ctx->t_cond);

		/* Add to the global list for results gathering */
		STAILQ_INSERT_TAIL(&glob.g_conn, conn, c_glob_link);
	}

	/*
	 * No more connections; notify the senders.
	 *
	 * NOTE:
	 * The marker for 'the end of connection list' has 0 in its
	 * c_in.sin_port.
	 */
	for (i = 0; i < nthr; ++i) {
		conn = aligned_alloc(SEND_CONN_CTX_ALIGN, sizeof(*conn));
		if (conn == NULL)
			err(1, "aligned_alloc failed");
		memset(conn, 0, sizeof(*conn));
		conn->c_s = -1;

		ctx = &ctx_arr[i];
		pthread_mutex_lock(&ctx->t_lock);
		STAILQ_INSERT_TAIL(&ctx->t_conn, conn, c_link);
		pthread_mutex_unlock(&ctx->t_lock);
		pthread_cond_signal(&ctx->t_cond);
	}

	/*
	 * Sender start sync, stage 1:
	 * Wait for connections establishment (slow).
	 */
	pthread_mutex_lock(&glob.g_lock);
	while (glob.g_nconn != 0)
		pthread_cond_wait(&glob.g_cond, &glob.g_lock);
	pthread_mutex_unlock(&glob.g_lock);

	/*
	 * Sender start sync, stage 2:
	 * Wait for senders to spin-wait; and once all senders spin-wait,
	 * release them by resetting g_nwait.
	 */
	while (atomic_cmpset_int(&glob.g_nwait, nthr + 1, 0) == 0)
		send_spinwait();

	fprintf(stderr, "start %d seconds sending test: %d threads, "
	    "%d connections\n", dur, nthr, nconn);

	/*
	 * Wait for the senders to finish and gather the results.
	 */

	memset(&end, 0, sizeof(end));		/* XXX stupid gcc warning */
	memset(&start, 0, sizeof(start));	/* XXX stupid gcc warning */

	for (i = 0; i < nthr; ++i) {
		ctx = &ctx_arr[i];
		pthread_join(ctx->t_tid, NULL);

		run = ctx->t_end;
		timespecsub(&run, &ctx->t_start);
		ctx->t_run_us = ((double)run.tv_sec * 1000000.0) +
		    ((double)run.tv_nsec / 1000.0);

		if (i == 0) {
			start = ctx->t_start;
			end = ctx->t_end;
		} else {
			if (timespeccmp(&start, &ctx->t_start, >))
				start = ctx->t_start;
			if (timespeccmp(&end, &ctx->t_end, <))
				end = ctx->t_end;
		}

#ifdef SEND_TIME_DEBUG
		fprintf(stderr, "start %ld.%ld, end %ld.%ld\n",
		    ctx->t_start.tv_sec, ctx->t_start.tv_nsec,
		    ctx->t_end.tv_sec, ctx->t_end.tv_nsec);
#endif
	}

#ifdef SEND_TIME_DEBUG
	fprintf(stderr, "start %ld.%ld, end %ld.%ld (final)\n",
	    start.tv_sec, start.tv_nsec, end.tv_sec, end.tv_nsec);
#endif

	run = end;
	timespecsub(&run, &start);
	total_run_us = ((double)run.tv_sec * 1000000.0) +
	    ((double)run.tv_nsec / 1000.0);
	total = 0.0;

	err_cnt = 0;
	has_minmax = 0;
	conn_min = 0.0;
	conn_max = 0.0;

	jain = 0.0;
	jain_res = 0.0;
	jain_cnt = 0;

	STAILQ_FOREACH(conn, &glob.g_conn, c_glob_link) {
		total += conn->c_stat;
		if (conn->c_err == 0) {
			double perf;	/* unit: Mbps */

			perf = (conn->c_stat * 8.0) /
			    ctx_arr[conn->c_thr_id].t_run_us;
			if (!has_minmax) {
				conn_min = perf;
				conn_max = perf;
				has_minmax = 1;
			} else {
				if (perf > conn_max)
					conn_max = perf;
				if (perf < conn_min)
					conn_min = perf;
			}
			jain += (perf * perf);
			jain_res += perf;
			++jain_cnt;
		} else {
			++err_cnt;
		}
	}

	jain *= jain_cnt;
	jain = (jain_res * jain_res) / jain;

	printf("Total: %.2lf Mbps, min/max %.2lf Mbps/%.2lf Mbps, jain %.2lf, "
	    "error %d\n", (total * 8.0) / total_run_us, conn_min, conn_max,
	    jain, err_cnt);

	if (log_err && err_cnt) {
		STAILQ_FOREACH(conn, &glob.g_conn, c_glob_link) {
			char name[INET_ADDRSTRLEN];
			double tmp_run;

			if (conn->c_err == 0)
				continue;

			run = conn->c_terr;
			timespecsub(&run, &ctx_arr[conn->c_thr_id].t_start);
			tmp_run = ((double)run.tv_sec * 1000000.0) +
			    ((double)run.tv_nsec / 1000.0);
			fprintf(stderr, "snd%d ->%s:%d, %ld sec, %.2lf Mbps, "
			    "errno %d\n",
			    conn->c_thr_id,
			    inet_ntop(AF_INET, &conn->c_in.sin_addr,
			        name, sizeof(name)),
			    ntohs(conn->c_in.sin_port),
			    run.tv_sec, (conn->c_stat * 8.0) / tmp_run,
			    conn->c_err);
			--err_cnt;
			if (err_cnt == 0)
				break;
		}
	}

	exit(0);
}

static void
send_build_addrlist(const struct sockaddr_in *in_arr, int in_arr_cnt,
    const struct sockaddr_in **daddr0, int *ndaddr0, int readto_ms)
{
	struct sockaddr_in *daddr;
	struct timeval readto;
	int i, ndaddr;

	daddr = NULL;
	ndaddr = 0;

	memset(&readto, 0, sizeof(readto));
	readto.tv_sec = readto_ms / 1000;
	readto.tv_usec = (readto_ms % 1000) * 1000;

	for (i = 0; i < in_arr_cnt; ++i) {
		const struct sockaddr_in *in = &in_arr[i];
		struct recv_info info_hdr;
		uint16_t *ports;
		int s, n, ports_sz, d;

again:
		s = socket(AF_INET, SOCK_STREAM, 0);
		if (s < 0)
			err(1, "socket failed");

		if (connect(s, (const struct sockaddr *)in, sizeof(*in)) < 0)
			err(1, "connect failed");

		if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
		    &readto, sizeof(readto)) < 0)
			err(1, "setsockopt(RCVTIMEO) failed");

		n = read(s, &info_hdr, sizeof(info_hdr));
		if (n != sizeof(info_hdr)) {
			if (n < 0) {
				if (errno == EAGAIN) {
					close(s);
					goto again;
				}
				err(1, "read info hdr failed");
			} else {
				errx(1, "read truncated info hdr");
			}
		}
		if (info_hdr.ndport == 0) {
			close(s);
			continue;
		}

		ports_sz = info_hdr.ndport * sizeof(uint16_t);
		ports = malloc(ports_sz);
		if (ports == NULL)
			err(1, "malloc failed");

		n = read(s, ports, ports_sz);
		if (n != ports_sz) {
			if (n < 0) {
				if (errno == EAGAIN) {
					free(ports);
					close(s);
					goto again;
				}
				err(1, "read ports failed");
			} else {
				errx(1, "read truncated ports");
			}
		}

		daddr = reallocf(daddr,
		    (ndaddr + info_hdr.ndport) * sizeof(struct sockaddr_in));
		if (daddr == NULL)
			err(1, "reallocf failed");

		for (d = ndaddr; d < ndaddr + info_hdr.ndport; ++d) {
			struct sockaddr_in *da = &daddr[d];

			*da = *in;
			da->sin_port = ports[d - ndaddr];
		}
		ndaddr += info_hdr.ndport;

		free(ports);
		close(s);
	}

#ifdef SEND_DEBUG
	for (i = 0; i < ndaddr; ++i) {
		const struct sockaddr_in *da = &daddr[i];
		char name[INET_ADDRSTRLEN];

		fprintf(stderr, "%s:%d\n",
		    inet_ntop(AF_INET, &da->sin_addr, name, sizeof(name)),
		    ntohs(da->sin_port));
	}
#endif

	*daddr0 = daddr;
	*ndaddr0 = ndaddr;
}

static void *
send_thread(void *xctx)
{
	struct send_thrctx *ctx = xctx;
	struct conn_ctx *timeo;
	struct kevent chg_evt;
	uint8_t *buf;
	int nconn = 0, kq, n, fd = -1;
	char name[32];

	snprintf(name, sizeof(name), "snd%d", ctx->t_id);
	pthread_set_name_np(pthread_self(), name);

	buf = malloc(SEND_BUFLEN);
	if (buf == NULL)
		err(1, "malloc failed");

	if (ctx->t_glob->g_sendfile) {
		char filename[] = "sendtmpXXX";

		fd = mkstemp(filename);
		if (fd < 0)
			err(1, "mkstemp failed");
		if (write(fd, buf, SEND_BUFLEN) != SEND_BUFLEN)
			err(1, "write to file failed");
		unlink(filename);
		free(buf);
		buf = NULL;
	}

	kq = kqueue();
	if (kq < 0)
		err(1, "kqueue failed");

	/*
	 * Establish the connections assigned to us and add the
	 * established connections to kqueue.
	 */
	for (;;) {
#ifdef SEND_DEBUG
		char addr_name[INET_ADDRSTRLEN];
#endif
		struct timeval readto;
		struct conn_ctx *conn;
		struct conn_ack ack;
		int on;

		pthread_mutex_lock(&ctx->t_lock);
		while (STAILQ_EMPTY(&ctx->t_conn))
			pthread_cond_wait(&ctx->t_cond, &ctx->t_lock);
		conn = STAILQ_FIRST(&ctx->t_conn);
		STAILQ_REMOVE_HEAD(&ctx->t_conn, c_link);
		pthread_mutex_unlock(&ctx->t_lock);

		if (conn->c_in.sin_port == 0) {
			/*
			 * The marker for 'the end of connection list'.
			 * See the related comment in main thread.
			 *
			 * NOTE:
			 * We reuse the marker as the udata for the
			 * kqueue timer.
			 */
			timeo = conn;
			break;
		}

		++nconn;
#ifdef SEND_DEBUG
		fprintf(stderr, "%s %s:%d\n", name,
		    inet_ntop(AF_INET, &conn->c_in.sin_addr,
		        addr_name, sizeof(addr_name)),
		    ntohs(conn->c_in.sin_port));
#endif

again:
		conn->c_s = socket(AF_INET, SOCK_STREAM, 0);
		if (conn->c_s < 0)
			err(1, "socket failed");

		if (connect(conn->c_s, (const struct sockaddr *)&conn->c_in,
		    sizeof(conn->c_in)) < 0)
			err(1, "connect failed");

		memset(&readto, 0, sizeof(readto));
		readto.tv_sec = ctx->t_glob->g_readto_ms / 1000;
		readto.tv_usec = (ctx->t_glob->g_readto_ms % 1000) * 1000;
		if (setsockopt(conn->c_s, SOL_SOCKET, SO_RCVTIMEO, &readto,
		    sizeof(readto)) < 0)
			err(1, "setsockopt(RCVTIMEO) failed");

		n = read(conn->c_s, &ack, sizeof(ack));
		if (n != sizeof(ack)) {
			if (n < 0) {
				if (errno == EAGAIN) {
					close(conn->c_s);
					goto again;
				}
				err(1, "read ack failed");
			} else {
				errx(1, "read truncated ack");
			}
		}

		on = 1;
		if (ioctl(conn->c_s, FIONBIO, &on, sizeof(on)) < 0)
			err(1, "ioctl(FIONBIO) failed");

		EV_SET(&chg_evt, conn->c_s, EVFILT_WRITE, EV_ADD, 0, 0, conn);
		n = kevent(kq, &chg_evt, 1, NULL, 0, NULL);
		if (n < 0)
			err(1, "kevent add failed");
	}
#ifdef SEND_DEBUG
	fprintf(stderr, "%s conn %d\n", name, nconn);
#endif

	/*
	 * Sender start sync, stage 1:
	 * Wait for connections establishment (slow).
	 */
	pthread_mutex_lock(&ctx->t_glob->g_lock);
	ctx->t_glob->g_nconn -= nconn;
	pthread_cond_broadcast(&ctx->t_glob->g_cond);
	while (ctx->t_glob->g_nconn != 0)
		pthread_cond_wait(&ctx->t_glob->g_cond, &ctx->t_glob->g_lock);
	pthread_mutex_unlock(&ctx->t_glob->g_lock);

	/*
	 * Sender start sync, stage2.
	 */
	/* Increase the g_nwait. */
	atomic_add_int(&ctx->t_glob->g_nwait, 1);
	/* Spin-wait for main thread to release us (reset g_nwait). */
	while (ctx->t_glob->g_nwait)
		send_spinwait();

#ifdef SEND_DEBUG
	fprintf(stderr, "%s start\n", name);
#endif

	/*
	 * Wire a kqueue timer, so that the sending can be terminated
	 * as requested.
	 *
	 * NOTE:
	 * Set -2 to c_s for timer udata, so we could distinguish it
	 * from real connections.
	 */
	timeo->c_s = -2;
	EV_SET(&chg_evt, 0, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0,
	    ctx->t_glob->g_dur * 1000L, timeo);
	n = kevent(kq, &chg_evt, 1, NULL, 0, NULL);
	if (n < 0)
		err(1, "kevent add failed");

	clock_gettime(CLOCK_MONOTONIC_PRECISE, &ctx->t_start);
	for (;;) {
		struct kevent evt[SEND_EVENT_MAX];
		int nevt, i;

		nevt = kevent(kq, NULL, 0, evt, SEND_EVENT_MAX, NULL);
		if (nevt < 0)
			err(1, "kevent failed");

		for (i = 0; i < nevt; ++i) {
			struct conn_ctx *conn = evt[i].udata;

			if (conn->c_s < 0) {
				if (conn->c_s == -2) {
					/* Timer expired */
					goto done;
				}
				continue;
			}

			if (fd >= 0) {
				off_t m, off;
				size_t len;

				off = conn->c_stat % SEND_BUFLEN;
				len = SEND_BUFLEN - off;

				n = sendfile(fd, conn->c_s, off, len, NULL,
				    &m, 0);
				if (n == 0 || (n < 0 && errno == EAGAIN))
					n = m;
			} else {
				n = write(conn->c_s, buf, SEND_BUFLEN);
			}

			if (n < 0) {
				if (errno != EAGAIN) {
					conn->c_err = errno;
					clock_gettime(CLOCK_MONOTONIC_PRECISE,
					    &conn->c_terr);
					close(conn->c_s);
					conn->c_s = -1;
				}
			} else {
				conn->c_stat += n;
			}
		}
	}
done:
	clock_gettime(CLOCK_MONOTONIC_PRECISE, &ctx->t_end);

	if (fd >= 0)
		close(fd);
	if (buf != NULL)
		free(buf);
	return NULL;
}
