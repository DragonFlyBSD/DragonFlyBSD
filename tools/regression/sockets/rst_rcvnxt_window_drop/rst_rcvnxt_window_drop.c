/*-
 * Regression test for TCP RST acceptance when delayed ACK state makes
 * RCV.NXT lie beyond last_ack_sent + rcv_wnd.
 *
 * Requires root: uses BPF to observe the client's data sequence number and a
 * raw IPv4 socket to inject the final RST at exactly RCV.NXT.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <net/bpf.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef TH_FLAGS
#define TH_FLAGS (TH_FIN|TH_SYN|TH_RST|TH_PUSH|TH_ACK|TH_URG)
#endif

#define LO_IFNAME "lo0"
#define RCVBUF 1200
#define DATALEN 900

struct tuple {
	struct in_addr src, dst;
	uint16_t sport, dport, win;
	uint32_t seq, ack;
};

static int
xsysctl(const char *name, int value, int *oldp)
{
	char cmd[128], buf[32];
	FILE *fp;

	snprintf(cmd, sizeof(cmd), "sysctl -n %s", name);
	fp = popen(cmd, "r");
	if (fp == NULL)
		return (-1);
	if (fgets(buf, sizeof(buf), fp) == NULL) {
		pclose(fp);
		return (-1);
	}
	if (pclose(fp) != 0)
		return (-1);
	*oldp = atoi(buf);

	snprintf(cmd, sizeof(cmd), "sysctl -q %s=%d", name, value);
	return (system(cmd));
}

static void
restore_sysctls(int rfc1323, int delayed_ack)
{
	char cmd[160];

	snprintf(cmd, sizeof(cmd),
	    "sysctl -q net.inet.tcp.rfc1323=%d net.inet.tcp.delayed_ack=%d",
	    rfc1323, delayed_ack);
	(void)system(cmd);
}

static uint16_t
cksum(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint32_t sum = 0;

	while (len > 1) {
		sum += (uint16_t)((p[0] << 8) | p[1]);
		p += 2;
		len -= 2;
	}
	if (len)
		sum += (uint16_t)(p[0] << 8);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return ((uint16_t)~sum);
}

static uint16_t
tcp_cksum(const struct ip *ip, const struct tcphdr *tcp)
{
	struct {
		struct in_addr src, dst;
		uint8_t zero, proto;
		uint16_t len;
		struct tcphdr tcp;
	} p;

	memset(&p, 0, sizeof(p));
	p.src = ip->ip_src;
	p.dst = ip->ip_dst;
	p.proto = IPPROTO_TCP;
	p.len = htons(sizeof(*tcp));
	p.tcp = *tcp;
	return (cksum(&p, sizeof(p)));
}

static int
open_bpf(int *ipoffp, u_int *blenp)
{
	char path[16];
	struct ifreq ifr;
	struct timeval tv;
	u_int one = 1;
	int fd, dlt, i;

	for (i = 0; i < 256; i++) {
		snprintf(path, sizeof(path), "/dev/bpf%d", i);
		fd = open(path, O_RDONLY);
		if (fd >= 0)
			break;
		if (errno != EBUSY && errno != ENOENT)
			err(1, "open(%s)", path);
	}
	if (i == 256)
		errx(1, "no free /dev/bpf device");

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, LO_IFNAME, sizeof(ifr.ifr_name));
	if (ioctl(fd, BIOCSETIF, &ifr) < 0)
		err(1, "BIOCSETIF(%s)", LO_IFNAME);
	if (ioctl(fd, BIOCIMMEDIATE, &one) < 0)
		err(1, "BIOCIMMEDIATE");
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	if (ioctl(fd, BIOCSRTIMEOUT, &tv) < 0)
		err(1, "BIOCSRTIMEOUT");
	if (ioctl(fd, BIOCGBLEN, blenp) < 0)
		err(1, "BIOCGBLEN");
	if (ioctl(fd, BIOCGDLT, &dlt) < 0)
		err(1, "BIOCGDLT");

	if (dlt == DLT_NULL)
		*ipoffp = 4;
#ifdef DLT_LOOP
	else if (dlt == DLT_LOOP)
		*ipoffp = 4;
#endif
#ifdef DLT_RAW
	else if (dlt == DLT_RAW)
		*ipoffp = 0;
#endif
	else
		errx(1, "unsupported BPF DLT %d on %s", dlt, LO_IFNAME);

	return (fd);
}

static int
listener(uint16_t *portp)
{
	struct sockaddr_in sin;
	socklen_t slen = sizeof(sin);
	int s, one = 1, rcvbuf = RCVBUF;

	s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0)
		err(1, "socket");
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
		err(1, "SO_REUSEADDR");
	if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0)
		err(1, "SO_RCVBUF");

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "bind");
	if (listen(s, 1) < 0)
		err(1, "listen");
	if (getsockname(s, (struct sockaddr *)&sin, &slen) < 0)
		err(1, "getsockname");
	*portp = ntohs(sin.sin_port);
	return (s);
}

static void
client(uint16_t port, int gofd)
{
	struct sockaddr_in sin;
	char go, buf[DATALEN];
	int s;

	memset(buf, 'x', sizeof(buf));
	s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0)
		err(1, "client socket");

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(port);
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "connect");
	if (read(gofd, &go, 1) != 1)
		err(1, "sync read");
	if (write(s, buf, sizeof(buf)) != sizeof(buf))
		err(1, "client write");

	/* Keep the real socket from sending its own RST/FIN during the test. */
	sleep(3);
	close(s);
	_exit(0);
}

