# $DragonFly: src/gnu/lib/gcc3/Attic/Makefile,v 1.5 2004/03/22 20:57:35 dillon Exp $
#
# gcc3 libraries must be built with gcc3.  When bootstraping from 4.x
# or from an older DFly without the .makeenv directive, we have to export
# CCVER manually.

SUBDIR= csu libgcc libgcc_r libstdc++ libsupc++

.if !defined(.DIRECTIVE_MAKEENV)
MAKE := CCVER=gcc3 ${MAKE}
.endif

# inter-directory dependancies on tconfig.h 
.ORDER:	${SUBDIR}

.include <bsd.subdir.mk>
