# $FreeBSD: src/sys/modules/snc/Makefile,v 1.1.2.1 2000/10/21 03:30:04 nyan Exp $
# $DragonFly: src/sys/dev/netif/snc/Attic/Makefile,v 1.4 2005/02/18 11:41:42 corecode Exp $

.PATH:	${.CURDIR}/../../dev/snc
KMOD	= if_snc
SRCS	= if_snc.c if_snc_cbus.c if_snc_pccard.c dp83932.c dp83932subr.c
SRCS	+= opt_inet.h opt_bdg.h device_if.h bus_if.h isa_if.h card_if.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
