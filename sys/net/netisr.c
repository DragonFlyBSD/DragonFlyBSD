/*-
 * Copyright (c) 2003 Jeffrey Hsu
 * Copyright (c) 2003 Jonathan Lemon
 * Copyright (c) 2003 Matthew Dillon
 *
 * $DragonFly: src/sys/net/netisr.c,v 1.17 2004/06/07 07:01:36 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/msgport.h>
#include <sys/proc.h>
#include <sys/interrupt.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <machine/cpufunc.h>
#include <machine/ipl.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

static struct netisr netisrs[NETISR_MAX];

/* Per-CPU thread to handle any protocol.  */
struct thread netisr_cpu[MAXCPU];
lwkt_port netisr_afree_rport;
lwkt_port netisr_sync_port;

/*
 * netisr_afree_rport replymsg function, only used to handle async
 * messages which the sender has abandoned to their fate.
 */
static void
netisr_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
    free(msg, M_LWKTMSG);
}

/*
 * We must construct a custom putport function (which runs in the context
 * of the message originator)
 *
 * Our custom putport must check for self-referential messages, which can
 * occur when the so_upcall routine is called (e.g. nfs).  Self referential
 * messages are executed synchronously.  However, we must panic if the message
 * is not marked DONE on completion because the self-referential case cannot
 * block without deadlocking.
 *
 * note: ms_target_port does not need to be set when returning a synchronous
 * error code.
 */
int
netmsg_put_port(lwkt_port_t port, lwkt_msg_t lmsg)
{
    int error;

    if ((lmsg->ms_flags & MSGF_ASYNC) == 0 && port->mp_td == curthread) {
	error = lmsg->ms_cmd.cm_func(lmsg);
	if (error == EASYNC && (lmsg->ms_flags & MSGF_DONE) == 0)
	    panic("netmsg_put_port: self-referential deadlock on netport");
	return(error);
    } else {
	return(lwkt_default_putport(port, lmsg));
    }
}

/*
 * UNIX DOMAIN sockets still have to run their uipc functions synchronously,
 * because they depend on the user proc context for a number of things 
 * (like creds) which we have not yet incorporated into the message structure.
 *
 * However, we maintain or message/port abstraction.  Having a special 
 * synchronous port which runs the commands synchronously gives us the
 * ability to serialize operations in one place later on when we start
 * removing the BGL.
 *
 * We clear MSGF_DONE prior to executing the message in order to close
 * any potential replymsg races with the flags field.  If a synchronous
 * result code is returned we set MSGF_DONE again.  MSGF_DONE's flag state
 * must be correct or the caller will be confused.
 */
static int
netmsg_sync_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
    int error;

    lmsg->ms_flags &= ~MSGF_DONE;
    lmsg->ms_target_port = port;	/* required for abort */
    error = lmsg->ms_cmd.cm_func(lmsg);
    if (error == EASYNC)
	error = lwkt_waitmsg(lmsg);
    else
	lmsg->ms_flags |= MSGF_DONE;
    return(error);
}

static void
netmsg_sync_abortport(lwkt_port_t port, lwkt_msg_t lmsg)
{
    lmsg->ms_abort_port = lmsg->ms_reply_port;
    lmsg->ms_flags |= MSGF_ABORTED;
    lmsg->ms_abort.cm_func(lmsg);
}

static void
netisr_init(void)
{
    int i;

    /*
     * Create default per-cpu threads for generic protocol handling.
     */
    for (i = 0; i < ncpus; ++i) {
	lwkt_create(netmsg_service_loop, NULL, NULL, &netisr_cpu[i], 0, i,
	    "netisr_cpu %d", i);
	netisr_cpu[i].td_msgport.mp_putport = netmsg_put_port;
    }

    /*
     * The netisr_afree_rport is a special reply port which automatically
     * frees the replied message.
     */
    lwkt_initport(&netisr_afree_rport, NULL);
    netisr_afree_rport.mp_replyport = netisr_autofree_reply;

    /*
     * The netisr_syncport is a special port which executes the message
     * synchronously and waits for it if EASYNC is returned.
     */
    lwkt_initport(&netisr_sync_port, NULL);
    netisr_sync_port.mp_putport = netmsg_sync_putport;
    netisr_sync_port.mp_abortport = netmsg_sync_abortport;
}

