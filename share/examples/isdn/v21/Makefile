# $FreeBSD: src/share/examples/isdn/v21/Makefile,v 1.1.2.1 2001/08/10 14:59:48 obrien Exp $
# $DragonFly: src/share/examples/isdn/v21/Makefile,v 1.2 2003/06/17 04:36:57 dillon Exp $

PROG=		v21modem
NOMAN=
LDADD=		-lutil -lm
WARNS?=		3

.if !defined(BSDTYPE)
BSDTYPE!=uname
.endif

.include <bsd.prog.mk>
