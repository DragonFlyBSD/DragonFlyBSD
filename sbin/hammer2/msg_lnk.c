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
 * FULLY ABORTING A ROUTED MESSAGE is handled via link-failure propagation
 * back to the originator.  Only the originator keeps tracks of a message.
 * Routers just pass it through.  If a route is lost during transit the
 * message is simply thrown away.
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
TAILQ_HEAD(h2span_media_queue, h2span_media);
TAILQ_HEAD(h2span_connect_queue, h2span_connect);
TAILQ_HEAD(h2span_relay_queue, h2span_relay);

RB_HEAD(h2span_cluster_tree, h2span_cluster);
RB_HEAD(h2span_node_tree, h2span_node);
RB_HEAD(h2span_link_tree, h2span_link);
RB_HEAD(h2span_relay_tree, h2span_relay);

/*
 * This represents a media
 */
struct h2span_media {
	TAILQ_ENTRY(h2span_media) entry;
	uuid_t	mediaid;
	int	refs;
	struct h2span_media_config {
		hammer2_copy_data_t	copy_run;
		hammer2_copy_data_t	copy_pend;
		pthread_t		thread;
		pthread_cond_t		cond;
		int			ctl;
		int			fd;
		hammer2_iocom_t		iocom;
		pthread_t		iocom_thread;
		enum { H2MC_STOPPED, H2MC_CONNECT, H2MC_RUNNING } state;
	} config[HAMMER2_COPYID_COUNT];
};

typedef struct h2span_media_config h2span_media_config_t;

#define H2CONFCTL_STOP		0x00000001
#define H2CONFCTL_UPDATE	0x00000002

/*
 * Received LNK_CONN transaction enables SPAN protocol over connection.
 * (may contain filter).  Typically one for each mount and several may
 * share the same media.
 */
