/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
/*
 * LNK_SPAN PROTOCOL SUPPORT FUNCTIONS - Please see sys/dmsg.h for an
 * involved explanation of the protocol.
 */

#include "dmsg_local.h"

/*
 * Maximum spanning tree distance.  This has the practical effect of
 * stopping tail-chasing closed loops when a feeder span is lost.
 */
#define DMSG_SPAN_MAXDIST	16

/*
 * RED-BLACK TREE DEFINITIONS
 *
 * We need to track:
 *
 * (1) shared fsid's (a cluster).
 * (2) unique fsid's (a node in a cluster) <--- LNK_SPAN transactions.
 *
 * We need to aggegate all active LNK_SPANs, aggregate, and create our own
 * outgoing LNK_SPAN transactions on each of our connections representing
 * the aggregated state.
 *
 * h2span_conn		- list of iocom connections who wish to receive SPAN
 *			  propagation from other connections.  Might contain
 *			  a filter string.  Only iocom's with an open
 *			  LNK_CONN transactions are applicable for SPAN
 *			  propagation.
 *
 * h2span_relay		- List of links relayed (via SPAN).  Essentially
 *			  each relay structure represents a LNK_SPAN
 *			  transaction that we initiated, verses h2span_link
 *			  which is a LNK_SPAN transaction that we received.
 *
 * --
 *
 * h2span_cluster	- Organizes the shared fsid's.  One structure for
 *			  each cluster.
 *
 * h2span_node		- Organizes the nodes in a cluster.  One structure
 *			  for each unique {cluster,node}, aka {fsid, pfs_fsid}.
 *
 * h2span_link		- Organizes all incoming and outgoing LNK_SPAN message
 *			  transactions related to a node.
 *
 *			  One h2span_link structure for each incoming LNK_SPAN
 *			  transaction.  Links selected for propagation back
 *			  out are also where the outgoing LNK_SPAN messages
 *			  are indexed into (so we can propagate changes).
 *
 *			  The h2span_link's use a red-black tree to sort the
 *			  distance hop metric for the incoming LNK_SPAN.  We
 *			  then select the top N for outgoing.  When the
 *			  topology changes the top N may also change and cause
 *			  new outgoing LNK_SPAN transactions to be opened
 *			  and less desireable ones to be closed, causing
 *			  transactional aborts within the message flow in
 *			  the process.
 *
 * Also note		- All outgoing LNK_SPAN message transactions are also
 *			  entered into a red-black tree for use by the routing
 *			  function.  This is handled by msg.c in the state
 *			  code, not here.
 */

struct h2span_link;
struct h2span_relay;
TAILQ_HEAD(h2span_conn_queue, h2span_conn);
TAILQ_HEAD(h2span_relay_queue, h2span_relay);

RB_HEAD(h2span_cluster_tree, h2span_cluster);
RB_HEAD(h2span_node_tree, h2span_node);
RB_HEAD(h2span_link_tree, h2span_link);
RB_HEAD(h2span_relay_tree, h2span_relay);
uint32_t DMsgRNSS;

/*
 * Received LNK_CONN transaction enables SPAN protocol over connection.
 * (may contain filter).  Typically one for each mount and several may
 * share the same media.
 */
struct h2span_conn {
	TAILQ_ENTRY(h2span_conn) entry;
	struct h2span_relay_tree tree;
	dmsg_state_t *state;
};

/*
 * All received LNK_SPANs are organized by cluster (pfs_clid),
 * node (pfs_fsid), and link (received LNK_SPAN transaction).
 */
struct h2span_cluster {
	RB_ENTRY(h2span_cluster) rbnode;
	struct h2span_node_tree tree;
	uuid_t	pfs_clid;		/* shared fsid */
	uint8_t	peer_type;
	char	cl_label[128];		/* cluster label (typ PEER_BLOCK) */
	int	refs;			/* prevents destruction */
};

struct h2span_node {
	RB_ENTRY(h2span_node) rbnode;
	struct h2span_link_tree tree;
	struct h2span_cluster *cls;
	uint8_t	pfs_type;
	uuid_t	pfs_fsid;		/* unique fsid */
	char	fs_label[128];		/* fs label (typ PEER_HAMMER2) */
	void	*opaque;
};

struct h2span_link {
	RB_ENTRY(h2span_link) rbnode;
	dmsg_state_t	*state;		/* state<->link */
	struct h2span_node *node;	/* related node */
	uint32_t	dist;
	uint32_t 	rnss;
	struct h2span_relay_queue relayq; /* relay out */
};

/*
 * Any LNK_SPAN transactions we receive which are relayed out other
 * connections utilize this structure to track the LNK_SPAN transactions
 * we initiate (relay out) on other connections.  We only relay out
 * LNK_SPANs on connections we have an open CONN transaction for.
 *
 * The relay structure points to the outgoing LNK_SPAN trans (out_state)
 * and to the incoming LNK_SPAN transaction (in_state).  The relay
 * structure holds refs on the related states.
 *
 * In many respects this is the core of the protocol... actually figuring
 * out what LNK_SPANs to relay.  The spanid used for relaying is the
 * address of the 'state' structure, which is why h2span_relay has to
 * be entered into a RB-TREE based at h2span_conn (so we can look
 * up the spanid to validate it).
 */
struct h2span_relay {
	TAILQ_ENTRY(h2span_relay) entry;	/* from link */
	RB_ENTRY(h2span_relay) rbnode;		/* from h2span_conn */
	struct h2span_conn	*conn;		/* related CONN transaction */
	dmsg_state_t		*source_rt;	/* h2span_link state */
	dmsg_state_t		*target_rt;	/* h2span_relay state */
};

typedef struct h2span_conn h2span_conn_t;
typedef struct h2span_cluster h2span_cluster_t;
typedef struct h2span_node h2span_node_t;
typedef struct h2span_link h2span_link_t;
typedef struct h2span_relay h2span_relay_t;

#define dmsg_termstr(array)	_dmsg_termstr((array), sizeof(array))

static h2span_relay_t *dmsg_generate_relay(h2span_conn_t *conn,
					h2span_link_t *slink);
static uint32_t dmsg_rnss(void);

static __inline
void
_dmsg_termstr(char *base, size_t size)
{
	base[size-1] = 0;
}

/*
 * Cluster peer_type, uuid, AND label must match for a match
 */
