/*
 * Copyright (c) 1998-2002 Luigi Rizzo, Universita` di Pisa
 * Portions Copyright (c) 2000 Akamba Corp.
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/netinet/ip_dummynet.c,v 1.24.2.22 2003/05/13 09:31:06 maxim Exp $
 * $DragonFly: src/sys/net/dummynet/ip_dummynet.c,v 1.43 2007/11/05 14:06:06 sephe Exp $
 */

#ifndef KLD_MODULE
#include "opt_ipfw.h"	/* for IPFW2 definition */
#endif

#ifdef DUMMYNET_DEBUG
#define DPRINTF(fmt, ...)	kprintf(fmt, __VA_ARGS__)
#else
#define DPRINTF(fmt, ...)	((void)0)
#endif

/*
 * This module implements IP dummynet, a bandwidth limiter/delay emulator
 * used in conjunction with the ipfw package.
 * Description of the data structures used is in ip_dummynet.h
 * Here you mainly find the following blocks of code:
 *  + variable declarations;
 *  + heap management functions;
 *  + scheduler and dummynet functions;
 *  + configuration and initialization.
 *
 * Most important Changes:
 *
 * 011004: KLDable
 * 010124: Fixed WF2Q behaviour
 * 010122: Fixed spl protection.
 * 000601: WF2Q support
 * 000106: Large rewrite, use heaps to handle very many pipes.
 * 980513: Initial release
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/thread2.h>

#include <net/ethernet.h>
#include <net/route.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <net/ipfw/ip_fw.h>
#include <net/dummynet/ip_dummynet.h>

#ifndef DN_CALLOUT_FREQ_MAX
#define DN_CALLOUT_FREQ_MAX	10000
#endif

/*
 * The maximum/minimum hash table size for queues.
 * These values must be a power of 2.
 */
#define DN_MIN_HASH_SIZE	4
#define DN_MAX_HASH_SIZE	65536

/*
 * Some macros are used to compare key values and handle wraparounds.
 * MAX64 returns the largest of two key values.
 */
#define DN_KEY_LT(a, b)		((int64_t)((a) - (b)) < 0)
#define DN_KEY_LEQ(a, b)	((int64_t)((a) - (b)) <= 0)
#define DN_KEY_GT(a, b)		((int64_t)((a) - (b)) > 0)
#define DN_KEY_GEQ(a, b)	((int64_t)((a) - (b)) >= 0)
#define MAX64(x, y)		((((int64_t)((y) - (x))) > 0) ? (y) : (x))

/*
 * We keep a private variable for the simulation time, but we could
 * probably use an existing one ("softticks" in sys/kern/kern_timer.c)
 */
static dn_key curr_time = 0; /* current simulation time */

static int dn_hash_size = 64;	/* default hash size */

/* statistics on number of queue searches and search steps */
static int searches, search_steps;
static int pipe_expire = 1;   /* expire queue if empty */
static int dn_max_ratio = 16; /* max queues/buckets ratio */

static int red_lookup_depth = 256;	/* RED - default lookup table depth */
static int red_avg_pkt_size = 512;      /* RED - default medium packet size */
static int red_max_pkt_size = 1500;     /* RED - default max packet size */

/*
 * Three heaps contain queues and pipes that the scheduler handles:
 *
 * ready_heap contains all dn_flow_queue related to fixed-rate pipes.
 *
 * wfq_ready_heap contains the pipes associated with WF2Q flows
 *
 * extract_heap contains pipes associated with delay lines.
 *
 */

MALLOC_DEFINE(M_DUMMYNET, "dummynet", "dummynet heap");

static struct dn_heap ready_heap, extract_heap, wfq_ready_heap;

static int heap_init(struct dn_heap *h, int size);
static int heap_insert (struct dn_heap *h, dn_key key1, void *p);
static void heap_extract(struct dn_heap *h, void *obj);

static void transmit_event(struct dn_pipe *pipe);
static void ready_event(struct dn_flow_queue *q);

static int sysctl_dn_hz(SYSCTL_HANDLER_ARGS);

static struct dn_pipe *all_pipes = NULL;	/* list of all pipes */
static struct dn_flow_set *all_flow_sets = NULL;/* list of all flow_sets */

static struct netmsg dn_netmsg;
static struct systimer dn_clock;
static int dn_hz = 1000;
static int dn_cpu = 0; /* TODO tunable */

SYSCTL_NODE(_net_inet_ip, OID_AUTO, dummynet,
		CTLFLAG_RW, 0, "Dummynet");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, hash_size,
	    CTLFLAG_RW, &dn_hash_size, 0, "Default hash table size");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, curr_time,
	    CTLFLAG_RD, &curr_time, 0, "Current tick");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, ready_heap,
	    CTLFLAG_RD, &ready_heap.size, 0, "Size of ready heap");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, extract_heap,
	    CTLFLAG_RD, &extract_heap.size, 0, "Size of extract heap");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, searches,
	    CTLFLAG_RD, &searches, 0, "Number of queue searches");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, search_steps,
	    CTLFLAG_RD, &search_steps, 0, "Number of queue search steps");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, expire,
	    CTLFLAG_RW, &pipe_expire, 0, "Expire queue if empty");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, max_chain_len,
	    CTLFLAG_RW, &dn_max_ratio, 0,
	"Max ratio between dynamic queues and buckets");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_lookup_depth,
	CTLFLAG_RD, &red_lookup_depth, 0, "Depth of RED lookup table");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_avg_pkt_size,
	CTLFLAG_RD, &red_avg_pkt_size, 0, "RED Medium packet size");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_max_pkt_size,
	CTLFLAG_RD, &red_max_pkt_size, 0, "RED Max packet size");
SYSCTL_PROC(_net_inet_ip_dummynet, OID_AUTO, hz, CTLTYPE_INT | CTLFLAG_RW,
	    0, 0, sysctl_dn_hz, "I", "Dummynet callout frequency");

static int config_pipe(struct dn_ioc_pipe *);
static int ip_dn_ctl(struct sockopt *sopt);

static void rt_unref(struct rtentry *);
static void dummynet_clock(systimer_t, struct intrframe *);
static void dummynet(struct netmsg *);
static void dummynet_flush(void);
static ip_dn_io_t dummynet_io;
static void dn_rule_delete(void *);

void dummynet_drain(void);	/* XXX unused */

static void
rt_unref(struct rtentry *rt)
{
    if (rt == NULL)
	return;
    if (rt->rt_refcnt <= 0)
	kprintf("-- warning, refcnt now %ld, decreasing\n", rt->rt_refcnt);
    RTFREE(rt);
}

/*
 * Heap management functions.
 *
 * In the heap, first node is element 0. Children of i are 2i+1 and 2i+2.
 * Some macros help finding parent/children so we can optimize them.
 *
 * heap_init() is called to expand the heap when needed.
 * Increment size in blocks of 16 entries.
 * XXX failure to allocate a new element is a pretty bad failure
 * as we basically stall a whole queue forever!!
 * Returns 1 on error, 0 on success
 */
#define HEAP_FATHER(x)		(((x) - 1) / 2)
#define HEAP_LEFT(x)		(2*(x) + 1)
#define HEAP_IS_LEFT(x)		((x) & 1)
#define HEAP_RIGHT(x)		(2*(x) + 2)
#define HEAP_SWAP(a, b, buffer)	{ buffer = a; a = b; b = buffer; }
#define HEAP_INCREMENT		15

static int
heap_init(struct dn_heap *h, int new_size)
{
    struct dn_heap_entry *p;

    if (h->size >= new_size) {
	kprintf("%s, Bogus call, have %d want %d\n", __func__,
		h->size, new_size);
	return 0;
    }

    new_size = (new_size + HEAP_INCREMENT) & ~HEAP_INCREMENT;
    p = kmalloc(new_size * sizeof(*p), M_DUMMYNET, M_WAITOK | M_ZERO);
    if (h->size > 0) {
	bcopy(h->p, p, h->size * sizeof(*p));
	kfree(h->p, M_DUMMYNET);
    }
    h->p = p;
    h->size = new_size;
    return 0;
}

/*
 * Insert element in heap. Normally, p != NULL, we insert p in
 * a new position and bubble up.  If p == NULL, then the element is
 * already in place, and key is the position where to start the
 * bubble-up.
 * Returns 1 on failure (cannot allocate new heap entry)
 *
 * If offset > 0 the position (index, int) of the element in the heap is
 * also stored in the element itself at the given offset in bytes.
 */
#define SET_OFFSET(heap, node) \
    if (heap->offset > 0) \
	*((int *)((char *)(heap->p[node].object) + heap->offset)) = node;

/*
 * RESET_OFFSET is used for sanity checks. It sets offset to an invalid value.
 */
