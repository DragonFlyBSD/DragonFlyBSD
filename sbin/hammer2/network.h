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
struct hammre2_state;
struct hammre2_msg;

TAILQ_HEAD(hammer2_msg_queue, hammer2_msg);
RB_HEAD(hammer2_state_tree, hammer2_state);

struct hammer2_state {
	RB_ENTRY(hammer2_state) rbnode;		/* indexed by msgid */
	struct hammer2_iocom *iocom;
	uint32_t	txcmd;			/* mostly for CMDF flags */
	uint32_t	rxcmd;			/* mostly for CMDF flags */
	uint16_t	source;			/* command originator */
	uint16_t	target;			/* reply originator */
	uint32_t	msgid;			/* {source,target,msgid} uniq */
	int		flags;
	int		error;
	struct hammer2_msg *msg;
	int (*func)(struct hammer2_iocom *, struct hammer2_msg *);
};

#define HAMMER2_STATE_INSERTED	0x0001
#define HAMMER2_STATE_DYNAMIC	0x0002

struct hammer2_msg {
	TAILQ_ENTRY(hammer2_msg) qentry;
	struct hammer2_state *state;
	size_t		hdr_size;
	size_t		aux_size;
	char		*aux_data;
	hammer2_any_t	any;
};

typedef struct hammer2_state hammer2_state_t;
typedef struct hammer2_msg hammer2_msg_t;
typedef struct hammer2_msg_queue hammer2_msg_queue_t;

int hammer2_state_cmp(hammer2_state_t *state1, hammer2_state_t *state2);
RB_PROTOTYPE(hammer2_state_tree, hammer2_state, rbnode, hammer2_state_cmp);

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
	size_t		hbytes;			/* header size */
	size_t		abytes;			/* aux_data size */
	size_t		already;		/* aux_data already decrypted */
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
#define HAMMER2_IOQ_ERROR_EALREADY	16	/* ignore this message */
#define HAMMER2_IOQ_ERROR_TRANS		17	/* state transaction issue */

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
	struct hammer2_state_tree staterd_tree; /* active messages */
	struct hammer2_state_tree statewr_tree; /* active messages */
};

typedef struct hammer2_iocom hammer2_iocom_t;

#define HAMMER2_IOCOMF_EOF	0x00000001	/* EOF or ERROR on desc */
#define HAMMER2_IOCOMF_RREQ	0x00000002	/* request read-data event */
#define HAMMER2_IOCOMF_WREQ	0x00000004	/* request write-avail event */
#define HAMMER2_IOCOMF_WIDLE	0x00000008	/* request write-avail event */
#define HAMMER2_IOCOMF_SIGNAL	0x00000010
#define HAMMER2_IOCOMF_CRYPTED	0x00000020	/* encrypt enabled */
