# $DragonFly: src/gnu/lib/gcc34/Makefile,v 1.7 2008/05/14 15:02:47 hasso Exp $

SUBDIR=		csu libgcc libgcov

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
