# $DragonFly: src/usr.sbin/dhcp/server/Attic/Makefile,v 1.1 2005/01/04 19:58:54 joerg Exp $

.include "../Makefile.common"

PROG=	dhcpd
MAN=	dhcpd.8 dhcpd.conf.5 dhcpd.leases.5
SRCS=	bootp.c class.c confpars.c db.c ddns.c dhcp.c dhcpd.c failover.c \
	mdb.c omapi.c salloc.c stables.c

LDADD=	${LIBDHCP} ${LIBRES} ${LIBOMAPI} ${LIBDST}

.include <bsd.prog.mk>
