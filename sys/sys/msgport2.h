/*
 * SYS/MSGPORT2.H
 *
 *	Implements Inlines for LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport2.h,v 1.13 2007/05/23 08:56:59 dillon Exp $
 */

#ifndef _SYS_MSGPORT2_H_
#define _SYS_MSGPORT2_H_

#ifndef _KERNEL

#error "This file should not be included by userland programs."

#else

#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif

/*
 * Initialize a LWKT message structure.  Note that if the message supports
 * an abort MSGF_ABORTABLE must be passed in flags.
 *
 * Note that other areas of the LWKT msg may already be initialized, so we
 * do not zero the message here.
 *
 * Messages are marked as DONE until sent.
 */
static __inline
void
lwkt_initmsg(lwkt_msg_t msg, lwkt_port_t rport, int flags)
{
    msg->ms_flags = MSGF_DONE | flags;
    msg->ms_reply_port = rport;
}

static __inline
void
lwkt_initmsg_abortable(lwkt_msg_t msg, lwkt_port_t rport, int flags,
		       void (*abortfn)(lwkt_msg_t))
{
    lwkt_initmsg(msg, rport, flags | MSGF_ABORTABLE);
    msg->ms_abortfn = abortfn;
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

#endif	/* _KERNEL */
#endif	/* _SYS_MSGPORT2_H_ */
