# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/gcc2/Attic/Makefile,v 1.6 2004/06/21 03:48:04 dillon Exp $
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

# This is to support bootstrap builds from FreeBSD-4.x.  While this
# would work in DFly too, it would not work properly when running make
# manually from a sub directory.
#
.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc2 ${MAKE}
.endif 

# inter-directory dependancies on tconfig.h
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