static
int
h2span_cluster_cmp(h2span_cluster_t *cls1, h2span_cluster_t *cls2)
{
	int r;

	if (cls1->peer_type < cls2->peer_type)
		return(-1);
	if (cls1->peer_type > cls2->peer_type)
		return(1);
	r = uuid_compare(&cls1->pfs_clid, &cls2->pfs_clid, NULL);
	if (r == 0)
		r = strcmp(cls1->cl_label, cls2->cl_label);

	return r;
}

/*
 * Match against fs_label/pfs_fsid.  Together these two items represent a
 * unique node.  In most cases the primary differentiator is pfs_fsid but
 * we also string-match fs_label.
 */
static
int
h2span_node_cmp(h2span_node_t *node1, h2span_node_t *node2)
{
	int r;

	r = strcmp(node1->fs_label, node2->fs_label);
	if (r == 0)
		r = uuid_compare(&node1->pfs_fsid, &node2->pfs_fsid, NULL);
	return (r);
}

/*
 * Sort/subsort must match h2span_relay_cmp() under any given node
 * to make the aggregation algorithm easier, so the best links are
 * in the same sorted order as the best relays.
 *
 * NOTE: We cannot use link*->state->msgid because this msgid is created
 *	 by each remote host and thus might wind up being the same.
 */
static
int
h2span_link_cmp(h2span_link_t *link1, h2span_link_t *link2)
{
	if (link1->dist < link2->dist)
		return(-1);
	if (link1->dist > link2->dist)
		return(1);
	if (link1->rnss < link2->rnss)
		return(-1);
	if (link1->rnss > link2->rnss)
		return(1);
#if 1
	if ((uintptr_t)link1->state < (uintptr_t)link2->state)
		return(-1);
	if ((uintptr_t)link1->state > (uintptr_t)link2->state)
		return(1);
#else
	if (link1->state->msgid < link2->state->msgid)
		return(-1);
	if (link1->state->msgid > link2->state->msgid)
		return(1);
#endif
	return(0);
}

/*
 * Relay entries are sorted by node, subsorted by distance and link
 * address (so we can match up the conn->tree relay topology with
 * a node's link topology).
 */
static
int
h2span_relay_cmp(h2span_relay_t *relay1, h2span_relay_t *relay2)
{
	h2span_link_t *link1 = relay1->source_rt->any.link;
	h2span_link_t *link2 = relay2->source_rt->any.link;

	if ((intptr_t)link1->node < (intptr_t)link2->node)
		return(-1);
	if ((intptr_t)link1->node > (intptr_t)link2->node)
		return(1);
	if (link1->dist < link2->dist)
		return(-1);
	if (link1->dist > link2->dist)
		return(1);
	if (link1->rnss < link2->rnss)
		return(-1);
	if (link1->rnss > link2->rnss)
		return(1);
#if 1
	if ((uintptr_t)link1->state < (uintptr_t)link2->state)
		return(-1);
	if ((uintptr_t)link1->state > (uintptr_t)link2->state)
		return(1);
#else
	if (link1->state->msgid < link2->state->msgid)
		return(-1);
	if (link1->state->msgid > link2->state->msgid)
		return(1);
#endif
	return(0);
}

RB_PROTOTYPE_STATIC(h2span_cluster_tree, h2span_cluster,
	     rbnode, h2span_cluster_cmp);
RB_PROTOTYPE_STATIC(h2span_node_tree, h2span_node,
	     rbnode, h2span_node_cmp);
RB_PROTOTYPE_STATIC(h2span_link_tree, h2span_link,
	     rbnode, h2span_link_cmp);
RB_PROTOTYPE_STATIC(h2span_relay_tree, h2span_relay,
	     rbnode, h2span_relay_cmp);

RB_GENERATE_STATIC(h2span_cluster_tree, h2span_cluster,
	     rbnode, h2span_cluster_cmp);
RB_GENERATE_STATIC(h2span_node_tree, h2span_node,
	     rbnode, h2span_node_cmp);
RB_GENERATE_STATIC(h2span_link_tree, h2span_link,
	     rbnode, h2span_link_cmp);
RB_GENERATE_STATIC(h2span_relay_tree, h2span_relay,
	     rbnode, h2span_relay_cmp);

/*
 * Global mutex protects cluster_tree lookups, connq, mediaq.
 */
static pthread_mutex_t cluster_mtx;
static struct h2span_cluster_tree cluster_tree = RB_INITIALIZER(cluster_tree);
static struct h2span_conn_queue connq = TAILQ_HEAD_INITIALIZER(connq);
static struct dmsg_media_queue mediaq = TAILQ_HEAD_INITIALIZER(mediaq);

static void dmsg_lnk_span(dmsg_msg_t *msg);
static void dmsg_lnk_conn(dmsg_msg_t *msg);
static void dmsg_lnk_circ(dmsg_msg_t *msg);
static void dmsg_lnk_relay(dmsg_msg_t *msg);
static void dmsg_relay_scan(h2span_conn_t *conn, h2span_node_t *node);
static void dmsg_relay_delete(h2span_relay_t *relay);

void
dmsg_msg_lnk_signal(dmsg_iocom_t *iocom __unused)
{
	pthread_mutex_lock(&cluster_mtx);
	dmsg_relay_scan(NULL, NULL);
	pthread_mutex_unlock(&cluster_mtx);
}

/*
 * DMSG_PROTO_LNK - Generic DMSG_PROTO_LNK.
 *	      (incoming iocom lock not held)
 *
 * This function is typically called for one-way and opening-transactions
 * since state->func is assigned after that, but it will also be called
 * if no state->func is assigned on transaction-open.
 */
void
dmsg_msg_lnk(dmsg_msg_t *msg)
{
	uint32_t icmd = msg->state ? msg->state->icmd : msg->any.head.cmd;

	switch(icmd & DMSGF_BASECMDMASK) {
	case DMSG_LNK_CONN:
		dmsg_lnk_conn(msg);
		break;
	case DMSG_LNK_SPAN:
		dmsg_lnk_span(msg);
		break;
	case DMSG_LNK_CIRC:
		dmsg_lnk_circ(msg);
		break;
	default:
		msg->iocom->usrmsg_callback(msg, 1);
		/* state invalid after reply */
		break;
	}
}

