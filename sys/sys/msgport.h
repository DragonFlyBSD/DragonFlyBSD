/*
 * SYS/MSGPORT.H
 *
 *	Implements LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport.h,v 1.5 2003/07/26 18:12:46 dillon Exp $
 */

#ifndef _SYS_MSGPORT_H_
#define _SYS_MSGPORT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>		/* TAILQ_* macros */
#endif

struct lwkt_msg;
struct lwkt_port;
struct thread;

typedef struct lwkt_msg		*lwkt_msg_t;
typedef struct lwkt_port	*lwkt_port_t;

typedef TAILQ_HEAD(lwkt_msg_queue, lwkt_msg) lwkt_msg_queue;

/*
 * The standard message and port structure for communications between
 * threads.  See kern/lwkt_msgport.c for documentation on how messages and
 * ports work.
 *
 * NOTE! 64-bit-align this structure.
 */
typedef struct lwkt_msg {
    TAILQ_ENTRY(lwkt_msg) ms_node;	/* link node (not always used) */
    union {
	struct lwkt_msg	*ms_next;	/* chaining / cache */
	union sysunion  *ms_sysnext;	/* chaining / cache */
	struct lwkt_msg  *ms_umsg;	/* user message (UVA address) */
    } opaque;
    lwkt_port_t ms_target_port;		/* only used in certain situations */
    lwkt_port_t	ms_reply_port;		/* asynch replies returned here */
    int		ms_abortreq;		/* set asynchronously */
    int		ms_cmd;
    int		ms_flags;
    int		ms_error;
    union {
	void	*ms_resultp;		/* misc pointer result */
	int	ms_result;		/* standard 'int'eger result */
	long	ms_lresult;		/* long result */
	int	ms_fds[2];		/* two int bit results */
	int32_t	ms_result32;		/* 32 bit result */
	int64_t	ms_result64;		/* 64 bit result */
	off_t	ms_offset;		/* off_t result */
    } u;
    int		ms_pad[2];		/* future use */
} lwkt_msg;

#define MSGF_DONE	0x0001		/* asynch message is complete */
#define MSGF_REPLY	0x0002		/* asynch message has been returned */
#define MSGF_QUEUED	0x0004		/* message has been queued sanitychk */
#define MSGF_ASYNC	0x0008		/* sync/async hint */

#define MSG_CMD_CDEV	0x00010000
#define MSG_CMD_VFS	0x00020000
#define MSG_CMD_SYSCALL	0x00030000
#define MSG_SUBCMD_MASK	0x0000FFFF

typedef struct lwkt_port {
    lwkt_msg_queue	mp_msgq;
    int			mp_flags;
    struct thread	*mp_td;
    int			(*mp_beginmsg)(lwkt_port_t port, lwkt_msg_t msg);
    void		(*mp_abortmsg)(lwkt_port_t port, lwkt_msg_t msg);
    void		(*mp_returnmsg)(lwkt_port_t port, lwkt_msg_t msg);
} lwkt_port;

#define MSGPORTF_WAITING	0x0001

#ifdef _KERNEL

extern void lwkt_init_port(lwkt_port_t port, struct thread *td);
extern void lwkt_initmsg_td(lwkt_msg_t msg, struct thread *td);
extern void lwkt_sendmsg(lwkt_port_t port, lwkt_msg_t msg);
extern int lwkt_domsg(lwkt_port_t port, lwkt_msg_t msg);
extern int lwkt_waitmsg(lwkt_msg_t msg);
extern void lwkt_replyport(lwkt_port_t port, lwkt_msg_t msg);
extern void lwkt_abortport(lwkt_port_t port, lwkt_msg_t msg);
extern int lwkt_putport(lwkt_port_t port, lwkt_msg_t msg);
extern void *lwkt_getport(lwkt_port_t port);
extern void *lwkt_waitport(lwkt_port_t port);

#endif

#endif
