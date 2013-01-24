CONTRIBDIR=	${.CURDIR}/../../../contrib/flex
.PATH:		${CONTRIBDIR}

LIB=    fl
SRCS=   libmain.c libyywrap.c
NOPIC=	yes

LINKS=  ${LIBDIR}/libfl.a ${LIBDIR}/libl.a
LINKS+=	${LIBDIR}/libfl.a ${LIBDIR}/libln.a

.if !defined(NOPROFILE)
LINKS+= ${PROFLIBDIR}/libfl.a ${PROFLIBDIR}/libl.a
LINKS+=	${PROFLIBDIR}/libfl.a ${PROFLIBDIR}/libln.a
.endif

.include <bsd.lib.mk>
