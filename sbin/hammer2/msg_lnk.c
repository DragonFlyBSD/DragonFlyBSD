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
 * LNK_SPAN PROTOCOL SUPPORT FUNCTIONS
 *
 * This code supports the LNK_SPAN protocol.  Essentially all PFS's
 * clients and services rendezvous with the userland hammer2 service and
 * open LNK_SPAN transactions using a message header linkid of 0,
 * registering any PFS's they have connectivity to with us.
 *
 * --
 *
 * Each registration maintains its own open LNK_SPAN message transaction.
 * The SPANs are collected, aggregated, and retransmitted over available
 * connections through the maintainance of additional LNK_SPAN message
 * transactions on each link.
 *
 * The msgid for each active LNK_SPAN transaction we receive allows us to
 * send a message to the target PFS (which might be one of many belonging
 * to the same cluster), by specifying that msgid as the linkid in any
 * message we send to the target PFS.
 *
 * Similarly the msgid we allocate for any LNK_SPAN transaction we transmit
 * (and remember we will maintain multiple open LNK_SPAN transactions on
 * each connection representing the topology span, so every node sees every
 * other node as a separate open transaction).  So, similarly the msgid for
 * these active transactions which we initiated can be used by the other
 * end to route messages through us to another node, ultimately winding up
 * at the identified hammer2 PFS.  We have to adjust the spanid in the message
 * header at each hop to be representative of the outgoing LNK_SPAN we
 * are forwarding the message through.
 *
 * --
 *
 * If we were to retransmit every LNK_SPAN transaction we receive it would
 * create a huge mess, so we have to aggregate all received LNK_SPAN
 * transactions, sort them by the fsid (the cluster) and sub-sort them by
 * the pfs_fsid (individual nodes in the cluster), and only retransmit
 * (create outgoing transactions) for a subset of the nearest distance-hops
 * for each individual node.
 *
 * The higher level protocols can then issue transactions to the nodes making
 * up a cluster to perform all actions required.
 *
 * --
 *
 * Since this is a large topology and a spanning tree protocol, links can
 * go up and down all the time.  Any time a link goes down its transaction
 * is closed.  The transaction has to be closed on both ends before we can
 * delete (and potentially reuse) the related spanid.  The LNK_SPAN being
 * closed may have been propagated out to other connections and those related
 * LNK_SPANs are also closed.  Ultimately all routes via the lost LNK_SPAN
 * go away, ultimately reaching all sources and all targets.
 *
 * Any messages in-transit using a route that goes away will be thrown away.
 * Open transactions are only tracked at the two end-points.  When a link
 * failure propagates to an end-point the related open transactions lose
 * their spanid and are automatically aborted.
 *
 * It is important to note that internal route nodes cannot just associate
 * a lost LNK_SPAN transaction with another route to the same destination.
 * Message transactions MUST be serialized and MUST be ordered.  All messages
 * for a transaction must run over the same route.  So if the route used by
 * an active transaction is lost, the related messages will be fully aborted
 * and the higher protocol levels will retry as appropriate.
 *
 * It is also important to note that several paths to the same PFS can be
 * propagated along the same link, which allows concurrency and even
 * redundancy over several network interfaces or via different routes through
 * the topology.  Any given transaction will use only a single route but busy
 * servers will often have hundreds of transactions active simultaniously,
 * so having multiple active paths through the network topology for A<->B
 * will improve performance.
 *
 * --
 *
 * Most protocols consolidate operations rather than simply relaying them.
 * This is particularly true of LEAF protocols (such as strict HAMMER2
 * clients), of which there can be millions connecting into the cluster at
 * various points.  The SPAN protocol is not used for these LEAF elements.
 *
 * Instead the primary service they connect to implements a proxy for the
 * client protocols so the core topology only has to propagate a couple of
 * LNK_SPANs and not millions.  LNK_SPANs are meant to be used only for
 * core master nodes and satellite slaves and cache nodes.
 */