SYSINIT(netisr, SI_SUB_PROTO_BEGIN, SI_ORDER_FIRST, netisr_init, NULL);

void
netmsg_service_loop(void *arg)
{
    struct netmsg *msg;

    while ((msg = lwkt_waitport(&curthread->td_msgport, NULL)))
	msg->nm_lmsg.ms_cmd.cm_func(&msg->nm_lmsg);
}

/*
 * Call the netisr directly.
 * Queueing may be done in the msg port layer at its discretion.
 */
void
netisr_dispatch(int num, struct mbuf *m)
{
    /* just queue it for now XXX JH */
    netisr_queue(num, m);
}

/*
 * Same as netisr_dispatch(), but always queue.
 * This is either used in places where we are not confident that
 * direct dispatch is possible, or where queueing is required.
 */
int
netisr_queue(int num, struct mbuf *m)
{
    struct netisr *ni;
    struct netmsg_packet *pmsg;
    lwkt_port_t port;

    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("netisr_queue: bad isr %d", num));

    ni = &netisrs[num];
    if (ni->ni_handler == NULL) {
	printf("netisr_queue: unregistered isr %d\n", num);
	return (EIO);
    }

    if (!(port = ni->ni_mport(m)))
	return (EIO);

    /* use better message allocation system with limits later XXX JH */
    pmsg = malloc(sizeof(struct netmsg_packet), M_LWKTMSG, M_WAITOK);

    lwkt_initmsg(&pmsg->nm_lmsg, &netisr_afree_rport, 0,
		lwkt_cmd_func((void *)ni->ni_handler), lwkt_cmd_op_none);
    pmsg->nm_packet = m;
    lwkt_sendmsg(port, &pmsg->nm_lmsg);
    return (0);
}

void
netisr_register(int num, lwkt_portfn_t mportfn, netisr_fn_t handler)
{
    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("netisr_register: bad isr %d", num));

    netisrs[num].ni_mport = mportfn;
    netisrs[num].ni_handler = handler;
}

int
netisr_unregister(int num)
{
    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("unregister_netisr: bad isr number: %d\n", num));

    /* XXX JH */
    return (0);
}

/*
 * Return message port for default handler thread on CPU 0.
 */
lwkt_port_t
cpu0_portfn(struct mbuf *m)
{
    return (&netisr_cpu[0].td_msgport);
}

/* ARGSUSED */
lwkt_port_t
cpu0_soport(struct socket *so __unused, struct sockaddr *nam __unused,
	    int req __unused)
{
    return (&netisr_cpu[0].td_msgport);
}

lwkt_port_t
sync_soport(struct socket *so __unused, struct sockaddr *nam __unused,
	    int req __unused)
{
    return (&netisr_sync_port);
}

/*
 * This function is used to call the netisr handler from the appropriate
 * netisr thread for polling and other purposes.
 */
void
schednetisr(int num)
{
    struct netisr *ni = &netisrs[num];
    struct netmsg *pmsg;
    lwkt_port_t port = &netisr_cpu[0].td_msgport;

    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("schednetisr: bad isr %d", num));

    pmsg = malloc(sizeof(struct netmsg), M_LWKTMSG, M_NOWAIT);
    if (pmsg) {
	lwkt_initmsg(&pmsg->nm_lmsg, &netisr_afree_rport, 0,
		    lwkt_cmd_func((void *)ni->ni_handler), lwkt_cmd_op_none);
	lwkt_sendmsg(port, &pmsg->nm_lmsg);
    }
}
