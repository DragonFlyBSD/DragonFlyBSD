# $FreeBSD: head/bin/ed/Makefile 235654 2012-05-19 17:55:49Z marcel $

PROG=	ed
SRCS=	buf.c cbc.c glbl.c io.c main.c re.c sub.c undo.c

LINKS=	${BINDIR}/ed ${BINDIR}/red
MLINKS=	ed.1 red.1

.if !defined(NO_CRYPT) && !defined(NO_OPENSSL)
CFLAGS+=-DDES
DPADD=	${LIBCRYPTO}
LDADD=	-lcrypto
.endif

.include <bsd.prog.mk>