#include "hammer2.h"

/*
 * Maximum spanning tree distance.  This has the practical effect of
 * stopping tail-chasing closed loops when a feeder span is lost.
 */
#define HAMMER2_SPAN_MAXDIST	16

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
 * h2span_connect	- list of iocom connections who wish to receive SPAN
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
TAILQ_HEAD(h2span_connect_queue, h2span_connect);
TAILQ_HEAD(h2span_relay_queue, h2span_relay);

RB_HEAD(h2span_cluster_tree, h2span_cluster);
RB_HEAD(h2span_node_tree, h2span_node);
RB_HEAD(h2span_link_tree, h2span_link);
RB_HEAD(h2span_relay_tree, h2span_relay);

/*
 * Received LNK_CONN transaction enables SPAN protocol over connection.
 * (may contain filter).
 */
struct h2span_connect {
	TAILQ_ENTRY(h2span_connect) entry;
	struct h2span_relay_tree tree;
	hammer2_state_t *state;
};

/*
 * All received LNK_SPANs are organized by cluster (pfs_clid),
 * node (pfs_fsid), and link (received LNK_SPAN transaction).
 */
struct h2span_cluster {
	RB_ENTRY(h2span_cluster) rbnode;
	struct h2span_node_tree tree;
	uuid_t	pfs_clid;		/* shared fsid */
};

struct h2span_node {
	RB_ENTRY(h2span_node) rbnode;
	struct h2span_link_tree tree;
	struct h2span_cluster *cls;
	uuid_t	pfs_fsid;		/* unique fsid */
	char label[64];
};

struct h2span_link {
	RB_ENTRY(h2span_link) rbnode;
	hammer2_state_t	*state;		/* state<->link */
	struct h2span_node *node;	/* related node */
	int32_t	dist;
	struct h2span_relay_queue relayq; /* relay out */
};

/*
 * Any LNK_SPAN transactions we receive which are relayed out other
 * connections utilize this structure to track the LNK_SPAN transaction
 * we initiate on the other connections, if selected for relay.
 *
 * In many respects this is the core of the protocol... actually figuring
 * out what LNK_SPANs to relay.  The spanid used for relaying is the
 * address of the 'state' structure, which is why h2span_relay has to
 * be entered into a RB-TREE based at h2span_connect (so we can look
 * up the spanid to validate it).
 */
struct h2span_relay {
	RB_ENTRY(h2span_relay) rbnode;	/* from h2span_connect */
	TAILQ_ENTRY(h2span_relay) entry; /* from link */
	struct h2span_connect *conn;
	hammer2_state_t	*state;		/* transmitted LNK_SPAN */
	struct h2span_link *link;	/* received LNK_SPAN */
};


typedef struct h2span_connect h2span_connect_t;
typedef struct h2span_cluster h2span_cluster_t;
typedef struct h2span_node h2span_node_t;
typedef struct h2span_link h2span_link_t;
typedef struct h2span_relay h2span_relay_t;

static
int
h2span_cluster_cmp(h2span_cluster_t *cls1, h2span_cluster_t *cls2)
{
	return(uuid_compare(&cls1->pfs_clid, &cls2->pfs_clid, NULL));
}

static
int
h2span_node_cmp(h2span_node_t *node1, h2span_node_t *node2)
{
	return(uuid_compare(&node1->pfs_fsid, &node2->pfs_fsid, NULL));
}

/*
 * NOTE: Sort/subsort must match h2span_relay_cmp() under any given
 *	 node.
 */
