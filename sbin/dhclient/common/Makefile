# common
# $FreeBSD: src/sbin/dhclient/common/Makefile,v 1.2.2.2 2002/07/19 18:46:27 ru Exp $
# $DragonFly: src/sbin/dhclient/common/Attic/Makefile,v 1.3 2005/01/04 19:58:54 joerg Exp $

.include "${.CURDIR}/../Makefile.common"

LIB=	dhcp
INTERNALLIB=		true

.if defined(BUILDING_SERVER)	# only install for dhclient
NOMAN=	true
.endif

SRCS=	alloc.c bpf.c comapi.c conflex.c ctrace.c discover.c dispatch.c \
	dlpi.c dns.c ethernet.c execute.c fddi.c icmp.c inet.c \
	lpf.c memory.c nit.c options.c packet.c parse.c print.c raw.c \
	resolv.c socket.c tables.c tr.c tree.c upf.c

PATCHES=dhcp-options.5.patch discover.c.patch dispatch.c.patch parse.c.patch

MAN=	dhcp-eval.5 dhcp-options.5

.include <bsd.lib.mk>
