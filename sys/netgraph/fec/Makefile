# $FreeBSD: src/sys/modules/netgraph/fec/Makefile,v 1.1.2.1 2002/11/01 21:39:31 julian Exp $
# $DragonFly: src/sys/netgraph/fec/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

KMOD=		ng_fec
SRCS= 		ng_fec.c opt_inet.h opt_inet6.h

KCFLAGS +=	-DINET -DINET6

.include <bsd.kmod.mk>
