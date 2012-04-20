# $FreeBSD: src/sys/modules/if_disc/Makefile,v 1.12 1999/12/07 17:39:01 shin Exp $

KMOD=   if_disc
SRCS=   if_disc.c opt_inet.h opt_inet6.h

NBPF?=	1

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

#opt_inet6.h:
#	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
