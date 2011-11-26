.include "../Makefile.inc"
.include "../Makefile.langs"

CONTRIBDIR=	${GCCDIR}/gcc

version.c: ${CONTRIBDIR}/version.c Makefile ../Makefile.inc
	> ${.TARGET}
	echo '#define BASEVER "${GCCCOMPLETEVER}"' >> ${.TARGET}
	echo '#define DATESTAMP " ${GCCDATESTAMP}"' >> ${.TARGET}
	echo '#define DEVPHASE ""' >> ${.TARGET}
	echo '#define REVISION ""' >> ${.TARGET}
	echo '#define PKGVERSION "(DragonFly) "' >> ${.TARGET}
	echo '#define BUGURL "<http://bugs.dragonflybsd.org>"' >> ${.TARGET}
	cat ${.ALLSRC:M*.c} >> ${.TARGET}

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
	echo '#include "auto-host.h"'			>> ${.TARGET}
	echo '#ifdef IN_GCC'				>> ${.TARGET}
	echo '# include "ansidecl.h"'			>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif /* GCC_BCONFIG_H */'		>> ${.TARGET}

tm.h:
	echo '#ifndef GCC_TM_H'				> ${.TARGET}
	echo '#define GCC_TM_H'				>> ${.TARGET}
	echo '#ifdef IN_GCC'				>> ${.TARGET}
.for H in ${TARGET_INC}
	echo '#include "$H"'				>> ${.TARGET}
.endfor
	echo '#if !defined GENERATOR_FILE && !defined USED_FOR_TARGET' >> ${.TARGET}
	echo '# include "insn-constants.h"'		>> ${.TARGET}
	echo '# include "insn-flags.h"'			>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif /* GCC_TM_H */'			>> ${.TARGET}

tm_p.h:
	echo '#ifndef GCC_TM_P_H'			> ${.TARGET}
	echo '#define GCC_TM_P_H'			>> ${.TARGET}
	echo '#ifdef IN_GCC'				>> ${.TARGET}
	echo '#include "config/${GCC_CPU}/${GCC_CPU}-protos.h"'	>> ${.TARGET}
	echo '#include "tm-preds.h"'			>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif /* GCC_TM_P_H */'			>> ${.TARGET}

optionlist: ${optionsfiles} Makefile
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-gather.awk ${optionsfiles} > optionlist

options.c: optionlist
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-functions.awk -f ${GCCDIR}/gcc/optc-gen.awk \
		-v header_name="config.h system.h coretypes.h tm.h" < optionlist > options.c

options.h: optionlist
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-functions.awk -f ${GCCDIR}/gcc/opth-gen.awk \
		< optionlist > options.h

specs.h:
	rm -f ${.TARGET}
	touch ${.TARGET}
.for F in ${LANG_SPECS_FILES}
	echo "#include \"${F}\"" >> ${.TARGET}
.endfor

all-tree.def:
	rm -f ${.TARGET}
	echo '#include "tree.def"' >> ${.TARGET}
	echo 'END_OF_BASE_TREE_CODES' >> ${.TARGET}
	echo '#include "c-common.def"' >> ${.TARGET}
.for F in ${lang_tree_files}
	echo '#include "$F"' >> ${.TARGET}
.endfor

CLEANFILES+=	version.c configargs.h bconfig.h tm.h tm_p.h
CLEANFILES+=	options.c options.h optionlist
CLEANFILES+=	specs.h all-tree.def

# keep this order!
afterdepend: version.c configargs.h bconfig.h tm.h tm_p.h options.h options.c
afterdepend: specs.h all-tree.def

.include <bsd.prog.mk>
