/*
 * Copyright (c) 2014 - 2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <sysexits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <timeconv.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/ethernet.h>

#include <net/ipfw3/ip_fw3.h>
#include <net/ipfw3_basic/ip_fw3_table.h>
#include <net/ipfw3_basic/ip_fw3_sync.h>
#include <net/ipfw3_basic/ip_fw3_basic.h>
#include <net/ipfw3_nat/ip_fw3_nat.h>
#include <net/dummynet3/ip_dummynet3.h>

#include "ipfw3.h"
#include "ipfw3dummynet.h"


extern int verbose;
extern int do_time;
extern int do_quiet;
extern int do_force;
extern int do_pipe;
extern int do_sort;


struct char_int_map dummynet_params[] = {
	{ "plr", 		TOK_PLR },
	{ "noerror", 		TOK_NOERROR },
	{ "buckets", 		TOK_BUCKETS },
	{ "dst-ip", 		TOK_DSTIP },
	{ "src-ip", 		TOK_SRCIP },
	{ "dst-port", 		TOK_DSTPORT },
	{ "src-port", 		TOK_SRCPORT },
	{ "proto", 		TOK_PROTO },
	{ "weight", 		TOK_WEIGHT },
	{ "all", 		TOK_ALL },
	{ "mask", 		TOK_MASK },
	{ "droptail", 		TOK_DROPTAIL },
	{ "red", 		TOK_RED },
	{ "gred", 		TOK_GRED },
	{ "bw",			TOK_BW },
	{ "bandwidth", 		TOK_BW },
	{ "delay", 		TOK_DELAY },
	{ "pipe", 		TOK_PIPE },
	{ "queue", 		TOK_QUEUE },
	{ "dummynet-params", 	TOK_NULL },
	{ NULL, 0 }
};


int
sort_q(const void *pa, const void *pb)
{
	int rev = (do_sort < 0);
	int field = rev ? -do_sort : do_sort;
	long long res = 0;
	const struct dn_ioc_flowqueue *a = pa;
	const struct dn_ioc_flowqueue *b = pb;

	switch(field) {
	case 1: /* pkts */
		res = a->len - b->len;
		break;
	case 2: /* bytes */
		res = a->len_bytes - b->len_bytes;
		break;

	case 3: /* tot pkts */
		res = a->tot_pkts - b->tot_pkts;
		break;

	case 4: /* tot bytes */
		res = a->tot_bytes - b->tot_bytes;
		break;
	}
	if (res < 0)
		res = -1;
	if (res > 0)
		res = 1;
	return (int)(rev ? res : -res);
}



/*
 * config dummynet pipe/queue
 */
