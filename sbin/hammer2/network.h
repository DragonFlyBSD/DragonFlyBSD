/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

#include <openssl/rsa.h>	/* public/private key functions */
#include <openssl/pem.h>	/* public/private key file load */
#include <openssl/err.h>
#include <openssl/evp.h>	/* aes_256_cbc functions */

/***************************************************************************
 *				CRYPTO HANDSHAKE			   *
 ***************************************************************************
 *
 * The initial public-key exchange is implementing by transmitting a
 * 512-byte buffer to the other side in a symmetrical fashion.  This
 * buffer contains the following:
 *
 * (1) A random session key.  512 bits is specified.  We use aes_256_cbc()
 *     and initialize the key with the first 256 bits and the iv[] with
 *     the second.  Note that the transmitted and received session
 *     keys are XOR'd together to create the session key used for
 *     communications (so even if the verifier is compromised the session
 *     will still be gobbly gook if the public key has not been completely
 *     broken).
 *
 * (2) A verifier to determine that the decode was successful.  It encodes
 *     an XOR of each group of 4 bytes from the session key.
 *
 * (3) Additional configuration and additional random data.
 *
 *     - The hammer2 message header magic for endian detect
 *
 *     - The hammer2 protocol version.  The two sides agree on the
 *	 smaller of the two.
 *
 *     - All unused fields (junk*) are filled with random data.
 *
 * This structure must be exactly 512 bytes and expects to use 256-byte
 * RSA keys.
 */
struct hammer2_handshake {
	char pad1[8];		/* 000 */
	uint16_t magic;		/* 008 HAMMER2_MSGHDR_MAGIC for endian detect */
	uint16_t version;	/* 00A hammer2 protocol version */
	uint32_t flags;		/* 00C protocol extension flags */
	uint8_t sess[64];	/* 010 512-bit session key */
	uint8_t verf[16];	/* 050 verifier = ~sess */
	char quickmsg[32];	/* 060 reason for connecting */
	char junk080[128];	/* 080-0FF */
	char pad2[8];		/* 100-107 */
	char junk100[256-8];	/* 108-1FF */
};

typedef struct hammer2_handshake hammer2_handshake_t;

#define HAMMER2_AES_KEY_SIZE	32
#define HAMMER2_AES_KEY_MASK	(HAMMER2_AES_KEY_SIZE - 1)
#define HAMMER2_AES_TYPE	aes_256_cbc
#define HAMMER2_AES_TYPE_EVP	EVP_aes_256_cbc()
#define HAMMER2_AES_TYPE_STR	#HAMMER2_AES_TYPE

/***************************************************************************
 *				LOW LEVEL MESSAGING			   *
 ***************************************************************************
 *
 * hammer2_msg - A standalone copy of a message, typically referenced by
 *		 or embedded in other structures, or used with I/O queues.
 *
 * These structures are strictly temporary, so they do not have to be
 * particularly optimized for size.  All possible message headers are
 * directly embedded (any), and the message may contain a reference
 * to allocated auxillary data.  The structure is recycled quite often
 * by a connection.
 *
 * This structure is typically not used for storing persistent message
 * state (see hammer2_persist for that).
 */
struct hammer2_iocom;
struct hammer2_persist;

struct hammer2_msg {
	struct hammer2_iocom *iocom;
	struct hammer2_persist  *persist;
	TAILQ_ENTRY(hammer2_msg) entry;	/* queue */
	char		*aux_data;	/* aux-data if any */
	int		aux_size;
	int		flags;
	hammer2_any_t	any;		/* raw extended msg header */
};

typedef struct hammer2_msg hammer2_msg_t;

TAILQ_HEAD(hammer2_msg_queue, hammer2_msg);
typedef struct hammer2_msg_queue hammer2_msg_queue_t;

#define HAMMER2_MSGX_BSWAPPED	0x0001

/*
 * hammer2_ioq - An embedded component of hammer2_connect, holds state
 * for the buffering and parsing of incoming and outgoing messages.
 */