/*
 * LNK_CONN - iocom identify message reception.
 *	      (incoming iocom lock not held)
 *
 * Remote node identifies itself to us, sets up a SPAN filter, and gives us
 * the ok to start transmitting SPANs.
 */
void
dmsg_lnk_conn(dmsg_msg_t *msg)
{
	dmsg_state_t *state = msg->state;
	dmsg_media_t *media;
	h2span_conn_t *conn;
	h2span_relay_t *relay;
	char *alloc = NULL;

	pthread_mutex_lock(&cluster_mtx);

	fprintf(stderr,
		"dmsg_lnk_conn: msg %p cmd %08x state %p "
		"txcmd %08x rxcmd %08x\n",
		msg, msg->any.head.cmd, state,
		state->txcmd, state->rxcmd);

	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
	case DMSG_LNK_CONN | DMSGF_CREATE:
	case DMSG_LNK_CONN | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * On transaction start we allocate a new h2span_conn and
		 * acknowledge the request, leaving the transaction open.
		 * We then relay priority-selected SPANs.
		 */
		fprintf(stderr, "LNK_CONN(%08x): %s/%s/%s\n",
			(uint32_t)msg->any.head.msgid,
			dmsg_uuid_to_str(&msg->any.lnk_conn.pfs_clid,
					    &alloc),
			msg->any.lnk_conn.cl_label,
			msg->any.lnk_conn.fs_label);
		free(alloc);

		conn = dmsg_alloc(sizeof(*conn));

		RB_INIT(&conn->tree);
		state->iocom->conn = conn;	/* XXX only one */
		conn->state = state;
		state->func = dmsg_lnk_conn;
		state->any.conn = conn;
		TAILQ_INSERT_TAIL(&connq, conn, entry);

		/*
		 * Set up media
		 */
		TAILQ_FOREACH(media, &mediaq, entry) {
			if (uuid_compare(&msg->any.lnk_conn.mediaid,
					 &media->mediaid, NULL) == 0) {
				break;
			}
		}
		if (media == NULL) {
			media = dmsg_alloc(sizeof(*media));
			media->mediaid = msg->any.lnk_conn.mediaid;
			TAILQ_INSERT_TAIL(&mediaq, media, entry);
		}
		state->media = media;
		++media->refs;

		if ((msg->any.head.cmd & DMSGF_DELETE) == 0) {
			msg->iocom->usrmsg_callback(msg, 0);
			dmsg_msg_result(msg, 0);
			dmsg_iocom_signal(msg->iocom);
			break;
		}
		/* FALL THROUGH */
	case DMSG_LNK_CONN | DMSGF_DELETE:
	case DMSG_LNK_ERROR | DMSGF_DELETE:
		/*
		 * On transaction terminate we clean out our h2span_conn
		 * and acknowledge the request, closing the transaction.
		 */
		fprintf(stderr, "LNK_CONN: Terminated\n");
		conn = state->any.conn;
		assert(conn);

		/*
		 * Adjust media refs
		 *
		 * Callback will clean out media config / user-opaque state
		 */
		media = state->media;
		--media->refs;
		if (media->refs == 0) {
			fprintf(stderr, "Media shutdown\n");
			TAILQ_REMOVE(&mediaq, media, entry);
			pthread_mutex_unlock(&cluster_mtx);
			msg->iocom->usrmsg_callback(msg, 0);
			pthread_mutex_lock(&cluster_mtx);
			dmsg_free(media);
		}
		state->media = NULL;

		/*
		 * Clean out all relays.  This requires terminating each
		 * relay transaction.
		 */
		while ((relay = RB_ROOT(&conn->tree)) != NULL) {
			dmsg_relay_delete(relay);
		}

		/*
		 * Clean out conn
		 */
		conn->state = NULL;
		msg->state->any.conn = NULL;
		msg->state->iocom->conn = NULL;
		TAILQ_REMOVE(&connq, conn, entry);
		dmsg_free(conn);

		dmsg_msg_reply(msg, 0);
		/* state invalid after reply */
		break;
	default:
		msg->iocom->usrmsg_callback(msg, 1);
#if 0
		if (msg->any.head.cmd & DMSGF_DELETE)
			goto deleteconn;
		dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
#endif
		break;
	}
	pthread_mutex_unlock(&cluster_mtx);
}

/*
 * LNK_SPAN - Spanning tree protocol message reception
 *	      (incoming iocom lock not held)
 *
 * Receive a spanning tree transactional message, creating or destroying
 * a SPAN and propagating it to other iocoms.
 */