void
config_dummynet(int ac, char **av)
{
	struct dn_ioc_pipe pipe;
	u_int32_t a;
	void *par = NULL;
	int i;
	char *end;

	NEXT_ARG;
	memset(&pipe, 0, sizeof pipe);
	/* Pipe number */
	if (ac && isdigit(**av)) {
		i = atoi(*av);
		NEXT_ARG;
		if (do_pipe == 1)
			pipe.pipe_nr = i;
		else
			pipe.fs.fs_nr = i;
	}

	while (ac > 0) {
		double d;

		int tok = match_token(dummynet_params, *av);
		NEXT_ARG;

		switch(tok) {
		case TOK_NOERROR:
			pipe.fs.flags_fs |= DN_NOERROR;
			break;

		case TOK_PLR:
			NEED1("plr needs argument 0..1\n");
			d = strtod(av[0], NULL);
			if (d > 1)
				d = 1;
			else if (d < 0)
				d = 0;
			pipe.fs.plr = (int)(d*0x7fffffff);
			NEXT_ARG;
			break;

		case TOK_QUEUE:
			NEED1("queue needs queue size\n");
			end = NULL;
			pipe.fs.qsize = getbw(av[0], &pipe.fs.flags_fs, 1024);
			NEXT_ARG;
			break;

		case TOK_BUCKETS:
			NEED1("buckets needs argument\n");
			pipe.fs.rq_size = strtoul(av[0], NULL, 0);
			NEXT_ARG;
			break;

		case TOK_MASK:
			NEED1("mask needs mask specifier\n");
			/*
			 * per-flow queue, mask is dst_ip, dst_port,
			 * src_ip, src_port, proto measured in bits
			 */
			par = NULL;

			pipe.fs.flow_mask.type = ETHERTYPE_IP;
			pipe.fs.flow_mask.u.ip.dst_ip = 0;
			pipe.fs.flow_mask.u.ip.src_ip = 0;
			pipe.fs.flow_mask.u.ip.dst_port = 0;
			pipe.fs.flow_mask.u.ip.src_port = 0;
			pipe.fs.flow_mask.u.ip.proto = 0;
			end = NULL;

			while (ac >= 1) {
				u_int32_t *p32 = NULL;
				u_int16_t *p16 = NULL;

				tok = match_token(dummynet_params, *av);
				NEXT_ARG;
				switch(tok) {
				case TOK_ALL:
					/*
					 * special case, all bits significant
					 */
					pipe.fs.flow_mask.u.ip.dst_ip = ~0;
					pipe.fs.flow_mask.u.ip.src_ip = ~0;
					pipe.fs.flow_mask.u.ip.dst_port = ~0;
					pipe.fs.flow_mask.u.ip.src_port = ~0;
					pipe.fs.flow_mask.u.ip.proto = ~0;
					pipe.fs.flags_fs |= DN_HAVE_FLOW_MASK;
					goto end_mask;

				case TOK_DSTIP:
					p32 = &pipe.fs.flow_mask.u.ip.dst_ip;
					break;

				case TOK_SRCIP:
					p32 = &pipe.fs.flow_mask.u.ip.src_ip;
					break;

				case TOK_DSTPORT:
					p16 = &pipe.fs.flow_mask.u.ip.dst_port;
					break;

				case TOK_SRCPORT:
					p16 = &pipe.fs.flow_mask.u.ip.src_port;
					break;

				case TOK_PROTO:
					break;

				default:
					NEXT_ARG;
					goto end_mask;
				}
				if (ac < 1)
					errx(EX_USAGE, "mask: value missing");
				if (*av[0] == '/') {
					a = strtoul(av[0]+1, &end, 0);
					a = (a == 32) ? ~0 : (1 << a) - 1;
				} else
					a = strtoul(av[0], &end, 0);
				if (p32 != NULL)
					*p32 = a;
				else if (p16 != NULL) {
					if (a > 65535)
						errx(EX_DATAERR,
						"mask: must be 16 bit");
					*p16 = (u_int16_t)a;
				} else {
					if (a > 255)
						errx(EX_DATAERR,
						"mask: must be 8 bit");
					pipe.fs.flow_mask.u.ip.proto =
						(uint8_t)a;
				}
				if (a != 0)
					pipe.fs.flags_fs |= DN_HAVE_FLOW_MASK;
				NEXT_ARG;
			} /* end while, config masks */

end_mask:
			break;

		case TOK_RED:
		case TOK_GRED:
			NEED1("red/gred needs w_q/min_th/max_th/max_p\n");
			pipe.fs.flags_fs |= DN_IS_RED;
			if (tok == TOK_GRED)
				pipe.fs.flags_fs |= DN_IS_GENTLE_RED;
			/*
			 * the format for parameters is w_q/min_th/max_th/max_p
			 */
			if ((end = strsep(&av[0], "/"))) {
				double w_q = strtod(end, NULL);
				if (w_q > 1 || w_q <= 0)
					errx(EX_DATAERR, "0 < w_q <= 1");
				pipe.fs.w_q = (int) (w_q * (1 << SCALE_RED));
			}
			if ((end = strsep(&av[0], "/"))) {
				pipe.fs.min_th = strtoul(end, &end, 0);
				if (*end == 'K' || *end == 'k')
					pipe.fs.min_th *= 1024;
			}
			if ((end = strsep(&av[0], "/"))) {
				pipe.fs.max_th = strtoul(end, &end, 0);
				if (*end == 'K' || *end == 'k')
					pipe.fs.max_th *= 1024;
			}
			if ((end = strsep(&av[0], "/"))) {
				double max_p = strtod(end, NULL);
				if (max_p > 1 || max_p <= 0)
					errx(EX_DATAERR, "0 < max_p <= 1");
				pipe.fs.max_p = (int)(max_p * (1 << SCALE_RED));
			}
			NEXT_ARG;
			break;

		case TOK_DROPTAIL:
			pipe.fs.flags_fs &= ~(DN_IS_RED|DN_IS_GENTLE_RED);
			break;

		case TOK_BW:
			NEED1("bw needs bandwidth\n");
			if (do_pipe != 1)
				errx(EX_DATAERR,
					"bandwidth only valid for pipes");
			/*
			 * set bandwidth value
			 */
			pipe.bandwidth = getbw(av[0], NULL, 1000);
			if (pipe.bandwidth < 0)
				errx(EX_DATAERR, "bandwidth too large");
			NEXT_ARG;
			break;

		case TOK_DELAY:
			if (do_pipe != 1)
				errx(EX_DATAERR, "delay only valid for pipes");
			NEED1("delay needs argument 0..10000ms\n");
			pipe.delay = strtoul(av[0], NULL, 0);
			NEXT_ARG;
			break;

		case TOK_WEIGHT:
			if (do_pipe == 1)
				errx(EX_DATAERR,
					"weight only valid for queues");
			NEED1("weight needs argument 0..100\n");
			pipe.fs.weight = strtoul(av[0], &end, 0);
			NEXT_ARG;
			break;

		case TOK_PIPE:
			if (do_pipe == 1)
				errx(EX_DATAERR, "pipe only valid for queues");
			NEED1("pipe needs pipe_number\n");
			pipe.fs.parent_nr = strtoul(av[0], &end, 0);
			NEXT_ARG;
			break;

		default:
			errx(EX_DATAERR, "unrecognised option ``%s''", *av);
		}
	}
	if (do_pipe == 1) {
		if (pipe.pipe_nr == 0)
			errx(EX_DATAERR, "pipe_nr must be > 0");
		if (pipe.delay > 10000)
			errx(EX_DATAERR, "delay must be < 10000");
	} else { /* do_pipe == 2, queue */
		if (pipe.fs.parent_nr == 0)
			errx(EX_DATAERR, "pipe must be > 0");
		if (pipe.fs.weight >100)
			errx(EX_DATAERR, "weight must be <= 100");
	}
	if (pipe.fs.flags_fs & DN_QSIZE_IS_BYTES) {
		if (pipe.fs.qsize > 1024*1024)
			errx(EX_DATAERR, "queue size must be < 1MB");
	} else {
		if (pipe.fs.qsize > 100)
			errx(EX_DATAERR, "2 <= queue size <= 100");
	}
	if (pipe.fs.flags_fs & DN_IS_RED) {
		size_t len;
		int lookup_depth, avg_pkt_size;
		double s, idle, weight, w_q;
		int clock_hz;
		int t;

		if (pipe.fs.min_th >= pipe.fs.max_th)
			errx(EX_DATAERR, "min_th %d must be < than max_th %d",
			pipe.fs.min_th, pipe.fs.max_th);
		if (pipe.fs.max_th == 0)
			errx(EX_DATAERR, "max_th must be > 0");

		len = sizeof(int);
		if (sysctlbyname("net.inet.ip.dummynet.red_lookup_depth",
			&lookup_depth, &len, NULL, 0) == -1)

			errx(1, "sysctlbyname(\"%s\")",
				"net.inet.ip.dummynet.red_lookup_depth");
		if (lookup_depth == 0)
			errx(EX_DATAERR, "net.inet.ip.dummynet.red_lookup_depth"
				" must be greater than zero");

		len = sizeof(int);
		if (sysctlbyname("net.inet.ip.dummynet.red_avg_pkt_size",
			&avg_pkt_size, &len, NULL, 0) == -1)

			errx(1, "sysctlbyname(\"%s\")",
				"net.inet.ip.dummynet.red_avg_pkt_size");
		if (avg_pkt_size == 0)
			errx(EX_DATAERR,
				"net.inet.ip.dummynet.red_avg_pkt_size must"
				" be greater than zero");

		len = sizeof(clock_hz);
		if (sysctlbyname("net.inet.ip.dummynet.hz", &clock_hz, &len,
				 NULL, 0) == -1) {
			errx(1, "sysctlbyname(\"%s\")",
				 "net.inet.ip.dummynet.hz");
		}

		/*
		 * Ticks needed for sending a medium-sized packet.
		 * Unfortunately, when we are configuring a WF2Q+ queue, we
		 * do not have bandwidth information, because that is stored
		 * in the parent pipe, and also we have multiple queues
		 * competing for it. So we set s=0, which is not very
		 * correct. But on the other hand, why do we want RED with
		 * WF2Q+ ?
		 */
		if (pipe.bandwidth == 0) /* this is a WF2Q+ queue */
			s = 0;
		else
			s = clock_hz * avg_pkt_size * 8 / pipe.bandwidth;

		/*
		 * max idle time (in ticks) before avg queue size becomes 0.
		 * NOTA: (3/w_q) is approx the value x so that
		 * (1-w_q)^x < 10^-3.
		 */
		w_q = ((double)pipe.fs.w_q) / (1 << SCALE_RED);
		idle = s * 3. / w_q;
		pipe.fs.lookup_step = (int)idle / lookup_depth;
		if (!pipe.fs.lookup_step)
			pipe.fs.lookup_step = 1;
		weight = 1 - w_q;
		for (t = pipe.fs.lookup_step; t > 0; --t)
			weight *= weight;
		pipe.fs.lookup_weight = (int)(weight * (1 << SCALE_RED));
	}
	i = do_set_x(IP_DUMMYNET_CONFIGURE, &pipe, sizeof pipe);
	if (i)
		err(1, "do_set_x(%s)", "IP_DUMMYNET_CONFIGURE");
}