static
int
h2span_link_cmp(h2span_link_t *link1, h2span_link_t *link2)
{
	if (link1->dist < link2->dist)
		return(-1);
	if (link1->dist > link2->dist)
		return(1);
	if ((intptr_t)link1 < (intptr_t)link2)
		return(-1);
	if ((intptr_t)link1 > (intptr_t)link2)
		return(1);
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
	if ((intptr_t)relay1->link->node < (intptr_t)relay2->link->node)
		return(-1);
	if ((intptr_t)relay1->link->node > (intptr_t)relay2->link->node)
		return(1);
	if ((intptr_t)relay1->link->dist < (intptr_t)relay2->link->dist)
		return(-1);
	if ((intptr_t)relay1->link->dist > (intptr_t)relay2->link->dist)
		return(1);
	if ((intptr_t)relay1->link < (intptr_t)relay2->link)
		return(-1);
	if ((intptr_t)relay1->link > (intptr_t)relay2->link)
		return(1);
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
 * Global mutex protects cluster_tree lookups.
 */
static pthread_mutex_t cluster_mtx;
static struct h2span_cluster_tree cluster_tree = RB_INITIALIZER(cluster_tree);
static struct h2span_connect_queue connq = TAILQ_HEAD_INITIALIZER(connq);

static void hammer2_lnk_span(hammer2_state_t *state, hammer2_msg_t *msg);
static void hammer2_lnk_conn(hammer2_state_t *state, hammer2_msg_t *msg);
static void hammer2_lnk_relay(hammer2_state_t *state, hammer2_msg_t *msg);
static void hammer2_relay_scan(h2span_connect_t *conn, h2span_node_t *node);
static void hammer2_relay_delete(h2span_relay_t *relay);

/*
 * Receive a HAMMER2_MSG_PROTO_LNK message.  This only called for
 * one-way and opening-transactions since state->func will be assigned
 * in all other cases.
 */
void
hammer2_msg_lnk(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	switch(msg->any.head.cmd & HAMMER2_MSGF_BASECMDMASK) {
	case HAMMER2_LNK_CONN:
		hammer2_lnk_conn(msg->state, msg);
		break;
	case HAMMER2_LNK_SPAN:
		hammer2_lnk_span(msg->state, msg);
		break;
	default:
		fprintf(stderr,
			"MSG_PROTO_LNK: Unknown msg %08x\n", msg->any.head.cmd);
		hammer2_msg_reply(iocom, msg, HAMMER2_MSG_ERR_NOSUPP);
		/* state invalid after reply */
		break;
	}
}

void
hammer2_lnk_conn(hammer2_state_t *state, hammer2_msg_t *msg)
{
	h2span_connect_t *conn;
	h2span_relay_t *relay;
	char *alloc = NULL;

	pthread_mutex_lock(&cluster_mtx);

	/*
	 * On transaction start we allocate a new h2span_connect and
	 * acknowledge the request, leaving the transaction open.
	 * We then relay priority-selected SPANs.
	 */
	if (msg->any.head.cmd & HAMMER2_MSGF_CREATE) {
		state->func = hammer2_lnk_conn;

		fprintf(stderr, "LNK_CONN(%08x): %s/%s\n",
			(uint32_t)msg->any.head.msgid,
			hammer2_uuid_to_str(&msg->any.lnk_conn.pfs_clid,
					    &alloc),
			msg->any.lnk_conn.label);
		free(alloc);

		conn = hammer2_alloc(sizeof(*conn));

		RB_INIT(&conn->tree);
		conn->state = state;
		state->any.conn = conn;
		TAILQ_INSERT_TAIL(&connq, conn, entry);

		hammer2_msg_result(state->iocom, msg, 0);

		/*
		 * Span-synchronize all nodes with the new connection
		 */
		hammer2_relay_scan(conn, NULL);
	}

	/*
	 * On transaction terminate we clean out our h2span_connect
	 * and acknowledge the request, closing the transaction.
	 */
	if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		fprintf(stderr, "LNK_CONN: Terminated\n");
		conn = state->any.conn;
		assert(conn);

		/*
		 * Clean out all relays.  This requires terminating each
		 * relay transaction.
		 */
		while ((relay = RB_ROOT(&conn->tree)) != NULL) {
			hammer2_relay_delete(relay);
		}

		/*
		 * Clean out conn
		 */
		conn->state = NULL;
		msg->state->any.conn = NULL;
		TAILQ_REMOVE(&connq, conn, entry);
		hammer2_free(conn);

		hammer2_msg_reply(state->iocom, msg, 0);
		/* state invalid after reply */
	}
	pthread_mutex_unlock(&cluster_mtx);
}

