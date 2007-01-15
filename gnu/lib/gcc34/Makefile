# $DragonFly: src/gnu/lib/gcc34/Makefile,v 1.6 2007/01/15 23:09:05 corecode Exp $

SUBDIR=		csu libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc34 ${MAKE}
.endif

.if !defined(NO_FORTRAN)
SUBDIR+= libg2c libfrtbegin
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.if !defined(NO_CXX)
SUBDIR+=	libstdc++ libsupc++
.endif

.include <bsd.subdir.mk>