void
show_dummynet(int ac, char *av[])
{
	void *data = NULL;
	int nbytes;
	int nalloc = 1024; 	/* start somewhere... */

	NEXT_ARG;

	nbytes = nalloc;
	while (nbytes >= nalloc) {
		nalloc = nalloc * 2 + 200;
		nbytes = nalloc;
		if ((data = realloc(data, nbytes)) == NULL)
			err(EX_OSERR, "realloc");
		if (do_get_x(IP_DUMMYNET_GET, data, &nbytes) < 0) {
			err(EX_OSERR, "do_get_x(IP_%s_GET)",
				do_pipe ? "DUMMYNET" : "FW");
		}
	}

	show_pipes(data, nbytes, ac, av);
	free(data);
}

void
show_pipes(void *data, int nbytes, int ac, char *av[])
{
	u_long rulenum;
	void *next = data;
	struct dn_ioc_pipe *p = (struct dn_ioc_pipe *)data;
	struct dn_ioc_flowset *fs;
	struct dn_ioc_flowqueue *q;
	int l;

	if (ac > 0)
		rulenum = strtoul(*av++, NULL, 10);
	else
		rulenum = 0;
	for (; nbytes >= sizeof(*p); p = (struct dn_ioc_pipe *)next) {
		double b = p->bandwidth;
		char buf[30];
		char prefix[80];

		if (p->fs.fs_type != DN_IS_PIPE)
			break; 	/* done with pipes, now queues */

		/*
		 * compute length, as pipe have variable size
		 */
		l = sizeof(*p) + p->fs.rq_elements * sizeof(*q);
		next = (void *)p + l;
		nbytes -= l;

		if (rulenum != 0 && rulenum != p->pipe_nr)
			continue;

		/*
		 * Print rate
		 */
		if (b == 0)
			sprintf(buf, "unlimited");
		else if (b >= 1000000)
			sprintf(buf, "%7.3f Mbit/s", b/1000000);
		else if (b >= 1000)
			sprintf(buf, "%7.3f Kbit/s", b/1000);
		else
			sprintf(buf, "%7.3f bit/s ", b);

		sprintf(prefix, "%05d: %s %4d ms ",
			p->pipe_nr, buf, p->delay);
		show_flowset_parms(&p->fs, prefix);
		if (verbose)
			printf(" V %20ju\n", (uintmax_t)p->V >> MY_M);

		q = (struct dn_ioc_flowqueue *)(p+1);
		show_queues(&p->fs, q);
	}

	for (fs = next; nbytes >= sizeof(*fs); fs = next) {
		char prefix[80];

		if (fs->fs_type != DN_IS_QUEUE)
			break;
		l = sizeof(*fs) + fs->rq_elements * sizeof(*q);
		next = (void *)fs + l;
		nbytes -= l;
		q = (struct dn_ioc_flowqueue *)(fs+1);
		sprintf(prefix, "q%05d: weight %d pipe %d ",
			fs->fs_nr, fs->weight, fs->parent_nr);
		show_flowset_parms(fs, prefix);
		show_queues(fs, q);
	}
}

