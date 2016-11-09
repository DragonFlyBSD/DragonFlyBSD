CONTRIBDIR=	${.CURDIR}/../../../contrib/flex
.PATH:		${CONTRIBDIR}

LIB=    l
SRCS=   libmain.c libyywrap.c
NOPIC=	yes

# Do not install these compat libraries during {build,quick}world.
.if !${INSTALL:M*/tools/install.sh}
LINKS=  ${LIBDIR}/libl.a ${LIBDIR}/libfl.a
LINKS+=	${LIBDIR}/libl.a ${LIBDIR}/libln.a

.if !defined(NOPROFILE)
LINKS+= ${PROFLIBDIR}/libl.a ${PROFLIBDIR}/libfl.a
LINKS+=	${PROFLIBDIR}/libl.a ${PROFLIBDIR}/libln.a
.endif
.endif

.include <bsd.lib.mk>