void
dmsg_lnk_span(dmsg_msg_t *msg)
{
	dmsg_state_t *state = msg->state;
	h2span_cluster_t dummy_cls;
	h2span_node_t dummy_node;
	h2span_cluster_t *cls;
	h2span_node_t *node;
	h2span_link_t *slink;
	h2span_relay_t *relay;
	char *alloc = NULL;

	assert((msg->any.head.cmd & DMSGF_REPLY) == 0);

	pthread_mutex_lock(&cluster_mtx);

	/*
	 * On transaction start we initialize the tracking infrastructure
	 */
	if (msg->any.head.cmd & DMSGF_CREATE) {
		assert(state->func == NULL);
		state->func = dmsg_lnk_span;

		dmsg_termstr(msg->any.lnk_span.cl_label);
		dmsg_termstr(msg->any.lnk_span.fs_label);

		/*
		 * Find the cluster
		 */
		dummy_cls.pfs_clid = msg->any.lnk_span.pfs_clid;
		dummy_cls.peer_type = msg->any.lnk_span.peer_type;
		bcopy(msg->any.lnk_span.cl_label,
		      dummy_cls.cl_label,
		      sizeof(dummy_cls.cl_label));
		cls = RB_FIND(h2span_cluster_tree, &cluster_tree, &dummy_cls);
		if (cls == NULL) {
			cls = dmsg_alloc(sizeof(*cls));
			cls->pfs_clid = msg->any.lnk_span.pfs_clid;
			cls->peer_type = msg->any.lnk_span.peer_type;
			bcopy(msg->any.lnk_span.cl_label,
			      cls->cl_label,
			      sizeof(cls->cl_label));
			RB_INIT(&cls->tree);
			RB_INSERT(h2span_cluster_tree, &cluster_tree, cls);
		}

		/*
		 * Find the node
		 */
		dummy_node.pfs_fsid = msg->any.lnk_span.pfs_fsid;
		bcopy(msg->any.lnk_span.fs_label, dummy_node.fs_label,
		      sizeof(dummy_node.fs_label));
		node = RB_FIND(h2span_node_tree, &cls->tree, &dummy_node);
		if (node == NULL) {
			node = dmsg_alloc(sizeof(*node));
			node->pfs_fsid = msg->any.lnk_span.pfs_fsid;
			node->pfs_type = msg->any.lnk_span.pfs_type;
			bcopy(msg->any.lnk_span.fs_label,
			      node->fs_label,
			      sizeof(node->fs_label));
			node->cls = cls;
			RB_INIT(&node->tree);
			RB_INSERT(h2span_node_tree, &cls->tree, node);
			if (msg->iocom->node_handler) {
				msg->iocom->node_handler(&node->opaque, msg,
							 DMSG_NODEOP_ADD);
			}
		}

		/*
		 * Create the link
		 */
		assert(state->any.link == NULL);
		slink = dmsg_alloc(sizeof(*slink));
		TAILQ_INIT(&slink->relayq);
		slink->node = node;
		slink->dist = msg->any.lnk_span.dist;
		slink->rnss = msg->any.lnk_span.rnss;
		slink->state = state;
		state->any.link = slink;

		RB_INSERT(h2span_link_tree, &node->tree, slink);

		fprintf(stderr,
			"LNK_SPAN(thr %p): %p %s cl=%s fs=%s dist=%d\n",
			msg->iocom,
			slink,
			dmsg_uuid_to_str(&msg->any.lnk_span.pfs_clid, &alloc),
			msg->any.lnk_span.cl_label,
			msg->any.lnk_span.fs_label,
			msg->any.lnk_span.dist);
		free(alloc);
#if 0
		dmsg_relay_scan(NULL, node);
#endif
		dmsg_iocom_signal(msg->iocom);
	}

	/*
	 * On transaction terminate we remove the tracking infrastructure.
	 */
	if (msg->any.head.cmd & DMSGF_DELETE) {
		slink = state->any.link;
		assert(slink != NULL);
		node = slink->node;
		cls = node->cls;

		fprintf(stderr, "LNK_DELE(thr %p): %p %s cl=%s fs=%s dist=%d\n",
			msg->iocom,
			slink,
			dmsg_uuid_to_str(&cls->pfs_clid, &alloc),
			state->msg->any.lnk_span.cl_label,
			state->msg->any.lnk_span.fs_label,
			state->msg->any.lnk_span.dist);
		free(alloc);

		/*
		 * Clean out all relays.  This requires terminating each
		 * relay transaction.
		 */
		while ((relay = TAILQ_FIRST(&slink->relayq)) != NULL) {
			dmsg_relay_delete(relay);
		}

		/*
		 * Clean out the topology
		 */
		RB_REMOVE(h2span_link_tree, &node->tree, slink);
		if (RB_EMPTY(&node->tree)) {
			RB_REMOVE(h2span_node_tree, &cls->tree, node);
			if (msg->iocom->node_handler) {
				msg->iocom->node_handler(&node->opaque, msg,
							 DMSG_NODEOP_DEL);
			}
			if (RB_EMPTY(&cls->tree) && cls->refs == 0) {
				RB_REMOVE(h2span_cluster_tree,
					  &cluster_tree, cls);
				dmsg_free(cls);
			}
			node->cls = NULL;
			dmsg_free(node);
			node = NULL;
		}
		state->any.link = NULL;
		slink->state = NULL;
		slink->node = NULL;
		dmsg_free(slink);

		/*
		 * We have to terminate the transaction
		 */
		dmsg_state_reply(state, 0);
		/* state invalid after reply */

		/*
		 * If the node still exists issue any required updates.  If
		 * it doesn't then all related relays have already been
		 * removed and there's nothing left to do.
		 */
#if 0
		if (node)
			dmsg_relay_scan(NULL, node);
#endif
		if (node)
			dmsg_iocom_signal(msg->iocom);
	}

	pthread_mutex_unlock(&cluster_mtx);
}

/*
 * LNK_CIRC - Virtual circuit protocol message reception
 *	      (incoming iocom lock not held)
 *
 * Handles all cases.
 */
