# $DragonFly: src/gnu/usr.bin/cc41/doc/Makefile,v 1.1 2006/09/27 12:10:35 corecode Exp $

.include "../Makefile.inc"

SRCDIR=		${GCCDIR}/gcc/doc
CONTRIBDIR=	${SRCDIR}

.PATH:	${SRCDIR}

INFO=	cpp cppinternals gcc gccint

INFOSECTION=		"Programming & development tools."
INFOENTRY_cpp=		"* CPP.					The GNU C preprocessor."
INFOENTRY_cppinternals=	"* CPP-internals.			The GNU C preprocessor's internals."
INFOENTRY_gcc=		"* GCC.					The GNU Compiler Collection."
INFOENTRY_gcint=	"* GCC-internals.			The GNU Compiler Collection's internals."

# Install as name41 for the time being
ORIGINFO:= ${INFO}
INFO=	#

.for _i in ${ORIGINFO}
INFO+=	${_i}41
INFOENTRY_${_i}41= ${INFOENTRY_${_i}:C/\./41./1}

${_i}41.info: ${_i}.info
	sed -E -e 's/[[:<:]]${_i}.info[[:>:]]/${_i}41.info/g' \
		-e 's/\*([^.]*): \(${_i}\)\./*\141: (${_i}41)./g' \
		-e 's/\((gccint|cpp|gcc)\)/(\141)/g' ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	${_i}.info
.endfor
# End magic rewriting

cpp.info: cpp.texi fdl.texi cppenv.texi cppopts.texi		\
	 gcc-common.texi gcc-vers.texi

gcc.info: gcc.texi gcc-common.texi gcc-vers.texi frontends.texi	\
	 standards.texi invoke.texi extend.texi md.texi objc.texi	\
	 gcov.texi trouble.texi bugreport.texi service.texi		\
	 contribute.texi compat.texi funding.texi gnu.texi gpl.texi	\
	 fdl.texi contrib.texi cppenv.texi cppopts.texi			\
	 implement-c.texi

gccint.info: gccint.texi gcc-common.texi gcc-vers.texi		\
	 contribute.texi makefile.texi configterms.texi options.texi	\
	 portability.texi interface.texi passes.texi c-tree.texi	\
	 rtl.texi md.texi tm.texi hostconfig.texi fragments.texi	\
	 configfiles.texi collect2.texi headerdirs.texi funding.texi	\
	 gnu.texi gpl.texi fdl.texi contrib.texi languages.texi		\
	 sourcebuild.texi gty.texi libgcc.texi cfg.texi tree-ssa.texi

cppinternals.info: cppinternals.texi gcc-common.texi gcc-vers.texi

gcc-vers.texi: ${SRCDIR}/../BASE-VER ${SRCDIR}/../DEV-PHASE
	(echo "@set version-GCC `cat ${SRCDIR}/../BASE-VER`"; \
		if [ "`cat ${SRCDIR}/../DEV-PHASE`" = "experimental" ]; \
		then echo "@set DEVELOPMENT"; \
		else echo "@clear DEVELOPMENT"; \
		fi) > $@T
	mv -f $@T $@

CLEANFILES+=	gcc-vers.texi

.for inctexi in fdl.texi funding.texi gcc-common.texi gpl.texi texinfo.texi
${inctexi}: ${CONTRIBDIR}/include/${inctexi}
	cp ${CONTRIBDIR}/include/${inctexi} ${.TARGET}
CLEANFILES+=	${inctexi}
.endfor

.include <bsd.info.mk>