void
hammer2_lnk_span(hammer2_state_t *state, hammer2_msg_t *msg)
{
	h2span_cluster_t dummy_cls;
	h2span_node_t dummy_node;
	h2span_cluster_t *cls;
	h2span_node_t *node;
	h2span_link_t *slink;
	h2span_relay_t *relay;
	char *alloc = NULL;

	pthread_mutex_lock(&cluster_mtx);

	/*
	 * On transaction start we initialize the tracking infrastructure
	 */
	if (msg->any.head.cmd & HAMMER2_MSGF_CREATE) {
		state->func = hammer2_lnk_span;

		msg->any.lnk_span.label[sizeof(msg->any.lnk_span.label)-1] = 0;

		fprintf(stderr, "LNK_SPAN: %s/%s dist=%d\n",
			hammer2_uuid_to_str(&msg->any.lnk_span.pfs_clid,
					    &alloc),
			msg->any.lnk_span.label,
			msg->any.lnk_span.dist);
		free(alloc);

		/*
		 * Find the cluster
		 */
		dummy_cls.pfs_clid = msg->any.lnk_span.pfs_clid;
		cls = RB_FIND(h2span_cluster_tree, &cluster_tree, &dummy_cls);
		if (cls == NULL) {
			cls = hammer2_alloc(sizeof(*cls));
			cls->pfs_clid = msg->any.lnk_span.pfs_clid;
			RB_INIT(&cls->tree);
			RB_INSERT(h2span_cluster_tree, &cluster_tree, cls);
		}

		/*
		 * Find the node
		 */
		dummy_node.pfs_fsid = msg->any.lnk_span.pfs_fsid;
		node = RB_FIND(h2span_node_tree, &cls->tree, &dummy_node);
		if (node == NULL) {
			node = hammer2_alloc(sizeof(*node));
			node->pfs_fsid = msg->any.lnk_span.pfs_fsid;
			node->cls = cls;
			RB_INIT(&node->tree);
			RB_INSERT(h2span_node_tree, &cls->tree, node);
			snprintf(node->label, sizeof(node->label),
				 "%s", msg->any.lnk_span.label);
		}

		/*
		 * Create the link
		 */
		assert(state->any.link == NULL);
		slink = hammer2_alloc(sizeof(*slink));
		TAILQ_INIT(&slink->relayq);
		slink->node = node;
		slink->dist = msg->any.lnk_span.dist;
		slink->state = state;
		state->any.link = slink;
		RB_INSERT(h2span_link_tree, &node->tree, slink);

		hammer2_relay_scan(NULL, node);
	}

	/*
	 * On transaction terminate we remove the tracking infrastructure.
	 */
	if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		slink = state->any.link;
		assert(slink != NULL);
		node = slink->node;
		cls = node->cls;

		/*
		 * Clean out all relays.  This requires terminating each
		 * relay transaction.
		 */
		while ((relay = TAILQ_FIRST(&slink->relayq)) != NULL) {
			hammer2_relay_delete(relay);
		}

		/*
		 * Clean out the topology
		 */
		RB_REMOVE(h2span_link_tree, &node->tree, slink);
		if (RB_EMPTY(&node->tree)) {
			RB_REMOVE(h2span_node_tree, &cls->tree, node);
			if (RB_EMPTY(&cls->tree)) {
				RB_REMOVE(h2span_cluster_tree,
					  &cluster_tree, cls);
				hammer2_free(cls);
			}
			node->cls = NULL;
			hammer2_free(node);
			node = NULL;
		}
		state->any.link = NULL;
		slink->state = NULL;
		slink->node = NULL;
		hammer2_free(slink);

		/*
		 * We have to terminate the transaction
		 */
		hammer2_state_reply(state, 0);
		/* state invalid after reply */

		/*
		 * If the node still exists issue any required updates.  If
		 * it doesn't then all related relays have already been
		 * removed and there's nothing left to do.
		 */
		if (node)
			hammer2_relay_scan(NULL, node);
	}

	pthread_mutex_unlock(&cluster_mtx);
}