struct h2span_connect {
	TAILQ_ENTRY(h2span_connect) entry;
	struct h2span_relay_tree tree;
	struct h2span_media *media;
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
	int	refs;			/* prevents destruction */
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
	struct hammer2_router *router;	/* route out this link */
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
 *
 * NOTE: Messages can be received via the LNK_SPAN transaction the
 *	 relay maintains, and can be replied via relay->router, but
 *	 messages are NOT initiated via a relay.  Messages are initiated
 *	 via incoming links (h2span_link's).
 *
 *	 relay->link represents the link being relayed, NOT the LNK_SPAN
 *	 transaction the relay is holding open.
 */
struct h2span_relay {
	RB_ENTRY(h2span_relay) rbnode;	/* from h2span_connect */
	TAILQ_ENTRY(h2span_relay) entry; /* from link */
	struct h2span_connect *conn;
	hammer2_state_t	*state;		/* transmitted LNK_SPAN */
	struct h2span_link *link;	/* LNK_SPAN being relayed */
	struct hammer2_router	*router;/* route out this relay */
};


typedef struct h2span_media h2span_media_t;
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
	h2span_link_t *link1 = relay1->link;
	h2span_link_t *link2 = relay2->link;

	if ((intptr_t)link1->node < (intptr_t)link2->node)
		return(-1);
	if ((intptr_t)link1->node > (intptr_t)link2->node)
		return(1);
	if (link1->dist < link2->dist)
		return(-1);
	if (link1->dist > link2->dist)
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
static struct h2span_connect_queue connq = TAILQ_HEAD_INITIALIZER(connq);
static struct h2span_media_queue mediaq = TAILQ_HEAD_INITIALIZER(mediaq);

static void hammer2_lnk_span(hammer2_msg_t *msg);
static void hammer2_lnk_conn(hammer2_msg_t *msg);
static void hammer2_lnk_relay(hammer2_msg_t *msg);
static void hammer2_relay_scan(h2span_connect_t *conn, h2span_node_t *node);
static void hammer2_relay_delete(h2span_relay_t *relay);

static void *hammer2_volconf_thread(void *info);
static void hammer2_volconf_stop(h2span_media_config_t *conf);
static void hammer2_volconf_start(h2span_media_config_t *conf,
				const char *hostname);

void
hammer2_msg_lnk_signal(hammer2_router_t *router __unused)
{
	pthread_mutex_lock(&cluster_mtx);
	hammer2_relay_scan(NULL, NULL);
	pthread_mutex_unlock(&cluster_mtx);
}

/*
 * Receive a HAMMER2_MSG_PROTO_LNK message.  This only called for
 * one-way and opening-transactions since state->func will be assigned
 * in all other cases.
 */
void
hammer2_msg_lnk(hammer2_msg_t *msg)
{
	switch(msg->any.head.cmd & HAMMER2_MSGF_BASECMDMASK) {
	case HAMMER2_LNK_CONN:
		hammer2_lnk_conn(msg);
		break;
	case HAMMER2_LNK_SPAN:
		hammer2_lnk_span(msg);
		break;
	default:
		fprintf(stderr,
			"MSG_PROTO_LNK: Unknown msg %08x\n", msg->any.head.cmd);
		hammer2_msg_reply(msg, HAMMER2_MSG_ERR_NOSUPP);
		/* state invalid after reply */
		break;
	}
}

void
hammer2_lnk_conn(hammer2_msg_t *msg)
{
	hammer2_state_t *state = msg->state;
	h2span_media_t *media;
	h2span_media_config_t *conf;
	h2span_connect_t *conn;
	h2span_relay_t *relay;
	char *alloc = NULL;
	int i;

	pthread_mutex_lock(&cluster_mtx);

	switch(msg->any.head.cmd & HAMMER2_MSGF_TRANSMASK) {
	case HAMMER2_LNK_CONN | HAMMER2_MSGF_CREATE:
	case HAMMER2_LNK_CONN | HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE:
		/*
		 * On transaction start we allocate a new h2span_connect and
		 * acknowledge the request, leaving the transaction open.
		 * We then relay priority-selected SPANs.
		 */
		fprintf(stderr, "LNK_CONN(%08x): %s/%s\n",
			(uint32_t)msg->any.head.msgid,
			hammer2_uuid_to_str(&msg->any.lnk_conn.pfs_clid,
					    &alloc),
			msg->any.lnk_conn.label);
		free(alloc);

		conn = hammer2_alloc(sizeof(*conn));

		RB_INIT(&conn->tree);
		conn->state = state;
		state->func = hammer2_lnk_conn;
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
			media = hammer2_alloc(sizeof(*media));
			media->mediaid = msg->any.lnk_conn.mediaid;
			TAILQ_INSERT_TAIL(&mediaq, media, entry);
		}
		conn->media = media;
		++media->refs;

		if ((msg->any.head.cmd & HAMMER2_MSGF_DELETE) == 0) {
			hammer2_msg_result(msg, 0);
			hammer2_router_signal(msg->router);
			break;
		}
		/* FALL THROUGH */
	case HAMMER2_LNK_CONN | HAMMER2_MSGF_DELETE:
	case HAMMER2_LNK_ERROR | HAMMER2_MSGF_DELETE:
deleteconn:
		/*
		 * On transaction terminate we clean out our h2span_connect
		 * and acknowledge the request, closing the transaction.
		 */
		fprintf(stderr, "LNK_CONN: Terminated\n");
		conn = state->any.conn;
		assert(conn);

		/*
		 * Clean out the media structure. If refs drops to zero we
		 * also clean out the media config threads.  These threads
		 * maintain span connections to other hammer2 service daemons.
		 */
		media = conn->media;
		if (--media->refs == 0) {
			fprintf(stderr, "Shutting down media spans\n");
			for (i = 0; i < HAMMER2_COPYID_COUNT; ++i) {
				conf = &media->config[i];

				if (conf->thread == NULL)
					continue;
				conf->ctl = H2CONFCTL_STOP;
				pthread_cond_signal(&conf->cond);
			}
			for (i = 0; i < HAMMER2_COPYID_COUNT; ++i) {
				conf = &media->config[i];

				if (conf->thread == NULL)
					continue;
				pthread_mutex_unlock(&cluster_mtx);
				pthread_join(conf->thread, NULL);
				pthread_mutex_lock(&cluster_mtx);
				conf->thread = NULL;
				pthread_cond_destroy(&conf->cond);
			}
			fprintf(stderr, "Media shutdown complete\n");
			TAILQ_REMOVE(&mediaq, media, entry);
			hammer2_free(media);
		}

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
		conn->media = NULL;
		conn->state = NULL;
		msg->state->any.conn = NULL;
		TAILQ_REMOVE(&connq, conn, entry);
		hammer2_free(conn);

		hammer2_msg_reply(msg, 0);
		/* state invalid after reply */
		break;
	case HAMMER2_LNK_VOLCONF:
		/*
		 * One-way volume-configuration message is transmitted
		 * over the open LNK_CONN transaction.
		 */
		fprintf(stderr, "RECEIVED VOLCONF\n");
		if (msg->any.lnk_volconf.index < 0 ||
		    msg->any.lnk_volconf.index >= HAMMER2_COPYID_COUNT) {
			fprintf(stderr, "VOLCONF: ILLEGAL INDEX %d\n",
				msg->any.lnk_volconf.index);
			break;
		}
		if (msg->any.lnk_volconf.copy.path[sizeof(msg->any.lnk_volconf.copy.path) - 1] != 0 ||
		    msg->any.lnk_volconf.copy.path[0] == 0) {
			fprintf(stderr, "VOLCONF: ILLEGAL PATH %d\n",
				msg->any.lnk_volconf.index);
			break;
		}
		conn = msg->state->any.conn;
		if (conn == NULL) {
			fprintf(stderr, "VOLCONF: LNK_CONN is missing\n");
			break;
		}
		conf = &conn->media->config[msg->any.lnk_volconf.index];
		conf->copy_pend = msg->any.lnk_volconf.copy;
		conf->ctl |= H2CONFCTL_UPDATE;
		if (conf->thread == NULL) {
			fprintf(stderr, "VOLCONF THREAD STARTED\n");
			pthread_cond_init(&conf->cond, NULL);
			pthread_create(&conf->thread, NULL,
				       hammer2_volconf_thread, (void *)conf);
		}
		pthread_cond_signal(&conf->cond);
		break;
	default:
		/*
		 * Failsafe
		 */
		if (msg->any.head.cmd & HAMMER2_MSGF_DELETE)
			goto deleteconn;
		hammer2_msg_reply(msg, HAMMER2_MSG_ERR_NOSUPP);
		break;
	}
	pthread_mutex_unlock(&cluster_mtx);
}

void
hammer2_lnk_span(hammer2_msg_t *msg)
{
	hammer2_state_t *state = msg->state;
	h2span_cluster_t dummy_cls;
	h2span_node_t dummy_node;
	h2span_cluster_t *cls;
	h2span_node_t *node;
	h2span_link_t *slink;
	h2span_relay_t *relay;
	char *alloc = NULL;

	assert((msg->any.head.cmd & HAMMER2_MSGF_REPLY) == 0);

	pthread_mutex_lock(&cluster_mtx);

	/*
	 * On transaction start we initialize the tracking infrastructure
	 */
	if (msg->any.head.cmd & HAMMER2_MSGF_CREATE) {
		assert(state->func == NULL);
		state->func = hammer2_lnk_span;

		msg->any.lnk_span.label[sizeof(msg->any.lnk_span.label)-1] = 0;

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

		/*
		 * Embedded router structure in link for message forwarding.
		 *
		 * The spanning id for the router is the message id of
		 * the SPAN link it is embedded in, allowing messages to
		 * be routed via &slink->router.
		 */
		slink->router = hammer2_router_alloc();
		slink->router->iocom = state->iocom;
		slink->router->link = slink;
		slink->router->target = state->msgid;
		hammer2_router_connect(slink->router);

		RB_INSERT(h2span_link_tree, &node->tree, slink);

		fprintf(stderr, "LNK_SPAN(thr %p): %p %s/%s dist=%d\n",
			msg->router->iocom,
			slink,
			hammer2_uuid_to_str(&msg->any.lnk_span.pfs_clid,
					    &alloc),
			msg->any.lnk_span.label,
			msg->any.lnk_span.dist);
		free(alloc);
#if 0
		hammer2_relay_scan(NULL, node);
#endif
		hammer2_router_signal(msg->router);
	}

	/*
	 * On transaction terminate we remove the tracking infrastructure.
	 */
	if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		slink = state->any.link;
		assert(slink != NULL);
		node = slink->node;
		cls = node->cls;

		fprintf(stderr, "LNK_DELE(thr %p): %p %s/%s dist=%d\n",
			msg->router->iocom,
			slink,
			hammer2_uuid_to_str(&cls->pfs_clid, &alloc),
			state->msg->any.lnk_span.label,
			state->msg->any.lnk_span.dist);
		free(alloc);

		/*
		 * Remove the router from consideration
		 */
		hammer2_router_disconnect(&slink->router);

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
			if (RB_EMPTY(&cls->tree) && cls->refs == 0) {
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
#if 0
		if (node)
			hammer2_relay_scan(NULL, node);
#endif
		if (node)
			hammer2_router_signal(msg->router);
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
hammer2_lnk_relay(hammer2_msg_t *msg)
{
	hammer2_state_t *state = msg->state;
	h2span_relay_t *relay;

	assert(msg->any.head.cmd & HAMMER2_MSGF_REPLY);

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
	 * Locate the first related relay for the node on this connection.
	 * relay will be NULL if there were none.
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
	/* fprintf(stderr, "LOOP\n"); */
	RB_FOREACH(slink, h2span_link_tree, &node->tree) {
		/*
		fprintf(stderr, "SLINK %p RELAY %p(%p)\n",
			slink, relay, relay ? relay->link : NULL);
		*/
		/*
		 * PROPAGATE THE BEST LINKS OVER THE SPECIFIED CONNECTION.
		 *
		 * Track relays while iterating the best links and construct
		 * missing relays when necessary.
		 *
		 * (If some prior better link was removed it would have also
		 *  removed the relay, so the relay can only match exactly or
		 *  be worse).
		 */
		if (relay && relay->link == slink) {
			/*
			 * Match, relay already in-place, get the next
			 * relay to match against the next slink.
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
			 *
			 * All later spans will also be too far away so
			 * we can break out of the loop.
			 */
			break;
		} else if (slink->state->iocom == conn->state->iocom) {
			/*
			 * No match but we would transmit a LNK_SPAN
			 * out the same connection it came in on, which
			 * can be trivially optimized out.
			 */
			break;
		} else {
			/*
			 * No match, distance is ok, construct a new relay.
			 * (slink is better than relay).
			 */
			hammer2_msg_t *msg;

			assert(relay == NULL ||
			       relay->link->node != slink->node ||
			       relay->link->dist >= slink->dist);
			relay = hammer2_alloc(sizeof(*relay));
			relay->conn = conn;
			relay->link = slink;

			msg = hammer2_msg_alloc(conn->state->iocom->router, 0,
						HAMMER2_LNK_SPAN |
						HAMMER2_MSGF_CREATE,
						hammer2_lnk_relay, relay);
			relay->state = msg->state;
			relay->router = hammer2_router_alloc();
			relay->router->iocom = relay->state->iocom;
			relay->router->relay = relay;
			relay->router->target = relay->state->msgid;

			msg->any.lnk_span = slink->state->msg->any.lnk_span;
			msg->any.lnk_span.dist = slink->dist + 1;

			hammer2_router_connect(relay->router);

			RB_INSERT(h2span_relay_tree, &conn->tree, relay);
			TAILQ_INSERT_TAIL(&slink->relayq, relay, entry);

			hammer2_msg_write(msg);

			fprintf(stderr,
				"RELAY SPAN %p RELAY %p ON CLS=%p NODE=%p DIST=%d "
				"FD %d state %p\n",
				slink,
				relay,
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
		"RELAY DELETE %p RELAY %p ON CLS=%p NODE=%p DIST=%d FD %d STATE %p\n",
		relay->link,
		relay,
		relay->link->node->cls, relay->link->node,
		relay->link->dist,
		relay->conn->state->iocom->sock_fd, relay->state);

	hammer2_router_disconnect(&relay->router);

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

static void *
hammer2_volconf_thread(void *info)
{
	h2span_media_config_t *conf = info;

	pthread_mutex_lock(&cluster_mtx);
	while ((conf->ctl & H2CONFCTL_STOP) == 0) {
		if (conf->ctl & H2CONFCTL_UPDATE) {
			fprintf(stderr, "VOLCONF UPDATE\n");
			conf->ctl &= ~H2CONFCTL_UPDATE;
			if (bcmp(&conf->copy_run, &conf->copy_pend,
				 sizeof(conf->copy_run)) == 0) {
				fprintf(stderr, "VOLCONF: no changes\n");
				continue;
			}
			/*
			 * XXX TODO - auto reconnect on lookup failure or
			 *		connect failure or stream failure.
			 */

			pthread_mutex_unlock(&cluster_mtx);
			hammer2_volconf_stop(conf);
			conf->copy_run = conf->copy_pend;
			if (conf->copy_run.copyid != 0 &&
			    strncmp(conf->copy_run.path, "span:", 5) == 0) {
				hammer2_volconf_start(conf,
						      conf->copy_run.path + 5);
			}
			pthread_mutex_lock(&cluster_mtx);
			fprintf(stderr, "VOLCONF UPDATE DONE state %d\n", conf->state);
		}
		if (conf->state == H2MC_CONNECT) {
			hammer2_volconf_start(conf, conf->copy_run.path + 5);
			pthread_mutex_unlock(&cluster_mtx);
			sleep(5);
			pthread_mutex_lock(&cluster_mtx);
		} else {
			pthread_cond_wait(&conf->cond, &cluster_mtx);
		}
	}
	pthread_mutex_unlock(&cluster_mtx);
	hammer2_volconf_stop(conf);
	return(NULL);
}

static
void
hammer2_volconf_stop(h2span_media_config_t *conf)
{
	switch(conf->state) {
	case H2MC_STOPPED:
		break;
	case H2MC_CONNECT:
		conf->state = H2MC_STOPPED;
		break;
	case H2MC_RUNNING:
		shutdown(conf->fd, SHUT_WR);
		pthread_join(conf->iocom_thread, NULL);
		conf->iocom_thread = NULL;
		break;
	}
}

static
void
hammer2_volconf_start(h2span_media_config_t *conf, const char *hostname)
{
	switch(conf->state) {
	case H2MC_STOPPED:
	case H2MC_CONNECT:
		conf->fd = hammer2_connect(hostname);
		if (conf->fd < 0) {
			fprintf(stderr, "Unable to connect to %s\n", hostname);
			conf->state = H2MC_CONNECT;
		} else {
			pthread_create(&conf->iocom_thread, NULL,
				       master_service,
				       (void *)(intptr_t)conf->fd);
			conf->state = H2MC_RUNNING;
		}
		break;
	case H2MC_RUNNING:
		break;
	}
}

/************************************************************************
 *			ROUTER AND MESSAGING HANDLES			*
 ************************************************************************
 *
 * Basically the idea here is to provide a stable data structure which
 * can be localized to the caller for higher level protocols to work with.
 * Depends on the context, these hammer2_handle's can be pooled by use-case
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
hammer2_cluster_get(uuid_t *pfs_clid)
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
hammer2_cluster_put(h2span_cluster_t *cls)
{
	pthread_mutex_lock(&cluster_mtx);
	assert(cls->refs > 0);
	--cls->refs;
	if (RB_EMPTY(&cls->tree) && cls->refs == 0) {
		RB_REMOVE(h2span_cluster_tree,
			  &cluster_tree, cls);
		hammer2_free(cls);
	}
	pthread_mutex_unlock(&cluster_mtx);
}

/*
 * Obtain a stable handle to a specific cluster node given its uuid.
 * This handle does NOT lock in the route to the node and is typically
 * used as part of the hammer2_handle_*() API to obtain a set of
 * stable nodes.
 */
h2span_node_t *
hammer2_node_get(h2span_cluster_t *cls, uuid_t *pfs_fsid)
{
}

#endif

#if 0
/*
 * Acquire a persistent router structure given the cluster and node ids.
 * Messages can be transacted via this structure while held.  If the route
 * is lost messages will return failure.
 */
hammer2_router_t *
hammer2_router_get(uuid_t *pfs_clid, uuid_t *pfs_fsid)
{
}

/*
 * Release previously acquired router.
 */
void
hammer2_router_put(hammer2_router_t *router)
{
}
#endif

/************************************************************************
 *				DEBUGGER				*
 ************************************************************************/
/*
 * Dumps the spanning tree
 */
void
shell_tree(hammer2_router_t *router, char *cmdbuf __unused)
{
	h2span_cluster_t *cls;
	h2span_node_t *node;
	h2span_link_t *slink;
	char *uustr = NULL;

	pthread_mutex_lock(&cluster_mtx);
	RB_FOREACH(cls, h2span_cluster_tree, &cluster_tree) {
		router_printf(router, "Cluster %s\n",
			     hammer2_uuid_to_str(&cls->pfs_clid, &uustr));
		RB_FOREACH(node, h2span_node_tree, &cls->tree) {
			router_printf(router, "    Node %s (%s)\n",
				 hammer2_uuid_to_str(&node->pfs_fsid, &uustr),
				 node->label);
			RB_FOREACH(slink, h2span_link_tree, &node->tree) {
				router_printf(router, "\tLink dist=%d via %d\n",
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
