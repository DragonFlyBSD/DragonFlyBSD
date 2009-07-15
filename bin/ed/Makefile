# $FreeBSD: src/bin/ed/Makefile,v 1.18.2.1 2001/12/13 09:58:12 ru Exp $
# $DragonFly: src/bin/ed/Makefile,v 1.10 2008/11/03 00:25:44 pavalos Exp $

PROG=	ed
SRCS=	buf.c cbc.c glbl.c io.c main.c re.c sub.c undo.c

LINKS=	${BINDIR}/ed ${BINDIR}/red
MLINKS=	ed.1 red.1
WARNS?=	2

.if exists(${.CURDIR}/../../secure) && !defined(NO_CRYPT)
CFLAGS+=-DDES
DPADD=	${LIBCRYPTO}
LDADD=	-lcrypto
.endif

.include <bsd.prog.mk>
