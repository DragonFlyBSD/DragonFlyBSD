/*
 * SYS/MSGPORT2.H
 *
 *	Implements Inlines for LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport2.h,v 1.1 2003/07/20 01:37:22 dillon Exp $
 */

#ifndef _SYS_MSGPORT2_H_
#define _SYS_MSGPORT2_H_

#ifdef _KERNEL

static __inline
int
lwkt_beginmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    return(port->mp_beginmsg(port, msg));
}

static __inline
int
lwkt_forwardmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    return(port->mp_beginmsg(port, msg));
}

static __inline
void
lwkt_abortmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    port->mp_abortmsg(port, msg);
}

static __inline
void
lwkt_replymsg(lwkt_msg_t msg, int error)
{
    lwkt_port_t port = msg->ms_reply_port;
    msg->ms_error = error;
    port->mp_returnmsg(port, msg);
}

#endif

#endif
