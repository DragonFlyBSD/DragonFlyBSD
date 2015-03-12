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
 * $FreeBSD: src/sys/netinet/ip_dummynet.h,v 1.10.2.9 2003/05/13 09:31:06 maxim Exp $
 * $DragonFly: src/sys/net/dummynet/ip_dummynet.h,v 1.19 2008/09/20 04:36:51 sephe Exp $
 */

#ifndef _IP_DUMMYNET3_H_
#define _IP_DUMMYNET3_H_

#ifndef _IP_DUMMYNET_H

#define MODULE_DUMMYNET_ID	2
#define MODULE_DUMMYNET_NAME	"dummynet"


#ifdef _KERNEL
//placeholder for kernel
#endif

enum ipfw_dummynet_opcodes {
	O_DUMMYNET_PIPE,
	O_DUMMYNET_QUEUE,
};

/*
 * We start with a heap, which is used in the scheduler to decide when to
 * transmit packets etc.
 *
 * The key for the heap is used for two different values:
 *
 * 1. Timer ticks- max 10K/second, so 32 bits are enough;
 *
 * 2. Virtual times.  These increase in steps of len/x, where len is the
 *	packet length, and x is either the weight of the flow, or the sum
 *	of all weights.
 *	If we limit to max 1000 flows and a max weight of 100, then x needs
 *	17 bits.  The packet size is 16 bits, so we can easily overflow if
 *	we do not allow errors.
 *
 * So we use a key "dn_key" which is 64 bits.
 *
 * MY_M is used as a shift count when doing fixed point arithmetic
 * (a better name would be useful...).
 */
typedef uint64_t	dn_key;	/* sorting key */

/*
 * Number of left shift to obtain a larger precision
 *
 * XXX With this scaling, max 1000 flows, max weight 100, 1Gbit/s, the
 * virtual time wraps every 15 days.
 */
#define MY_M		16

#ifdef _KERNEL

/*
 * A heap entry is made of a key and a pointer to the actual object stored
 * in the heap.
 *
 * The heap is an array of dn_heap_entry entries, dynamically allocated.
 * Current size is "size", with "elements" actually in use.
 *
 * The heap normally supports only ordered insert and extract from the top.
 * If we want to extract an object from the middle of the heap, we have to
 * know where the object itself is located in the heap (or we need to scan
 * the whole array).  To this purpose, an object has a field (int) which
 * contains the index of the object itself into the heap.  When the object
 * is moved, the field must also be updated.  The offset of the index in the
 * object is stored in the 'offset' field in the heap descriptor.  The
 * assumption is that this offset is non-zero if we want to support extract
 * from the middle.
 */
struct dn_heap_entry {
	dn_key key;	/* sorting key.  Topmost element is smallest one */
	void *object;	/* object pointer */
};

struct dn_heap {
	int size;
	int elements;
	int offset; /* XXX if > 0 this is the offset of direct ptr to obj */
	struct dn_heap_entry *p;	/* really an array of "size" entries */
};

struct dn_flow_id {
	uint16_t fid_type;	/* ETHERTYPE_ */
	uint16_t pad;
	union {
		struct {
			uint32_t dst_ip;
			uint32_t src_ip;
			uint16_t dst_port;
			uint16_t src_port;
			uint8_t proto;
			uint8_t flags;
		} inet;
	} fid_u;
#define fid_dst_ip	fid_u.inet.dst_ip
#define fid_src_ip	fid_u.inet.src_ip
#define fid_dst_port	fid_u.inet.dst_port
#define fid_src_port	fid_u.inet.src_port
#define fid_proto	fid_u.inet.proto
#define fid_flags	fid_u.inet.flags
};

typedef void	(*ip_dn_unref_priv_t)(void *);
struct lwkt_port;

/*
 * struct dn_pkt identifies a packet in the dummynet queue, but is also used
 * to tag packets passed back to the various destinations (ip_input(),
 * ip_output() and so on).
 *
 * It is a tag (PACKET_TAG_DUMMYNET) associated with the actual mbuf.
 */
struct dn_pkt {
	struct mbuf *dn_m;
	TAILQ_ENTRY(dn_pkt) dn_next;

	void *dn_priv;
	ip_dn_unref_priv_t dn_unref_priv;

