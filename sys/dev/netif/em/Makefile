#$FreeBSD: src/sys/modules/em/Makefile,v 1.1.2.3 2002/06/18 21:00:56 pdeuskar Exp $
#$DragonFly: src/sys/dev/netif/em/Makefile,v 1.2 2003/06/17 04:28:43 dillon Exp $
.PATH:  ${.CURDIR}/../../dev/em
KMOD    = if_em
SRCS    = device_if.h bus_if.h pci_if.h opt_bdg.h
SRCS    += if_em.c if_em_hw.c

clean:
	rm -f opt_bdg.h device_if.h bus_if.h pci_if.h setdef*
	rm -f *.o *.kld *.ko
	rm -f @ machine

.include <bsd.kmod.mk>