void
dmsg_lnk_circ(dmsg_msg_t *msg)
{
	dmsg_circuit_t *circA;
	dmsg_circuit_t *circB;
	dmsg_state_t *rx_state;
	dmsg_state_t *tx_state;
	dmsg_state_t *state;
	dmsg_state_t dummy;
	dmsg_msg_t *fwd_msg;
	dmsg_iocom_t *iocomA;
	dmsg_iocom_t *iocomB;
	int disconnect;

	/*pthread_mutex_lock(&cluster_mtx);*/

	if (DMsgDebugOpt >= 4)
		fprintf(stderr, "CIRC receive cmd=%08x\n", msg->any.head.cmd);

	switch (msg->any.head.cmd & (DMSGF_CREATE |
				     DMSGF_DELETE |
				     DMSGF_REPLY)) {
	case DMSGF_CREATE:
	case DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * (A) wishes to establish a virtual circuit through us to (B).
		 * (B) is specified by lnk_circ.target (the message id for
		 * a LNK_SPAN that (A) received from us which represents (B)).
		 *
		 * Designate the originator of the circuit (the current
		 * remote end) as (A) and the other side as (B).
		 *
		 * Accept the VC but do not reply.  We will wait for the end-
		 * to-end reply to propagate back.
		 */
		iocomA = msg->iocom;

		/*
		 * Locate the open transaction state that the other end
		 * specified in <target>.  This will be an open SPAN
		 * transaction that we transmitted (h2span_relay) over
		 * the interface the LNK_CIRC is being received on.
		 *
		 * (all LNK_CIRC's that we transmit are on circuit0)
		 */
		pthread_mutex_lock(&iocomA->mtx);
		dummy.msgid = msg->any.lnk_circ.target;
		tx_state = RB_FIND(dmsg_state_tree,
				   &iocomA->circuit0.statewr_tree,
				   &dummy);
		pthread_mutex_unlock(&iocomA->mtx);
		if (tx_state == NULL) {
			/* XXX SMP race */
			fprintf(stderr, "dmsg_lnk_circ: no circuit\n");
			dmsg_msg_reply(msg, DMSG_ERR_CANTCIRC);
			break;
		}
		if (tx_state->icmd != DMSG_LNK_SPAN) {
			/* XXX SMP race */
			fprintf(stderr, "dmsg_lnk_circ: not LNK_SPAN\n");
			dmsg_msg_reply(msg, DMSG_ERR_CANTCIRC);
			break;
		}

		/* locate h2span_link */
		rx_state = tx_state->any.relay->source_rt;

		/*
		 * A wishes to establish a VC through us to the
		 * specified target.
		 *
		 * A sends us the msgid of an open SPAN transaction
		 * it received from us as <target>.
		 */
		circA = dmsg_alloc(sizeof(*circA));
		dmsg_circuit_init(iocomA, circA);
		circA->state = msg->state;	/* LNK_CIRC state */
		circA->msgid = msg->state->msgid;
		circA->span_state = tx_state;	/* H2SPAN_RELAY state */
		circA->is_relay = 1;
		circA->refs = 2;		/* state and peer */

		/*
		 * Upgrade received state so we act on both it and its
		 * peer (created below) symmetrically.
		 */
		msg->state->any.circ = circA;
		msg->state->func = dmsg_lnk_circ;

		iocomB = rx_state->iocom;

		circB = dmsg_alloc(sizeof(*circB));
		dmsg_circuit_init(iocomB, circB);

		/*
		 * Create a LNK_CIRC transaction on B
		 */
		fwd_msg = dmsg_msg_alloc(&iocomB->circuit0,
					 0, DMSG_LNK_CIRC | DMSGF_CREATE,
					 dmsg_lnk_circ, circB);
		fwd_msg->state->any.circ = circB;
		fwd_msg->any.lnk_circ.target = rx_state->msgid;
		circB->state = fwd_msg->state;	/* LNK_CIRC state */
		circB->msgid = fwd_msg->any.head.msgid;
		circB->span_state = rx_state;	/* H2SPAN_LINK state */
		circB->is_relay = 0;
		circB->refs = 2;		/* state and peer */

		if (DMsgDebugOpt >= 4)
			fprintf(stderr, "CIRC forward %p->%p\n", circA, circB);

		/*
		 * Link the two circuits together.
		 */
		circA->peer = circB;
		circB->peer = circA;

		if (iocomA < iocomB) {
			pthread_mutex_lock(&iocomA->mtx);
			pthread_mutex_lock(&iocomB->mtx);
		} else {
			pthread_mutex_lock(&iocomB->mtx);
			pthread_mutex_lock(&iocomA->mtx);
		}
		if (RB_INSERT(dmsg_circuit_tree, &iocomA->circuit_tree, circA))
			assert(0);
		if (RB_INSERT(dmsg_circuit_tree, &iocomB->circuit_tree, circB))
			assert(0);
		if (iocomA < iocomB) {
			pthread_mutex_unlock(&iocomB->mtx);
			pthread_mutex_unlock(&iocomA->mtx);
		} else {
			pthread_mutex_unlock(&iocomA->mtx);
			pthread_mutex_unlock(&iocomB->mtx);
		}

		dmsg_msg_write(fwd_msg);

		if ((msg->any.head.cmd & DMSGF_DELETE) == 0)
			break;
		/* FALL THROUGH TO DELETE */
	case DMSGF_DELETE:
		/*
		 * (A) Is deleting the virtual circuit, propogate closure
		 * to (B).
		 */
		iocomA = msg->iocom;
		if (msg->state->any.circ == NULL) {
			/* already returned an error/deleted */
			break;
		}
		circA = msg->state->any.circ;
		circB = circA->peer;
		assert(msg->state == circA->state);

		/*
		 * We are closing B's send side.  If B's receive side is
		 * already closed we disconnect the circuit from B's state.
		 */
		disconnect = 0;
		if (circB && (state = circB->state) != NULL) {
			if (state->rxcmd & DMSGF_DELETE) {
				disconnect = 1;
				circB->state = NULL;
				state->any.circ = NULL;
				dmsg_circuit_drop(circB);
			}
			dmsg_state_reply(state, msg->any.head.error);
		}

		/*
		 * We received a close on A.  If A's send side is already
		 * closed we disconnect the circuit from A's state.
		 */
		if (circA && (state = circA->state) != NULL) {
			if (state->txcmd & DMSGF_DELETE) {
				disconnect = 1;
				circA->state = NULL;
				state->any.circ = NULL;
				dmsg_circuit_drop(circA);
			}
		}

		/*
		 * Disconnect the peer<->peer association
		 */
		if (disconnect) {
			if (circB) {
				circA->peer = NULL;
				circB->peer = NULL;
				dmsg_circuit_drop(circA);
				dmsg_circuit_drop(circB); /* XXX SMP */
			}
		}
		break;
	case DMSGF_REPLY | DMSGF_CREATE:
	case DMSGF_REPLY | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * (B) is acknowledging the creation of the virtual
		 * circuit.  This propagates all the way back to (A), though
		 * it should be noted that (A) can start issuing commands
		 * via the virtual circuit before seeing this reply.
		 */
		circB = msg->state->any.circ;
		assert(circB);
		circA = circB->peer;
		assert(msg->state == circB->state);
		assert(circA);
		if ((msg->any.head.cmd & DMSGF_DELETE) == 0) {
			dmsg_state_result(circA->state, msg->any.head.error);
			break;
		}
		/* FALL THROUGH TO DELETE */
	case DMSGF_REPLY | DMSGF_DELETE:
		/*
		 * (B) Is deleting the virtual circuit or acknowledging
		 * our deletion of the virtual circuit, propogate closure
		 * to (A).
		 */
		iocomB = msg->iocom;
		circB = msg->state->any.circ;
		circA = circB->peer;
		assert(msg->state == circB->state);

		/*
		 * We received a close on (B), propagate to (A).  If we have
		 * already received the close from (A) we disconnect the state.
		 */
		disconnect = 0;
		if (circA && (state = circA->state) != NULL) {
			if (state->rxcmd & DMSGF_DELETE) {
				disconnect = 1;
				circA->state = NULL;
				state->any.circ = NULL;
				dmsg_circuit_drop(circA);
			}
			dmsg_state_reply(state, msg->any.head.error);
		}

		/*
		 * We received a close on (B).  If (B)'s send side is already
		 * closed we disconnect the state.
		 */
		if (circB && (state = circB->state) != NULL) {
			if (state->txcmd & DMSGF_DELETE) {
				disconnect = 1;
				circB->state = NULL;
				state->any.circ = NULL;
				dmsg_circuit_drop(circB);
			}
		}

		/*
		 * Disconnect the peer<->peer association
		 */
		if (disconnect) {
			if (circA) {
				circB->peer = NULL;
				circA->peer = NULL;
				dmsg_circuit_drop(circB);
				dmsg_circuit_drop(circA); /* XXX SMP */
			}
		}
		break;
	}

	/*pthread_mutex_lock(&cluster_mtx);*/
}

