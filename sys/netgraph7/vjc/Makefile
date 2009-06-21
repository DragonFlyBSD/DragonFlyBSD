# $FreeBSD: src/sys/modules/netgraph/vjc/Makefile,v 1.2.2.3 2001/12/21 09:00:50 ru Exp $
# $DragonFly: src/sys/netgraph/vjc/Makefile,v 1.3 2003/08/14 23:26:45 dillon Exp $
# $Whistle: Makefile,v 1.1 1999/01/24 06:48:07 archie Exp $

KMOD=		ng_vjc
SRCS= 		ng_vjc.c slcompress.c
KMODDEPS=	netgraph

.PATH:		${.CURDIR}/../../net/ppp_layer

.include <bsd.kmod.mk>
