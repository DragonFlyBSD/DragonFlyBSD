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
 */

#include "opt_ipdn.h"

/*
 * This module implements IP dummynet, a bandwidth limiter/delay emulator.
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
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <net/route.h>

#include <netinet/in_var.h>
#include <netinet/ip_var.h>

#include <net/dummynet/ip_dummynet.h>

#ifdef DUMMYNET_DEBUG
#define DPRINTF(fmt, ...)	kprintf(fmt, __VA_ARGS__)
#else
#define DPRINTF(fmt, ...)	((void)0)
#endif

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

#define DN_NR_HASH_MAX		16
#define DN_NR_HASH_MASK		(DN_NR_HASH_MAX - 1)
#define DN_NR_HASH(nr)		\
	((((nr) >> 12) ^ ((nr) >> 8) ^ ((nr) >> 4) ^ (nr)) & DN_NR_HASH_MASK)

MALLOC_DEFINE(M_DUMMYNET, "dummynet", "dummynet heap");

extern int	ip_dn_cpu;

static dn_key	curr_time = 0;		/* current simulation time */
static int	dn_hash_size = 64;	/* default hash size */
static int	pipe_expire = 1;	/* expire queue if empty */
static int	dn_max_ratio = 16;	/* max queues/buckets ratio */

/*
 * Statistics on number of queue searches and search steps
 */
static int	searches;
static int	search_steps;

/*
 * RED parameters
 */
static int	red_lookup_depth = 256;	/* default lookup table depth */
static int	red_avg_pkt_size = 512;	/* default medium packet size */
static int	red_max_pkt_size = 1500;/* default max packet size */

/*
 * Three heaps contain queues and pipes that the scheduler handles:
 *
 *  + ready_heap	contains all dn_flow_queue related to fixed-rate pipes.
 *  + wfq_ready_heap	contains the pipes associated with WF2Q flows
 *  + extract_heap	contains pipes associated with delay lines.
 */
static struct dn_heap	ready_heap;
static struct dn_heap	extract_heap;
static struct dn_heap	wfq_ready_heap;

static struct dn_pipe_head	pipe_table[DN_NR_HASH_MAX];
static struct dn_flowset_head	flowset_table[DN_NR_HASH_MAX];

/*
 * Variables for dummynet systimer
 */
static struct netmsg_base dn_netmsg;
static struct systimer	dn_clock;
static int		dn_hz = 1000;

static int	sysctl_dn_hz(SYSCTL_HANDLER_ARGS);

SYSCTL_DECL(_net_inet_ip_dummynet);

SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, hash_size, CTLFLAG_RW,
	   &dn_hash_size, 0, "Default hash table size");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, curr_time, CTLFLAG_RD,
	   &curr_time, 0, "Current tick");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, expire, CTLFLAG_RW,
	   &pipe_expire, 0, "Expire queue if empty");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, max_chain_len, CTLFLAG_RW,
	   &dn_max_ratio, 0, "Max ratio between dynamic queues and buckets");

SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, ready_heap, CTLFLAG_RD,
	   &ready_heap.size, 0, "Size of ready heap");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, extract_heap, CTLFLAG_RD,
	   &extract_heap.size, 0, "Size of extract heap");

SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, searches, CTLFLAG_RD,
	   &searches, 0, "Number of queue searches");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, search_steps, CTLFLAG_RD,
	   &search_steps, 0, "Number of queue search steps");

SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_lookup_depth, CTLFLAG_RD,
	   &red_lookup_depth, 0, "Depth of RED lookup table");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_avg_pkt_size, CTLFLAG_RD,
	   &red_avg_pkt_size, 0, "RED Medium packet size");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_max_pkt_size, CTLFLAG_RD,
	   &red_max_pkt_size, 0, "RED Max packet size");

SYSCTL_PROC(_net_inet_ip_dummynet, OID_AUTO, hz, CTLTYPE_INT | CTLFLAG_RW,
	    0, 0, sysctl_dn_hz, "I", "Dummynet callout frequency");

static int	heap_init(struct dn_heap *, int);
static int	heap_insert(struct dn_heap *, dn_key, void *);
static void	heap_extract(struct dn_heap *, void *);

static void	transmit_event(struct dn_pipe *);
static void	ready_event(struct dn_flow_queue *);
static void	ready_event_wfq(struct dn_pipe *);

