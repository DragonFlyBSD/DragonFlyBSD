# $FreeBSD: src/usr.bin/lex/lib/Makefile,v 1.7 1999/08/28 01:02:49 peter Exp $
# $DragonFly: src/usr.bin/lex/lib/Makefile,v 1.3 2005/07/30 13:59:45 joerg Exp $

LIB=    ln
SRCS=   libmain.c libyywrap.c
NOPIC=	yes

LINKS=  ${LIBDIR}/libln.a ${LIBDIR}/libl.a
LINKS+=	${LIBDIR}/libln.a ${LIBDIR}/libfl.a

.if !defined(NOPROFILE)
LINKS+= ${PROFLIBDIR}/libln.a ${PROFLIBDIR}/libl.a
LINKS+=	${PROFLIBDIR}/libln.a ${PROFLIBDIR}/libfl.a
.endif

.include <bsd.lib.mk>

