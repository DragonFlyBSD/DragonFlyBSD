# $DragonFly: src/gnu/usr.bin/cc40/cc_tools/gencheck/Attic/Makefile,v 1.1 2005/06/05 22:43:20 corecode Exp $

.include "../Makefile.inc"
.include "../../Makefile.langs"

PROG=	gencheck
SRCS=	gencheck.c gencheck.h

CLEANFILES+=	gencheck.h

gencheck.h: Makefile ../../Makefile.langs
	-rm -f ${.TARGET}
.if defined(gencheck_defs)
.for F in ${gencheck_defs}
	echo "#include \"${F}\"" >> ${.TARGET}
.endfor
.else
	touch ${.TARGET}
.endif

.include <bsd.hostprog.mk>