#define RESET_OFFSET(heap, node) \
    if (heap->offset > 0) \
	*((int *)((char *)(heap->p[node].object) + heap->offset)) = -1;

static int
heap_insert(struct dn_heap *h, dn_key key1, void *p)
{
    int son = h->elements;

    if (p == NULL) {	/* Data already there, set starting point */
	son = key1;
    } else {		/* Insert new element at the end, possibly resize */
	son = h->elements;
	if (son == h->size) { /* Need resize... */
	    if (heap_init(h, h->elements + 1))
		return 1; /* Failure... */
	}
	h->p[son].object = p;
	h->p[son].key = key1;
	h->elements++;
    }

    while (son > 0) {	/* Bubble up */
	int father = HEAP_FATHER(son);
	struct dn_heap_entry tmp;

	if (DN_KEY_LT(h->p[father].key, h->p[son].key))
	    break; /* Found right position */

	/* 'son' smaller than 'father', swap and repeat */
	HEAP_SWAP(h->p[son], h->p[father], tmp);
	SET_OFFSET(h, son);
	son = father;
    }
    SET_OFFSET(h, son);
    return 0;
}

/*
 * Remove top element from heap, or obj if obj != NULL
 */
static void
heap_extract(struct dn_heap *h, void *obj)
{
    int child, father, max = h->elements - 1;

    if (max < 0) {
	kprintf("warning, extract from empty heap 0x%p\n", h);
	return;
    }

    father = 0; /* Default: move up smallest child */
    if (obj != NULL) { /* Extract specific element, index is at offset */
	if (h->offset <= 0)
	    panic("%s from middle not supported on this heap!!!\n", __func__);

	father = *((int *)((char *)obj + h->offset));
	if (father < 0 || father >= h->elements) {
	    panic("%s father %d out of bound 0..%d\n", __func__,
	    	  father, h->elements);
	}
    }
    RESET_OFFSET(h, father);

    child = HEAP_LEFT(father);		/* Left child */
    while (child <= max) {		/* Valid entry */
	if (child != max && DN_KEY_LT(h->p[child + 1].key, h->p[child].key))
	    child = child + 1;		/* Take right child, otherwise left */
	h->p[father] = h->p[child];
	SET_OFFSET(h, father);
	father = child;
	child = HEAP_LEFT(child);	/* Left child for next loop */
    }
    h->elements--;
    if (father != max) {
	/*
	 * Fill hole with last entry and bubble up, reusing the insert code
	 */
	h->p[father] = h->p[max];
	heap_insert(h, father, NULL);	/* This one cannot fail */
    }
}

/*
 * heapify() will reorganize data inside an array to maintain the
 * heap property.  It is needed when we delete a bunch of entries.
 */
static void
heapify(struct dn_heap *h)
{
    int i;

    for (i = 0; i < h->elements; i++)
	heap_insert(h, i , NULL);
}

/*
 * Cleanup the heap and free data structure
 */
static void
heap_free(struct dn_heap *h)
{
    if (h->size > 0)
	kfree(h->p, M_DUMMYNET);
    bzero(h, sizeof(*h));
}

/*
 * --- End of heap management functions ---
 */

/*
 * Scheduler functions:
 *
 * transmit_event() is called when the delay-line needs to enter
 * the scheduler, either because of existing pkts getting ready,
 * or new packets entering the queue.  The event handled is the delivery
 * time of the packet.
 *
 * ready_event() does something similar with fixed-rate queues, and the
 * event handled is the finish time of the head pkt.
 *
 * wfq_ready_event() does something similar with WF2Q queues, and the
 * event handled is the start time of the head pkt.
 *
 * In all cases, we make sure that the data structures are consistent
 * before passing pkts out, because this might trigger recursive
 * invocations of the procedures.
 */
static void
transmit_event(struct dn_pipe *pipe)
{
    struct dn_pkt *pkt;

    while ((pkt = pipe->head) && DN_KEY_LEQ(pkt->output_time, curr_time)) {
    	struct rtentry *rt;

	/*
	 * First unlink, then call procedures, since ip_input() can invoke
	 * ip_output() and viceversa, thus causing nested calls
	 */
	pipe->head = pkt->dn_next;

	/*
	 * NOTE:
	 * 'pkt' should _not_ be touched after calling
	 * ip_output(), ip_input(), ether_demux() and ether_output_frame()
	 */
	switch (pkt->dn_dir) {
	case DN_TO_IP_OUT:
	    /*
	     * 'pkt' will be freed in ip_output, so we keep
	     * a reference of the 'rtentry' beforehand.
	     */
	    rt = pkt->ro.ro_rt;
	    ip_output(pkt->dn_m, NULL, NULL, 0, NULL, NULL);
	    rt_unref(rt);
	    break;

	case DN_TO_IP_IN :
	    ip_input(pkt->dn_m);
	    break;

	case DN_TO_ETH_DEMUX:
	    {
		struct mbuf *m = pkt->dn_m;
		struct ether_header *eh;

		if (m->m_len < ETHER_HDR_LEN &&
		    (m = m_pullup(m, ETHER_HDR_LEN)) == NULL) {
		    kprintf("dummynet: pullup fail, dropping pkt\n");
		    break;
		}
		/*
		 * Same as ether_input, make eh be a pointer into the mbuf
		 */
		eh = mtod(m, struct ether_header *);
		m_adj(m, ETHER_HDR_LEN);
		ether_demux(NULL, eh, m);
	    }
	    break;

	case DN_TO_ETH_OUT:
	    ether_output_frame(pkt->ifp, pkt->dn_m);
	    break;

	default:
	    kprintf("dummynet: bad switch %d!\n", pkt->dn_dir);
	    m_freem(pkt->dn_m);
	    break;
	}
    }

    /*
     * If there are leftover packets, put into the heap for next event
     */
    if ((pkt = pipe->head)) {
	/*
	 * XXX should check errors on heap_insert, by draining the
	 * whole pipe and hoping in the future we are more successful
	 */
	heap_insert(&extract_heap, pkt->output_time, pipe);
    }
}

/*
 * The following macro computes how many ticks we have to wait
 * before being able to transmit a packet. The credit is taken from
 * either a pipe (WF2Q) or a flow_queue (per-flow queueing)
 */
#define SET_TICKS(pkt, q, p)	\
    (pkt->dn_m->m_pkthdr.len*8*dn_hz - (q)->numbytes + p->bandwidth - 1 ) / \
	    p->bandwidth;

/*
 * Extract pkt from queue, compute output time (could be now)
 * and put into delay line (p_queue)
 */
static void
move_pkt(struct dn_pkt *pkt, struct dn_flow_queue *q,
	 struct dn_pipe *p, int len)
{
    q->head = pkt->dn_next;
    q->len--;
    q->len_bytes -= len;

    pkt->output_time = curr_time + p->delay;

    if (p->head == NULL)
	p->head = pkt;
    else
	p->tail->dn_next = pkt;
    p->tail = pkt;
    p->tail->dn_next = NULL;
}

/*
 * ready_event() is invoked every time the queue must enter the
 * scheduler, either because the first packet arrives, or because
 * a previously scheduled event fired.
 * On invokation, drain as many pkts as possible (could be 0) and then
 * if there are leftover packets reinsert the pkt in the scheduler.
 */
static void
ready_event(struct dn_flow_queue *q)
{
    struct dn_pkt *pkt;
    struct dn_pipe *p = q->fs->pipe;
    int p_was_empty;

    if (p == NULL) {
	kprintf("ready_event- pipe is gone\n");
	return;
    }
    p_was_empty = (p->head == NULL);

    /*
     * Schedule fixed-rate queues linked to this pipe:
     * Account for the bw accumulated since last scheduling, then
     * drain as many pkts as allowed by q->numbytes and move to
     * the delay line (in p) computing output time.
     * bandwidth==0 (no limit) means we can drain the whole queue,
     * setting len_scaled = 0 does the job.
     */
    q->numbytes += (curr_time - q->sched_time) * p->bandwidth;
    while ((pkt = q->head) != NULL) {
	int len = pkt->dn_m->m_pkthdr.len;
	int len_scaled = p->bandwidth ? len*8*dn_hz : 0;

	if (len_scaled > q->numbytes)
	    break;
	q->numbytes -= len_scaled;
	move_pkt(pkt, q, p, len);
    }

    /*
     * If we have more packets queued, schedule next ready event
     * (can only occur when bandwidth != 0, otherwise we would have
     * flushed the whole queue in the previous loop).
     * To this purpose we record the current time and compute how many
     * ticks to go for the finish time of the packet.
     */
    if ((pkt = q->head) != NULL) {	/* this implies bandwidth != 0 */
	dn_key t = SET_TICKS(pkt, q, p); /* ticks i have to wait */

	q->sched_time = curr_time;

	/*
	 * XXX should check errors on heap_insert, and drain the whole
	 * queue on error hoping next time we are luckier.
	 */
	heap_insert(&ready_heap, curr_time + t, q);
    } else {	/* RED needs to know when the queue becomes empty */
	q->q_time = curr_time;
	q->numbytes = 0;
    }

    /*
     * If the delay line was empty call transmit_event(p) now.
     * Otherwise, the scheduler will take care of it.
     */
    if (p_was_empty)
	transmit_event(p);
}

