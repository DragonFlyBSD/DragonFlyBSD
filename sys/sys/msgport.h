/*
 * SYS/MSGPORT.H
 *
 *	Implements LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport.h,v 1.1 2003/07/20 01:37:22 dillon Exp $
 */

#ifndef _SYS_MSGPORT_H_
#define _SYS_MSGPORT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>		/* TAILQ_* macros */
#endif

struct lwkt_msg;
struct lwkt_port;

typedef struct lwkt_msg		*lwkt_msg_t;
typedef struct lwkt_port	*lwkt_port_t;

/*
 * The standard message and port structure for communications between
 * threads.  See kern/lwkt_msgport.c for documentation on how messages and
 * ports work.
 */
typedef struct lwkt_msg {
    TAILQ_ENTRY(lwkt_msg) ms_node;	/* link node (not always used) */
    lwkt_port_t ms_target_port;		/* only used in certain situations */
    lwkt_port_t	ms_reply_port;		/* asynch replies returned here */
    int		ms_abortreq;		/* set asynchronously */
    int		ms_cmd;
    int		ms_flags;
    int		ms_error;
} lwkt_msg;

#define MSGF_DONE	0x0001		/* asynch message is complete */
#define MSGF_REPLY	0x0002		/* asynch message has been returned */
#define MSGF_QUEUED	0x0004		/* message has been queued sanitychk */
#define MSGF_ASYNC	0x0008		/* sync/async hint */

typedef struct lwkt_port {
    lwkt_msg_queue	mp_msgq;
    int			mp_flags;
    thread_t		mp_td;
    int			(*mp_beginmsg)(lwkt_port_t port, lwkt_msg_t msg);
    void		(*mp_abortmsg)(lwkt_port_t port, lwkt_msg_t msg);
    void		(*mp_returnmsg)(lwkt_port_t port, lwkt_msg_t msg);
} lwkt_port;

#define MSGPORTF_WAITING	0x0001

#ifdef _KERNEL

extern void lwkt_init_port(lwkt_port_t port, thread_t td);
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
