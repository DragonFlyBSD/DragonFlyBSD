# $FreeBSD: src/gnu/usr.bin/cc/doc/Makefile,v 1.10 1999/08/27 23:35:19 peter Exp $
# $DragonFly: src/gnu/usr.bin/cc/doc/Attic/Makefile,v 1.3 2004/12/21 13:10:47 joerg Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/cp ${GCCDIR}

INFO=	gcc2 cpp2 gxxint

INFOSECTION=    "Gcc Documentation"
INFOENTRY_gcc2=  "* gcc2: (gcc2).		The EGCS GNU C compiler."
INFOENTRY_cpp2=  "* cpp2: (cpp2).		The EGCS GNU C pre-processor."
INFOENTRY_gxxint= "* gxxint: (gxxint).		The EGCS GNU compiler family internal documentation."

gcc2.info: gcc2.texi invoke.texi install.texi extend.texi gcov.texi rtl.texi \
	  md.texi tm.texi

gcc2.texi: gcc.texi
	cp ${GCCDIR}/gcc.texi ${.TARGET}
cpp2.texi: cpp.texi
	cp ${GCCDIR}/cpp.texi ${.TARGET}

CLEANFILES+=	gcc2.texi cpp2.texi

MAKEINFOFLAGS+= -I ${GCCDIR} -I ${GCCDIR}/cp

.include <bsd.info.mk>
