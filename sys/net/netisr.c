/*-
 * Copyright (c) 2003 Matthew Dillon
 *
 * $DragonFly: src/sys/net/netisr.c,v 1.1 2003/06/29 03:28:45 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/interrupt.h>
#include <net/netisr.h>
#include <machine/cpufunc.h>
#include <machine/ipl.h>

static int isrmask;
static int isrsoftint_installed;
static netisr_t *netisrs[NETISR_MAX];

static void
swi_net(void *arg)
{
    int mask;
    int bit;
    netisr_t *func;
	
    while ((mask = isrmask) != 0) {
	bit = bsfl(mask);
	if (btrl(&isrmask, bit)) {
	    if ((func = netisrs[bit]) != NULL)
		func();
	}
    }
}

int
register_netisr(int num, netisr_t *handler)
{
    if (num < 0 || num >= (sizeof(netisrs)/sizeof(*netisrs)) ) {
	printf("register_netisr: bad isr number: %d\n", num);
	return (EINVAL);
    }
    if (isrsoftint_installed == 0) {
	isrsoftint_installed = 1;
	register_swi(SWI_NET, swi_net, NULL, "swi_net");
    }
    netisrs[num] = handler;
    return (0);
}

int
unregister_netisr(int num)
{
    if (num < 0 || num >= (sizeof(netisrs)/sizeof(*netisrs)) ) {
	printf("unregister_netisr: bad isr number: %d\n", num);
	return (EINVAL);
    }
    netisrs[num] = NULL;
    return (0);
}

void
schednetisr(int isrnum) 
{
    atomic_set_int(&isrmask, 1 << isrnum);
    setsoftnet();
}

