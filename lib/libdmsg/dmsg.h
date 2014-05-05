/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/dmsg.h>

#include <pthread.h>

#if 0
#include <openssl/rsa.h>	/* public/private key functions */
#include <openssl/pem.h>	/* public/private key file load */
#endif
#include <openssl/err.h>
#include <openssl/evp.h>	/* aes_256_cbc functions */

#define DMSG_DEFAULT_DIR	"/etc/hammer2"
#define DMSG_PATH_REMOTE	DMSG_DEFAULT_DIR "/remote"

#define DMSG_LISTEN_PORT	987

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
struct dmsg_handshake {
	char pad1[8];		/* 000 */
	uint16_t magic;		/* 008 DMSG_HDR_MAGIC for endian detect */
	uint16_t version;	/* 00A hammer2 protocol version */
	uint32_t flags;		/* 00C protocol extension flags */
	uint8_t sess[64];	/* 010 512-bit session key */
	uint8_t verf[16];	/* 050 verifier = ~sess */
	char quickmsg[32];	/* 060 reason for connecting */
	char junk080[128];	/* 080-0FF */
	char pad2[8];		/* 100-107 */
	char junk100[256-8];	/* 108-1FF */
};

typedef struct dmsg_handshake dmsg_handshake_t;


#define DMSG_CRYPTO_CHUNK_SIZE		DMSG_ALIGN
#define DMSG_MAX_IV_SIZE		32

#define DMSG_CRYPTO_GCM_IV_FIXED_SIZE	4
#define DMSG_CRYPTO_GCM_IV_SIZE		12
#define DMSG_CRYPTO_GCM_KEY_SIZE	32
#define DMSG_CRYPTO_GCM_TAG_SIZE	16

#define DMSG_CRYPTO_ALGO_GCM_IDX	0

#define DMSG_CRYPTO_ALGO		DMSG_CRYPTO_ALGO_GCM_IDX

/***************************************************************************
 *				LOW LEVEL MESSAGING			   *
 ***************************************************************************
 *
 * dmsg_msg - A standalone copy of a message, typically referenced by
 *		 or embedded in other structures, or used with I/O queues.
 *
 * These structures are strictly temporary, so they do not have to be
 * particularly optimized for size.  All possible message headers are
 * directly embedded (any), and the message may contain a reference
 * to allocated auxillary data.  The structure is recycled quite often
 * by a connection.
 */
struct dmsg_iocom;
struct dmsg_state;
struct dmsg_msg;

TAILQ_HEAD(dmsg_state_queue, dmsg_state);
TAILQ_HEAD(dmsg_msg_queue, dmsg_msg);
RB_HEAD(dmsg_state_tree, dmsg_state);

struct h2span_link;
struct h2span_relay;
struct h2span_conn;

/*
 * This represents a media, managed by LNK_CONN connection state
 */
TAILQ_HEAD(dmsg_media_queue, dmsg_media);

struct dmsg_media {
	TAILQ_ENTRY(dmsg_media) entry;
	uuid_t  mediaid;
	int     refs;
	void	*usrhandle;
};

typedef struct dmsg_media dmsg_media_t;

/*
 * The state structure is ref-counted.  The iocom cannot go away while
 * state structures are active.  However, the related h2span_* linkages
 * can be destroyed and NULL'd out if the state is terminated in both
 * directions.
 */
struct dmsg_state {
	RB_ENTRY(dmsg_state) rbnode;		/* by state->msgid */
	TAILQ_HEAD(, dmsg_state) subq;		/* active stacked states */
	TAILQ_ENTRY(dmsg_state) entry;		/* on parent subq */
	struct dmsg_iocom *iocom;
	struct dmsg_state *parent;		/* transaction stacking */
	struct dmsg_state *relay;		/* routing */
	uint32_t	icmd;			/* command creating state */
	uint32_t	txcmd;			/* mostly for CMDF flags */
	uint32_t	rxcmd;			/* mostly for CMDF flags */
	uint64_t	msgid;
	int		flags;
	int		error;
	int		refs;			/* prevent destruction */
	void (*func)(struct dmsg_msg *);
	union {
		void *any;
		struct h2span_link *link;
		struct h2span_conn *conn;
		struct h2span_relay *relay;
	} any;
	dmsg_media_t	*media;
};