static int	config_pipe(struct dn_ioc_pipe *);
static void	dummynet_flush(void);

static void	dummynet_clock(systimer_t, int, struct intrframe *);
static void	dummynet(netmsg_t);

static struct dn_pipe *dn_find_pipe(int);
static struct dn_flow_set *dn_locate_flowset(int, int);

typedef void	(*dn_pipe_iter_t)(struct dn_pipe *, void *);
static void	dn_iterate_pipe(dn_pipe_iter_t, void *);

typedef void	(*dn_flowset_iter_t)(struct dn_flow_set *, void *);
static void	dn_iterate_flowset(dn_flowset_iter_t, void *);

static ip_dn_io_t	dummynet_io;
static ip_dn_ctl_t	dummynet_ctl;

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
    int son;

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
	    panic("%s from middle not supported on this heap!!!", __func__);

	father = *((int *)((char *)obj + h->offset));
	if (father < 0 || father >= h->elements) {
	    panic("%s father %d out of bound 0..%d", __func__,
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
 * ready_event_wfq() does something similar with WF2Q queues, and the
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

    while ((pkt = TAILQ_FIRST(&pipe->p_queue)) &&
    	   DN_KEY_LEQ(pkt->output_time, curr_time)) {
	TAILQ_REMOVE(&pipe->p_queue, pkt, dn_next);
	ip_dn_packet_redispatch(pkt);
    }

    /*
     * If there are leftover packets, put into the heap for next event
     */
    if ((pkt = TAILQ_FIRST(&pipe->p_queue)) != NULL) {
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
    TAILQ_REMOVE(&q->queue, pkt, dn_next);
    q->len--;
    q->len_bytes -= len;

    pkt->output_time = curr_time + p->delay;

    TAILQ_INSERT_TAIL(&p->p_queue, pkt, dn_next);
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
    p_was_empty = TAILQ_EMPTY(&p->p_queue);

    /*
     * Schedule fixed-rate queues linked to this pipe:
     * Account for the bw accumulated since last scheduling, then
     * drain as many pkts as allowed by q->numbytes and move to
     * the delay line (in p) computing output time.
     * bandwidth==0 (no limit) means we can drain the whole queue,
     * setting len_scaled = 0 does the job.
     */
    q->numbytes += (curr_time - q->sched_time) * p->bandwidth;
    while ((pkt = TAILQ_FIRST(&q->queue)) != NULL) {
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
    if ((pkt = TAILQ_FIRST(&q->queue)) != NULL) {
    	/* This implies bandwidth != 0 */
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
    int p_was_empty = TAILQ_EMPTY(&p->p_queue);
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
	    struct dn_pkt *pkt = TAILQ_FIRST(&q->queue);
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
		len = TAILQ_FIRST(&q->queue)->dn_m->m_pkthdr.len;
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
	TAILQ_LAST(&p->p_queue, dn_pkt_queue)->output_time += t;
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

static void
dn_expire_pipe_cb(struct dn_pipe *pipe, void *dummy __unused)
{
    if (pipe->idle_heap.elements > 0 &&
	DN_KEY_LT(pipe->idle_heap.p[0].key, pipe->V)) {
	struct dn_flow_queue *q = pipe->idle_heap.p[0].object;

	heap_extract(&pipe->idle_heap, NULL);
	q->S = q->F + 1; /* Mark timestamp as invalid */
	pipe->sum -= q->fs->weight;
    }
}

/*
 * This is called once per tick, or dn_hz times per second.  It is used to
 * increment the current tick counter and schedule expired events.
 */
static void
dummynet(netmsg_t msg)
{
    void *p;
    struct dn_heap *h;
    struct dn_heap *heaps[3];
    int i;

    heaps[0] = &ready_heap;		/* Fixed-rate queues */
    heaps[1] = &wfq_ready_heap;		/* WF2Q queues */
    heaps[2] = &extract_heap;		/* Delay line */

    /* Reply ASAP */
    crit_enter();
    lwkt_replymsg(&msg->lmsg, 0);
    crit_exit();

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

    /* Sweep pipes trying to expire idle flow_queues */
    dn_iterate_pipe(dn_expire_pipe_cb, NULL);
}

/*
 * Unconditionally expire empty queues in case of shortage.
 * Returns the number of queues freed.
 */
static int
expire_queues(struct dn_flow_set *fs)
{
    int i, initial_elements = fs->rq_elements;

    if (fs->last_expired == time_uptime)
	return 0;

    fs->last_expired = time_uptime;

    for (i = 0; i <= fs->rq_size; i++) { /* Last one is overflow */
	struct dn_flow_queue *q, *qn;

	LIST_FOREACH_MUTABLE(q, &fs->rq[i], q_link, qn) {
	    if (!TAILQ_EMPTY(&q->queue) || q->S != q->F + 1)
		continue;

 	    /*
	     * Entry is idle, expire it
	     */
	    LIST_REMOVE(q, q_link);
	    kfree(q, M_DUMMYNET);

	    KASSERT(fs->rq_elements > 0,
		    ("invalid rq_elements %d", fs->rq_elements));
	    fs->rq_elements--;
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
	if (!LIST_EMPTY(&fs->rq[i]))
	    return LIST_FIRST(&fs->rq[i]);
    }

    q = kmalloc(sizeof(*q), M_DUMMYNET, M_INTWAIT | M_NULLOK | M_ZERO);
    if (q == NULL)
	return NULL;

    q->fs = fs;
    q->hash_slot = i;
    q->S = q->F + 1;   /* hack - mark timestamp as invalid */
    TAILQ_INIT(&q->queue);

    LIST_INSERT_HEAD(&fs->rq[i], q, q_link);
    fs->rq_elements++;

    return q;
}

/*
 * Given a flow_set and a pkt in last_pkt, find a matching queue
 * after appropriate masking. The queue is moved to front
 * so that further searches take less time.
 */
static struct dn_flow_queue *
find_queue(struct dn_flow_set *fs, struct dn_flow_id *id)
{
    struct dn_flow_queue *q;
    int i = 0;

    if (!(fs->flags_fs & DN_HAVE_FLOW_MASK)) {
	q = LIST_FIRST(&fs->rq[0]);
    } else {
	struct dn_flow_queue *qn;

	/* First, do the masking */
	id->fid_dst_ip &= fs->flow_mask.fid_dst_ip;
	id->fid_src_ip &= fs->flow_mask.fid_src_ip;
	id->fid_dst_port &= fs->flow_mask.fid_dst_port;
	id->fid_src_port &= fs->flow_mask.fid_src_port;
	id->fid_proto &= fs->flow_mask.fid_proto;
	id->fid_flags = 0; /* we don't care about this one */

	/* Then, hash function */
	i = ((id->fid_dst_ip) & 0xffff) ^
	    ((id->fid_dst_ip >> 15) & 0xffff) ^
	    ((id->fid_src_ip << 1) & 0xffff) ^
	    ((id->fid_src_ip >> 16 ) & 0xffff) ^
	    (id->fid_dst_port << 1) ^ (id->fid_src_port) ^
	    (id->fid_proto);
	i = i % fs->rq_size;

	/*
	 * Finally, scan the current list for a match and
	 * expire idle flow queues
	 */
	searches++;
	LIST_FOREACH_MUTABLE(q, &fs->rq[i], q_link, qn) {
	    search_steps++;
	    if (id->fid_dst_ip == q->id.fid_dst_ip &&
		id->fid_src_ip == q->id.fid_src_ip &&
		id->fid_dst_port == q->id.fid_dst_port &&
		id->fid_src_port == q->id.fid_src_port &&
		id->fid_proto == q->id.fid_proto &&
		id->fid_flags == q->id.fid_flags) {
		break; /* Found */
	    } else if (pipe_expire && TAILQ_EMPTY(&q->queue) &&
	    	       q->S == q->F + 1) {
		/*
		 * Entry is idle and not in any heap, expire it
		 */
		LIST_REMOVE(q, q_link);
		kfree(q, M_DUMMYNET);

		KASSERT(fs->rq_elements > 0,
			("invalid rq_elements %d", fs->rq_elements));
		fs->rq_elements--;
	    }
	}
	if (q && LIST_FIRST(&fs->rq[i]) != q) { /* Found and not in front */
	    LIST_REMOVE(q, q_link);
	    LIST_INSERT_HEAD(&fs->rq[i], q, q_link);
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

static void
dn_iterate_pipe(dn_pipe_iter_t func, void *arg)
{
    int i;

    for (i = 0; i < DN_NR_HASH_MAX; ++i) {
	struct dn_pipe_head *pipe_hdr = &pipe_table[i];
	struct dn_pipe *pipe, *pipe_next;

	LIST_FOREACH_MUTABLE(pipe, pipe_hdr, p_link, pipe_next)
	    func(pipe, arg);
    }
}

static void
dn_iterate_flowset(dn_flowset_iter_t func, void *arg)
{
    int i;

    for (i = 0; i < DN_NR_HASH_MAX; ++i) {
	struct dn_flowset_head *fs_hdr = &flowset_table[i];
	struct dn_flow_set *fs, *fs_next;

	LIST_FOREACH_MUTABLE(fs, fs_hdr, fs_link, fs_next)
	    func(fs, arg);
    }
}

static struct dn_pipe *
dn_find_pipe(int pipe_nr)
{
    struct dn_pipe_head *pipe_hdr;
    struct dn_pipe *p;

    pipe_hdr = &pipe_table[DN_NR_HASH(pipe_nr)];
    LIST_FOREACH(p, pipe_hdr, p_link) {
	if (p->pipe_nr == pipe_nr)
	    break;
    }
    return p;
}

static struct dn_flow_set *
dn_find_flowset(int fs_nr)
{
    struct dn_flowset_head *fs_hdr;
    struct dn_flow_set *fs;

    fs_hdr = &flowset_table[DN_NR_HASH(fs_nr)];
    LIST_FOREACH(fs, fs_hdr, fs_link) {
	if (fs->fs_nr == fs_nr)
	    break;
    }
    return fs;
}

static struct dn_flow_set *
dn_locate_flowset(int pipe_nr, int is_pipe)
{
    struct dn_flow_set *fs = NULL;

    if (!is_pipe) {
	fs = dn_find_flowset(pipe_nr);
    } else {
	struct dn_pipe *p;

	p = dn_find_pipe(pipe_nr);
	if (p != NULL)
	    fs = &p->fs;
    }
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
dummynet_io(struct mbuf *m)
{
    struct dn_pkt *pkt;
    struct m_tag *tag;
    struct dn_flow_set *fs;
    struct dn_pipe *pipe;
    uint64_t len = m->m_pkthdr.len;
    struct dn_flow_queue *q = NULL;
    int is_pipe, pipe_nr;

    tag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
    pkt = m_tag_data(tag);

    is_pipe = pkt->dn_flags & DN_FLAGS_IS_PIPE;
    pipe_nr = pkt->pipe_nr;

    /*
     * This is a dummynet rule, so we expect a O_PIPE or O_QUEUE rule
     */
    fs = dn_locate_flowset(pipe_nr, is_pipe);
    if (fs == NULL)
	goto dropit;	/* This queue/pipe does not exist! */

    pipe = fs->pipe;
    if (pipe == NULL) { /* Must be a queue, try find a matching pipe */
	pipe = dn_find_pipe(fs->parent_nr);
	if (pipe != NULL) {
	    fs->pipe = pipe;
	} else {
	    kprintf("No pipe %d for queue %d, drop pkt\n",
	    	    fs->parent_nr, fs->fs_nr);
	    goto dropit;
	}
    }

    q = find_queue(fs, &pkt->id);
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

    TAILQ_INSERT_TAIL(&q->queue, pkt, dn_next);
    q->len++;
    q->len_bytes += len;

    if (TAILQ_FIRST(&q->queue) != pkt)	/* Flow was not idle, we are done */
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
    return 0;

dropit:
    if (q)
	q->drops++;
    return ENOBUFS;
}

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
#ifdef INVARIANTS
    int rq_elements = 0;
#endif

    for (i = 0; i <= fs->rq_size; i++) {
	struct dn_flow_queue *q;

	while ((q = LIST_FIRST(&fs->rq[i])) != NULL) {
	    struct dn_pkt *pkt;

	    while ((pkt = TAILQ_FIRST(&q->queue)) != NULL) {
	    	TAILQ_REMOVE(&q->queue, pkt, dn_next);
	    	ip_dn_packet_free(pkt);
	    }

	    LIST_REMOVE(q, q_link);
	    kfree(q, M_DUMMYNET);

#ifdef INVARIANTS
	    rq_elements++;
#endif
	}
    }
    KASSERT(rq_elements == fs->rq_elements,
	    ("# rq elements mismatch, freed %d, total %d",
	     rq_elements, fs->rq_elements));
    fs->rq_elements = 0;

    if (all) {
	/* RED - free lookup table */
	if (fs->w_q_lookup)
	    kfree(fs->w_q_lookup, M_DUMMYNET);

	if (fs->rq)
	    kfree(fs->rq, M_DUMMYNET);

	/*
	 * If this fs is not part of a pipe, free it
	 *
	 * fs->pipe == NULL could happen, if 'fs' is a WF2Q and
	 * - No packet belongs to that flow set is delivered by
	 *   dummynet_io(), i.e. parent pipe is not installed yet.
	 * - Parent pipe is deleted.
	 */
	if (fs->pipe == NULL || (fs->pipe && fs != &fs->pipe->fs))
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

    while ((pkt = TAILQ_FIRST(&pipe->p_queue)) != NULL) {
	TAILQ_REMOVE(&pipe->p_queue, pkt, dn_next);
	ip_dn_packet_free(pkt);
    }

    heap_free(&pipe->scheduler_heap);
    heap_free(&pipe->not_eligible_heap);
    heap_free(&pipe->idle_heap);
}

/*
 * Delete all pipes and heaps returning memory.
 */
static void
dummynet_flush(void)
{
    struct dn_pipe_head pipe_list;
    struct dn_flowset_head fs_list;
    struct dn_pipe *p;
    struct dn_flow_set *fs;
    int i;

    /*
     * Prevent future matches...
     */
    LIST_INIT(&pipe_list);
    for (i = 0; i < DN_NR_HASH_MAX; ++i) {
	struct dn_pipe_head *pipe_hdr = &pipe_table[i];

    	while ((p = LIST_FIRST(pipe_hdr)) != NULL) {
	    LIST_REMOVE(p, p_link);
	    LIST_INSERT_HEAD(&pipe_list, p, p_link);
	}
    }

    LIST_INIT(&fs_list);
    for (i = 0; i < DN_NR_HASH_MAX; ++i) {
	struct dn_flowset_head *fs_hdr = &flowset_table[i];

	while ((fs = LIST_FIRST(fs_hdr)) != NULL) {
	    LIST_REMOVE(fs, fs_link);
	    LIST_INSERT_HEAD(&fs_list, fs, fs_link);
	}
    }

    /* Free heaps so we don't have unwanted events */
    heap_free(&ready_heap);
    heap_free(&wfq_ready_heap);
    heap_free(&extract_heap);

    /*
     * Now purge all queued pkts and delete all pipes
     */
    /* Scan and purge all flow_sets. */
    while ((fs = LIST_FIRST(&fs_list)) != NULL) {
	LIST_REMOVE(fs, fs_link);
	purge_flow_set(fs, 1);
    }

    while ((p = LIST_FIRST(&pipe_list)) != NULL) {
	LIST_REMOVE(p, p_link);
	purge_pipe(p);
	kfree(p, M_DUMMYNET);
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
    int i, alloc_size;

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
    alloc_size = x->rq_size + 1;

    x->rq = kmalloc(alloc_size * sizeof(struct dn_flowqueue_head),
		    M_DUMMYNET, M_WAITOK | M_ZERO);
    x->rq_elements = 0;

    for (i = 0; i < alloc_size; ++i)
	LIST_INIT(&x->rq[i]);
}

static void
set_flowid_parms(struct dn_flow_id *id, const struct dn_ioc_flowid *ioc_id)
{
    id->fid_dst_ip = ioc_id->u.ip.dst_ip;
    id->fid_src_ip = ioc_id->u.ip.src_ip;
    id->fid_dst_port = ioc_id->u.ip.dst_port;
    id->fid_src_port = ioc_id->u.ip.src_port;
    id->fid_proto = ioc_id->u.ip.proto;
    id->fid_flags = ioc_id->u.ip.flags;
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

    /*
     * Validate pipe number
     */
    if (ioc_pipe->pipe_nr > DN_PIPE_NR_MAX || ioc_pipe->pipe_nr < 0)
	return EINVAL;

    error = EINVAL;
    if (ioc_pipe->pipe_nr != 0) {	/* This is a pipe */
	struct dn_pipe *x, *p;

	/* Locate pipe */
	p = dn_find_pipe(ioc_pipe->pipe_nr);

	if (p == NULL) {	/* New pipe */
	    x = kmalloc(sizeof(struct dn_pipe), M_DUMMYNET, M_WAITOK | M_ZERO);
	    x->pipe_nr = ioc_pipe->pipe_nr;
	    x->fs.pipe = x;
	    TAILQ_INIT(&x->p_queue);

	    /*
	     * idle_heap is the only one from which we extract from the middle.
	     */
	    x->idle_heap.size = x->idle_heap.elements = 0;
	    x->idle_heap.offset = __offsetof(struct dn_flow_queue, heap_pos);
	} else {
	    int i;

	    x = p;

	    /* Flush accumulated credit for all queues */
	    for (i = 0; i <= x->fs.rq_size; i++) {
		struct dn_flow_queue *q;

		LIST_FOREACH(q, &x->fs.rq[i], q_link)
		    q->numbytes = 0;
	    }
	}

	x->bandwidth = ioc_pipe->bandwidth;
	x->numbytes = 0; /* Just in case... */
	x->delay = ioc_pipe->delay;

	set_fs_parms(&x->fs, ioc_fs);

	if (x->fs.rq == NULL) {	/* A new pipe */
	    struct dn_pipe_head *pipe_hdr;

	    alloc_hash(&x->fs, ioc_fs);

	    pipe_hdr = &pipe_table[DN_NR_HASH(x->pipe_nr)];
	    LIST_INSERT_HEAD(pipe_hdr, x, p_link);
	}
    } else {	/* Config flow_set */
	struct dn_flow_set *x, *fs;

	/* Locate flow_set */
	fs = dn_find_flowset(ioc_fs->fs_nr);

	if (fs == NULL) {	/* New flow_set */
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
	    if (ioc_fs->parent_nr != 0 && fs->parent_nr != ioc_fs->parent_nr)
		goto back;
	    x = fs;
	}

	set_fs_parms(x, ioc_fs);

	if (x->rq == NULL) {	/* A new flow_set */
	    struct dn_flowset_head *fs_hdr;

	    alloc_hash(x, ioc_fs);

	    fs_hdr = &flowset_table[DN_NR_HASH(x->fs_nr)];
	    LIST_INSERT_HEAD(fs_hdr, x, fs_link);
	}
    }
    error = 0;

back:
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

static void
dn_unref_pipe_cb(struct dn_flow_set *fs, void *pipe0)
{
    struct dn_pipe *pipe = pipe0;

    if (fs->pipe == pipe) {
	kprintf("++ ref to pipe %d from fs %d\n",
		pipe->pipe_nr, fs->fs_nr);
	fs->pipe = NULL;
	purge_flow_set(fs, 0);
    }
}

/*
 * Fully delete a pipe or a queue, cleaning up associated info.
 */
static int
delete_pipe(const struct dn_ioc_pipe *ioc_pipe)
{
    struct dn_pipe *p;
    int error;

    if (ioc_pipe->pipe_nr == 0 && ioc_pipe->fs.fs_nr == 0)
	return EINVAL;
    if (ioc_pipe->pipe_nr != 0 && ioc_pipe->fs.fs_nr != 0)
	return EINVAL;

    if (ioc_pipe->pipe_nr > DN_NR_HASH_MAX || ioc_pipe->pipe_nr < 0)
    	return EINVAL;

    error = EINVAL;
    if (ioc_pipe->pipe_nr != 0) {	/* This is an old-style pipe */
	/* Locate pipe */
	p = dn_find_pipe(ioc_pipe->pipe_nr);
	if (p == NULL)
	    goto back; /* Not found */

	/* Unlink from pipe hash table */
	LIST_REMOVE(p, p_link);

	/* Remove all references to this pipe from flow_sets */
	dn_iterate_flowset(dn_unref_pipe_cb, p);

	fs_remove_from_heap(&ready_heap, &p->fs);
	purge_pipe(p);	/* Remove all data associated to this pipe */

	/* Remove reference to here from extract_heap and wfq_ready_heap */
	pipe_remove_from_heap(&extract_heap, p);
	pipe_remove_from_heap(&wfq_ready_heap, p);

	kfree(p, M_DUMMYNET);
    } else {	/* This is a WF2Q queue (dn_flow_set) */
	struct dn_flow_set *fs;

	/* Locate flow_set */
	fs = dn_find_flowset(ioc_pipe->fs.fs_nr);
	if (fs == NULL)
	    goto back; /* Not found */

	LIST_REMOVE(fs, fs_link);

	if ((p = fs->pipe) != NULL) {
	    /* Update total weight on parent pipe and cleanup parent heaps */
	    p->sum -= fs->weight * fs->backlogged;
	    fs_remove_from_heap(&p->not_eligible_heap, fs);
	    fs_remove_from_heap(&p->scheduler_heap, fs);
#if 1	/* XXX should i remove from idle_heap as well ? */
	    fs_remove_from_heap(&p->idle_heap, fs);
#endif
	}
	purge_flow_set(fs, 1);
    }
    error = 0;

back:
    return error;
}

/*
 * helper function used to copy data from kernel in DUMMYNET_GET
 */
static void
dn_copy_flowid(const struct dn_flow_id *id, struct dn_ioc_flowid *ioc_id)
{
    ioc_id->type = ETHERTYPE_IP;
    ioc_id->u.ip.dst_ip = id->fid_dst_ip;
    ioc_id->u.ip.src_ip = id->fid_src_ip;
    ioc_id->u.ip.dst_port = id->fid_dst_port;
    ioc_id->u.ip.src_port = id->fid_src_port;
    ioc_id->u.ip.proto = id->fid_proto;
    ioc_id->u.ip.flags = id->fid_flags;
}

static void *
dn_copy_flowqueues(const struct dn_flow_set *fs, void *bp)
{
    struct dn_ioc_flowqueue *ioc_fq = bp;
    int i, copied = 0;

    for (i = 0; i <= fs->rq_size; i++) {
	const struct dn_flow_queue *q;

	LIST_FOREACH(q, &fs->rq[i], q_link) {
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

	    ioc_fq++;
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

static void
dn_calc_pipe_size_cb(struct dn_pipe *pipe, void *sz)
{
    size_t *size = sz;

    *size += sizeof(struct dn_ioc_pipe) +
	     pipe->fs.rq_elements * sizeof(struct dn_ioc_flowqueue);
}

static void
dn_calc_fs_size_cb(struct dn_flow_set *fs, void *sz)
{
    size_t *size = sz;

    *size += sizeof(struct dn_ioc_flowset) +
	     fs->rq_elements * sizeof(struct dn_ioc_flowqueue);
}

static void
dn_copyout_pipe_cb(struct dn_pipe *pipe, void *bp0)
{
    char **bp = bp0;
    struct dn_ioc_pipe *ioc_pipe = (struct dn_ioc_pipe *)(*bp);

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
    *bp += sizeof(*ioc_pipe);
    *bp = dn_copy_flowqueues(&pipe->fs, *bp);
}

static void
dn_copyout_fs_cb(struct dn_flow_set *fs, void *bp0)
{
    char **bp = bp0;
    struct dn_ioc_flowset *ioc_fs = (struct dn_ioc_flowset *)(*bp);

    /*
     * Copy flow set descriptor
     */
    dn_copy_flowset(fs, ioc_fs, DN_IS_QUEUE);

    /*
     * Copy flow queue descriptors
     */
    *bp += sizeof(*ioc_fs);
    *bp = dn_copy_flowqueues(fs, *bp);
}

static int
dummynet_get(struct dn_sopt *dn_sopt)
{
    char *buf, *bp;
    size_t size = 0;

    /*
     * Compute size of data structures: list of pipes and flow_sets.
     */
    dn_iterate_pipe(dn_calc_pipe_size_cb, &size);
    dn_iterate_flowset(dn_calc_fs_size_cb, &size);

    /*
     * Copyout pipe/flow_set/flow_queue
     */
    bp = buf = kmalloc(size, M_TEMP, M_WAITOK | M_ZERO);
    dn_iterate_pipe(dn_copyout_pipe_cb, &bp);
    dn_iterate_flowset(dn_copyout_fs_cb, &bp);

    /* Temp memory will be freed by caller */
    dn_sopt->dn_sopt_arg = buf;
    dn_sopt->dn_sopt_arglen = size;
    return 0;
}

/*
 * Handler for the various dummynet socket options (get, flush, config, del)
 */
static int
dummynet_ctl(struct dn_sopt *dn_sopt)
{
    int error = 0;

    switch (dn_sopt->dn_sopt_name) {
    case IP_DUMMYNET_GET:
	error = dummynet_get(dn_sopt);
	break;

    case IP_DUMMYNET_FLUSH:
	dummynet_flush();
	break;

    case IP_DUMMYNET_CONFIGURE:
	KKASSERT(dn_sopt->dn_sopt_arglen == sizeof(struct dn_ioc_pipe));
	error = config_pipe(dn_sopt->dn_sopt_arg);
	break;

    case IP_DUMMYNET_DEL:	/* Remove a pipe or flow_set */
	KKASSERT(dn_sopt->dn_sopt_arglen == sizeof(struct dn_ioc_pipe));
	error = delete_pipe(dn_sopt->dn_sopt_arg);
	break;

    default:
	kprintf("%s -- unknown option %d\n", __func__, dn_sopt->dn_sopt_name);
	error = EINVAL;
	break;
    }
    return error;
}

static void
dummynet_clock(systimer_t info __unused, int in_ipi __unused,
    struct intrframe *frame __unused)
{
    KASSERT(mycpuid == ip_dn_cpu,
	    ("dummynet systimer comes on cpu%d, should be %d!",
	     mycpuid, ip_dn_cpu));

    crit_enter();
    if (DUMMYNET_LOADED && (dn_netmsg.lmsg.ms_flags & MSGF_DONE))
	lwkt_sendmsg(netisr_cpuport(mycpuid), &dn_netmsg.lmsg);
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
ip_dn_init_dispatch(netmsg_t msg)
{
    int i, error = 0;

    KASSERT(mycpuid == ip_dn_cpu,
    	    ("%s runs on cpu%d, instead of cpu%d", __func__,
	     mycpuid, ip_dn_cpu));

    crit_enter();

    if (DUMMYNET_LOADED) {
	kprintf("DUMMYNET already loaded\n");
	error = EEXIST;
	goto back;
    }

    kprintf("DUMMYNET initialized (011031)\n");

    for (i = 0; i < DN_NR_HASH_MAX; ++i)
    	LIST_INIT(&pipe_table[i]);

    for (i = 0; i < DN_NR_HASH_MAX; ++i)
	LIST_INIT(&flowset_table[i]);

    ready_heap.size = ready_heap.elements = 0;
    ready_heap.offset = 0;

    wfq_ready_heap.size = wfq_ready_heap.elements = 0;
    wfq_ready_heap.offset = 0;

    extract_heap.size = extract_heap.elements = 0;
    extract_heap.offset = 0;

    ip_dn_ctl_ptr = dummynet_ctl;
    ip_dn_io_ptr = dummynet_io;

    netmsg_init(&dn_netmsg, NULL, &netisr_adone_rport,
		0, dummynet);
    systimer_init_periodic_nq(&dn_clock, dummynet_clock, NULL, dn_hz);

back:
    crit_exit();
    lwkt_replymsg(&msg->lmsg, error);
}

static int
ip_dn_init(void)
{
    struct netmsg_base smsg;

    if (ip_dn_cpu >= ncpus) {
	kprintf("%s: CPU%d does not exist, switch to CPU0\n",
		__func__, ip_dn_cpu);
	ip_dn_cpu = 0;
    }

    netmsg_init(&smsg, NULL, &curthread->td_msgport,
		0, ip_dn_init_dispatch);
    lwkt_domsg(netisr_cpuport(ip_dn_cpu), &smsg.lmsg, 0);
    return smsg.lmsg.ms_error;
}

#ifdef KLD_MODULE

static void
ip_dn_stop_dispatch(netmsg_t msg)
{
    crit_enter();

    dummynet_flush();

    ip_dn_ctl_ptr = NULL;
    ip_dn_io_ptr = NULL;

    systimer_del(&dn_clock);

    crit_exit();
    lwkt_replymsg(&msg->lmsg, 0);
}


static void
ip_dn_stop(void)
{
    struct netmsg_base smsg;

    netmsg_init(&smsg, NULL, &curthread->td_msgport,
		0, ip_dn_stop_dispatch);
    lwkt_domsg(netisr_cpuport(ip_dn_cpu), &smsg.lmsg, 0);

    netmsg_service_sync();
}

#endif	/* KLD_MODULE */

static int
dummynet_modevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
	return ip_dn_init();

    case MOD_UNLOAD:
#ifndef KLD_MODULE
	kprintf("dummynet statically compiled, cannot unload\n");
	return EINVAL;
#else
	ip_dn_stop();
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
MODULE_VERSION(dummynet, 1);
