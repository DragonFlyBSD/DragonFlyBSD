/*
 * SYS/MSGPORT.H
 *
 *	Implements LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport.h,v 1.14 2004/03/06 19:40:32 dillon Exp $
 */

#ifndef _SYS_MSGPORT_H_
#define _SYS_MSGPORT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>		/* TAILQ_* macros */
#endif
#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>
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
	struct lwkt_msg *ms_next;	/* chaining / cache */
	union sysunion	*ms_sysunnext;	/* chaining / cache */
	struct lwkt_msg	*ms_umsg;	/* user message (UVA address) */
    } opaque;
    lwkt_port_t ms_target_port;		/* current target or relay port */
    lwkt_port_t	ms_reply_port;		/* asynch replies returned here */
    int		ms_unused1;
    int		ms_cmd;			/* message command */
    int		ms_flags;		/* message flags */
#define ms_copyout_start	ms_msgsize
    int		ms_msgsize;		/* size of message */
    int		ms_error;		/* positive error code or 0 */
    union {
	void	*ms_resultp;		/* misc pointer data or result */
	int	ms_result;		/* standard 'int'eger result */
	long	ms_lresult;		/* long result */
	int	ms_fds[2];		/* two int bit results */
	__int32_t ms_result32;		/* 32 bit result */
	__int64_t ms_result64;		/* 64 bit result */
	__off_t	ms_offset;		/* off_t result */
    } u;
#define ms_copyout_end	ms_pad[0]
    int		ms_pad[2];		/* future use */
} lwkt_msg;

#define ms_copyout_size	(offsetof(struct lwkt_msg, ms_copyout_end) - offsetof(struct lwkt_msg, ms_copyout_start))

#define MSGF_DONE	0x0001		/* asynch message is complete */
#define MSGF_REPLY	0x0002		/* asynch message has been returned */
#define MSGF_QUEUED	0x0004		/* message has been queued sanitychk */
#define MSGF_ASYNC	0x0008		/* sync/async hint */
#define MSGF_ABORTED	0x0010		/* message was aborted flag */

#define MSG_CMD_CDEV	0x00010000
#define MSG_CMD_VFS	0x00020000
#define MSG_CMD_SYSCALL	0x00030000
#define MSG_CMD_NETMSG	0x00040000
#define MSG_SUBCMD_MASK	0x0000FFFF

#ifdef _KERNEL
#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_LWKTMSG);
#endif
#endif

typedef struct lwkt_port {
    lwkt_msg_queue	mp_msgq;
    int			mp_flags;
    int			mp_refs;	/* references to port structure */
    struct thread	*mp_td;
    int			(*mp_putport)(lwkt_port_t, lwkt_msg_t);
    void *		(*mp_waitport)(lwkt_port_t, lwkt_msg_t);
    void		(*mp_replyport)(lwkt_port_t, lwkt_msg_t);
    void		(*mp_abortport)(lwkt_port_t, lwkt_msg_t);
} lwkt_port;

#define MSGPORTF_WAITING	0x0001

/*
 * These functions are good for userland as well as the kernel.  The 
 * messaging function support for userland is provided by the kernel's
 * kern/lwkt_msgport.c.  The port functions are provided by userland.
 */
extern void lwkt_initport(lwkt_port_t, struct thread *);
extern void lwkt_sendmsg(lwkt_port_t, lwkt_msg_t);
extern int lwkt_domsg(lwkt_port_t, lwkt_msg_t);
extern void *lwkt_getport(lwkt_port_t);

#endif