#define DMSG_STATE_INSERTED	0x0001
#define DMSG_STATE_DYNAMIC	0x0002
#define DMSG_STATE_NODEID	0x0004		/* manages a node id */
#define DMSG_STATE_UNUSED_0008	0x0008
#define DMSG_STATE_OPPOSITE	0x0010		/* initiated by other end */
#define DMSG_STATE_CIRCUIT	0x0020		/* LNK_SPAN special case */
#define DMSG_STATE_ROOT		0x8000		/* iocom->state0 */

/*
 * This is the core in-memory representation of a message structure.
 * state is the local representation of the transactional state and
 * will point to &iocom->state0 for non-transactional messages.
 *
 * Message headers are embedded while auxillary data is separately allocated.
 */
struct dmsg_msg {
	TAILQ_ENTRY(dmsg_msg) qentry;
	struct dmsg_state *state;		/* message state */
	size_t		hdr_size;
	size_t		aux_size;
	char		*aux_data;
	uint32_t	tcmd;			/* easy-switch cmd */
	dmsg_any_t 	any;			/* must be last element */
};

typedef struct dmsg_state dmsg_state_t;
typedef struct dmsg_msg dmsg_msg_t;
typedef struct dmsg_msg_queue dmsg_msg_queue_t;

int dmsg_state_cmp(dmsg_state_t *state1, dmsg_state_t *state2);
RB_PROTOTYPE(dmsg_state_tree, dmsg_state, rbnode, dmsg_state_cmp);

/*
 * dmsg_ioq - An embedded component of dmsg_conn, holds state
 * for the buffering and parsing of incoming and outgoing messages.
 *
 * cdx - beg  - processed buffer data, encrypted or decrypted
 * end - cdn  - unprocessed buffer data not yet encrypted or decrypted
 */
struct dmsg_ioq {
	enum { DMSG_MSGQ_STATE_HEADER1,
	       DMSG_MSGQ_STATE_HEADER2,
	       DMSG_MSGQ_STATE_AUXDATA1,
	       DMSG_MSGQ_STATE_AUXDATA2,
	       DMSG_MSGQ_STATE_ERROR } state;
	size_t		fifo_beg;		/* buffered data */
	size_t		fifo_cdx;		/* cdx-beg processed */
	size_t		fifo_cdn;		/* end-cdn unprocessed */
	size_t		fifo_end;
	size_t		hbytes;			/* header size */
	size_t		abytes;			/* aligned aux_data size */
	size_t		unaligned_aux_size;	/* actual aux_data size */
	int		error;
	int		seq;			/* salt sequencer */
	int		msgcount;
	EVP_CIPHER_CTX	ctx;
	char		iv[DMSG_MAX_IV_SIZE]; /* encrypt or decrypt iv[] */
	dmsg_msg_t	*msg;
	dmsg_msg_queue_t msgq;
	char		buf[DMSG_BUF_SIZE];	/* staging buffer */
};

typedef struct dmsg_ioq dmsg_ioq_t;

#define DMSG_IOQ_ERROR_SYNC		1	/* bad magic / out of sync */
#define DMSG_IOQ_ERROR_EOF		2	/* unexpected EOF */
#define DMSG_IOQ_ERROR_SOCK		3	/* read() error on socket */
#define DMSG_IOQ_ERROR_FIELD		4	/* invalid field */
#define DMSG_IOQ_ERROR_HCRC		5	/* core header crc bad */
#define DMSG_IOQ_ERROR_XCRC		6	/* ext header crc bad */
#define DMSG_IOQ_ERROR_ACRC		7	/* aux data crc bad */
#define DMSG_IOQ_ERROR_STATE		8	/* bad state */
#define DMSG_IOQ_ERROR_NOPEER		9	/* bad socket peer */
#define DMSG_IOQ_ERROR_NORKEY		10	/* no remote keyfile found */
#define DMSG_IOQ_ERROR_NOLKEY		11	/* no local keyfile found */
#define DMSG_IOQ_ERROR_KEYXCHGFAIL	12	/* key exchange failed */
#define DMSG_IOQ_ERROR_KEYFMT		13	/* key file format problem */
#define DMSG_IOQ_ERROR_BADURANDOM	14	/* /dev/urandom is bad */
#define DMSG_IOQ_ERROR_MSGSEQ		15	/* message sequence error */
#define DMSG_IOQ_ERROR_EALREADY		16	/* ignore this message */
#define DMSG_IOQ_ERROR_TRANS		17	/* state transaction issue */
#define DMSG_IOQ_ERROR_IVWRAP		18	/* IVs exhaused */
#define DMSG_IOQ_ERROR_MACFAIL		19	/* MAC of encr alg failed */
#define DMSG_IOQ_ERROR_ALGO		20	/* Misc. encr alg error */
#define DMSG_IOQ_ERROR_UNUSED21		21
#define DMSG_IOQ_ERROR_BAD_CIRCUIT	22	/* unconfigured circuit */
#define DMSG_IOQ_ERROR_UNUSED23		23
#define DMSG_IOQ_ERROR_ASSYM		24	/* Assymetric path */

