# $DragonFly: src/gnu/usr.bin/cc41/cc_prep/Makefile,v 1.1 2006/09/27 12:10:33 corecode Exp $

.include "../Makefile.inc"
.include "../Makefile.langs"

CONTRIBDIR=	${GCCDIR}/gcc

BASEVER_s!=	cat ${CONTRIBDIR}/BASE-VER

version.c: ${CONTRIBDIR}/version.c Makefile BASE-VER
	> ${.TARGET}
	echo '#define BASEVER "${BASEVER_s}"' >> ${.TARGET}
	echo '#define DATESTAMP ""' >> ${.TARGET}
	echo '#define DEVPHASE ""' >> ${.TARGET}
	echo '#define VERSUFFIX " (DragonFly)"' >> ${.TARGET}
	sed -e '/^#define VERSUFFIX/d' < ${.ALLSRC:M*c} >> ${.TARGET}

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
#.if exists(${GCCDIR}/gcc/config/${GCC_CPU}/${GCC_CPU}-modes.def)
#	echo '#define EXTRA_MODES_FILE "${GCC_CPU}/${GCC_CPU}-modes.def"' >> ${.TARGET}
#.endif
	echo '#endif /* GCC_TM_H */'			>> ${.TARGET}

tm_p.h:
	echo '#include "${GCC_CPU}/${GCC_CPU}-protos.h"'	>> ${.TARGET}
	echo '#include "tm-preds.h"'				>> ${.TARGET}

optionlist: ${optionsfiles} Makefile
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-gather.awk ${optionsfiles} > optionlist

options.c: optionlist
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-functions.awk -f ${GCCDIR}/gcc/optc-gen.awk \
		-v header_name="config.h system.h coretypes.h tm.h" < optionlist > options.c

options.h: optionlist
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-functions.awk -f ${GCCDIR}/gcc/opth-gen.awk \
		< optionlist > options.h

CLEANFILES+=	version.c configargs.h bconfig.h tm.h tm_p.h
CLEANFILES+=	options.c options.h optionlist

.include <bsd.prog.mk>

# keep this order!
depend: version.c configargs.h bconfig.h tm.h tm_p.h options.h options.c
