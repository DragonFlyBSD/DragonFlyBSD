# $DragonFly: src/gnu/lib/gcc41/Makefile,v 1.1 2006/09/27 12:10:33 corecode Exp $

SUBDIR=		libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc41 ${MAKE}
.endif

.if !defined(NO_CXX)
SUBDIR+=	libstdc++ libsupc++
.endif

.include <bsd.subdir.mk>
