/*-
 * Copyright (c) 2003 Jeffrey Hsu
 * Copyright (c) 2003 Jonathan Lemon
 * Copyright (c) 2003 Matthew Dillon
 *
 * $DragonFly: src/sys/net/netisr.c,v 1.10 2004/04/05 18:53:03 dillon Exp $
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

/*
 * netisr_afree_rport replymsg function, only used to handle async
 * messages which the sender has abandoned to their fate.
 */
static void
netisr_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
    free(msg, M_LWKTMSG);
}

static void
netisr_init(void)
{
    int i;

    /* Create default per-cpu threads for generic protocol handling. */
    for (i = 0; i < ncpus; ++i) {
	lwkt_create(netmsg_service_loop, NULL, NULL, &netisr_cpu[i], 0, i,
	    "netisr_cpu %d", i);
    }
    lwkt_initport(&netisr_afree_rport, NULL);
    netisr_afree_rport.mp_replyport = netisr_autofree_reply;
}

SYSINIT(netisr, SI_SUB_PROTO_BEGIN, SI_ORDER_FIRST, netisr_init, NULL);

/*
 * We must construct a custom putport function (which runs in the context
 * of the message originator)
 * Our custom putport must check for self-referential messages, which can
 * occur when the so_upcall routine is called (e.g. nfs).  Self referential
 * messages are simply executed synchronously.
 */
static int
netmsg_put_port(lwkt_port_t port, lwkt_msg_t lmsg)
{
    if (port->mp_td == curthread) {
	struct netmsg *msg = (void *)lmsg;
	return(msg->nm_handler(msg));
    }
    return(lwkt_default_putport(port, lmsg));
}

void
netmsg_service_loop(void *arg)
{
    int error;
    thread_t td;
    struct netmsg *msg;

    td = curthread;
    td->td_msgport.mp_putport = netmsg_put_port;

    while ((msg = lwkt_waitport(&td->td_msgport, NULL))) {
	error = msg->nm_handler(msg);
	lwkt_replymsg(&msg->nm_lmsg, error);
    }
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
    if (!(pmsg = malloc(sizeof(struct netmsg_packet), M_LWKTMSG, M_NOWAIT)))
	return (ENOBUFS);

    lwkt_initmsg_rp(&pmsg->nm_lmsg, &netisr_afree_rport, CMD_NETMSG_NEWPKT);
    pmsg->nm_packet = m;
    pmsg->nm_handler = ni->ni_handler;
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
cpu0_soport(struct socket *so __unused, struct sockaddr *nam __unused)
{
    return (&netisr_cpu[0].td_msgport);
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

    if (!(pmsg = malloc(sizeof(struct netmsg), M_LWKTMSG, M_NOWAIT)))
	return;

    lwkt_initmsg_rp(&pmsg->nm_lmsg, &netisr_afree_rport, CMD_NETMSG_POLL);
    pmsg->nm_handler = ni->ni_handler;
    lwkt_sendmsg(port, &pmsg->nm_lmsg);
}
