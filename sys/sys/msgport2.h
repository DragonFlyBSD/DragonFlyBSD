/*
 * SYS/MSGPORT2.H
 *
 *	Implements Inlines for LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport2.h,v 1.10 2004/06/04 20:35:39 dillon Exp $
 */

#ifndef _SYS_MSGPORT2_H_
#define _SYS_MSGPORT2_H_

#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif

#define lwkt_cmd_op_none	lwkt_cmd_op(0)

typedef int (*lwkt_cmd_func_t)(lwkt_msg_t);

/*
 * Initialize a LWKT message structure.  Note that if the message supports
 * an abort MSGF_ABORTABLE must be passed in flags and an abort command
 * supplied.  If abort is not supported then lwkt_cmd_op_none is passed as
 * the abort command argument by convention.
 *
 * Note that other areas of the LWKT msg may already be initialized, so we
 * do not zero the message here.
 */
static __inline
void
lwkt_initmsg(lwkt_msg_t msg, lwkt_port_t rport, int flags, 
		lwkt_cmd_t cmd, lwkt_cmd_t abort)
{
    msg->ms_cmd = cmd;		/* opaque */
    if (flags & MSGF_ABORTABLE)	/* constant optimized conditional */
	msg->ms_abort = abort;	/* opaque */
    msg->ms_flags = MSGF_DONE | flags;
    msg->ms_reply_port = rport;
    msg->ms_msgsize = 0;
}

/*
 * These inlines convert specific types to the lwkt_cmd_t type.  The compiler
 * should be able to optimize this whole mess out.
 */
static __inline
lwkt_cmd_t
lwkt_cmd_op(int op)
{
    lwkt_cmd_t cmd;

    cmd.cm_op = op;
    return(cmd);
}

static __inline
lwkt_cmd_t
lwkt_cmd_func(int (*func)(lwkt_msg_t))
{
    lwkt_cmd_t cmd;

    cmd.cm_func = func;
    return(cmd);
}

static __inline
void
lwkt_initmsg_simple(lwkt_msg_t msg, int op)
{
    lwkt_initmsg(msg, &curthread->td_msgport, 0,
	lwkt_cmd_op(op), lwkt_cmd_op(0));
}

static __inline
void
lwkt_reinitmsg(lwkt_msg_t msg, lwkt_port_t rport)
{
    msg->ms_flags = (msg->ms_flags & (MSGF_ASYNC | MSGF_ABORTABLE)) | MSGF_DONE;
    msg->ms_reply_port = rport;
}

static __inline
int
lwkt_beginmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    return(port->mp_putport(port, msg));
}

static __inline
int
lwkt_waitmsg(lwkt_msg_t msg)
{
    lwkt_port_t port = msg->ms_reply_port;
    return(((lwkt_msg_t)port->mp_waitport(port, msg))->ms_error);
}

static __inline
void
lwkt_replymsg(lwkt_msg_t msg, int error)
{   
    lwkt_port_t port;

    msg->ms_error = error;
    port = msg->ms_reply_port;
    port->mp_replyport(port, msg);
}

static __inline
void *
lwkt_waitport(lwkt_port_t port, lwkt_msg_t msg)
{
    return(port->mp_waitport(port, msg));
}

static __inline
int
lwkt_checkmsg(lwkt_msg_t msg)
{
    return(msg->ms_flags & MSGF_DONE);
}

#endif