	uint32_t dn_flags;		/* action when packet comes out. */
#define DN_FLAGS_IS_PIPE	0x10
#define DN_FLAGS_DIR_MASK	0x0f
#define DN_TO_IP_OUT		1
#define DN_TO_IP_IN		2
#define DN_TO_ETH_DEMUX		4
#define DN_TO_ETH_OUT		5
#define DN_TO_MAX		6

	dn_key output_time;		/* when the pkt is due for delivery */
	struct ifnet *ifp;		/* interface, for ip_output */
	struct sockaddr_in *dn_dst;
	struct route ro;		/* route, for ip_output. MUST COPY */
	int flags;			/* flags, for ip_output (IPv6 ?) */

	u_short pipe_nr;		/* pipe/flow_set number */
	u_short pad;

	struct dn_flow_id id;	/* flow id */
	int cpuid;			/* target cpuid, for assertion */
	struct lwkt_port *msgport;	/* target msgport */
};
TAILQ_HEAD(dn_pkt_queue, dn_pkt);

/*
 * Overall structure of dummynet (with WF2Q+):
 *
 * In dummynet, packets are selected with the firewall rules, and passed to
 * two different objects: PIPE or QUEUE.
 *
 * A QUEUE is just a queue with configurable size and queue management policy.
 * It is also associated with a mask (to discriminate among different flows),
 * a weight (used to give different shares of the bandwidth to different flows)
 * and a "pipe", which essentially supplies the transmit clock for all queues
 * associated with that pipe.
 *
 * A PIPE emulates a fixed-bandwidth link, whose bandwidth is configurable.
 * The "clock" for a pipe comes from an internal timer.  A pipe is also
 * associated with one (or more, if masks are used) queue, where all packets
 * for that pipe are stored.
 *
 * The bandwidth available on the pipe is shared by the queues associated with
 * that pipe (only one in case the packet is sent to a PIPE) according to the
 * WF2Q+ scheduling algorithm and the configured weights.
 *
 * In general, incoming packets are stored in the appropriate queue, which is
 * then placed into one of a few heaps managed by a scheduler to decide when
 * the packet should be extracted.  The scheduler (a function called dummynet())
 * is run at every timer tick, and grabs queues from the head of the heaps when
 * they are ready for processing.
 *
 * There are three data structures definining a pipe and associated queues:
 *
 *  + dn_pipe, which contains the main configuration parameters related to
 *	delay and bandwidth;
 *  + dn_flow_set, which contains WF2Q+ configuration, flow masks, plr and
 *	RED configuration;
 *  + dn_flow_queue, which is the per-flow queue (containing the packets)
 *
 * Multiple dn_flow_set can be linked to the same pipe, and multiple
 * dn_flow_queue can be linked to the same dn_flow_set.
 * All data structures are linked in a linear list which is used for
 * housekeeping purposes.
 *
 * During configuration, we create and initialize the dn_flow_set and dn_pipe
 * structures (a dn_pipe also contains a dn_flow_set).
 *
 * At runtime: packets are sent to the appropriate dn_flow_set (either WFQ
 * ones, or the one embedded in the dn_pipe for fixed-rate flows), which in
 * turn dispatches them to the appropriate dn_flow_queue (created dynamically
 * according to the masks).
 *
 * The transmit clock for fixed rate flows (ready_event()) selects the
 * dn_flow_queue to be used to transmit the next packet. For WF2Q,
 * wfq_ready_event() extract a pipe which in turn selects the right flow using
 * a number of heaps defined into the pipe itself.
 */

/*
 * Per flow queue.  This contains the flow identifier, the queue of packets,
 * counters, and parameters used to support both RED and WF2Q+.
 *
 * A dn_flow_queue is created and initialized whenever a packet for a new
 * flow arrives.
 */
struct dn_flow_queue {
	struct dn_flow_id id;
	LIST_ENTRY(dn_flow_queue) q_link;

	struct dn_pkt_queue queue;	/* queue of packets */
	u_int len;
	u_int len_bytes;
	u_long numbytes;		/* credit for transmission (dynamic queues) */

	uint64_t tot_pkts;		/* statistics counters */
	uint64_t tot_bytes;
	uint32_t drops;

	int hash_slot;		/* debugging/diagnostic */