struct hammer2_ioq {
	enum { HAMMER2_MSGQ_STATE_HEADER1,
	       HAMMER2_MSGQ_STATE_HEADER2,
	       HAMMER2_MSGQ_STATE_AUXDATA1,
	       HAMMER2_MSGQ_STATE_AUXDATA2,
	       HAMMER2_MSGQ_STATE_ERROR } state;
	int		fifo_beg;		/* buffered data */
	int		fifo_cdx;		/* encrypt/decrypt index */
	int		fifo_end;
	int		hbytes;			/* header size */
	int		abytes;			/* aux_data size */
	int		already;		/* aux_data already decrypted */
	int		error;
	int		seq;			/* salt sequencer */
	int		msgcount;
	EVP_CIPHER_CTX	ctx;
	char		iv[HAMMER2_AES_KEY_SIZE]; /* encrypt or decrypt iv[] */
	hammer2_msg_t	*msg;
	hammer2_msg_queue_t msgq;
	char		buf[HAMMER2_MSGBUF_SIZE]; /* staging buffer */
};

typedef struct hammer2_ioq hammer2_ioq_t;

#define HAMMER2_IOQ_ERROR_SYNC		1	/* bad magic / out of sync */
#define HAMMER2_IOQ_ERROR_EOF		2	/* unexpected EOF */
#define HAMMER2_IOQ_ERROR_SOCK		3	/* read() error on socket */
#define HAMMER2_IOQ_ERROR_FIELD		4	/* invalid field */
#define HAMMER2_IOQ_ERROR_HCRC		5	/* core header crc bad */
#define HAMMER2_IOQ_ERROR_XCRC		6	/* ext header crc bad */
#define HAMMER2_IOQ_ERROR_ACRC		7	/* aux data crc bad */
#define HAMMER2_IOQ_ERROR_STATE		8	/* bad state */
#define HAMMER2_IOQ_ERROR_NOPEER	9	/* bad socket peer */
#define HAMMER2_IOQ_ERROR_NORKEY	10	/* no remote keyfile found */
#define HAMMER2_IOQ_ERROR_NOLKEY	11	/* no local keyfile found */
#define HAMMER2_IOQ_ERROR_KEYXCHGFAIL	12	/* key exchange failed */
#define HAMMER2_IOQ_ERROR_KEYFMT	13	/* key file format problem */
#define HAMMER2_IOQ_ERROR_BADURANDOM	14	/* /dev/urandom is bad */
#define HAMMER2_IOQ_ERROR_MSGSEQ	15	/* message sequence error */

#define HAMMER2_IOQ_MAXIOVEC    16

/*
 * hammer2_iocom - governs a messaging stream connection
 */
struct hammer2_iocom {
	hammer2_ioq_t	ioq_rx;
	hammer2_ioq_t	ioq_tx;
	hammer2_msg_queue_t freeq;		/* free msgs hdr only */
	hammer2_msg_queue_t freeq_aux;		/* free msgs w/aux_data */
	void	(*recvmsg_callback)(struct hammer2_iocom *);
	void	(*sendmsg_callback)(struct hammer2_iocom *);
	void	(*altmsg_callback)(struct hammer2_iocom *);
	int	sock_fd;			/* comm socket or pipe */
	int	alt_fd;				/* thread signal, tty, etc */
	int	flags;
	int	rxmisc;
	int	txmisc;
	char	sess[HAMMER2_AES_KEY_SIZE];	/* aes_256_cbc key */
};

typedef struct hammer2_iocom hammer2_iocom_t;

#define HAMMER2_IOCOMF_EOF	0x00000001	/* EOF or ERROR on desc */
#define HAMMER2_IOCOMF_RREQ	0x00000002	/* request read-data event */
#define HAMMER2_IOCOMF_WREQ	0x00000004	/* request write-avail event */
#define HAMMER2_IOCOMF_WIDLE	0x00000008	/* request write-avail event */
#define HAMMER2_IOCOMF_SIGNAL	0x00000010
#define HAMMER2_IOCOMF_CRYPTED	0x00000020	/* encrypt enabled */

/***************************************************************************
 *				HIGH LEVEL MESSAGING			   *
 ***************************************************************************
 *
 * Persistent state is stored via the hammer2_persist structure.
 */
