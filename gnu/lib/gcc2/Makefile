# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/gcc2/Attic/Makefile,v 1.5 2004/06/14 19:35:35 joerg Exp $
#
# GCC2 libraries must be built with GCC2.  If make does not support .makeenv
# we have to hack it by changing ${MAKE}

SUBDIR= libgcc

.if !defined(NO_CXX)
SUBDIR+= libstdc++
.endif

.if !defined(NO_OBJC)
SUBDIR+= libobjc
.endif

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := CCVER=gcc2 ${MAKE}
.endif 

# inter-directory dependancies on tconfig.h
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