/*
 * Update relay transactions for SPANs.
 *
 * Called with cluster_mtx held.
 */
static void dmsg_relay_scan_specific(h2span_node_t *node,
					h2span_conn_t *conn);

static void
dmsg_relay_scan(h2span_conn_t *conn, h2span_node_t *node)
{
	h2span_cluster_t *cls;

	if (node) {
		/*
		 * Iterate specific node
		 */
		TAILQ_FOREACH(conn, &connq, entry)
			dmsg_relay_scan_specific(node, conn);
	} else {
		/*
		 * Full iteration.
		 *
		 * Iterate cluster ids, nodes, and either a specific connection
		 * or all connections.
		 */
		RB_FOREACH(cls, h2span_cluster_tree, &cluster_tree) {
			/*
			 * Iterate node ids
			 */
			RB_FOREACH(node, h2span_node_tree, &cls->tree) {
				/*
				 * Synchronize the node's link (received SPANs)
				 * with each connection's relays.
				 */
				if (conn) {
					dmsg_relay_scan_specific(node, conn);
				} else {
					TAILQ_FOREACH(conn, &connq, entry) {
					    dmsg_relay_scan_specific(node,
									conn);
					}
					assert(conn == NULL);
				}
			}
		}
	}
}

/*
 * Update the relay'd SPANs for this (node, conn).
 *
 * Iterate links and adjust relays to match.  We only propagate the top link
 * for now (XXX we want to propagate the top two).
 *
 * The dmsg_relay_scan_cmp() function locates the first relay element
 * for any given node.  The relay elements will be sub-sorted by dist.
 */
struct relay_scan_info {
	h2span_node_t *node;
	h2span_relay_t *relay;
};

static int
dmsg_relay_scan_cmp(h2span_relay_t *relay, void *arg)
{
	struct relay_scan_info *info = arg;

	if ((intptr_t)relay->source_rt->any.link->node < (intptr_t)info->node)
		return(-1);
	if ((intptr_t)relay->source_rt->any.link->node > (intptr_t)info->node)
		return(1);
	return(0);
}

static int
dmsg_relay_scan_callback(h2span_relay_t *relay, void *arg)
{
	struct relay_scan_info *info = arg;

	info->relay = relay;
	return(-1);
}

static void
dmsg_relay_scan_specific(h2span_node_t *node, h2span_conn_t *conn)
{
	struct relay_scan_info info;
	h2span_relay_t *relay;
	h2span_relay_t *next_relay;
	h2span_link_t *slink;
	dmsg_lnk_conn_t *lconn;
	dmsg_lnk_span_t *lspan;
	int count;
	int maxcount = 2;
#ifdef REQUIRE_SYMMETRICAL
	uint32_t lastdist = DMSG_SPAN_MAXDIST;
	uint32_t lastrnss = 0;
#endif

	info.node = node;
	info.relay = NULL;

	/*
	 * Locate the first related relay for the node on this connection.
	 * relay will be NULL if there were none.
	 */
	RB_SCAN(h2span_relay_tree, &conn->tree,
		dmsg_relay_scan_cmp, dmsg_relay_scan_callback, &info);
	relay = info.relay;
	info.relay = NULL;
	if (relay)
		assert(relay->source_rt->any.link->node == node);

	if (DMsgDebugOpt > 8)
		fprintf(stderr, "relay scan for connection %p\n", conn);

	/*
	 * Iterate the node's links (received SPANs) in distance order,
	 * lowest (best) dist first.
	 *
	 * PROPAGATE THE BEST LINKS OVER THE SPECIFIED CONNECTION.
	 *
	 * Track relays while iterating the best links and construct
	 * missing relays when necessary.
	 *
	 * (If some prior better link was removed it would have also
	 *  removed the relay, so the relay can only match exactly or
	 *  be worse).
	 */
	count = 0;
	RB_FOREACH(slink, h2span_link_tree, &node->tree) {
		/*
		 * Increment count of successful relays.  This isn't
		 * quite accurate if we break out but nothing after
		 * the loop uses (count).
		 *
		 * If count exceeds the maximum number of relays we desire
		 * we normally want to break out.  However, in order to
		 * guarantee a symmetric path we have to continue if both
		 * (dist) and (rnss) continue to match.  Otherwise the SPAN
		 * propagation in the reverse direction may choose different
		 * routes and we will not have a symmetric path.
		 *
		 * NOTE: Spanning tree does not have to be symmetrical so
		 *	 this code is not currently enabled.
		 */
		if (++count >= maxcount) {
#ifdef REQUIRE_SYMMETRICAL
			if (lastdist != slink->dist || lastrnss != slink->rnss)
				break;
#else
			break;
#endif
			/* go beyond the nominal maximum desired relays */
		}

		/*
		 * Match, relay already in-place, get the next
		 * relay to match against the next slink.
		 */
		if (relay && relay->source_rt->any.link == slink) {
			relay = RB_NEXT(h2span_relay_tree, &conn->tree, relay);
			continue;
		}

		/*
		 * We might want this SLINK, if it passes our filters.
		 *
		 * The spanning tree can cause closed loops so we have
		 * to limit slink->dist.
		 */
		if (slink->dist > DMSG_SPAN_MAXDIST)
			break;

		/*
		 * Don't bother transmitting a LNK_SPAN out the same
		 * connection it came in on.  Trivial optimization.
		 */
		if (slink->state->iocom == conn->state->iocom)
			break;

		/*
		 * NOTE ON FILTERS: The protocol spec allows non-requested
		 * SPANs to be transmitted, the other end is expected to
		 * leave their transactions open but otherwise ignore them.
		 *
		 * Don't bother transmitting if the remote connection
		 * is not accepting this SPAN's peer_type.
		 *
		 * pfs_mask is typically used so pure clients can filter
		 * out receiving SPANs for other pure clients.
		 */
		lspan = &slink->state->msg->any.lnk_span;
		lconn = &conn->state->msg->any.lnk_conn;
		if (((1LLU << lspan->peer_type) & lconn->peer_mask) == 0)
			break;
		if (((1LLU << lspan->pfs_type) & lconn->pfs_mask) == 0)
			break;

#if 0
		/*
		 * Do not give pure clients visibility to other pure clients
		 */
		if (lconn->pfs_type == DMSG_PFSTYPE_CLIENT &&
		    lspan->pfs_type == DMSG_PFSTYPE_CLIENT) {
			break;
		}
#endif

		/*
		 * Connection filter, if cluster uuid is not NULL it must
		 * match the span cluster uuid.  Only applies when the
		 * peer_type matches.
		 */
		if (lspan->peer_type == lconn->peer_type &&
		    !uuid_is_nil(&lconn->pfs_clid, NULL) &&
		    uuid_compare(&slink->node->cls->pfs_clid,
				 &lconn->pfs_clid, NULL)) {
			break;
		}

		/*
		 * Connection filter, if cluster label is not empty it must
		 * match the span cluster label.  Only applies when the
		 * peer_type matches.
		 */
		if (lspan->peer_type == lconn->peer_type &&
		    lconn->cl_label[0] &&
		    strcmp(lconn->cl_label, slink->node->cls->cl_label)) {
			break;
		}

		/*
		 * NOTE! pfs_fsid differentiates nodes within the same cluster
		 *	 so we obviously don't want to match those.  Similarly
		 *	 for fs_label.
		 */

		/*
		 * Ok, we've accepted this SPAN for relaying.
		 */
		assert(relay == NULL ||
		       relay->source_rt->any.link->node != slink->node ||
		       relay->source_rt->any.link->dist >= slink->dist);
		relay = dmsg_generate_relay(conn, slink);
#ifdef REQUIRE_SYMMETRICAL
		lastdist = slink->dist;
		lastrnss = slink->rnss;
#endif

		/*
		 * Match (created new relay), get the next relay to
		 * match against the next slink.
		 */
		relay = RB_NEXT(h2span_relay_tree, &conn->tree, relay);
	}

	/*
	 * Any remaining relay's belonging to this connection which match
	 * the node are in excess of the current aggregate spanning state
	 * and should be removed.
	 */
	while (relay && relay->source_rt->any.link->node == node) {
		next_relay = RB_NEXT(h2span_relay_tree, &conn->tree, relay);
		fprintf(stderr, "RELAY DELETE FROM EXTRAS\n");
		dmsg_relay_delete(relay);
		relay = next_relay;
	}
}

