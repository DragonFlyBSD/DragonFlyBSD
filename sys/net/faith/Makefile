# $FreeBSD: src/sys/modules/if_faith/Makefile,v 1.1.2.1 2001/12/15 01:07:54 brooks Exp $
# $DragonFly: src/sys/net/faith/Makefile,v 1.3 2005/02/18 11:41:42 corecode Exp $

.PATH: ${.CURDIR}/../../net

KMOD=	if_faith
SRCS=	if_faith.c opt_inet.h opt_inet6.h
CFLAGS+= -DNFAITH=0
NOMAN=

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
