/*-
 * Copyright (c) 2003 Jeffrey Hsu
 * Copyright (c) 2003 Jonathan Lemon
 * Copyright (c) 2003 Matthew Dillon
 *
 * $DragonFly: src/sys/net/netisr.c,v 1.2 2003/09/15 23:38:13 hsu Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/interrupt.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <machine/cpufunc.h>
#include <machine/ipl.h>

int isrmask;
static int isrsoftint_installed;
static struct netisr netisrs[NETISR_MAX];

/* SYSCTL_NODE(_net, OID_AUTO, isr, CTLFLAG_RW, 0, "netisr counters"); */

static int netisr_directdispatch = 0;
/*
SYSCTL_INT(_net_isr, OID_AUTO, directdispatch, CTLFLAG_RW,
    &netisr_directdispatch, 0, "enable direct dispatch");
*/

static void
swi_net(void *arg)
{
    int mask;
    int bit;
	
    while ((mask = isrmask) != 0) {
	bit = bsfl(mask);
	if (btrl(&isrmask, bit)) {
	    struct netisr *ni = &netisrs[bit];
	    netisr_fn_t func = ni->ni_handler;

	    if (ni->ni_queue) {
		while (1) {
		    struct mbuf *m;
		    int s;

		    s = splimp();
		    IF_DEQUEUE(ni->ni_queue, m);
		    splx(s);
		    if (!m)
			break;
		    func(m);
		}
	    } else
	        func(NULL);
	}
    }
}

/*
 * Call the netisr directly instead of queueing the packet, if possible.
 */
void
netisr_dispatch(int num, struct mbuf *m)
{
    struct netisr *ni;

    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("bad isr %d", num));

    ni = &netisrs[num];

    if (!ni->ni_queue) {
	m_freem(m);
	return;
    }

    if (netisr_directdispatch) {
      /*
       * missing check for concurrent execution from swi_net() XXX JH
       * Address this after conversion to message ports.
       */
	ni->ni_handler(m);
    } else {
	if (IF_HANDOFF(ni->ni_queue, m, NULL))
	    schednetisr(num);
    }
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

    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("bad isr %d", num));

    ni = &netisrs[num];

    if (!ni->ni_queue) {
	m_freem(m);
	return (ENOBUFS);
    }

    if (!IF_HANDOFF(ni->ni_queue, m, NULL))
	return (ENOBUFS);

    schednetisr(num);
    return (0);
}

int
netisr_register(int num, netisr_fn_t handler, struct ifqueue *ifq)
{
    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("bad isr %d", num));

    if (isrsoftint_installed == 0) {
	isrsoftint_installed = 1;
	register_swi(SWI_NET, swi_net, NULL, "swi_net");
    }
    netisrs[num].ni_handler = handler;
    netisrs[num].ni_queue = ifq;
    return (0);
}

int
netisr_unregister(int num)
{
    KASSERT((num > 0 && num <= (sizeof(netisrs)/sizeof(netisrs[0]))),
	("unregister_netisr: bad isr number: %d\n", num));

    if (netisrs[num].ni_queue != NULL) {
	int s;

	s = splimp();
	IF_DRAIN(netisrs[num].ni_queue);
	splx(s);
    }
    netisrs[num].ni_handler = NULL;
    return (0);
}
