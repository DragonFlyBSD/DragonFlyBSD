# $FreeBSD: src/sys/modules/ubsec/Makefile,v 1.2.2.2 2003/06/05 04:13:48 sam Exp $
# $DragonFly: src/sys/dev/crypto/ubsec/Makefile,v 1.4 2006/06/25 11:02:38 corecode Exp $

.PATH:	${.CURDIR}/../../dev/ubsec
KMOD	= ubsec
SRCS	= ubsec.c opt_ubsec.h
SRCS   += device_if.h bus_if.h pci_if.h
SRCS   += opt_bus.h crypto_if.h

.if !defined(BUILDING_WITH_KERNEL)
opt_ubsec.h:
	echo "#define UBSEC_DEBUG 1" > opt_ubsec.h
.endif

.include <bsd.kmod.mk>
