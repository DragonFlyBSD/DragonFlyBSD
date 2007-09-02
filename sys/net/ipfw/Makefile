# $FreeBSD: src/sys/modules/ipfw/Makefile,v 1.11.2.1 2003/02/14 14:09:21 maxim Exp $
# $DragonFly: src/sys/net/ipfw/Makefile,v 1.3 2007/09/02 13:27:23 sephe Exp $

.PATH:	${.CURDIR}/../../netinet
KMOD=	ipfw
NOMAN=
CFLAGS+= -DIPFIREWALL
SRCS=	ip_fw2.c
#
#If you want it verbose
#CFLAGS+= -DIPFIREWALL_VERBOSE
#CFLAGS+= -DIPFIREWALL_VERBOSE_LIMIT=100
#
#If you want it to pass all packets by default
#CFLAGS+= -DIPFIREWALL_DEFAULT_TO_ACCEPT
#

.include <bsd.kmod.mk>
