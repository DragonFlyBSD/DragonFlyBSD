# $FreeBSD: src/usr.bin/dnsquery/Makefile,v 1.7 1999/11/30 06:25:18 peter Exp $
# $DragonFly: src/usr.bin/dnsquery/Attic/Makefile,v 1.2 2003/06/17 04:29:25 dillon Exp $

USE_LIBBIND=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/dnsquery
.PATH:	${BIND_DIR}/doc/man

PROG=	dnsquery

.include <bsd.prog.mk>