/*
 * Helper function to generate missing relay.
 *
 * cluster_mtx must be held
 */
static
h2span_relay_t *
dmsg_generate_relay(h2span_conn_t *conn, h2span_link_t *slink)
{
	h2span_relay_t *relay;
	dmsg_msg_t *msg;

	relay = dmsg_alloc(sizeof(*relay));
	relay->conn = conn;
	relay->source_rt = slink->state;
	/* relay->source_rt->any.link = slink; */

	/*
	 * NOTE: relay->target_rt->any.relay set to relay by alloc.
	 */
	msg = dmsg_msg_alloc(&conn->state->iocom->circuit0,
			     0, DMSG_LNK_SPAN | DMSGF_CREATE,
			     dmsg_lnk_relay, relay);
	relay->target_rt = msg->state;

	msg->any.lnk_span = slink->state->msg->any.lnk_span;
	msg->any.lnk_span.dist = slink->dist + 1;
	msg->any.lnk_span.rnss = slink->rnss + dmsg_rnss();

	RB_INSERT(h2span_relay_tree, &conn->tree, relay);
	TAILQ_INSERT_TAIL(&slink->relayq, relay, entry);

	dmsg_msg_write(msg);

	return (relay);
}

/*
 * Messages received on relay SPANs.  These are open transactions so it is
 * in fact possible for the other end to close the transaction.
 *
 * XXX MPRACE on state structure
 */
static void
dmsg_lnk_relay(dmsg_msg_t *msg)
{
	dmsg_state_t *state = msg->state;
	h2span_relay_t *relay;

	assert(msg->any.head.cmd & DMSGF_REPLY);

	if (msg->any.head.cmd & DMSGF_DELETE) {
		pthread_mutex_lock(&cluster_mtx);
		fprintf(stderr, "RELAY DELETE FROM LNK_RELAY MSG\n");
		if ((relay = state->any.relay) != NULL) {
			dmsg_relay_delete(relay);
		} else {
			dmsg_state_reply(state, 0);
		}
		pthread_mutex_unlock(&cluster_mtx);
	}
}

/*
 * cluster_mtx held by caller
 */
static
void
dmsg_relay_delete(h2span_relay_t *relay)
{
	fprintf(stderr,
		"RELAY DELETE %p RELAY %p ON CLS=%p NODE=%p DIST=%d FD %d STATE %p\n",
		relay->source_rt->any.link,
		relay,
		relay->source_rt->any.link->node->cls, relay->source_rt->any.link->node,
		relay->source_rt->any.link->dist,
		relay->conn->state->iocom->sock_fd, relay->target_rt);

	RB_REMOVE(h2span_relay_tree, &relay->conn->tree, relay);
	TAILQ_REMOVE(&relay->source_rt->any.link->relayq, relay, entry);

	if (relay->target_rt) {
		relay->target_rt->any.relay = NULL;
		dmsg_state_reply(relay->target_rt, 0);
		/* state invalid after reply */
		relay->target_rt = NULL;
	}
	relay->conn = NULL;
	relay->source_rt = NULL;
	dmsg_free(relay);
}

/************************************************************************
 *			MESSAGE ROUTING AND SOURCE VALIDATION		*
 ************************************************************************/

