# $FreeBSD: src/usr.bin/host/Makefile,v 1.7 1999/11/30 06:25:19 peter Exp $
# $DragonFly: src/usr.bin/host/Makefile,v 1.2 2003/06/17 04:29:27 dillon Exp $

USE_LIBBIND=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/host
.PATH:	${BIND_DIR}/doc/man

PROG=	host

.include <bsd.prog.mk>
