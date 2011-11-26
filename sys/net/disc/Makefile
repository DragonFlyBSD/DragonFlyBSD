# $FreeBSD: src/sys/modules/if_disc/Makefile,v 1.12 1999/12/07 17:39:01 shin Exp $
# $DragonFly: src/sys/net/disc/Makefile,v 1.3 2005/02/18 11:41:42 corecode Exp $

.PATH:  ${.CURDIR}/../../net
KMOD=   if_disc
SRCS=   if_disc.c opt_inet.h opt_inet6.h

NBPF?=	1

CFLAGS+= ${PROTOS}

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > opt_inet.h

#opt_inet6.h:
#	echo "#define INET6 1" > opt_inet6.h
.endif

.include <bsd.kmod.mk>
