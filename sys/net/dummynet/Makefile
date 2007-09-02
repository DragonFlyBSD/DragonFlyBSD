# $FreeBSD: src/sys/modules/dummynet/Makefile,v 1.1.2.2 2003/04/08 10:18:00 maxim Exp $
# $DragonFly: src/sys/net/dummynet/Makefile,v 1.4 2007/09/02 13:27:23 sephe Exp $

.PATH:  ${.CURDIR}/../../netinet
KMOD=   dummynet
SRCS=   ip_dummynet.c
NOMAN=
KMODDEPS= ipfw

.include <bsd.kmod.mk>