static int
parse_data(const uint8_t *p, size_t n, int ipoff, uint16_t dport,
    struct tuple *t)
{
	const struct ip *ip;
	const struct tcphdr *th;
	size_t ihl, thl, iplen;

	if (n < (size_t)ipoff + sizeof(*ip))
		return (0);
	ip = (const struct ip *)(const void *)(p + ipoff);
	if (ip->ip_v != 4 || ip->ip_p != IPPROTO_TCP)
		return (0);
	ihl = (size_t)ip->ip_hl << 2;
	if (n < (size_t)ipoff + ihl + sizeof(*th))
		return (0);
	th = (const struct tcphdr *)(const void *)(p + ipoff + ihl);
	thl = (size_t)th->th_off << 2;
	iplen = ntohs(ip->ip_len);
	if (iplen < ihl + thl + DATALEN)
		return (0);
	if (ntohs(th->th_dport) != dport)
		return (0);
	if ((th->th_flags & TH_ACK) == 0)
		return (0);
	if (iplen - ihl - thl != DATALEN)
		return (0);

	t->src = ip->ip_src;
	t->dst = ip->ip_dst;
	t->sport = ntohs(th->th_sport);
	t->dport = ntohs(th->th_dport);
	t->seq = ntohl(th->th_seq);
	t->ack = ntohl(th->th_ack);
	t->win = ntohs(th->th_win);
	return (1);
}

static void
capture_data(int bpf, u_int blen, int ipoff, uint16_t port, struct tuple *t)
{
	struct bpf_hdr *bh;
	uint8_t *buf, *p, *end;
	ssize_t n;

	buf = malloc(blen);
	if (buf == NULL)
		err(1, "malloc");

	for (;;) {
		n = read(bpf, buf, blen);
		if (n < 0)
			err(1, "read bpf");
		if (n == 0)
			errx(1, "timed out waiting for data packet");
		p = buf;
		end = buf + n;
		while (p + sizeof(*bh) <= end) {
			bh = (struct bpf_hdr *)(void *)p;
			if (p + bh->bh_hdrlen + bh->bh_caplen > end)
				break;
			if (parse_data(p + bh->bh_hdrlen, bh->bh_caplen,
			    ipoff, port, t)) {
				free(buf);
				return;
			}
			p += BPF_WORDALIGN(bh->bh_hdrlen + bh->bh_caplen);
		}
	}
}

