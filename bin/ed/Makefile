# $FreeBSD: src/bin/ed/Makefile,v 1.18.2.1 2001/12/13 09:58:12 ru Exp $
# $DragonFly: src/bin/ed/Makefile,v 1.3 2003/08/03 16:45:14 dillon Exp $

PROG=	ed
SRCS=	buf.c cbc.c glbl.c io.c main.c re.c sub.c undo.c
LINKS=	${BINDIR}/ed ${BINDIR}/red
MLINKS=	ed.1 red.1

.if exists(${.CURDIR}/../../secure) && !defined(NOCRYPT)
DISTRIBUTION=crypto
CFLAGS+=-DDES
DPADD=	${LIBCIPHER}
LDADD=	-lcipher
.endif

.include <bsd.prog.mk>
