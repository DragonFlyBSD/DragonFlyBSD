# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/gcc2/Attic/Makefile,v 1.2 2004/01/30 02:35:01 dillon Exp $
#
# GCC2 libraries must be built with GCC2.  If make does not support .makeenv
# we have to hack it by changing ${MAKE}

SUBDIR= csu libgcc

.if ${OBJFORMAT} == aout
.if !defined(NOLIBC_R)
SUBDIR+= libgcc_r
.endif
.endif

.if !defined(NO_CXX)
SUBDIR+= libstdc++
.endif

.if !defined(NO_OBJC)
SUBDIR+= libobjc
.endif

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := CCVER=gcc2 ${MAKE}
.endif 

.include <bsd.subdir.mk>
