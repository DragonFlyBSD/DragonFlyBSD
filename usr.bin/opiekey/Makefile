# $FreeBSD: src/usr.bin/opiekey/Makefile,v 1.4.2.4 2002/07/15 14:17:08 des Exp $
# $DragonFly: src/usr.bin/opiekey/Makefile,v 1.2 2003/06/17 04:29:30 dillon Exp $
#
OPIE_DIST?=	${.CURDIR}/../../contrib/opie

PROG=	opiekey

CFLAGS+=-I${.CURDIR}/../../lib/libopie
CFLAGS+=-I${OPIE_DIST}
CFLAGS+=-DINSECURE_OVERRIDE

DPADD=	${LIBOPIE} ${LIBMD}
LDADD=	-lopie -lmd

LINKS=	${BINDIR}/opiekey ${BINDIR}/otp-md4
LINKS+=	${BINDIR}/opiekey ${BINDIR}/otp-md5
LINKS+=	${BINDIR}/opiekey ${BINDIR}/otp-sha

MLINKS=	opiekey.1 otp-md4.1 opiekey.1 otp-md5.1 opiekey.1 otp-sha.1

.PATH:	${OPIE_DIST}

.include <bsd.prog.mk>
