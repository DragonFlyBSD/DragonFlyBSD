# $DragonFly: src/gnu/lib/gcc3/Attic/Makefile,v 1.7 2004/06/21 03:48:04 dillon Exp $
#
# gcc3 libraries must be built with gcc3.  When bootstraping from 4.x
# or from an older DFly without the .makeenv directive, we have to export
# CCVER manually.

SUBDIR= libgcc libstdc++ libsupc++

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := HOST_CCVER=${HOST_CCVER} CCVER=gcc3 ${MAKE}
.endif

# inter-directory dependancies on tconfig.h 
.ORDER:	${SUBDIR}

.include <bsd.subdir.mk>
