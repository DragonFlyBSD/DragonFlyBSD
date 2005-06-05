# $DragonFly: src/gnu/lib/gcc40/Attic/Makefile,v 1.1 2005/06/05 22:43:21 corecode Exp $

SUBDIR=		libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc40 ${MAKE}
.endif

#.if !defined(NO_OBJC)
#SUBDIR+=	libobjc
#.endif

.if !defined(NO_CXX)
SUBDIR+=	libstdc++ libsupc++
.endif

.include <bsd.subdir.mk>