/*
 * Called when we can transmit packets on WF2Q queues.  Take pkts out of
 * the queues at their start time, and enqueue into the delay line.
 * Packets are drained until p->numbytes < 0.  As long as
 * len_scaled >= p->numbytes, the packet goes into the delay line
 * with a deadline p->delay.  For the last packet, if p->numbytes < 0,
 * there is an additional delay.
 */
static void
ready_event_wfq(struct dn_pipe *p)
{
    int p_was_empty = (p->head == NULL);
    struct dn_heap *sch = &p->scheduler_heap;
    struct dn_heap *neh = &p->not_eligible_heap;

    p->numbytes += (curr_time - p->sched_time) * p->bandwidth;

    /*
     * While we have backlogged traffic AND credit, we need to do
     * something on the queue.
     */
    while (p->numbytes >= 0 && (sch->elements > 0 || neh->elements > 0)) {
	if (sch->elements > 0) { /* Have some eligible pkts to send out */
	    struct dn_flow_queue *q = sch->p[0].object;
	    struct dn_pkt *pkt = q->head;
	    struct dn_flow_set *fs = q->fs;
	    uint64_t len = pkt->dn_m->m_pkthdr.len;
	    int len_scaled = p->bandwidth ? len*8*dn_hz : 0;

	    heap_extract(sch, NULL);	/* Remove queue from heap */
	    p->numbytes -= len_scaled;
	    move_pkt(pkt, q, p, len);

	    p->V += (len << MY_M) / p->sum;	/* Update V */
	    q->S = q->F;			/* Update start time */

	    if (q->len == 0) {	/* Flow not backlogged any more */
		fs->backlogged--;
		heap_insert(&p->idle_heap, q->F, q);
	    } else {		/* Still backlogged */
		/*
		 * Update F and position in backlogged queue, then
		 * put flow in not_eligible_heap (we will fix this later).
		 */
		len = q->head->dn_m->m_pkthdr.len;
		q->F += (len << MY_M) / (uint64_t)fs->weight;
		if (DN_KEY_LEQ(q->S, p->V))
		    heap_insert(neh, q->S, q);
		else
		    heap_insert(sch, q->F, q);
	    }
	}

	/*
	 * Now compute V = max(V, min(S_i)).  Remember that all elements in
	 * sch have by definition S_i <= V so if sch is not empty, V is surely
	 * the max and we must not update it.  Conversely, if sch is empty
	 * we only need to look at neh.
	 */
	if (sch->elements == 0 && neh->elements > 0)
	    p->V = MAX64(p->V, neh->p[0].key);

	/*
	 * Move from neh to sch any packets that have become eligible
	 */
	while (neh->elements > 0 && DN_KEY_LEQ(neh->p[0].key, p->V)) {
	    struct dn_flow_queue *q = neh->p[0].object;

	    heap_extract(neh, NULL);
	    heap_insert(sch, q->F, q);
	}
    }

    if (sch->elements == 0 && neh->elements == 0 && p->numbytes >= 0 &&
    	p->idle_heap.elements > 0) {
	/*
	 * No traffic and no events scheduled.  We can get rid of idle-heap.
	 */
	int i;

	for (i = 0; i < p->idle_heap.elements; i++) {
	    struct dn_flow_queue *q = p->idle_heap.p[i].object;

	    q->F = 0;
	    q->S = q->F + 1;
	}
	p->sum = 0;
	p->V = 0;
	p->idle_heap.elements = 0;
    }

    /*
     * If we are getting clocks from dummynet and if we are under credit,
     * schedule the next ready event.
     * Also fix the delivery time of the last packet.
     */
    if (p->numbytes < 0) { /* This implies bandwidth>0 */
	dn_key t = 0; /* Number of ticks i have to wait */

	if (p->bandwidth > 0)
	    t = (p->bandwidth - 1 - p->numbytes) / p->bandwidth;
	p->tail->output_time += t;
	p->sched_time = curr_time;

	/*
	 * XXX should check errors on heap_insert, and drain the whole
	 * queue on error hoping next time we are luckier.
	 */
	heap_insert(&wfq_ready_heap, curr_time + t, p);
    }

    /*
     * If the delay line was empty call transmit_event(p) now.
     * Otherwise, the scheduler will take care of it.
     */
    if (p_was_empty)
	transmit_event(p);
}

/*
 * This is called once per tick, or dn_hz times per second.  It is used to
 * increment the current tick counter and schedule expired events.
 */
static void
dummynet(struct netmsg *msg)
{
    void *p;
    struct dn_heap *h;
    struct dn_heap *heaps[3];
    int i;
    struct dn_pipe *pe;

    heaps[0] = &ready_heap;		/* Fixed-rate queues */
    heaps[1] = &wfq_ready_heap;		/* WF2Q queues */
    heaps[2] = &extract_heap;		/* Delay line */

    crit_enter();

    /* Reply ASAP */
    lwkt_replymsg(&msg->nm_lmsg, 0);

    curr_time++;
    for (i = 0; i < 3; i++) {
	h = heaps[i];
	while (h->elements > 0 && DN_KEY_LEQ(h->p[0].key, curr_time)) {
	    if (h->p[0].key > curr_time) {
		kprintf("-- dummynet: warning, heap %d is %d ticks late\n",
		    i, (int)(curr_time - h->p[0].key));
	    }

	    p = h->p[0].object;		/* Store a copy before heap_extract */
	    heap_extract(h, NULL);	/* Need to extract before processing */

	    if (i == 0)
		ready_event(p);
	    else if (i == 1)
		ready_event_wfq(p);
	    else
		transmit_event(p);
	}
    }

    /*
     * Sweep pipes trying to expire idle flow_queues
     */
    for (pe = all_pipes; pe; pe = pe->next) {
	if (pe->idle_heap.elements > 0 &&
	    DN_KEY_LT(pe->idle_heap.p[0].key, pe->V)) {
	    struct dn_flow_queue *q = pe->idle_heap.p[0].object;

	    heap_extract(&pe->idle_heap, NULL);
	    q->S = q->F + 1; /* Mark timestamp as invalid */
	    pe->sum -= q->fs->weight;
	}
    }

    crit_exit();
}

/*
 * Unconditionally expire empty queues in case of shortage.
 * Returns the number of queues freed.
 */
static int
expire_queues(struct dn_flow_set *fs)
{
    struct dn_flow_queue *q, *prev;
    int i, initial_elements = fs->rq_elements;

    if (fs->last_expired == time_second)
	return 0;

    fs->last_expired = time_second;

    for (i = 0; i <= fs->rq_size; i++) { /* Last one is overflow */
	for (prev = NULL, q = fs->rq[i]; q != NULL;) {
	    if (q->head != NULL || q->S != q->F + 1) {
		prev = q;
		q = q->next;
 	    } else {	/* Entry is idle, expire it */
		struct dn_flow_queue *old_q = q;

		if (prev != NULL)
		    prev->next = q = q->next;
		else
		    fs->rq[i] = q = q->next;
		fs->rq_elements-- ;
		kfree(old_q, M_DUMMYNET);
	    }
	}
    }
    return initial_elements - fs->rq_elements;
}

/*
 * If room, create a new queue and put at head of slot i;
 * otherwise, create or use the default queue.
 */
static struct dn_flow_queue *
create_queue(struct dn_flow_set *fs, int i)
{
    struct dn_flow_queue *q;

    if (fs->rq_elements > fs->rq_size * dn_max_ratio &&
	expire_queues(fs) == 0) {
	/*
	 * No way to get room, use or create overflow queue.
	 */
	i = fs->rq_size;
	if (fs->rq[i] != NULL)
	    return fs->rq[i];
    }

    q = kmalloc(sizeof(*q), M_DUMMYNET, M_INTWAIT | M_NULLOK | M_ZERO);
    if (q == NULL)
	return NULL;

    q->fs = fs;
    q->hash_slot = i;
    q->next = fs->rq[i];
    q->S = q->F + 1;   /* hack - mark timestamp as invalid */
    fs->rq[i] = q;
    fs->rq_elements++;

    return q;
}