#define DMSG_IOQ_MAXIOVEC    16

/*
 * dmsg_iocom - governs a messaging stream connection
 */
struct dmsg_iocom {
	char		*label;			/* label for error reporting */
	dmsg_ioq_t	ioq_rx;
	dmsg_ioq_t	ioq_tx;
	dmsg_msg_queue_t freeq;			/* free msgs hdr only */
	dmsg_msg_queue_t freeq_aux;		/* free msgs w/aux_data */
	dmsg_state_t	state0;			/* root state for stacking */
	struct dmsg_state_tree  staterd_tree;   /* active transactions */
	struct dmsg_state_tree  statewr_tree;   /* active transactions */
	int	sock_fd;			/* comm socket or pipe */
	int	alt_fd;				/* thread signal, tty, etc */
	int	wakeupfds[2];			/* pipe wakes up iocom thread */
	int	flags;
	int	rxmisc;
	int	txmisc;
	void	(*signal_callback)(struct dmsg_iocom *);
	void	(*altmsg_callback)(struct dmsg_iocom *);
	void	(*rcvmsg_callback)(dmsg_msg_t *msg);
	void	(*usrmsg_callback)(dmsg_msg_t *msg, int unmanaged);
	dmsg_msg_queue_t txmsgq;		/* tx msgq from remote */
	struct h2span_conn *conn;		/* if LNK_CONN active */
	uint64_t	conn_msgid;		/* LNK_CONN circuit */
	pthread_mutex_t	mtx;			/* mutex for state*tree/rmsgq */
};

typedef struct dmsg_iocom dmsg_iocom_t;

#define DMSG_IOCOMF_EOF		0x00000001	/* EOF or ERROR on desc */
#define DMSG_IOCOMF_RREQ	0x00000002	/* request read-data event */
#define DMSG_IOCOMF_WREQ	0x00000004	/* request write-avail event */
#define DMSG_IOCOMF_RWORK	0x00000008	/* immediate work pending */
#define DMSG_IOCOMF_WWORK	0x00000010	/* immediate work pending */
#define DMSG_IOCOMF_PWORK	0x00000020	/* immediate work pending */
#define DMSG_IOCOMF_ARWORK	0x00000040	/* immediate work pending */
#define DMSG_IOCOMF_AWWORK	0x00000080	/* immediate work pending */
#define DMSG_IOCOMF_SWORK	0x00000100	/* immediate work pending */
#define DMSG_IOCOMF_CRYPTED	0x00000200	/* encrypt enabled */
#define DMSG_IOCOMF_CLOSEALT	0x00000400	/* close alt_fd */

/*
 * Crypto algorithm table and related typedefs.
 */
typedef int (*algo_init_fn)(dmsg_ioq_t *, char *, int, char *, int, int);
typedef int (*algo_enc_fn)(dmsg_ioq_t *, char *, char *, int, int *);
typedef int (*algo_dec_fn)(dmsg_ioq_t *, char *, char *, int, int *);

struct crypto_algo {
	const char	*name;
	int		keylen;
	int		taglen;
	algo_init_fn	init;
	algo_enc_fn	enc_chunk;
	algo_dec_fn	dec_chunk;
};

/*
 * Master service thread info
 */
struct dmsg_master_service_info {
	int	fd;
	int	altfd;
	int	noclosealt;
	int	detachme;
	char	*label;
	void	*handle;
	void	(*altmsg_callback)(dmsg_iocom_t *iocom);
	void	(*usrmsg_callback)(dmsg_msg_t *msg, int unmanaged);
	void	(*exit_callback)(void *handle);
};

typedef struct dmsg_master_service_info dmsg_master_service_info_t;

