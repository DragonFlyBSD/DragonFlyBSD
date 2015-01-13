/*
 * BUFQUEUES.C
 *
 * cc -I/usr/src/sys bufqueues.c -o /usr/local/bin/bufqueues -lkvm
 *
 * bufqueues
 *
 * Output buf(9) queues usages
 *
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * by Antonio Huete Jimenez <tuxillo@quantumachine.net>
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

#define _KERNEL_STRUCTURES_
#include <sys/param.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/bio.h>
#include <sys/queue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <getopt.h>
#include <math.h>

struct nlist Nl[] = {
    { "_bufpcpu" },
    { "_ncpus" },
    { "_nbuf" },
    { NULL }
};

TAILQ_HEAD(bqueues, buf);

/*
 * Buffer queues.
 */
enum bufq_type {
	BQUEUE_NONE,    /* not on any queue */
	BQUEUE_LOCKED,  /* locked buffers */
	BQUEUE_CLEAN,   /* non-B_DELWRI buffers */
	BQUEUE_DIRTY,   /* B_DELWRI buffers */
	BQUEUE_DIRTY_HW,   /* B_DELWRI buffers - heavy weight */
	BQUEUE_EMPTYKVA, /* empty buffer headers with KVA assignment */
	BQUEUE_EMPTY,    /* empty buffer headers */

	BUFFER_QUEUES /* number of buffer queues */
};

struct bufpcpu {
	struct spinlock spin;
	struct bqueues bufqueues[BUFFER_QUEUES];
} __cachealign;

int verboseopt;

static int kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes, int out);
static void scan_queues(kvm_t *kd, int cpu, struct bufpcpu *bqp);
static void loaddelay(struct timespec *ts, const char *arg);

static const char *q2s(int queue);

/* Globals */
int qcounter[BUFFER_QUEUES];
int failcount;
int totalcount;
int nbuf;

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    struct bufpcpu bpcpu;
    struct timespec delay = { 1, 0 };
    kvm_t *kd;
    int count;
    int ncpus;
    int ch;
    int cpu;
    int q;

    while ((ch = getopt(ac, av, "M:N:v")) != -1) {
	switch(ch) {
	case 'v':
	    ++verboseopt;
	    break;
	case 'M':
	    corefile = optarg;
	    break;
	case 'N':
	    sysfile = optarg;
	    break;
	default:
	    fprintf(stderr, "%s [-M core] [-N system]\n", av[0]);
	    exit(1);
	}
    }
    ac -= optind;
    av += optind;

    if ((kd = kvm_open(sysfile, corefile, NULL, O_RDONLY, "kvm:")) == NULL) {
	perror("kvm_open");
	exit(1);
    }
    if (kvm_nlist(kd, Nl) != 0) {
	perror("kvm_nlist");
	exit(1);
    }

    if (ac > 0)
	    loaddelay(&delay, av[0]);

    kkread(kd, Nl[1].n_value, &ncpus, sizeof(ncpus), 1);
    kkread(kd, Nl[2].n_value, &nbuf, sizeof(nbuf), 1);

    for (count = 0; ; ++count) {
	    for (cpu = 0; cpu < ncpus; cpu++) {
		    kkread(kd, Nl[0].n_value + cpu * sizeof(struct bufpcpu),
			&bpcpu, sizeof(struct bufpcpu), 1);
		    scan_queues(kd, cpu, &bpcpu);
	    }

	    if (count && !verboseopt) {
		    if ((count & 15) == 1)
			    printf("  NONE  LOCKED  CLEAN  DIRTY  "
				"DIRTY_HW  EMPTYKVA  EMPTY  OFF-QUEUE KVMFAIL\n");
		    printf("%6d %7d %6d %6d %9d %9d %6d %10d %7d\n",
			qcounter[0], qcounter[1], qcounter[2],
			qcounter[3], qcounter[4], qcounter[5],
			qcounter[6], (nbuf - totalcount), failcount);
	    }

	    /* If in verbose mode only output detailed bufs info once */
	    if (verboseopt)
		    break;
	    nanosleep(&delay, NULL);
	    bzero(&qcounter, sizeof(qcounter));
	    totalcount = 0;
	    failcount = 0;
    }
    return(0);
}

static const char *q2s(int queue)
{
	switch(queue) {
	case BQUEUE_NONE:
		return "NONE";
	case BQUEUE_LOCKED:
		return "LOCKED";
	case BQUEUE_CLEAN:
		return "CLEAN";
	case BQUEUE_DIRTY:
		return "DIRTY";
	case BQUEUE_DIRTY_HW:
		return "DIRTY_HW";
	case BQUEUE_EMPTYKVA:
		return "EMPTYKVA";
	case BQUEUE_EMPTY:
		return "EMPTY";
	default:
		return "INVALID";
	}
}

void
scan_queues(kvm_t *kd, int cpu, struct bufpcpu *bqp)
{
	struct buf b, *tmp;
	int q;

	for (q = 0; q < BUFFER_QUEUES; q++) {
		if (bqp->bufqueues[q].tqh_first == NULL)
			continue;
		kkread(kd, (u_long)bqp->bufqueues[q].tqh_first, &b, sizeof(b), 1);
		tmp = bqp->bufqueues[q].tqh_first;
		if (tmp != NULL)
			qcounter[q]++;
		while (tmp != NULL) {
			if (verboseopt)
				printf("cpu=%d queue=%8s buf=%p", cpu, q2s(q), tmp);
			tmp = b.b_freelist.tqe_next;
			if (kkread(kd, (u_long)tmp, &b, sizeof(b), 0) == -1) {
				if (verboseopt)
					printf(" [F] ");
				failcount++;
			}
			if (verboseopt)
				printf("\n");
			qcounter[q]++;
			totalcount++;	/* All scanned bufs */
		}
	}
}

/*
 * Convert a delay string (e.g. "0.1") into a timespec.
 */
static
void
loaddelay(struct timespec *ts, const char *arg)
{
	double d;

	d = strtod(arg, NULL);
	if (d < 0.001)
		d = 0.001;
	ts->tv_sec = (int)d;
	ts->tv_nsec = (int)(modf(d, &d) * 1000000000.0);
}

static int
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes, int out)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
	    if (out) {
		    perror("kvm_read");
		    exit(1);
	    }
	    return -1;
    }
    return 0;
}
