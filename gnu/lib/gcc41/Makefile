# $DragonFly: src/gnu/lib/gcc41/Makefile,v 1.2 2007/01/15 23:09:05 corecode Exp $

SUBDIR=		csu libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc41 ${MAKE}
.endif

.if !defined(NO_CXX)
SUBDIR+=	libstdc++ libsupc++
.endif

.include <bsd.subdir.mk>
