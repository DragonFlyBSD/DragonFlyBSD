#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/whois/Makefile,v 1.3.2.2 2001/08/23 07:20:13 mike Exp $
# $DragonFly: src/usr.bin/whois/Makefile,v 1.3 2004/01/31 06:56:44 dillon Exp $

PROG=	whois
WARNS?=	2

.if defined(SOCKS)
CFLAGS+=-DSOCKS
CFLAGS+=-Dconnect=Rconnect -Dgetsockname=Rgetsockname -Dlisten=Rlisten \
	-Daccept=Raccept -Drcmd=Rrcmd -Dbind=Rbind -Dselect=Rselect
LDADD+=	-lsocks
.endif

.include <bsd.prog.mk>
