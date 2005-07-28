# $FreeBSD: src/sys/modules/snc/Makefile,v 1.1.2.1 2000/10/21 03:30:04 nyan Exp $
# $DragonFly: src/sys/dev/netif/snc/Attic/Makefile,v 1.5 2005/07/28 00:06:10 joerg Exp $

KMOD	= if_snc
SRCS	= if_snc.c if_snc_cbus.c if_snc_pccard.c dp83932.c dp83932subr.c
SRCS	+= opt_inet.h opt_bdg.h device_if.h bus_if.h isa_if.h card_if.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