void
show_queues(struct dn_ioc_flowset *fs, struct dn_ioc_flowqueue *q)
{
	int l;

	printf("mask: 0x%02x 0x%08x/0x%04x -> 0x%08x/0x%04x\n",
		fs->flow_mask.u.ip.proto,
		fs->flow_mask.u.ip.src_ip, fs->flow_mask.u.ip.src_port,
		fs->flow_mask.u.ip.dst_ip, fs->flow_mask.u.ip.dst_port);
	if (fs->rq_elements == 0)
		return;

	printf("BKT Prot ___Source IP/port____ "
		"____Dest. IP/port____ Tot_pkt/bytes Pkt/Byte Drp\n");
	if (do_sort != 0)
		heapsort(q, fs->rq_elements, sizeof(*q), sort_q);
	for (l = 0; l < fs->rq_elements; l++) {
		struct in_addr ina;
		struct protoent *pe;

		ina.s_addr = htonl(q[l].id.u.ip.src_ip);
		printf("%3d ", q[l].hash_slot);
		pe = getprotobynumber(q[l].id.u.ip.proto);
		if (pe)
			printf("%-4s ", pe->p_name);
		else
			printf("%4u ", q[l].id.u.ip.proto);
		printf("%15s/%-5d ",
			inet_ntoa(ina), q[l].id.u.ip.src_port);
		ina.s_addr = htonl(q[l].id.u.ip.dst_ip);
		printf("%15s/%-5d ",
			inet_ntoa(ina), q[l].id.u.ip.dst_port);
		printf("%4ju %8ju %2u %4u %3u\n",
			(uintmax_t)q[l].tot_pkts, (uintmax_t)q[l].tot_bytes,
			q[l].len, q[l].len_bytes, q[l].drops);
		if (verbose)
			printf(" S %20ju F %20ju\n",
				(uintmax_t)q[l].S, (uintmax_t)q[l].F);
	}
}

