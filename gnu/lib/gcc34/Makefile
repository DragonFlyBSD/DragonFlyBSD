# $DragonFly: src/gnu/lib/gcc34/Makefile,v 1.4 2005/06/19 16:18:22 joerg Exp $

SUBDIR=		libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc34 ${MAKE}
.endif

.if !defined(NO_FORTRAN)
SUBDIR+= libg2c
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.if !defined(NO_CXX)
SUBDIR+=	libstdc++ libsupc++
.endif

.include <bsd.subdir.mk>
