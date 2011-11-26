# $DragonFly: src/gnu/lib/gcc41/Makefile,v 1.3 2008/06/30 12:16:03 hasso Exp $

SUBDIR=		csu libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc41 ${MAKE}
.endif

.if !defined(NO_CXX)
SUBDIR+=	libstdc++ libsupc++
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.include <bsd.subdir.mk>
