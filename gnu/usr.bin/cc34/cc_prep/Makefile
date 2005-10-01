# $DragonFly: src/gnu/usr.bin/cc34/cc_prep/Makefile,v 1.8 2005/10/01 12:40:33 corecode Exp $

.include "../Makefile.inc"

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${GCCDIR}/gcc

version_local.c: version.c Makefile
	sed -e 's/\(const char version.*\)";/\1 [DragonFly] (propolice, visibility)";/' < ${.ALLSRC:M*c} > ${.TARGET}

configargs.h: Makefile
	echo '/* Generated automatically. */'		> ${.TARGET}
	echo 'static const char configuration_arguments[] = '	>> ${.TARGET}
	echo '	"DragonFly/${TARGET_ARCH} system compiler";'	>> ${.TARGET}
	echo 'static const char thread_model[] = "posix";'	>> ${.TARGET}
	echo						>> ${.TARGET}
	echo 'static const struct {'			>> ${.TARGET}
	echo '  const char *name, *value;'		>> ${.TARGET}
	echo '} configure_default_options[] ='		>> ${.TARGET}
	echo '{ { NULL, NULL} };'			>> ${.TARGET}

bconfig.h:
	echo '#ifndef GCC_BCONFIG_H'			> ${.TARGET}
	echo '#define GCC_BCONFIG_H'			>> ${.TARGET}
	echo '#include "dragonfly-native.h"'		>> ${.TARGET}
.if exists(${GCCDIR}/gcc/config/${GCC_CPU}/${GCC_CPU}-modes.def)
	echo '#define EXTRA_MODES_FILE "${GCC_CPU}/${GCC_CPU}-modes.def"' >> ${.TARGET}
.endif
	echo '#ifdef IN_GCC'				>> ${.TARGET}
	echo '# include "ansidecl.h"'			>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif /* GCC_BCONFIG_H */'		>> ${.TARGET}

tm.h:
	echo '#ifndef GCC_TM_H'				> ${.TARGET}
	echo '#define GCC_TM_H'				>> ${.TARGET}
.if defined(TARGET_CPU_DEFAULT)
	echo "#define TARGET_CPU_DEFAULT (${TARGET_CPU_DEFAULT})" >> ${.TARGET}
.endif
	echo '#ifdef IN_GCC'				>> ${.TARGET}
.for H in ${TARGET_INC}
	echo '#include "$H"'				>> ${.TARGET}
.endfor
	echo '#if !defined GENERATOR_FILE && !defined USED_FOR_TARGET' >> ${.TARGET}
	echo '# include "insn-constants.h"'		>> ${.TARGET}
	echo '# include "insn-flags.h"'			>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
.if exists(${GCCDIR}/gcc/config/${GCC_CPU}/${GCC_CPU}-modes.def)
	echo '#define EXTRA_MODES_FILE "${GCC_CPU}/${GCC_CPU}-modes.def"' >> ${.TARGET}
.endif
	echo '#endif /* GCC_TM_H */'			>> ${.TARGET}

tm_p.h:
	echo '#include "${GCC_CPU}/${GCC_CPU}-protos.h"'	>> ${.TARGET}
	echo '#include "tm-preds.h"'				>> ${.TARGET}


CLEANFILES+=	version_local.c configargs.h bconfig.h tm.h tm_p.h

.include <bsd.prog.mk>

# keep this order!
depend: version_local.c configargs.h bconfig.h tm.h tm_p.h

# we don't use SRCS, so we can't use the normal bsd.dep.mk code
.for _PSRC in ${PATCHES:M*.patch}
.for _PC in ${_PSRC:T:S/.patch$//:S|,|/|g}

${_PC}: ${CONTRIBDIR}/${_PC} ${_PSRC}
	mkdir -p ${.TARGET:H}
	patch -o ${.TARGET} -i ${.ALLSRC:M*.patch} ${CONTRIBDIR}/${.TARGET}

CLEANFILES:=	${CLEANFILES} ${_PC}

depend: ${_PC}
.endfor
.endfor
