# $DragonFly: src/gnu/lib/gcc34/Makefile,v 1.1 2004/06/14 22:27:52 joerg Exp $

SUBDIR=		libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := CCVER=gcc3 ${MAKE}
.endif

SUBDIR+=	libobjc
SUBDIR+=	libstdc++ libsupc++

.include <bsd.subdir.mk>
