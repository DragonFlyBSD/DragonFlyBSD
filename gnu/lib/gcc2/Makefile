# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/gcc2/Attic/Makefile,v 1.3 2004/03/20 16:27:39 drhodus Exp $
#
# GCC2 libraries must be built with GCC2.  If make does not support .makeenv
# we have to hack it by changing ${MAKE}

SUBDIR= csu libgcc

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