/*
 * Messages received on relay SPANs.  These are open transactions so it is
 * in fact possible for the other end to close the transaction.
 *
 * XXX MPRACE on state structure
 */
static void
hammer2_lnk_relay(hammer2_state_t *state, hammer2_msg_t *msg)
{
	h2span_relay_t *relay;

	if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		pthread_mutex_lock(&cluster_mtx);
		if ((relay = state->any.relay) != NULL) {
			hammer2_relay_delete(relay);
		} else {
			hammer2_state_reply(state, 0);
		}
		pthread_mutex_unlock(&cluster_mtx);
	}
}

/*
 * Update relay transactions for SPANs.
 *
 * Called with cluster_mtx held.
 */
static void hammer2_relay_scan_specific(h2span_node_t *node,
					h2span_connect_t *conn);

static void
hammer2_relay_scan(h2span_connect_t *conn, h2span_node_t *node)
{
	h2span_cluster_t *cls;

	if (node) {
		/*
		 * Iterate specific node
		 */
		TAILQ_FOREACH(conn, &connq, entry)
			hammer2_relay_scan_specific(node, conn);
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
					hammer2_relay_scan_specific(node, conn);
				} else {
					TAILQ_FOREACH(conn, &connq, entry) {
					    hammer2_relay_scan_specific(node,
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
 * The hammer2_relay_scan_cmp() function locates the first relay element
 * for any given node.  The relay elements will be sub-sorted by dist.
 */
struct relay_scan_info {
	h2span_node_t *node;
	h2span_relay_t *relay;
};

static int
hammer2_relay_scan_cmp(h2span_relay_t *relay, void *arg)
{
	struct relay_scan_info *info = arg;

	if ((intptr_t)relay->link->node < (intptr_t)info->node)
		return(-1);
	if ((intptr_t)relay->link->node > (intptr_t)info->node)
		return(1);
	return(0);
}

static int
hammer2_relay_scan_callback(h2span_relay_t *relay, void *arg)
{
	struct relay_scan_info *info = arg;

	info->relay = relay;
	return(-1);
}

static void
hammer2_relay_scan_specific(h2span_node_t *node, h2span_connect_t *conn)
{
	struct relay_scan_info info;
	h2span_relay_t *relay;
	h2span_relay_t *next_relay;
	h2span_link_t *slink;
	int count = 2;

	info.node = node;
	info.relay = NULL;

	/*
	 * Locate the first related relay for the connection.  relay will
	 * be NULL if there were none.
	 */
	RB_SCAN(h2span_relay_tree, &conn->tree,
		hammer2_relay_scan_cmp, hammer2_relay_scan_callback, &info);
	relay = info.relay;
	info.relay = NULL;
	if (relay)
		assert(relay->link->node == node);

	if (DebugOpt > 8)
		fprintf(stderr, "relay scan for connection %p\n", conn);

	/*
	 * Iterate the node's links (received SPANs) in distance order,
	 * lowest (best) dist first.
	 */
	RB_FOREACH(slink, h2span_link_tree, &node->tree) {
		/*
		 * PROPAGATE THE BEST LINKS OVER THE SPECIFIED CONNECTION.
		 *
		 * Track relays while iterating the best links and construct
		 * missing relays when necessary.
		 *
		 * (If some prior better link was removed it would have also
		 *  removed the relay, so the relay can only match exactly or
		 *  be worst).
		 */
		if (relay && relay->link == slink) {
			/*
			 * Match, get the next relay to match against the
			 * next slink.
			 */
			relay = RB_NEXT(h2span_relay_tree, &conn->tree, relay);
			if (--count == 0)
				break;
		} else if (slink->dist > HAMMER2_SPAN_MAXDIST) {
			/*
			 * No match but span distance is too great,
			 * do not relay.  This prevents endless closed
			 * loops with ever-incrementing distances when
			 * the seed span is lost in the graph.
			 */
			/* no code needed */
		} else {
			/*
			 * No match, distance is ok, construct a new relay.
			 */
			hammer2_msg_t *msg;

			assert(relay == NULL ||
			       relay->link->dist <= slink->dist);
			relay = hammer2_alloc(sizeof(*relay));
			relay->conn = conn;
			relay->link = slink;

			RB_INSERT(h2span_relay_tree, &conn->tree, relay);
			TAILQ_INSERT_TAIL(&slink->relayq, relay, entry);

			msg = hammer2_msg_alloc(conn->state->iocom, 0,
						HAMMER2_LNK_SPAN |
						HAMMER2_MSGF_CREATE);
			msg->any.lnk_span = slink->state->msg->any.lnk_span;
			++msg->any.lnk_span.dist; /* XXX add weighting */

			hammer2_msg_write(conn->state->iocom, msg,
					  hammer2_lnk_relay, relay,
					  &relay->state);
			fprintf(stderr,
				"RELAY SPAN ON CLS=%p NODE=%p DIST=%d "
				"FD %d state %p\n",
				node->cls, node, slink->dist,
				conn->state->iocom->sock_fd, relay->state);

			/*
			 * Match (created new relay), get the next relay to
			 * match against the next slink.
			 */
			relay = RB_NEXT(h2span_relay_tree, &conn->tree, relay);
			if (--count == 0)
				break;
		}
	}

	/*
	 * Any remaining relay's belonging to this connection which match
	 * the node are in excess of the current aggregate spanning state
	 * and should be removed.
	 */
	while (relay && relay->link->node == node) {
		next_relay = RB_NEXT(h2span_relay_tree, &conn->tree, relay);
		hammer2_relay_delete(relay);
		relay = next_relay;
	}
}

static
void
hammer2_relay_delete(h2span_relay_t *relay)
{
	fprintf(stderr,
		"RELAY DELETE ON CLS=%p NODE=%p DIST=%d FD %d STATE %p\n",
		relay->link->node->cls, relay->link->node,
		relay->link->dist,
		relay->conn->state->iocom->sock_fd, relay->state);

	RB_REMOVE(h2span_relay_tree, &relay->conn->tree, relay);
	TAILQ_REMOVE(&relay->link->relayq, relay, entry);

	if (relay->state) {
		relay->state->any.relay = NULL;
		hammer2_state_reply(relay->state, 0);
		/* state invalid after reply */
		relay->state = NULL;
	}
	relay->conn = NULL;
	relay->link = NULL;
	hammer2_free(relay);
}

/*
 * Dumps the spanning tree
 */
void
shell_tree(hammer2_iocom_t *iocom, char *cmdbuf __unused)
{
	h2span_cluster_t *cls;
	h2span_node_t *node;
	h2span_link_t *slink;
	char *uustr = NULL;

	pthread_mutex_lock(&cluster_mtx);
	RB_FOREACH(cls, h2span_cluster_tree, &cluster_tree) {
		iocom_printf(iocom, "Cluster %s\n",
			     hammer2_uuid_to_str(&cls->pfs_clid, &uustr));
		RB_FOREACH(node, h2span_node_tree, &cls->tree) {
			iocom_printf(iocom, "    Node %s (%s)\n",
				 hammer2_uuid_to_str(&node->pfs_fsid, &uustr),
				 node->label);
			RB_FOREACH(slink, h2span_link_tree, &node->tree) {
				iocom_printf(iocom, "\tLink dist=%d via %d\n",
					     slink->dist,
					     slink->state->iocom->sock_fd);
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
