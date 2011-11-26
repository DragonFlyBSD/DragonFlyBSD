# $FreeBSD: src/share/examples/isdn/v21/Makefile,v 1.1.2.1 2001/08/10 14:59:48 obrien Exp $

PROG=		v21modem
NOMAN=
LDADD=		-lutil -lm
DPADD=		${LIBUTIL} ${LIBM}
WARNS?=		3

.if !defined(BSDTYPE)
BSDTYPE!=uname
.endif

.include <bsd.prog.mk>
