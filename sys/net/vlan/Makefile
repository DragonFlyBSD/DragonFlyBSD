# $FreeBSD: src/sys/modules/if_vlan/Makefile,v 1.1.2.1 2001/12/04 20:09:32 brooks Exp $

KMOD=	if_vlan
SRCS=	if_vlan.c
SRCS+=	opt_inet.h
SRCS+=	device_if.h bus_if.h
KCFLAGS+= -DNVLAN=0

.if !defined(KERNBUILDDIR)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