/*
 * Given a flow_set and a pkt in last_pkt, find a matching queue
 * after appropriate masking. The queue is moved to front
 * so that further searches take less time.
 */
static struct dn_flow_queue *
find_queue(struct dn_flow_set *fs, struct ipfw_flow_id *id)
{
    struct dn_flow_queue *q, *prev;
    int i = 0;

    if (!(fs->flags_fs & DN_HAVE_FLOW_MASK)) {
	q = fs->rq[0];
    } else {
	/* First, do the masking */
	id->dst_ip &= fs->flow_mask.dst_ip;
	id->src_ip &= fs->flow_mask.src_ip;
	id->dst_port &= fs->flow_mask.dst_port;
	id->src_port &= fs->flow_mask.src_port;
	id->proto &= fs->flow_mask.proto;
	id->flags = 0; /* we don't care about this one */

	/* Then, hash function */
	i = ((id->dst_ip) & 0xffff) ^
	    ((id->dst_ip >> 15) & 0xffff) ^
	    ((id->src_ip << 1) & 0xffff) ^
	    ((id->src_ip >> 16 ) & 0xffff) ^
	    (id->dst_port << 1) ^ (id->src_port) ^
	    (id->proto);
	i = i % fs->rq_size;

	/* Finally, scan the current list for a match */
	searches++;
	for (prev = NULL, q = fs->rq[i]; q;) {
	    search_steps++;
	    if (id->dst_ip == q->id.dst_ip &&
		id->src_ip == q->id.src_ip &&
		id->dst_port == q->id.dst_port &&
		id->src_port == q->id.src_port &&
		id->proto == q->id.proto &&
		id->flags == q->id.flags) {
		break; /* Found */
	    } else if (pipe_expire && q->head == NULL && q->S == q->F + 1) {
		/* Entry is idle and not in any heap, expire it */
		struct dn_flow_queue *old_q = q;

		if (prev != NULL)
		    prev->next = q = q->next;
		else
		    fs->rq[i] = q = q->next;
		fs->rq_elements--;
		kfree(old_q, M_DUMMYNET);
		continue;
	    }
	    prev = q;
	    q = q->next;
	}
	if (q && prev != NULL) { /* Found and not in front */
	    prev->next = q->next;
	    q->next = fs->rq[i];
	    fs->rq[i] = q;
	}
    }
    if (q == NULL) {	/* No match, need to allocate a new entry */
	q = create_queue(fs, i);
	if (q != NULL)
	    q->id = *id;
    }
    return q;
}

static int
red_drops(struct dn_flow_set *fs, struct dn_flow_queue *q, int len)
{
    /*
     * RED algorithm
     *
     * RED calculates the average queue size (avg) using a low-pass filter
     * with an exponential weighted (w_q) moving average:
     * 	avg  <-  (1-w_q) * avg + w_q * q_size
     * where q_size is the queue length (measured in bytes or * packets).
     *
     * If q_size == 0, we compute the idle time for the link, and set
     *	avg = (1 - w_q)^(idle/s)
     * where s is the time needed for transmitting a medium-sized packet.
     *
     * Now, if avg < min_th the packet is enqueued.
     * If avg > max_th the packet is dropped. Otherwise, the packet is
     * dropped with probability P function of avg.
     */

    int64_t p_b = 0;
    u_int q_size = (fs->flags_fs & DN_QSIZE_IS_BYTES) ? q->len_bytes : q->len;

    DPRINTF("\n%d q: %2u ", (int)curr_time, q_size);

    /* Average queue size estimation */
    if (q_size != 0) {
	/*
	 * Queue is not empty, avg <- avg + (q_size - avg) * w_q
	 */
	int diff = SCALE(q_size) - q->avg;
	int64_t v = SCALE_MUL((int64_t)diff, (int64_t)fs->w_q);

	q->avg += (int)v;
    } else {
	/*
	 * Queue is empty, find for how long the queue has been
	 * empty and use a lookup table for computing
	 * (1 - * w_q)^(idle_time/s) where s is the time to send a
	 * (small) packet.
	 * XXX check wraps...
	 */
	if (q->avg) {
	    u_int t = (curr_time - q->q_time) / fs->lookup_step;

	    q->avg = (t < fs->lookup_depth) ?
		     SCALE_MUL(q->avg, fs->w_q_lookup[t]) : 0;
	}
    }
    DPRINTF("avg: %u ", SCALE_VAL(q->avg));

    /* Should i drop? */

    if (q->avg < fs->min_th) {
	/* Accept packet */
	q->count = -1;
	return 0;
    }

    if (q->avg >= fs->max_th) { /* Average queue >=  Max threshold */
	if (fs->flags_fs & DN_IS_GENTLE_RED) {
	    /*
	     * According to Gentle-RED, if avg is greater than max_th the
	     * packet is dropped with a probability
	     *	p_b = c_3 * avg - c_4
	     * where c_3 = (1 - max_p) / max_th, and c_4 = 1 - 2 * max_p
	     */
	    p_b = SCALE_MUL((int64_t)fs->c_3, (int64_t)q->avg) - fs->c_4;
	} else {
	    q->count = -1;
	    kprintf("- drop\n");
	    return 1;
	}
    } else if (q->avg > fs->min_th) {
	/*
	 * We compute p_b using the linear dropping function p_b = c_1 *
	 * avg - c_2, where c_1 = max_p / (max_th - min_th), and c_2 =
	 * max_p * min_th / (max_th - min_th)
	 */
	p_b = SCALE_MUL((int64_t)fs->c_1, (int64_t)q->avg) - fs->c_2;
    }
    if (fs->flags_fs & DN_QSIZE_IS_BYTES)
	p_b = (p_b * len) / fs->max_pkt_size;

    if (++q->count == 0) {
	q->random = krandom() & 0xffff;
    } else {
	/*
	 * q->count counts packets arrived since last drop, so a greater
	 * value of q->count means a greater packet drop probability.
	 */
	if (SCALE_MUL(p_b, SCALE((int64_t)q->count)) > q->random) {
	    q->count = 0;
	    DPRINTF("%s", "- red drop");
	    /* After a drop we calculate a new random value */
	    q->random = krandom() & 0xffff;
	    return 1;    /* Drop */
	}
    }
    /* End of RED algorithm */
    return 0; /* Accept */
}

static __inline struct dn_flow_set *
locate_flowset(int pipe_nr, struct ip_fw *rule)
{
    ipfw_insn *cmd = rule->cmd + rule->act_ofs;
    struct dn_flow_set *fs;

    if (cmd->opcode == O_LOG)
	cmd += F_LEN(cmd);

    fs = ((ipfw_insn_pipe *)cmd)->pipe_ptr;
    if (fs != NULL)
	return fs;

    if (cmd->opcode == O_QUEUE) {
	for (fs = all_flow_sets; fs && fs->fs_nr != pipe_nr; fs = fs->next)
	    ;	/* EMPTY */
    } else {
	struct dn_pipe *p;

	for (p = all_pipes; p && p->pipe_nr != pipe_nr; p = p->next)
	    ;	/* EMPTY */
	if (p != NULL)
	    fs = &p->fs;
    }

    /* record for the future */
    ((ipfw_insn_pipe *)cmd)->pipe_ptr = fs;
    return fs;
}

/*
 * Dummynet hook for packets.  Below 'pipe' is a pipe or a queue
 * depending on whether WF2Q or fixed bw is used.
 *
 * pipe_nr	pipe or queue the packet is destined for.
 * dir		where shall we send the packet after dummynet.
 * m		the mbuf with the packet
 * fwa->oif	the 'ifp' parameter from the caller.
 *		NULL in ip_input, destination interface in ip_output
 * fwa->ro	route parameter (only used in ip_output, NULL otherwise)
 * fwa->dst	destination address, only used by ip_output
 * fwa->rule	matching rule, in case of multiple passes
 * fwa->flags	flags from the caller, only used in ip_output
 */