void
show_flowset_parms(struct dn_ioc_flowset *fs, char *prefix)
{
	char qs[30];
	char plr[30];
	char red[90]; 	/* Display RED parameters */
	int l;

	l = fs->qsize;
	if (fs->flags_fs & DN_QSIZE_IS_BYTES) {
		if (l >= 8192)
			sprintf(qs, "%d KB", l / 1024);
		else
			sprintf(qs, "%d B", l);
	} else
		sprintf(qs, "%3d sl.", l);
	if (fs->plr)
		sprintf(plr, "plr %f", 1.0 * fs->plr / (double)(0x7fffffff));
	else
		plr[0] = '\0';
	if (fs->flags_fs & DN_IS_RED)	/* RED parameters */
		sprintf(red,
			"\n\t %cRED w_q %f min_th %d max_th %d max_p %f",
			(fs->flags_fs & DN_IS_GENTLE_RED) ? 'G' : ' ',
			1.0 * fs->w_q / (double)(1 << SCALE_RED),
			SCALE_VAL(fs->min_th),
			SCALE_VAL(fs->max_th),
			1.0 * fs->max_p / (double)(1 << SCALE_RED));
	else
		sprintf(red, "droptail");

	printf("%s %s%s %d queues (%d buckets) %s\n",
		prefix, qs, plr, fs->rq_elements, fs->rq_size, red);
}

unsigned long
getbw(const char *str, u_short *flags, int kb)
{
	unsigned long val;
	int inbytes = 0;
	char *end;

	val = strtoul(str, &end, 0);
	if (*end == 'k' || *end == 'K') {
		++end;
		val *= kb;
	} else if (*end == 'm' || *end == 'M') {
		++end;
		val *= kb * kb;
	}

	/*
	 * Deal with bits or bytes or b(bits) or B(bytes). If there is no
	 * trailer assume bits.
	 */
	if (strncasecmp(end, "bit", 3) == 0) {
		;
	} else if (strncasecmp(end, "byte", 4) == 0) {
		inbytes = 1;
	} else if (*end == 'b') {
		;
	} else if (*end == 'B') {
		inbytes = 1;
	}

	/*
	 * Return in bits if flags is NULL, else flag bits
	 * or bytes in flags and return the unconverted value.
	 */
	if (inbytes && flags)
		*flags |= DN_QSIZE_IS_BYTES;
	else if (inbytes && flags == NULL)
		val *= 8;

	return(val);
}

void
dummynet_flush(void)
{
	int cmd = IP_FW_FLUSH;
	if (do_pipe) {
		cmd = IP_DUMMYNET_FLUSH;
	}
	if (!do_force) {
		int c;

		printf("Are you sure? [yn] ");
		fflush(stdout);
		do {
			c = toupper(getc(stdin));
			while (c != '\n' && getc(stdin) != '\n')
				if (feof(stdin))
					return; /* and do not flush */
		} while (c != 'Y' && c != 'N');
		if (c == 'N')	/* user said no */
			return;
	}
	if (do_set_x(cmd, NULL, 0) < 0 ) {
		if (do_pipe)
			errx(EX_USAGE, "pipe/queue in use");
		else
			errx(EX_USAGE, "do_set_x(IP_FW_FLUSH) failed");
	}
	if (!do_quiet) {
		printf("Flushed all %s.\n", do_pipe ? "pipes" : "rules");
	}
}

void
dummynet_main(int ac, char **av)
{
	if (!strncmp(*av, "config", strlen(*av))) {
		config_dummynet(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		dummynet_flush();
	} else if (!strncmp(*av, "show", strlen(*av))) {
		show_dummynet(ac, av);
	} else {
		errx(EX_USAGE, "bad ipfw pipe command `%s'", *av);
	}
}