	/* RED parameters */
	int avg;			/* average queue length est. (scaled) */
	int count;			/* arrivals since last RED drop */
	int random;			/* random value (scaled) */
	uint32_t q_time;		/* start of queue idle time */

	/* WF2Q+ support */
	struct dn_flow_set *fs;	/* parent flow set */
	int heap_pos;		/* position (index) of struct in heap */
	dn_key sched_time;		/* current time when queue enters ready_heap */

	dn_key S, F;		/* start time, finish time */
	/*
	 * Setting F < S means the timestamp is invalid. We only need
	 * to test this when the queue is empty.
	 */
};
LIST_HEAD(dn_flowqueue_head, dn_flow_queue);

/*
 * flow_set descriptor.  Contains the "template" parameters for the queue
 * configuration, and pointers to the hash table of dn_flow_queue's.
 *
 * The hash table is an array of lists -- we identify the slot by hashing
 * the flow-id, then scan the list looking for a match.
 * The size of the hash table (buckets) is configurable on a per-queue basis.
 *
 * A dn_flow_set is created whenever a new queue or pipe is created (in the
 * latter case, the structure is located inside the struct dn_pipe).
 */
struct dn_flow_set {
	u_short fs_nr;		/* flow_set number */
	u_short flags_fs;		/* see 'Flow set flags' */

	LIST_ENTRY(dn_flow_set) fs_link;

	struct dn_pipe *pipe;	/* pointer to parent pipe */
	u_short parent_nr;		/* parent pipe#, 0 if local to a pipe */

	int weight;			/* WFQ queue weight */
	int qsize;			/* queue size in slots or bytes */
	int plr;			/* pkt loss rate (2^31-1 means 100%) */

	struct dn_flow_id flow_mask;

	/* hash table of queues onto this flow_set */
	int rq_size;		/* number of slots */
	int rq_elements;		/* active elements */
	struct dn_flowqueue_head *rq;/* array of rq_size entries */

	uint32_t last_expired;	/* do not expire too frequently */
	int backlogged;		/* #active queues for this flowset */

	/* RED parameters */
	int w_q;			/* queue weight (scaled) */
	int max_th;			/* maximum threshold for queue (scaled) */
	int min_th;			/* minimum threshold for queue (scaled) */
	int max_p;			/* maximum value for p_b (scaled) */
	u_int c_1;			/* max_p/(max_th-min_th) (scaled) */
	u_int c_2;			/* max_p*min_th/(max_th-min_th) (scaled) */
	u_int c_3;			/* for GRED, (1-max_p)/max_th (scaled) */
	u_int c_4;			/* for GRED, 1 - 2*max_p (scaled) */
	u_int *w_q_lookup;		/* lookup table for computing (1-w_q)^t */
	u_int lookup_depth;		/* depth of lookup table */
	int lookup_step;		/* granularity inside the lookup table */
	int lookup_weight;		/* equal to (1-w_q)^t / (1-w_q)^(t+1) */
	int avg_pkt_size;		/* medium packet size */
	int max_pkt_size;		/* max packet size */
};
LIST_HEAD(dn_flowset_head, dn_flow_set);

/*
 * Pipe descriptor. Contains global parameters, delay-line queue, and the
 * flow_set used for fixed-rate queues.
 *
 * For WF2Q+ support it also has 3 heaps holding dn_flow_queue:
 *  + not_eligible_heap, for queues whose start time is higher than the
 *	virtual time. Sorted by start time.
 *  + scheduler_heap, for queues eligible for scheduling.  Sorted by finish
 *	time.
 *  + idle_heap, all flows that are idle and can be removed.  We do that on
 *	each tick so we do not slow down too much operations during forwarding.
 */
struct dn_pipe {		/* a pipe */
	int pipe_nr;		/* number */
	int bandwidth;		/* really, bytes/tick. */
	int delay;			/* really, ticks */

	struct dn_pkt_queue p_queue;/* packets in delay line */
	LIST_ENTRY(dn_pipe) p_link;

	/* WF2Q+ */
	struct dn_heap scheduler_heap; /* top extract - key Finish time*/
	struct dn_heap not_eligible_heap; /* top extract- key Start time */
	struct dn_heap idle_heap;	/* random extract - key Start=Finish time */