static int
dummynet_io(struct mbuf *m, int pipe_nr, int dir, struct ip_fw_args *fwa)
{
    struct dn_pkt *pkt;
    struct m_tag *tag;
    struct dn_flow_set *fs;
    struct dn_pipe *pipe;
    uint64_t len = m->m_pkthdr.len;
    struct dn_flow_queue *q = NULL;
    int is_pipe;
    ipfw_insn *cmd;

    crit_enter();

    cmd = fwa->rule->cmd + fwa->rule->act_ofs;
    if (cmd->opcode == O_LOG)
	cmd += F_LEN(cmd);

    KASSERT(cmd->opcode == O_PIPE || cmd->opcode == O_QUEUE,
    	    ("Rule is not PIPE or QUEUE, opcode %d\n", cmd->opcode));

    is_pipe = (cmd->opcode == O_PIPE);
    pipe_nr &= 0xffff;

    /*
     * This is a dummynet rule, so we expect a O_PIPE or O_QUEUE rule
     */
    fs = locate_flowset(pipe_nr, fwa->rule);
    if (fs == NULL)
	goto dropit;	/* This queue/pipe does not exist! */

    pipe = fs->pipe;
    if (pipe == NULL) { /* Must be a queue, try find a matching pipe */
	for (pipe = all_pipes; pipe && pipe->pipe_nr != fs->parent_nr;
	     pipe = pipe->next)
	    ;	/* EMPTY */
	if (pipe != NULL) {
	    fs->pipe = pipe;
	} else {
	    kprintf("No pipe %d for queue %d, drop pkt\n",
	    	    fs->parent_nr, fs->fs_nr);
	    goto dropit;
	}
    }

    q = find_queue(fs, &fwa->f_id);
    if (q == NULL)
	goto dropit;	/* Cannot allocate queue */

    /*
     * Update statistics, then check reasons to drop pkt
     */
    q->tot_bytes += len;
    q->tot_pkts++;

    if (fs->plr && krandom() < fs->plr)
	goto dropit;	/* Random pkt drop */

    if (fs->flags_fs & DN_QSIZE_IS_BYTES) {
    	if (q->len_bytes > fs->qsize)
	    goto dropit;	/* Queue size overflow */
    } else {
	if (q->len >= fs->qsize)
	    goto dropit;	/* Queue count overflow */
    }

    if ((fs->flags_fs & DN_IS_RED) && red_drops(fs, q, len))
	goto dropit;

    /*
     * Build and enqueue packet + parameters
     */
    tag = m_tag_get(PACKET_TAG_DUMMYNET, sizeof(*pkt), MB_DONTWAIT /* XXX */);
    if (tag == NULL)
	goto dropit;
    m_tag_prepend(m, tag);

    pkt = m_tag_data(tag);
    bzero(pkt, sizeof(*pkt)); /* XXX expensive to zero */

    pkt->rule = fwa->rule;
    pkt->dn_next = NULL;
    pkt->dn_m = m;
    pkt->dn_dir = dir;

    pkt->ifp = fwa->oif;
    if (dir == DN_TO_IP_OUT) {
	/*
	 * We need to copy *ro because for ICMP pkts (and maybe others)
	 * the caller passed a pointer into the stack; dst might also be
	 * a pointer into *ro so it needs to be updated.
	 */
	pkt->ro = *(fwa->ro);
	if (fwa->ro->ro_rt)
	    fwa->ro->ro_rt->rt_refcnt++;
	if (fwa->dst == (struct sockaddr_in *)&fwa->ro->ro_dst) {
	    /* 'dst' points into 'ro' */
	    fwa->dst = (struct sockaddr_in *)&(pkt->ro.ro_dst);
	}

	pkt->dn_dst = fwa->dst;
	pkt->flags = fwa->flags;
    }
    if (q->head == NULL)
	q->head = pkt;
    else
	q->tail->dn_next = pkt;
    q->tail = pkt;
    q->len++;
    q->len_bytes += len;

    if (q->head != pkt)	/* Flow was not idle, we are done */
	goto done;

    /*
     * If we reach this point the flow was previously idle, so we need
     * to schedule it.  This involves different actions for fixed-rate
     * or WF2Q queues.
     */
    if (is_pipe) {
	/*
	 * Fixed-rate queue: just insert into the ready_heap.
	 */
	dn_key t = 0;

	if (pipe->bandwidth)
	    t = SET_TICKS(pkt, q, pipe);

	q->sched_time = curr_time;
	if (t == 0)	/* Must process it now */
	    ready_event(q);
	else
	    heap_insert(&ready_heap, curr_time + t, q);
    } else {
	/*
	 * WF2Q:
	 * First, compute start time S: if the flow was idle (S=F+1)
	 * set S to the virtual time V for the controlling pipe, and update
	 * the sum of weights for the pipe; otherwise, remove flow from
	 * idle_heap and set S to max(F, V).
	 * Second, compute finish time F = S + len/weight.
	 * Third, if pipe was idle, update V = max(S, V).
	 * Fourth, count one more backlogged flow.
	 */
	if (DN_KEY_GT(q->S, q->F)) { /* Means timestamps are invalid */
	    q->S = pipe->V;
	    pipe->sum += fs->weight; /* Add weight of new queue */
	} else {
	    heap_extract(&pipe->idle_heap, q);
	    q->S = MAX64(q->F, pipe->V);
	}
	q->F = q->S + (len << MY_M) / (uint64_t)fs->weight;

	if (pipe->not_eligible_heap.elements == 0 &&
	    pipe->scheduler_heap.elements == 0)
	    pipe->V = MAX64(q->S, pipe->V);

	fs->backlogged++;

	/*
	 * Look at eligibility.  A flow is not eligibile if S>V (when
	 * this happens, it means that there is some other flow already
	 * scheduled for the same pipe, so the scheduler_heap cannot be
	 * empty).  If the flow is not eligible we just store it in the
	 * not_eligible_heap.  Otherwise, we store in the scheduler_heap
	 * and possibly invoke ready_event_wfq() right now if there is
	 * leftover credit.
	 * Note that for all flows in scheduler_heap (SCH), S_i <= V,
	 * and for all flows in not_eligible_heap (NEH), S_i > V.
	 * So when we need to compute max(V, min(S_i)) forall i in SCH+NEH,
	 * we only need to look into NEH.
	 */
	if (DN_KEY_GT(q->S, pipe->V)) {	/* Not eligible */
	    if (pipe->scheduler_heap.elements == 0)
		kprintf("++ ouch! not eligible but empty scheduler!\n");
	    heap_insert(&pipe->not_eligible_heap, q->S, q);
	} else {
	    heap_insert(&pipe->scheduler_heap, q->F, q);
	    if (pipe->numbytes >= 0) {	/* Pipe is idle */
		if (pipe->scheduler_heap.elements != 1)
		    kprintf("*** OUCH! pipe should have been idle!\n");
		DPRINTF("Waking up pipe %d at %d\n",
			pipe->pipe_nr, (int)(q->F >> MY_M));
		pipe->sched_time = curr_time;
		ready_event_wfq(pipe);
	    }
	}
    }
done:
    crit_exit();
    return 0;

dropit:
    crit_exit();
    if (q)
	q->drops++;
    m_freem(m);
    return ((fs && (fs->flags_fs & DN_NOERROR)) ? 0 : ENOBUFS);
}

/*
 * Below, the rt_unref is only needed when (pkt->dn_dir == DN_TO_IP_OUT)
 * Doing this would probably save us the initial bzero of dn_pkt
 */
#define DN_FREE_PKT(pkt)	{		\
	struct dn_pkt *n = pkt;			\
	pkt = n->dn_next;			\
	rt_unref (n->ro.ro_rt);			\
	m_freem(n->dn_m);	}

/*
 * Dispose all packets and flow_queues on a flow_set.
 * If all=1, also remove red lookup table and other storage,
 * including the descriptor itself.
 * For the one in dn_pipe MUST also cleanup ready_heap...
 */
static void
purge_flow_set(struct dn_flow_set *fs, int all)
{
    int i;

    for (i = 0; i <= fs->rq_size; i++) {
	struct dn_flow_queue *q, *qn;

	for (q = fs->rq[i]; q; q = qn) {
	    struct dn_pkt *pkt;

	    for (pkt = q->head; pkt;)
		DN_FREE_PKT(pkt);

	    qn = q->next;
	    kfree(q, M_DUMMYNET);
	}
	fs->rq[i] = NULL;
    }
    fs->rq_elements = 0;

    if (all) {
	/* RED - free lookup table */
	if (fs->w_q_lookup)
	    kfree(fs->w_q_lookup, M_DUMMYNET);

	if (fs->rq)
	    kfree(fs->rq, M_DUMMYNET);

	/* If this fs is not part of a pipe, free it */
	if (fs->pipe && fs != &fs->pipe->fs)
	    kfree(fs, M_DUMMYNET);
    }
}

/*
 * Dispose all packets queued on a pipe (not a flow_set).
 * Also free all resources associated to a pipe, which is about
 * to be deleted.
 */
static void
purge_pipe(struct dn_pipe *pipe)
{
    struct dn_pkt *pkt;

    purge_flow_set(&pipe->fs, 1);

    for (pkt = pipe->head; pkt;)
	DN_FREE_PKT(pkt);

    heap_free(&pipe->scheduler_heap);
    heap_free(&pipe->not_eligible_heap);
    heap_free(&pipe->idle_heap);
}

/*
 * Delete all pipes and heaps returning memory. Must also
 * remove references from all ipfw rules to all pipes.
 */
