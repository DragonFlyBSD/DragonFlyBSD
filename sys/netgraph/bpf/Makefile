# $FreeBSD: src/sys/modules/netgraph/bpf/Makefile,v 1.2.4.3 2001/12/21 09:00:47 ru Exp $
# $DragonFly: src/sys/netgraph/bpf/Makefile,v 1.3 2003/08/14 23:26:36 dillon Exp $
# $Whistle: Makefile,v 1.1 1999/12/03 01:44:28 archie Exp $

KMOD=		ng_bpf
SRCS= 		ng_bpf.c bpf_filter.c
KMODDEPS=	netgraph

.PATH:		${.CURDIR}/../../net

.include <bsd.kmod.mk>