int
dmsg_circuit_route(dmsg_msg_t *msg)
{
	dmsg_iocom_t *iocom = msg->iocom;
	dmsg_circuit_t *circ;
	dmsg_circuit_t *peer;
	dmsg_circuit_t dummy;
	int error = 0;

	/*
	 * Relay occurs before any state processing, msg state should always
	 * be NULL.
	 */
	assert(msg->state == NULL);

	/*
	 * Lookup the circuit on the incoming iocom.
	 */
	pthread_mutex_lock(&iocom->mtx);

	dummy.msgid = msg->any.head.circuit;
	circ = RB_FIND(dmsg_circuit_tree, &iocom->circuit_tree, &dummy);
	assert(circ);
	peer = circ->peer;
	dmsg_circuit_hold(peer);

	if (DMsgDebugOpt >= 4) {
		fprintf(stderr,
			"CIRC relay %08x %p->%p\n",
			msg->any.head.cmd, circ, peer);
	}

	msg->iocom = peer->iocom;
	msg->any.head.circuit = peer->msgid;
	dmsg_circuit_drop_locked(msg->circuit);
	msg->circuit = peer;

	pthread_mutex_unlock(&iocom->mtx);

	dmsg_msg_write(msg);
	error = DMSG_IOQ_ERROR_ROUTED;

	return error;
}

/************************************************************************
 *			ROUTER AND MESSAGING HANDLES			*
 ************************************************************************
 *
 * Basically the idea here is to provide a stable data structure which
 * can be localized to the caller for higher level protocols to work with.
 * Depends on the context, these dmsg_handle's can be pooled by use-case
 * and remain persistent through a client (or mount point's) life.
 */

#if 0
/*
 * Obtain a stable handle on a cluster given its uuid.  This ties directly
 * into the global cluster topology, creating the structure if necessary
 * (even if the uuid does not exist or does not exist yet), and preventing
 * the structure from getting ripped out from under us while we hold a
 * pointer to it.
 */
h2span_cluster_t *
dmsg_cluster_get(uuid_t *pfs_clid)
{
	h2span_cluster_t dummy_cls;
	h2span_cluster_t *cls;

	dummy_cls.pfs_clid = *pfs_clid;
	pthread_mutex_lock(&cluster_mtx);
	cls = RB_FIND(h2span_cluster_tree, &cluster_tree, &dummy_cls);
	if (cls)
		++cls->refs;
	pthread_mutex_unlock(&cluster_mtx);
	return (cls);
}

void
dmsg_cluster_put(h2span_cluster_t *cls)
{
	pthread_mutex_lock(&cluster_mtx);
	assert(cls->refs > 0);
	--cls->refs;
	if (RB_EMPTY(&cls->tree) && cls->refs == 0) {
		RB_REMOVE(h2span_cluster_tree,
			  &cluster_tree, cls);
		dmsg_free(cls);
	}
	pthread_mutex_unlock(&cluster_mtx);
}

/*
 * Obtain a stable handle to a specific cluster node given its uuid.
 * This handle does NOT lock in the route to the node and is typically
 * used as part of the dmsg_handle_*() API to obtain a set of
 * stable nodes.
 */
h2span_node_t *
dmsg_node_get(h2span_cluster_t *cls, uuid_t *pfs_fsid)
{
}

#endif

/*
 * Dumps the spanning tree
 *
 * DEBUG ONLY
 */
void
dmsg_shell_tree(dmsg_circuit_t *circuit, char *cmdbuf __unused)
{
	h2span_cluster_t *cls;
	h2span_node_t *node;
	h2span_link_t *slink;
	h2span_relay_t *relay;
	char *uustr = NULL;

	pthread_mutex_lock(&cluster_mtx);
	RB_FOREACH(cls, h2span_cluster_tree, &cluster_tree) {
		dmsg_circuit_printf(circuit, "Cluster %s %s (%s)\n",
				  dmsg_peer_type_to_str(cls->peer_type),
				  dmsg_uuid_to_str(&cls->pfs_clid, &uustr),
				  cls->cl_label);
		RB_FOREACH(node, h2span_node_tree, &cls->tree) {
			dmsg_circuit_printf(circuit, "    Node %02x %s (%s)\n",
				node->pfs_type,
				dmsg_uuid_to_str(&node->pfs_fsid, &uustr),
				node->fs_label);
			RB_FOREACH(slink, h2span_link_tree, &node->tree) {
				dmsg_circuit_printf(circuit,
					    "\tSLink msgid %016jx "
					    "dist=%d via %d\n",
					    (intmax_t)slink->state->msgid,
					    slink->dist,
					    slink->state->iocom->sock_fd);
				TAILQ_FOREACH(relay, &slink->relayq, entry) {
					dmsg_circuit_printf(circuit,
					    "\t    Relay-out msgid %016jx "
					    "via %d\n",
					    (intmax_t)relay->target_rt->msgid,
					    relay->target_rt->iocom->sock_fd);
				}
			}
		}
	}
	pthread_mutex_unlock(&cluster_mtx);
	if (uustr)
		free(uustr);
#if 0
	TAILQ_FOREACH(conn, &connq, entry) {
	}
#endif
}

/*
 * DEBUG ONLY
 *
 * Locate the state representing an incoming LNK_SPAN given its msgid.
 */
int
dmsg_debug_findspan(uint64_t msgid, dmsg_state_t **statep)
{
	h2span_cluster_t *cls;
	h2span_node_t *node;
	h2span_link_t *slink;

	pthread_mutex_lock(&cluster_mtx);
	RB_FOREACH(cls, h2span_cluster_tree, &cluster_tree) {
		RB_FOREACH(node, h2span_node_tree, &cls->tree) {
			RB_FOREACH(slink, h2span_link_tree, &node->tree) {
				if (slink->state->msgid == msgid) {
					*statep = slink->state;
					goto found;
				}
			}
		}
	}
	pthread_mutex_unlock(&cluster_mtx);
	*statep = NULL;
	return(ENOENT);
found:
	pthread_mutex_unlock(&cluster_mtx);
	return(0);
}

/*
 * Random number sub-sort value to add to SPAN rnss fields on relay.
 * This allows us to differentiate spans with the same <dist> field
 * for relaying purposes.  We must normally limit the number of relays
 * for any given SPAN origination but we must also guarantee that a
 * symmetric reverse path exists, so we use the rnss field as a sub-sort
 * (since there can be thousands or millions if we only match on <dist>),
 * and if there STILL too many spans we go past the limit.
 */
static
uint32_t
dmsg_rnss(void)
{
	if (DMsgRNSS == 0) {
		pthread_mutex_lock(&cluster_mtx);
		while (DMsgRNSS == 0) {
			srandomdev();
			DMsgRNSS = random();
		}
		pthread_mutex_unlock(&cluster_mtx);
	}
	return(DMsgRNSS);
}