static void
dummynet_flush(void)
{
    struct dn_pipe *p;
    struct dn_flow_set *fs;

    crit_enter();

    /* Remove all references to pipes ... */
    flush_pipe_ptrs(NULL);

    /* Prevent future matches... */
    p = all_pipes;
    all_pipes = NULL;
    fs = all_flow_sets;
    all_flow_sets = NULL;

    /* Free heaps so we don't have unwanted events */
    heap_free(&ready_heap);
    heap_free(&wfq_ready_heap);
    heap_free(&extract_heap);

    crit_exit();

    /*
     * Now purge all queued pkts and delete all pipes
     */
    /* Scan and purge all flow_sets. */
    while (fs != NULL) {
	struct dn_flow_set *curr_fs = fs;

	fs = curr_fs->next;
	purge_flow_set(curr_fs, 1);
    }
    while (p != NULL) {
	struct dn_pipe *curr_p = p;

	p = curr_p->next;
	purge_pipe(curr_p);
	kfree(curr_p, M_DUMMYNET);
    }
}


extern struct ip_fw *ip_fw_default_rule;

static void
dn_rule_delete_fs(struct dn_flow_set *fs, void *r)
{
    int i;

    for (i = 0; i <= fs->rq_size; i++) { /* Last one is ovflow */
	struct dn_flow_queue *q;

	for (q = fs->rq[i]; q; q = q->next) {
	    struct dn_pkt *pkt;

	    for (pkt = q->head; pkt; pkt = pkt->dn_next) {
		if (pkt->rule == r)
		    pkt->rule = ip_fw_default_rule;
	    }
	}
    }
}

/*
 * When a firewall rule is deleted, scan all queues and remove the flow-id
 * from packets matching this rule.
 */
void
dn_rule_delete(void *r)
{
    struct dn_pipe *p;
    struct dn_flow_set *fs;

    /*
     * If the rule references a queue (dn_flow_set), then scan
     * the flow set, otherwise scan pipes. Should do either, but doing
     * both does not harm.
     */

    for (fs = all_flow_sets; fs; fs = fs->next)
	dn_rule_delete_fs(fs, r);

    for (p = all_pipes; p; p = p->next) {
	struct dn_pkt *pkt;

	fs = &p->fs;
	dn_rule_delete_fs(fs, r);

	for (pkt = p->head; pkt; pkt = pkt->dn_next) {
	    if (pkt->rule == r)
		pkt->rule = ip_fw_default_rule;
	}
    }
}

/*
 * setup RED parameters
 */
static int
config_red(const struct dn_ioc_flowset *ioc_fs, struct dn_flow_set *x)
{
    int i;

    x->w_q = ioc_fs->w_q;
    x->min_th = SCALE(ioc_fs->min_th);
    x->max_th = SCALE(ioc_fs->max_th);
    x->max_p = ioc_fs->max_p;

    x->c_1 = ioc_fs->max_p / (ioc_fs->max_th - ioc_fs->min_th);
    x->c_2 = SCALE_MUL(x->c_1, SCALE(ioc_fs->min_th));
    if (x->flags_fs & DN_IS_GENTLE_RED) {
	x->c_3 = (SCALE(1) - ioc_fs->max_p) / ioc_fs->max_th;
	x->c_4 = (SCALE(1) - 2 * ioc_fs->max_p);
    }

    /* If the lookup table already exist, free and create it again */
    if (x->w_q_lookup) {
	kfree(x->w_q_lookup, M_DUMMYNET);
	x->w_q_lookup = NULL ;
    }

    if (red_lookup_depth == 0) {
	kprintf("net.inet.ip.dummynet.red_lookup_depth must be > 0\n");
	kfree(x, M_DUMMYNET);
	return EINVAL;
    }
    x->lookup_depth = red_lookup_depth;
    x->w_q_lookup = kmalloc(x->lookup_depth * sizeof(int),
    			    M_DUMMYNET, M_WAITOK);

    /* Fill the lookup table with (1 - w_q)^x */
    x->lookup_step = ioc_fs->lookup_step;
    x->lookup_weight = ioc_fs->lookup_weight;

    x->w_q_lookup[0] = SCALE(1) - x->w_q;
    for (i = 1; i < x->lookup_depth; i++)
	x->w_q_lookup[i] = SCALE_MUL(x->w_q_lookup[i - 1], x->lookup_weight);

    if (red_avg_pkt_size < 1)
	red_avg_pkt_size = 512;
    x->avg_pkt_size = red_avg_pkt_size;

    if (red_max_pkt_size < 1)
	red_max_pkt_size = 1500;
    x->max_pkt_size = red_max_pkt_size;

    return 0;
}

static void
alloc_hash(struct dn_flow_set *x, const struct dn_ioc_flowset *ioc_fs)
{
    if (x->flags_fs & DN_HAVE_FLOW_MASK) {
	int l = ioc_fs->rq_size;

	/* Allocate some slots */
	if (l == 0)
	    l = dn_hash_size;

	if (l < DN_MIN_HASH_SIZE)
	    l = DN_MIN_HASH_SIZE;
	else if (l > DN_MAX_HASH_SIZE)
	    l = DN_MAX_HASH_SIZE;

	x->rq_size = l;
    } else {
	/* One is enough for null mask */
	x->rq_size = 1;
    }
    x->rq = kmalloc((1 + x->rq_size) * sizeof(struct dn_flow_queue *),
		    M_DUMMYNET, M_WAITOK | M_ZERO);
    x->rq_elements = 0;
}

static void
set_flowid_parms(struct ipfw_flow_id *id, const struct dn_ioc_flowid *ioc_id)
{
    id->dst_ip = ioc_id->u.ip.dst_ip;
    id->src_ip = ioc_id->u.ip.src_ip;
    id->dst_port = ioc_id->u.ip.dst_port;
    id->src_port = ioc_id->u.ip.src_port;
    id->proto = ioc_id->u.ip.proto;
    id->flags = ioc_id->u.ip.flags;
}

static void
set_fs_parms(struct dn_flow_set *x, const struct dn_ioc_flowset *ioc_fs)
{
    x->flags_fs = ioc_fs->flags_fs;
    x->qsize = ioc_fs->qsize;
    x->plr = ioc_fs->plr;
    set_flowid_parms(&x->flow_mask, &ioc_fs->flow_mask);
    if (x->flags_fs & DN_QSIZE_IS_BYTES) {
	if (x->qsize > 1024 * 1024)
	    x->qsize = 1024 * 1024;
    } else {
	if (x->qsize == 0 || x->qsize > 100)
	    x->qsize = 50;
    }

    /* Configuring RED */
    if (x->flags_fs & DN_IS_RED)
	config_red(ioc_fs, x);	/* XXX should check errors */
}

/*
 * setup pipe or queue parameters.
 */

