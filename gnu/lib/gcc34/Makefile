# $DragonFly: src/gnu/lib/gcc34/Makefile,v 1.3 2004/12/21 13:10:47 joerg Exp $

SUBDIR=		libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc34 ${MAKE}
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.if !defined(NO_CXX)
SUBDIR+=	libstdc++ libsupc++
.endif

.include <bsd.subdir.mk>
