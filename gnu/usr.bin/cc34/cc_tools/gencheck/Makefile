# $DragonFly: src/gnu/usr.bin/cc34/cc_tools/gencheck/Makefile,v 1.1 2004/06/14 22:27:54 joerg Exp $

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