static int
config_pipe(struct dn_ioc_pipe *ioc_pipe)
{
    struct dn_ioc_flowset *ioc_fs = &ioc_pipe->fs;
    int error;

    /*
     * The config program passes parameters as follows:
     * bw	bits/second (0 means no limits)
     * delay	ms (must be translated into ticks)
     * qsize	slots or bytes
     */
    ioc_pipe->delay = (ioc_pipe->delay * dn_hz) / 1000;

    /*
     * We need either a pipe number or a flow_set number
     */
    if (ioc_pipe->pipe_nr == 0 && ioc_fs->fs_nr == 0)
	return EINVAL;
    if (ioc_pipe->pipe_nr != 0 && ioc_fs->fs_nr != 0)
	return EINVAL;

    crit_enter();

    error = EINVAL;
    if (ioc_pipe->pipe_nr != 0) {	/* This is a pipe */
	struct dn_pipe *x, *a, *b;

	/* Locate pipe */
	for (a = NULL, b = all_pipes; b && b->pipe_nr < ioc_pipe->pipe_nr;
	     a = b, b = b->next)
	    ;	/* EMPTY */

	if (b == NULL || b->pipe_nr != ioc_pipe->pipe_nr) { /* New pipe */
	    x = kmalloc(sizeof(struct dn_pipe), M_DUMMYNET, M_WAITOK | M_ZERO);
	    x->pipe_nr = ioc_pipe->pipe_nr;
	    x->fs.pipe = x;

	    /*
	     * idle_heap is the only one from which we extract from the middle.
	     */
	    x->idle_heap.size = x->idle_heap.elements = 0;
	    x->idle_heap.offset = __offsetof(struct dn_flow_queue, heap_pos);
	} else {
	    int i;

	    x = b;

	    /* Flush accumulated credit for all queues */
	    for (i = 0; i <= x->fs.rq_size; i++) {
		struct dn_flow_queue *q;

		for (q = x->fs.rq[i]; q; q = q->next)
		    q->numbytes = 0;
	    }
	}

	x->bandwidth = ioc_pipe->bandwidth;
	x->numbytes = 0; /* Just in case... */
	x->delay = ioc_pipe->delay;

	set_fs_parms(&x->fs, ioc_fs);

	if (x->fs.rq == NULL) {	/* A new pipe */
	    alloc_hash(&x->fs, ioc_fs);

	    x->next = b;
	    if (a == NULL)
		all_pipes = x;
	    else
		a->next = x;
	}
    } else {	/* Config flow_set */
	struct dn_flow_set *x, *a, *b;

	/* Locate flow_set */
	for (a = NULL, b = all_flow_sets; b && b->fs_nr < ioc_fs->fs_nr;
	     a = b, b = b->next)
	    ;	/* EMPTY */

	if (b == NULL || b->fs_nr != ioc_fs->fs_nr) { /* New flow_set */
	    if (ioc_fs->parent_nr == 0)	/* Need link to a pipe */
		goto back;

	    x = kmalloc(sizeof(struct dn_flow_set), M_DUMMYNET,
	    		M_WAITOK | M_ZERO);
	    x->fs_nr = ioc_fs->fs_nr;
	    x->parent_nr = ioc_fs->parent_nr;
	    x->weight = ioc_fs->weight;
	    if (x->weight == 0)
		x->weight = 1;
	    else if (x->weight > 100)
		x->weight = 100;
	} else {
	    /* Change parent pipe not allowed; must delete and recreate */
	    if (ioc_fs->parent_nr != 0 && b->parent_nr != ioc_fs->parent_nr)
		goto back;
	    x = b;
	}

	set_fs_parms(x, ioc_fs);

	if (x->rq == NULL) {	/* A new flow_set */
	    alloc_hash(x, ioc_fs);

	    x->next = b;
	    if (a == NULL)
		all_flow_sets = x;
	    else
		a->next = x;
	}
    }
    error = 0;

back:
    crit_exit();
    return error;
}

/*
 * Helper function to remove from a heap queues which are linked to
 * a flow_set about to be deleted.
 */
static void
fs_remove_from_heap(struct dn_heap *h, struct dn_flow_set *fs)
{
    int i = 0, found = 0;

    while (i < h->elements) {
	if (((struct dn_flow_queue *)h->p[i].object)->fs == fs) {
	    h->elements--;
	    h->p[i] = h->p[h->elements];
	    found++;
	} else {
	    i++;
	}
    }
    if (found)
	heapify(h);
}

/*
 * helper function to remove a pipe from a heap (can be there at most once)
 */
static void
pipe_remove_from_heap(struct dn_heap *h, struct dn_pipe *p)
{
    if (h->elements > 0) {
	int i;

	for (i = 0; i < h->elements; i++) {
	    if (h->p[i].object == p) { /* found it */
		h->elements--;
		h->p[i] = h->p[h->elements];
		heapify(h);
		break;
	    }
	}
    }
}

/*
 * drain all queues. Called in case of severe mbuf shortage.
 */
void
dummynet_drain(void)
{
    struct dn_flow_set *fs;
    struct dn_pipe *p;
    struct dn_pkt *pkt;

    heap_free(&ready_heap);
    heap_free(&wfq_ready_heap);
    heap_free(&extract_heap);

    /* remove all references to this pipe from flow_sets */
    for (fs = all_flow_sets; fs; fs= fs->next)
	purge_flow_set(fs, 0);

    for (p = all_pipes; p; p= p->next) {
	purge_flow_set(&p->fs, 0);
	for (pkt = p->head; pkt ;)
	    DN_FREE_PKT(pkt);
	p->head = p->tail = NULL;
    }
}

/*
 * Fully delete a pipe or a queue, cleaning up associated info.
 */
static int
delete_pipe(const struct dn_ioc_pipe *ioc_pipe)
{
    int error;

    if (ioc_pipe->pipe_nr == 0 && ioc_pipe->fs.fs_nr == 0)
	return EINVAL;
    if (ioc_pipe->pipe_nr != 0 && ioc_pipe->fs.fs_nr != 0)
	return EINVAL;

    crit_enter();

    error = EINVAL;
    if (ioc_pipe->pipe_nr != 0) {	/* This is an old-style pipe */
	struct dn_pipe *a, *b;
	struct dn_flow_set *fs;

	/* Locate pipe */
	for (a = NULL, b = all_pipes; b && b->pipe_nr < ioc_pipe->pipe_nr;
	     a = b, b = b->next)
	    ;	/* EMPTY */
	if (b == NULL || b->pipe_nr != ioc_pipe->pipe_nr)
	    goto back; /* Not found */

	/* Unlink from list of pipes */
	if (a == NULL)
	    all_pipes = b->next;
	else
	    a->next = b->next;

	/* Remove references to this pipe from the ip_fw rules. */
	flush_pipe_ptrs(&b->fs);

	/* Remove all references to this pipe from flow_sets */
	for (fs = all_flow_sets; fs; fs = fs->next) {
	    if (fs->pipe == b) {
		kprintf("++ ref to pipe %d from fs %d\n",
			ioc_pipe->pipe_nr, fs->fs_nr);
		fs->pipe = NULL;
		purge_flow_set(fs, 0);
	    }
	}
	fs_remove_from_heap(&ready_heap, &b->fs);
	purge_pipe(b);	/* Remove all data associated to this pipe */

	/* Remove reference to here from extract_heap and wfq_ready_heap */
	pipe_remove_from_heap(&extract_heap, b);
	pipe_remove_from_heap(&wfq_ready_heap, b);

	kfree(b, M_DUMMYNET);
    } else {	/* This is a WF2Q queue (dn_flow_set) */
	struct dn_flow_set *a, *b;

	/* Locate flow_set */
	for (a = NULL, b = all_flow_sets; b && b->fs_nr < ioc_pipe->fs.fs_nr;
	     a = b, b = b->next)
	    ;	/* EMPTY */
	if (b == NULL || b->fs_nr != ioc_pipe->fs.fs_nr)
	    goto back; /* Not found */

	if (a == NULL)
	    all_flow_sets = b->next;
	else
	    a->next = b->next;

	/* Remove references to this flow_set from the ip_fw rules. */
	flush_pipe_ptrs(b);

	if (b->pipe != NULL) {
	    /* Update total weight on parent pipe and cleanup parent heaps */
	    b->pipe->sum -= b->weight * b->backlogged;
	    fs_remove_from_heap(&b->pipe->not_eligible_heap, b);
	    fs_remove_from_heap(&b->pipe->scheduler_heap, b);
#if 1	/* XXX should i remove from idle_heap as well ? */
	    fs_remove_from_heap(&b->pipe->idle_heap, b);
#endif
	}
	purge_flow_set(b, 1);
    }
    error = 0;

back:
    crit_exit();
    return error;
}

/*
 * helper function used to copy data from kernel in DUMMYNET_GET
 */
static void
dn_copy_flowid(const struct ipfw_flow_id *id, struct dn_ioc_flowid *ioc_id)
{
    ioc_id->type = ETHERTYPE_IP;
    ioc_id->u.ip.dst_ip = id->dst_ip;
    ioc_id->u.ip.src_ip = id->src_ip;
    ioc_id->u.ip.dst_port = id->dst_port;
    ioc_id->u.ip.src_port = id->src_port;
    ioc_id->u.ip.proto = id->proto;
    ioc_id->u.ip.flags = id->flags;
}

static void *
dn_copy_flowqueues(const struct dn_flow_set *fs, void *bp)
{
    const struct dn_flow_queue *q;
    struct dn_ioc_flowqueue *ioc_fq = bp;
    int i, copied = 0;

    for (i = 0; i <= fs->rq_size; i++) {
	for (q = fs->rq[i]; q; q = q->next, ioc_fq++) {
	    if (q->hash_slot != i) {	/* XXX ASSERT */
		kprintf("++ at %d: wrong slot (have %d, "
			"should be %d)\n", copied, q->hash_slot, i);
	    }
	    if (q->fs != fs) {		/* XXX ASSERT */
		kprintf("++ at %d: wrong fs ptr (have %p, should be %p)\n",
			i, q->fs, fs);
	    }

	    copied++;

	    ioc_fq->len = q->len;
	    ioc_fq->len_bytes = q->len_bytes;
	    ioc_fq->tot_pkts = q->tot_pkts;
	    ioc_fq->tot_bytes = q->tot_bytes;
	    ioc_fq->drops = q->drops;
	    ioc_fq->hash_slot = q->hash_slot;
	    ioc_fq->S = q->S;
	    ioc_fq->F = q->F;
	    dn_copy_flowid(&q->id, &ioc_fq->id);
	}
    }

    if (copied != fs->rq_elements) {	/* XXX ASSERT */
	kprintf("++ wrong count, have %d should be %d\n",
		copied, fs->rq_elements);
    }
    return ioc_fq;
}

