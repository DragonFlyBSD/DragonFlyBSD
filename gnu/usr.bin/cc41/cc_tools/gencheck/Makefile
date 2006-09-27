# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/gencheck/Makefile,v 1.1 2006/09/27 12:10:34 corecode Exp $

.include "../Makefile.inc"

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
