# $FreeBSD: src/sys/modules/if_disc/Makefile,v 1.12 1999/12/07 17:39:01 shin Exp $
# $DragonFly: src/sys/net/disc/Makefile,v 1.2 2003/06/17 04:28:44 dillon Exp $

.PATH:  ${.CURDIR}/../../net
KMOD=   if_disc
SRCS=   if_disc.c opt_inet.h opt_inet6.h
NOMAN=

NBPF?=	1

CFLAGS+= ${PROTOS}

opt_inet.h:
	echo "#define INET 1" > opt_inet.h

#opt_inet6.h:
#	echo "#define INET6 1" > opt_inet6.h

.include <bsd.kmod.mk>