static void
dn_copy_flowset(const struct dn_flow_set *fs, struct dn_ioc_flowset *ioc_fs,
		u_short fs_type)
{
    ioc_fs->fs_type = fs_type;

    ioc_fs->fs_nr = fs->fs_nr;
    ioc_fs->flags_fs = fs->flags_fs;
    ioc_fs->parent_nr = fs->parent_nr;

    ioc_fs->weight = fs->weight;
    ioc_fs->qsize = fs->qsize;
    ioc_fs->plr = fs->plr;

    ioc_fs->rq_size = fs->rq_size;
    ioc_fs->rq_elements = fs->rq_elements;

    ioc_fs->w_q = fs->w_q;
    ioc_fs->max_th = fs->max_th;
    ioc_fs->min_th = fs->min_th;
    ioc_fs->max_p = fs->max_p;

    dn_copy_flowid(&fs->flow_mask, &ioc_fs->flow_mask);
}

static int
dummynet_get(struct sockopt *sopt)
{
    struct dn_flow_set *fs;
    struct dn_pipe *pipe;
    char *buf, *bp;
    size_t size = 0;
    int error = 0;

    crit_enter();

    /*
     * Compute size of data structures: list of pipes and flow_sets.
     */
    for (pipe = all_pipes; pipe; pipe = pipe->next) {
	size += sizeof(struct dn_ioc_pipe) +
	    pipe->fs.rq_elements * sizeof(struct dn_ioc_flowqueue);
    }

    for (fs = all_flow_sets; fs; fs = fs->next) {
	size += sizeof(struct dn_ioc_flowset) +
	    fs->rq_elements * sizeof(struct dn_ioc_flowqueue);
    }

    bp = buf = kmalloc(size, M_TEMP, M_WAITOK | M_ZERO);

    for (pipe = all_pipes; pipe; pipe = pipe->next) {
	struct dn_ioc_pipe *ioc_pipe = (struct dn_ioc_pipe *)bp;

	/*
	 * Copy flow set descriptor associated with this pipe
	 */
	dn_copy_flowset(&pipe->fs, &ioc_pipe->fs, DN_IS_PIPE);

	/*
	 * Copy pipe descriptor
	 */
	ioc_pipe->bandwidth = pipe->bandwidth;
	ioc_pipe->pipe_nr = pipe->pipe_nr;
	ioc_pipe->V = pipe->V;
	/* Convert delay to milliseconds */
	ioc_pipe->delay = (pipe->delay * 1000) / dn_hz;

	/*
	 * Copy flow queue descriptors
	 */
	bp += sizeof(*ioc_pipe);
	bp = dn_copy_flowqueues(&pipe->fs, bp);
    }

    for (fs = all_flow_sets; fs; fs = fs->next) {
	struct dn_ioc_flowset *ioc_fs = (struct dn_ioc_flowset *)bp;

	/*
	 * Copy flow set descriptor
	 */
	dn_copy_flowset(fs, ioc_fs, DN_IS_QUEUE);

	/*
	 * Copy flow queue descriptors
	 */
	bp += sizeof(*ioc_fs);
	bp = dn_copy_flowqueues(fs, bp);
    }

    crit_exit();

    error = sooptcopyout(sopt, buf, size);
    kfree(buf, M_TEMP);
    return error;
}

/*
 * Handler for the various dummynet socket options (get, flush, config, del)
 */
static int
ip_dn_ctl(struct sockopt *sopt)
{
    struct dn_ioc_pipe tmp_ioc_pipe;
    int error = 0;

    /* Disallow sets in really-really secure mode. */
    if (sopt->sopt_dir == SOPT_SET) {
	if (securelevel >= 3)
	    return EPERM;
    }

    switch (sopt->sopt_name) {
    case IP_DUMMYNET_GET:
	error = dummynet_get(sopt);
	break;

    case IP_DUMMYNET_FLUSH:
	dummynet_flush();
	break;

    case IP_DUMMYNET_CONFIGURE:
	error = sooptcopyin(sopt, &tmp_ioc_pipe, sizeof(tmp_ioc_pipe),
			    sizeof(tmp_ioc_pipe));
	if (error)
	    break;
	error = config_pipe(&tmp_ioc_pipe);
	break;

    case IP_DUMMYNET_DEL:	/* Remove a pipe or flow_set */
	error = sooptcopyin(sopt, &tmp_ioc_pipe, sizeof(tmp_ioc_pipe),
			    sizeof(tmp_ioc_pipe));
	if (error)
	    break;
	error = delete_pipe(&tmp_ioc_pipe);
	break;

    default:
	kprintf("%s -- unknown option %d\n", __func__, sopt->sopt_name);
	error = EINVAL;
	break;
    }
    return error;
}

static void
dummynet_clock(systimer_t info __unused, struct intrframe *frame __unused)
{
    KASSERT(mycpu->gd_cpuid == dn_cpu,
	    ("systimer comes on a different cpu!\n"));

    crit_enter();
    if (dn_netmsg.nm_lmsg.ms_flags & MSGF_DONE)
	lwkt_sendmsg(cpu_portfn(mycpu->gd_cpuid), &dn_netmsg.nm_lmsg);
    crit_exit();
}

static int
sysctl_dn_hz(SYSCTL_HANDLER_ARGS)
{
    int error, val;

    val = dn_hz;
    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error || req->newptr == NULL)
	return error;
    if (val <= 0)
	return EINVAL;
    else if (val > DN_CALLOUT_FREQ_MAX)
	val = DN_CALLOUT_FREQ_MAX;

    crit_enter();
    dn_hz = val;
    systimer_adjust_periodic(&dn_clock, val);
    crit_exit();

    return 0;
}

static void
ip_dn_register_systimer(struct netmsg *msg)
{
    systimer_init_periodic_nq(&dn_clock, dummynet_clock, NULL, dn_hz);
    lwkt_replymsg(&msg->nm_lmsg, 0);
}

static void
ip_dn_deregister_systimer(struct netmsg *msg)
{
    systimer_del(&dn_clock);
    lwkt_replymsg(&msg->nm_lmsg, 0);
}

static void
ip_dn_init(void)
{
    struct netmsg smsg;
    lwkt_port_t port;

    kprintf("DUMMYNET initialized (011031)\n");

    all_pipes = NULL;
    all_flow_sets = NULL;

    ready_heap.size = ready_heap.elements = 0;
    ready_heap.offset = 0;

    wfq_ready_heap.size = wfq_ready_heap.elements = 0;
    wfq_ready_heap.offset = 0;

    extract_heap.size = extract_heap.elements = 0;
    extract_heap.offset = 0;

    ip_dn_ctl_ptr = ip_dn_ctl;
    ip_dn_io_ptr = dummynet_io;
    ip_dn_ruledel_ptr = dn_rule_delete;

    netmsg_init(&dn_netmsg, &netisr_adone_rport, 0, dummynet);

    netmsg_init(&smsg, &curthread->td_msgport, 0, ip_dn_register_systimer);
    port = cpu_portfn(dn_cpu);
    lwkt_domsg(port, &smsg.nm_lmsg, 0);
}

static void
ip_dn_stop(void)
{
    struct netmsg smsg;
    lwkt_port_t port;

    netmsg_init(&smsg, &curthread->td_msgport, 0, ip_dn_deregister_systimer);
    port = cpu_portfn(dn_cpu);
    lwkt_domsg(port, &smsg.nm_lmsg, 0);

    dummynet_flush();

    ip_dn_ctl_ptr = NULL;
    ip_dn_io_ptr = NULL;
    ip_dn_ruledel_ptr = NULL;

    netmsg_service_sync();
}

static int
dummynet_modevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
	crit_enter();
	if (DUMMYNET_LOADED) {
	    crit_exit();
	    kprintf("DUMMYNET already loaded\n");
	    return EEXIST;
	}
	ip_dn_init();
	crit_exit();
	break;
    
    case MOD_UNLOAD:
#ifndef KLD_MODULE
	kprintf("dummynet statically compiled, cannot unload\n");
	return EINVAL ;
#else
	crit_enter();
	ip_dn_stop();
	crit_exit();
#endif
	break;

    default:
	break;
    }
    return 0;
}

static moduledata_t dummynet_mod = {
    "dummynet",
    dummynet_modevent,
    NULL
};
DECLARE_MODULE(dummynet, dummynet_mod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_DEPEND(dummynet, ipfw, 1, 1, 1);
MODULE_VERSION(dummynet, 1);
