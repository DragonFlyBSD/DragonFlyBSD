/*
 * SYS/MSGPORT2.H
 *
 *	Implements Inlines for LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport2.h,v 1.4 2003/11/20 06:05:31 dillon Exp $
 */

#ifndef _SYS_MSGPORT2_H_
#define _SYS_MSGPORT2_H_

static __inline
void
lwkt_initmsg(lwkt_msg_t msg, lwkt_port_t rport, int cmd)
{
    msg->ms_cmd = cmd;
    msg->ms_flags = MSGF_DONE;
    msg->ms_reply_port = rport;
    msg->ms_msgsize = 0;
    msg->ms_maxsize = 0;
}

static __inline
void
lwkt_reinitmsg(lwkt_msg_t msg, lwkt_port_t rport)
{
    msg->ms_flags = (msg->ms_flags & MSGF_ASYNC) | MSGF_DONE;
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
lwkt_forwardmsg(lwkt_port_t port, lwkt_msg_t msg)
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
lwkt_abortmsg(lwkt_msg_t msg)
{
    lwkt_port_t port = msg->ms_target_port;
    port->mp_abortport(port, msg);
}

static __inline
void
lwkt_replymsg(lwkt_msg_t msg, int error)
{
    lwkt_port_t port = msg->ms_reply_port;
    msg->ms_error = error;
    port->mp_replyport(port, msg);
}

#endif