static void
inject_rst(const struct tuple *t)
{
	struct sockaddr_in dst;
	struct {
		struct ip ip;
		struct tcphdr th;
	} pkt;
	int s, one = 1;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ip.ip_v = 4;
	pkt.ip.ip_hl = sizeof(struct ip) >> 2;
	pkt.ip.ip_len = htons(sizeof(pkt));
	pkt.ip.ip_id = htons(0x5253);
	pkt.ip.ip_ttl = 64;
	pkt.ip.ip_p = IPPROTO_TCP;
	pkt.ip.ip_src = t->src;
	pkt.ip.ip_dst = t->dst;
	pkt.ip.ip_sum = cksum(&pkt.ip, sizeof(pkt.ip));

	pkt.th.th_sport = htons(t->sport);
	pkt.th.th_dport = htons(t->dport);
	pkt.th.th_seq = htonl(t->seq + DATALEN);
	pkt.th.th_ack = htonl(t->ack);
	pkt.th.th_off = sizeof(struct tcphdr) >> 2;
	pkt.th.th_flags = TH_RST | TH_ACK;
	pkt.th.th_win = htons(t->win);
	pkt.th.th_sum = tcp_cksum(&pkt.ip, &pkt.th);

	s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
	if (s < 0)
		err(1, "raw socket");
	if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0)
		err(1, "IP_HDRINCL");

	memset(&dst, 0, sizeof(dst));
	dst.sin_len = sizeof(dst);
	dst.sin_family = AF_INET;
	dst.sin_addr = t->dst;
	if (sendto(s, &pkt, sizeof(pkt), 0,
	    (struct sockaddr *)&dst, sizeof(dst)) != sizeof(pkt))
		err(1, "sendto rst");
	close(s);
}

int
main(void)
{
	struct tuple t;
	uint16_t port;
	u_int blen;
	int old_rfc1323, old_delack, bpf, ipoff, ls, cs, pfd[2], flags, st;
	pid_t pid;
	char go = 1, buf[DATALEN];
	ssize_t n;

	if (geteuid() != 0)
		errx(1, "must be run as root");

	if (xsysctl("net.inet.tcp.rfc1323", 0, &old_rfc1323) != 0 ||
	    xsysctl("net.inet.tcp.delayed_ack", 1, &old_delack) != 0)
		errx(1, "failed to set required TCP sysctls");

	bpf = open_bpf(&ipoff, &blen);
	ls = listener(&port);
	if (pipe(pfd) < 0)
		err(1, "pipe");

	pid = fork();
	if (pid < 0)
		err(1, "fork");
	if (pid == 0) {
		close(pfd[1]);
		client(port, pfd[0]);
	}

	close(pfd[0]);
	cs = accept(ls, NULL, NULL);
	if (cs < 0)
		err(1, "accept");
	flags = fcntl(cs, F_GETFL, 0);
	if (flags < 0 || fcntl(cs, F_SETFL, flags | O_NONBLOCK) < 0)
		err(1, "nonblock");

	if (write(pfd[1], &go, 1) != 1)
		err(1, "sync write");
	close(pfd[1]);

	capture_data(bpf, blen, ipoff, port, &t);
	inject_rst(&t);
	usleep(20000);

	n = read(cs, buf, sizeof(buf));
	if (n != DATALEN) {
		fprintf(stderr, "FAIL: first read %zd, expected %d: %s\n",
		    n, DATALEN, strerror(errno));
		goto fail;
	}

	n = read(cs, buf, sizeof(buf));
	if (n < 0 && errno == ECONNRESET) {
		fprintf(stderr, "PASS\n");
		restore_sysctls(old_rfc1323, old_delack);
		kill(pid, SIGTERM);
		waitpid(pid, &st, 0);
		return (0);
	}

	if (n < 0)
		fprintf(stderr, "FAIL: second read got %s, expected ECONNRESET\n",
		    strerror(errno));
	else
		fprintf(stderr, "FAIL: second read returned %zd\n", n);

fail:
	restore_sysctls(old_rfc1323, old_delack);
	kill(pid, SIGTERM);
	waitpid(pid, &st, 0);
	return (1);
}