	dn_key V;			/* virtual time */
	int sum;			/* sum of weights of all active sessions */
	int numbytes;		/* bits I can transmit (more or less). */

	dn_key sched_time;		/* time pipe was scheduled in ready_heap */

	struct dn_flow_set fs;	/* used with fixed-rate flows */
};
LIST_HEAD(dn_pipe_head, dn_pipe);

struct dn_sopt {
	int	dn_sopt_name;
	void	*dn_sopt_arg;
	size_t	dn_sopt_arglen;
};

typedef int	ip_dn_ctl_t(struct dn_sopt *);
typedef int	ip_dn_io_t(struct mbuf *);

extern ip_dn_ctl_t	*ip_dn_ctl_ptr;
extern ip_dn_io_t	*ip_dn_io_ptr;

void	ip_dn_queue(struct mbuf *);
void	ip_dn_packet_free(struct dn_pkt *);
void	ip_dn_packet_redispatch(struct dn_pkt *);
int	ip_dn_sockopt(struct sockopt *);

#define	DUMMYNET_LOADED	(ip_dn_io_ptr != NULL)

#endif	/* _KERNEL */

struct dn_ioc_flowid {
	uint16_t type;	/* ETHERTYPE_ */
	uint16_t pad;
	union {
		struct {
			uint32_t dst_ip;
			uint32_t src_ip;
			uint16_t dst_port;
			uint16_t src_port;
			uint8_t proto;
			uint8_t flags;
		} ip;
		uint8_t pad[64];
	} u;
};

struct dn_ioc_flowqueue {
	u_int len;
	u_int len_bytes;

	uint64_t tot_pkts;
	uint64_t tot_bytes;
	uint32_t drops;

	int hash_slot;		/* debugging/diagnostic */
	dn_key S;			/* virtual start time */
	dn_key F;			/* virtual finish time */

	struct dn_ioc_flowid id;
	uint8_t reserved[16];
};

struct dn_ioc_flowset {
	u_short fs_type;		/* DN_IS_{QUEUE,PIPE}, MUST be first */

	u_short fs_nr;		/* flow_set number */
	u_short flags_fs;		/* see 'Flow set flags' */
	u_short parent_nr;		/* parent pipe#, 0 if local to a pipe */

	int weight;			/* WFQ queue weight */
	int qsize;			/* queue size in slots or bytes */
	int plr;			/* pkt loss rate (2^31-1 means 100%) */

	/* Hash table information */
	int rq_size;		/* number of slots */
	int rq_elements;		/* active elements */

	/* RED parameters */
	int w_q;			/* queue weight (scaled) */
	int max_th;			/* maximum threshold for queue (scaled) */
	int min_th;			/* minimum threshold for queue (scaled) */
	int max_p;			/* maximum value for p_b (scaled) */
	int lookup_step;		/* granularity inside the lookup table */
	int lookup_weight;		/* equal to (1-w_q)^t / (1-w_q)^(t+1) */

	struct dn_ioc_flowid flow_mask;
	uint8_t reserved[16];
};

struct dn_ioc_pipe {
	struct dn_ioc_flowset fs;	/* MUST be first */

	int pipe_nr;		/* pipe number */
	int bandwidth;		/* bit/second */
	int delay;			/* milliseconds */

	dn_key V;			/* virtual time */

	uint8_t reserved[16];
};

/*
 * Flow set flags
 */
#define DN_HAVE_FLOW_MASK	0x0001
#define DN_IS_RED		0x0002
#define DN_IS_GENTLE_RED	0x0004
#define DN_QSIZE_IS_BYTES	0x0008	/* queue size is measured in bytes */
#define DN_NOERROR		0x0010	/* do not report ENOBUFS on drops */
#define DN_IS_PIPE		0x4000
#define DN_IS_QUEUE		0x8000

/*
 * Macros for RED
 */
#define SCALE_RED		16
#define SCALE(x)		((x) << SCALE_RED)
#define SCALE_VAL(x)		((x) >> SCALE_RED)
#define SCALE_MUL(x, y)		(((x) * (y)) >> SCALE_RED)

/*
 * Maximum pipe number
 */
#define DN_PIPE_NR_MAX		65536

#endif
#endif /* !_IP_DUMMYNET_H */
