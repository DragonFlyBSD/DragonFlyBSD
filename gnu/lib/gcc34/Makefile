# $DragonFly: src/gnu/lib/gcc34/Makefile,v 1.2 2004/06/21 03:48:04 dillon Exp $

SUBDIR=		libgcc libgcov

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc34 ${MAKE}
.endif

SUBDIR+=	libobjc
SUBDIR+=	libstdc++ libsupc++

.include <bsd.subdir.mk>
