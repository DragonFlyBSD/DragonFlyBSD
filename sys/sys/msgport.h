/*
 * SYS/MSGPORT.H
 *
 *	Implements LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport.h,v 1.16 2004/04/15 00:50:05 dillon Exp $
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
 * For the most part a message may only be manipulated by whomever currently
 * owns it, which generally means the originating port if the message has
 * not been sent yet or has been replied, and the target port if the message
 * has been sent and/or is undergoing processing.
 *
 * The one exception to this rule is an abort.  Aborts must be initiated
 * by the originator and may 'chase' the target (especially if a message
 * is being forwarded), potentially even 'chase' the message all the way
 * back to the originator if it races against the target replying the
 * message.  The ms_abort_port field is the only field that may be modified
 * by the originator or intermediate target (when the abort is chasing
 * a forwarding or reply op).  An abort may cause a reply to be delayed
 * until the abort catches up to it.
 *
 * Finally, note that an abort can requeue a message to its current target
 * port after the message has been pulled off of it, so you CANNOT use
 * ms_node for your own purposes after you have pulled a message request
 * off its port.
 *
 * NOTE! 64-bit-align this structure.
 */
typedef struct lwkt_msg {
    TAILQ_ENTRY(lwkt_msg) ms_node;	/* link node (see note above) */
    union {
	struct lwkt_msg *ms_next;	/* chaining / cache */
	union sysunion	*ms_sysunnext;	/* chaining / cache */
	struct lwkt_msg	*ms_umsg;	/* user message (UVA address) */
    } opaque;
    lwkt_port_t ms_target_port;		/* current target or relay port */
    lwkt_port_t	ms_reply_port;		/* async replies returned here */
    lwkt_port_t ms_abort_port;		/* abort chasing port */
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
#define MSGF_REPLY1	0x0002		/* asynch message has been returned */
#define MSGF_QUEUED	0x0004		/* message has been queued sanitychk */
#define MSGF_ASYNC	0x0008		/* sync/async hint */
#define MSGF_ABORTED	0x0010		/* message was aborted flag */
#define MSGF_PCATCH	0x0020		/* catch proc signal while waiting */
#define MSGF_REPLY2	0x0040		/* reply processed by rport cpu */

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
void lwkt_initport(lwkt_port_t, struct thread *);
void lwkt_sendmsg(lwkt_port_t, lwkt_msg_t);
int lwkt_domsg(lwkt_port_t, lwkt_msg_t);
int lwkt_forwardmsg(lwkt_port_t, lwkt_msg_t);
void lwkt_abortmsg(lwkt_msg_t);
void *lwkt_getport(lwkt_port_t);

int lwkt_default_putport(lwkt_port_t port, lwkt_msg_t msg);
void *lwkt_default_waitport(lwkt_port_t port, lwkt_msg_t msg);
void lwkt_default_replyport(lwkt_port_t port, lwkt_msg_t msg);
void lwkt_default_abortport(lwkt_port_t port, lwkt_msg_t msg);

#endif
