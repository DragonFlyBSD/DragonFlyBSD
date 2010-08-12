# $NetBSD: Makefile,v 1.4 2007/05/28 12:06:34 tls Exp $
# $DragonFly: src/usr.sbin/bthcid/Makefile,v 1.1 2008/01/30 14:10:19 hasso Exp $

PROG=		bthcid
MAN=		bthcid.8 bthcid.conf.5
SRCS=		bthcid.c hci.c client.c config.c lexer.l parser.y

DPADD+=		${LIBBLUETOOTH} ${LIBUTIL}
LDADD+=		-lbluetooth -lutil
CFLAGS+=	-I${.CURDIR}/../../sys -I${.CURDIR}

.include <bsd.prog.mk>
