# $DragonFly: src/gnu/usr.bin/cc40/doc/Attic/Makefile,v 1.2 2005/09/26 03:01:46 y0netan1 Exp $

.include "../Makefile.inc"

SRCDIR=		${GCCDIR}/gcc/doc
CONTRIBDIR=	${SRCDIR}
G77DIR=		${GCCDIR}/gcc/f

.PATH:	${SRCDIR}

INFO=	cpp cppinternals gcc gccint

#.if !defined(NO_FORTRAN)
#INFO+=	g77
#.endif

INFOSECTION=		"Programming & development tools."
INFOENTRY_cpp=		"* CPP.					The GNU C preprocessor."
INFOENTRY_cppinternals=	"* CPP-internals.			The GNU C preprocessor's internals."
INFOENTRY_g77=		"* G77.					The GNU Fortran compiler."
INFOENTRY_gcc=		"* GCC.					The GNU Compiler Collection."
INFOENTRY_gcint=	"* GCC-internals.			The GNU Compiler Collection's internals."

# Install as name40 for the time being
ORIGINFO:= ${INFO}
INFO=	#

.for _i in ${ORIGINFO}
INFO+=	${_i}40
INFOENTRY_${_i}40= ${INFOENTRY_${_i}:C/\./40./1}

${_i}40.info: ${_i}.info
	sed -E -e 's/[[:<:]]${_i}.info[[:>:]]/${_i}40.info/g' \
		-e 's/\*([^.]*): \(${_i}\)\./*\140: (${_i}40)./g' \
		-e 's/\((gccint|cpp|gcc)\)/(\140)/g' ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	${_i}.info
.endfor
# End magic rewriting

cpp.info: cpp.texi fdl.texi gcc-common.texi cppenv.texi cppopts.texi

cppinternals.info: cppinternals.texi gcc-common.texi

gcc.info: gcc.texi gcc-common.texi frontends.texi standards.texi invoke.texi \
	  extend.texi md.texi objc.texi gcov.texi trouble.texi bugreport.texi \
	  service.texi contribute.texi compat.texi funding.texi gnu.texi \
	  gpl.texi fdl.texi contrib.texi cppenv.texi cppopts.texi

gccint.info: gccint.texi gcc-common.texi contribute.texi makefile.texi \
	     configterms.texi portability.texi interface.texi passes.texi \
	     c-tree.texi rtl.texi md.texi tm.texi hostconfig.texi \
	     fragments.texi configfiles.texi collect2.texi headerdirs.texi \
	     funding.texi gnu.texi gpl.texi fdl.texi contrib.texi \
	     languages.texi sourcebuild.texi gty.texi libgcc.texi

g77.info: g77.texi bugs.texi ffe.texi g77_invoke.texi \
	  news.texi root.texi fdl.texi gpl.texi funding.texi \
	  gcc-common.texi intdoc.texi

.for inctexi in fdl.texi funding.texi gcc-common.texi gpl.texi texinfo.texi
${inctexi}: ${CONTRIBDIR}/include/${inctexi}
	cp ${CONTRIBDIR}/include/${inctexi} ${.TARGET}
CLEANFILES+=	${inctexi}
.endfor

.for g77texi in bugs.texi ffe.texi news.texi root.texi \
		intdoc.texi
${g77texi}: ${G77DIR}/${g77texi}
	cp ${G77DIR}/${g77texi} ${.TARGET}
CLEANFILES+=	${g77texi}
.endfor

g77.texi: ${G77DIR}
	sed 's/invoke\.texi/g77_invoke\.texi/' < ${G77DIR}/g77.texi > ${.TARGET}

g77_invoke.texi: ${G77DIR}/invoke.texi
	cp ${G77DIR}/invoke.texi ${.TARGET}

CLEANFILES+=	g77.texi g77_invoke.texi

.include <bsd.info.mk>