struct hammer2_persist {
	uint32_t	lcmd;		/* recent command direction */
	uint32_t	lrep;		/* recent reply direction */
};

typedef struct hammer2_persist hammer2_persist_t;

#if 0



/*
 * The global registration structure consolidates information accumulated
 * via the spanning tree algorithm and tells us which connection (link)
 * is the best path to get to any given registration.
 *
 * glob_node	- Splay entry for this registration in the global index
 *		  of all registrations.
 *
 * glob_entry	- tailq entry when this registration's best_span element
 *		  has changed state.
 *
 * span_list	- Head of a simple list of spanning tree entries which
 *		  we use to determine the best link.
 *
 * best_span	- Which of the span structure on span_list is the best
 *		  one.
 *
 * source_root	- Splay tree root indexing all mesasges sent from this
 *		  registration.  The messages are indexed by
 *		  {linkid,msgid} XXX
 *
 * target_root	- Splay tree root indexing all messages being sent to
 *		  this registration.  The messages are indexed by
 *		  {linkid,msgid}. XXX
 *
 *
 * Whenever spanning tree data causes a registration's best_link field to
 * change that registration is transmitted as spanning tree data to every
 * active link.  Note that pure clients to the cluster, of which there can
 * be millions, typically do not transmit spanning tree data to each other.
 *
 * Each registration is assigned a unique linkid local to the node (another
 * node might assign a different linkid to the same registration).  This
 * linkid must be persistent as long as messages are active and is used
 * to identify the message source and target.
 */
TAILQ_HEAD(hammer2_span_list, hammer2_span);
typedef struct hammer2_span_list hammer2_span_list_t;

struct hammer2_reg {
	SPLAY_ENTRY(hammer2_reg) glob_node;	/* index of registrations */
	TAILQ_ENTRY(hammer2_reg) glob_entry;	/* when modified */
	hammer2_span_list_t	span_list;	/* list of hammer2_span's */
	hammer2_span_t		*best_span;	/* best span entry */
	hammer2_pmsg_splay_head_t source_root; 	/* msgs sent from reg */
	hammer2_pmsg_splay_head_t target_root; 	/* msgs sent to reg */
	uuid_t	pfs_id;				/* key field */
	uuid_t  pfs_fsid;			/* key field */
	uint32_t linkid;
	int	flags;
	int	refs;
};

#define HAMMER2_PROTO_REGF_MODIFIED	0x0001

/*
 * Each link (connection) collects spanning tree data received via the
 * link and stores it in these span structures.
 */
struct hammer2_span {
	TAILQ_ENTRY(hammer2_span)	span_entry;	/* from hammer2_reg */
	SPLAY_ENTRY(hammer2_span)	span_node;	/* from hammer2_link */
	hammer2_reg_t			*reg;
	hammer2_link_t			*link;
	int				weight;
};

/*
 * Most hammer2 messages represent transactions and have persistent state
 * which must be recorded.  Some messages, such as cache states and inode
 * representations are very long-lasting transactions.
 *
 * Each node in the graph must keep track of the message state in order
 * to perform the proper action when a connection is lost.  To do this
 * the message is indexed on the source and target (global) registration,
 * and the actual span element the message was received on and transmitted
 * to is recorded (allowing us to retrieve the physical links involved).
 *
 * The {source_reg, target_reg, msgid} uniquely identifies a message.  Any
 * streaming operations using the same msgid use the same rendezvous.
 *
 * It is important to note that recorded state must use the same physical
 * link (and thus the same chain of links across the graph) as was 'forged'
 * by the initial message for that msgid.  If the source span a message is
 * received on does not match the recorded source, or the recorded target
 * is no longer routeable, the message will be returned or generate an ABORT
 * with LINKFAIL as appropriate.
 */
struct hammer2_pmsg {
	SPLAY_ENTRY(hammer2_pmsg) source_reg;
	SPLAY_ENTRY(hammer2_pmsg) target_reg;
	hammer2_span_t	*source;
	hammer2_span_t	*target;
	uint16_t	msgid;
};

#endif