/*
 * node callbacks
 */
#define DMSG_NODEOP_ADD		1
#define DMSG_NODEOP_DEL		2

/*
 * icrc
 */
uint32_t dmsg_icrc32(const void *buf, size_t size);
uint32_t dmsg_icrc32c(const void *buf, size_t size, uint32_t crc);

/*
 * debug
 */
const char *dmsg_basecmd_str(uint32_t cmd);
const char *dmsg_msg_str(dmsg_msg_t *msg);

/*
 * subs
 */
void *dmsg_alloc(size_t bytes);
void dmsg_free(void *ptr);
const char *dmsg_uuid_to_str(uuid_t *uuid, char **strp);
const char *dmsg_peer_type_to_str(uint8_t type);
int dmsg_connect(const char *hostname);

/*
 * Msg support functions
 */
void dmsg_bswap_head(dmsg_hdr_t *head);
void dmsg_ioq_init(dmsg_iocom_t *iocom, dmsg_ioq_t *ioq);
void dmsg_ioq_done(dmsg_iocom_t *iocom, dmsg_ioq_t *ioq);
void dmsg_iocom_init(dmsg_iocom_t *iocom, int sock_fd, int alt_fd,
			void (*state_func)(dmsg_iocom_t *iocom),
			void (*rcvmsg_func)(dmsg_msg_t *msg),
			void (*usrmsg_func)(dmsg_msg_t *msg, int unmanaged),
			void (*altmsg_func)(dmsg_iocom_t *iocom));
void dmsg_iocom_restate(dmsg_iocom_t *iocom,
			void (*state_func)(dmsg_iocom_t *iocom),
			void (*rcvmsg_func)(dmsg_msg_t *msg));
void dmsg_iocom_label(dmsg_iocom_t *iocom, const char *ctl, ...);
void dmsg_iocom_signal(dmsg_iocom_t *iocom);
void dmsg_iocom_done(dmsg_iocom_t *iocom);
dmsg_msg_t *dmsg_msg_alloc(dmsg_state_t *state, size_t aux_size, uint32_t cmd,
			void (*func)(dmsg_msg_t *), void *data);
void dmsg_msg_reply(dmsg_msg_t *msg, uint32_t error);
void dmsg_msg_result(dmsg_msg_t *msg, uint32_t error);
void dmsg_state_reply(dmsg_state_t *state, uint32_t error);
void dmsg_state_result(dmsg_state_t *state, uint32_t error);

void dmsg_msg_free(dmsg_msg_t *msg);

void dmsg_iocom_core(dmsg_iocom_t *iocom);
dmsg_msg_t *dmsg_ioq_read(dmsg_iocom_t *iocom);
void dmsg_msg_write(dmsg_msg_t *msg);

void dmsg_iocom_drain(dmsg_iocom_t *iocom);
void dmsg_iocom_flush1(dmsg_iocom_t *iocom);
void dmsg_iocom_flush2(dmsg_iocom_t *iocom);

void dmsg_state_relay(dmsg_msg_t *msg);
void dmsg_state_cleanuprx(dmsg_iocom_t *iocom, dmsg_msg_t *msg);
void dmsg_state_free(dmsg_state_t *state);

/*
 * Msg protocol functions
 */
void dmsg_msg_lnk_signal(dmsg_iocom_t *iocom);
void dmsg_msg_lnk(dmsg_msg_t *msg);
void dmsg_msg_dbg(dmsg_msg_t *msg);
void dmsg_shell_tree(dmsg_iocom_t *iocom, char *cmdbuf __unused);
int dmsg_debug_findspan(uint64_t msgid, dmsg_state_t **statep);
dmsg_state_t *dmsg_findspan(const char *label);


/*
 * Crypto functions
 */
void dmsg_crypto_setup(void);
void dmsg_crypto_negotiate(dmsg_iocom_t *iocom);
void dmsg_crypto_decrypt(dmsg_iocom_t *iocom, dmsg_ioq_t *ioq);
int dmsg_crypto_encrypt(dmsg_iocom_t *iocom, dmsg_ioq_t *ioq,
			struct iovec *iov, int n, size_t *nactp);

/*
 * Service daemon functions
 */
void *dmsg_master_service(void *data);
void dmsg_printf(dmsg_iocom_t *iocom, const char *ctl, ...) __printflike(2, 3);

extern int DMsgDebugOpt;
