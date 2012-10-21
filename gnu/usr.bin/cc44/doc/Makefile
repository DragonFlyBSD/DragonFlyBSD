.include "../Makefile.inc"

SRCDIR=		${GCCDIR}/gcc/doc
CONTRIBDIR=	${SRCDIR}

.PATH:	${SRCDIR}

ORIGINFO=	cpp cppinternals gcc gccint

INFOSECTION=		"Programming & development tools."
INFOENTRY_cpp=		"* CPP.					The GNU C preprocessor."
INFOENTRY_cppinternals=	"* CPP-internals.			The GNU C preprocessor's internals."
INFOENTRY_gcc=		"* GCC.					The GNU Compiler Collection."
INFOENTRY_gcint=	"* GCC-internals.			The GNU Compiler Collection's internals."

# Install as nameVER for the time being
.for _i in ${ORIGINFO}
.for docsfx in ${GCCSHORTVER}
.if empty(INFO:M${_i}${docsfx})
INFO+=	${_i}${docsfx}
INFOENTRY_${_i}${docsfx}= ${INFOENTRY_${_i}:C/\./${docsfx}./1}

${_i}${docsfx}.info: ${_i}.info
	sed -E -e 's/[[:<:]]${_i}.info[[:>:]]/${_i}${docsfx}.info/g' \
		-e 's/\*([^.]*): \(${_i}\)\./*\1${docsfx}: (${_i}${docsfx})./g' \
		-e 's/\((gccint|cpp|gcc)\)/(\1${docsfx})/g' ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	${_i}${docsufx}.info
.endif
.endfor
.endfor
# End magic rewriting

cpp.info: cpp.texi fdl.texi cppenv.texi cppopts.texi		\
	 gcc-common.texi gcc-vers.texi

gcc.info: gcc.texi gcc-common.texi gcc-vers.texi frontends.texi	\
	 standards.texi invoke.texi extend.texi md.texi objc.texi	\
	 gcov.texi trouble.texi bugreport.texi service.texi		\
	 contribute.texi compat.texi funding.texi gnu.texi gpl_v3.texi	\
	 fdl.texi contrib.texi cppenv.texi cppopts.texi			\
	 implement-c.texi

gccint.info: gccint.texi gcc-common.texi gcc-vers.texi		\
	 contribute.texi makefile.texi configterms.texi options.texi	\
	 portability.texi interface.texi passes.texi c-tree.texi	\
	 rtl.texi md.texi tm.texi hostconfig.texi fragments.texi	\
	 configfiles.texi collect2.texi headerdirs.texi funding.texi	\
	 gnu.texi gpl_v3.texi fdl.texi contrib.texi languages.texi	\
	 sourcebuild.texi gty.texi libgcc.texi cfg.texi tree-ssa.texi	\
	 loop.texi generic.texi gimple.texi

cppinternals.info: cppinternals.texi gcc-common.texi gcc-vers.texi

gcc-vers.texi: Makefile ../Makefile.inc
	(echo "@set version-GCC ${GCCCOMPLETEVER}"; \
	 echo "@clear DEVELOPMENT"; \
	) > $@T
	echo "@set srcdir ${GCCDIR}/gcc" >> $@T
	echo "@set VERSION_PACKAGE (DragonFly)" >> $@T
	echo "@set BUGURL @uref{http://bugs.dragonflybsd.org}" >> $@T
	mv -f $@T $@

CLEANFILES+=	gcc-vers.texi

.for inctexi in fdl.texi funding.texi gcc-common.texi gpl_v3.texi texinfo.texi
${inctexi}: ${CONTRIBDIR}/include/${inctexi}
	cp ${CONTRIBDIR}/include/${inctexi} ${.TARGET}
CLEANFILES+=	${inctexi}
.endfor

.include <bsd.info.mk>
