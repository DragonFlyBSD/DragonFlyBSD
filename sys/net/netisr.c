/*-
 * Copyright (c) 2003 Jeffrey Hsu
 * Copyright (c) 2003 Jonathan Lemon
 * Copyright (c) 2003 Matthew Dillon
 *
 * $DragonFly: src/sys/net/netisr.c,v 1.6 2003/11/20 06:05:31 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/proc.h>
#include <sys/interrupt.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <machine/cpufunc.h>
#include <machine/ipl.h>

struct netmsg {
    struct lwkt_msg	nm_lmsg;
    struct mbuf		*nm_packet;
    netisr_fn_t		nm_handler;
};

#define CMD_NETMSG_NEWPKT	(MSG_CMD_NETMSG | 0x0001)
#define CMD_NETMSG_POLL		(MSG_CMD_NETMSG | 0x0002)

static struct netisr netisrs[NETISR_MAX];

/* Per-CPU thread to handle any protocol.  */
struct thread netisr_cpu[MAXCPU];

static void
netisr_init(void)
{
    int i;

    /* Create default per-cpu threads for generic protocol handling. */
    for (i = 0; i < ncpus; ++i)
	lwkt_create(netmsg_service_loop, NULL, NULL, &netisr_cpu[i], 0, i,
	    "netisr_cpu %d", i);
}

SYSINIT(netisr, SI_SUB_PROTO_BEGIN, SI_ORDER_FIRST, netisr_init, NULL);

void
netmsg_service_loop(void *arg)
{
    struct netmsg *msg;

    while ((msg = lwkt_waitport(&curthread->td_msgport, NULL))) {
	struct mbuf *m = msg->nm_packet;
	netisr_fn_t handler = msg->nm_handler;

	if (handler) {
		handler(m);
	} else if (m) {
		while (m->m_type == MT_TAG)
			m = m->m_next;
		KKASSERT(m != NULL);
		m_freem(m);
	}
	free(msg, M_TEMP);
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
    struct netmsg *pmsg;
    lwkt_port_t port;

    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("bad isr %d", num));

    ni = &netisrs[num];
    if (ni->ni_handler == NULL) {
	printf("netisr_queue: unregistered isr %d\n", num);
	return EIO;
    }

    /* use better message allocation system with limits later XXX JH */
    if (!(pmsg = malloc(sizeof(struct netmsg), M_TEMP, M_NOWAIT)))
	return ENOBUFS;

    if (!(port = ni->ni_mport(m)))
	return EIO;

    lwkt_initmsg(&pmsg->nm_lmsg, port, CMD_NETMSG_NEWPKT);
    pmsg->nm_packet = m;
    pmsg->nm_handler = ni->ni_handler;
    lwkt_sendmsg(port, &pmsg->nm_lmsg);
    return (0);
}

void
netisr_register(int num, lwkt_portfn_t mportfn, netisr_fn_t handler)
{
    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("bad isr %d", num));

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

/*
 * This function is used to call the netisr handler from the appropriate
 * netisr thread for polling and other purposes.  pmsg->nm_packet will be
 * undefined.  At the moment operation is restricted to non-packet ISRs only.
 */
void
schednetisr(int num)
{
    struct netisr *ni = &netisrs[num];
    struct netmsg *pmsg;
    lwkt_port_t port = &netisr_cpu[0].td_msgport;

    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("bad isr %d", num));

    if (!(pmsg = malloc(sizeof(struct netmsg), M_TEMP, M_NOWAIT)))
	return;

    lwkt_initmsg(&pmsg->nm_lmsg, port, CMD_NETMSG_POLL);
    pmsg->nm_handler = ni->ni_handler;
    lwkt_sendmsg(port, &pmsg->nm_lmsg);
}
