# $FreeBSD: src/sys/modules/rl/Makefile,v 1.6 2000/01/28 11:26:34 bde Exp $
# $DragonFly: src/sys/dev/netif/ie/Makefile,v 1.1 2004/04/02 11:31:27 hmp Exp $

.PATH:  ${.CURDIR}/../../dev/netif/ie
KMOD= if_ie
SRCS= if_ie.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS+= use_ie.h opt_inet.h opt_ipx.h

use_ie.h:
	echo "#define NIE 1" > ${.TARGET}

opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_ipx.h:
	echo "" > ${.TARGET}

.include <bsd.kmod.mk>

