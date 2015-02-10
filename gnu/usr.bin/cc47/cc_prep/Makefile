.include "../Makefile.inc"
.include "../Makefile.langs"

CONTRIBDIR=	${GCCDIR}/gcc

LANG1=c
LANG2=, c++
LANG3=, objc
LANG4=, LTO

version.c: ${CONTRIBDIR}/version.c Makefile ../Makefile.inc
	> ${.TARGET}
	echo '#define BASEVER "${GCCCOMPLETEVER}"' >> ${.TARGET}
	echo '#define DATESTAMP ""' >> ${.TARGET}
	echo '#define DEVPHASE ""' >> ${.TARGET}
	echo '#define REVISION " [DragonFly] Release/${GCCDATESTAMP}"' >> ${.TARGET}
	echo '#define PKGVERSION ""' >> ${.TARGET}
	echo '#define BUGURL "<http://bugs.dragonflybsd.org>"' >> ${.TARGET}
	cat ${.ALLSRC:M*.c} >> ${.TARGET}

bversion.h:
	echo "#define BUILDING_GCC_MAJOR `echo $(GCCCOMPLETEVER) | sed -e 's/^\([0-9]*\).*$$/\1/'`" > ${.TARGET}
	echo "#define BUILDING_GCC_MINOR `echo $(GCCCOMPLETEVER) | sed -e 's/^[0-9]*\.\([0-9]*\).*$$/\1/'`" >> ${.TARGET}
	echo "#define BUILDING_GCC_PATCHLEVEL `echo $(GCCCOMPLETEVER) | sed -e 's/^[0-9]*\.[0-9]*\.\([0-9]*\)$$/\1/'`" >> ${.TARGET}
	echo "#define BUILDING_GCC_VERSION (BUILDING_GCC_MAJOR * 1000 + BUILDING_GCC_MINOR)" >> ${.TARGET}

plugin-version.h:
	echo '#include "configargs.h"' > ${.TARGET}
	echo >> ${.TARGET}
	echo "#define GCCPLUGIN_VERSION_MAJOR   `echo $(GCCCOMPLETEVER) | sed -e 's/^\([0-9]*\).*$$/\1/'`" >> ${.TARGET}
	echo "#define GCCPLUGIN_VERSION_MINOR   `echo $(GCCCOMPLETEVER) | sed -e 's/^[0-9]*\.\([0-9]*\).*$$/\1/'`" >> ${.TARGET}
	echo "#define GCCPLUGIN_VERSION_PATCHLEVEL   `echo $(GCCCOMPLETEVER) | sed -e 's/^[0-9]*\.[0-9]*\.\([0-9]*\)$$/\1/'`" >> ${.TARGET}
	echo "#define GCCPLUGIN_VERSION  (GCCPLUGIN_VERSION_MAJOR*1000 + GCCPLUGIN_VERSION_MINOR)" >> ${.TARGET}
	echo >> ${.TARGET}
	echo 'static char basever[] = "${GCCCOMPLETEVER}";' >> ${.TARGET}
	echo "static char datestamp[] = \"`echo ${GCCDATESTAMP} | sed -e 's/\.//g'`\";" >> ${.TARGET}
	echo 'static char devphase[] = "release";' >> ${.TARGET}
	echo 'static char revision[] = "";' >> ${.TARGET}
	echo >> ${.TARGET}
	echo 'static struct plugin_gcc_version gcc_version = {basever, datestamp,' >> ${.TARGET}
	echo '  devphase, revision, configuration_arguments};' >> ${.TARGET}

configargs.h: Makefile
	echo '/* Generated automatically. */'		> ${.TARGET}
	echo 'static const char configuration_arguments[] = '	>> ${.TARGET}
	echo '	"DragonFly/${TARGET_ARCH} system compiler (${LANG1}${LANG2}${LANG3}${LANG4})";'	>> ${.TARGET}
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
	echo '# include "$H"'				>> ${.TARGET}
.endfor
	echo '#if !defined GENERATOR_FILE && !defined USED_FOR_TARGET' >> ${.TARGET}
	echo '# include "insn-constants.h"'		>> ${.TARGET}
	echo '# include "insn-flags.h"'			>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '# include "defaults.h"'			>> ${.TARGET}
	echo '#endif /* GCC_TM_H */'			>> ${.TARGET}

tm_p.h:
	echo '#ifndef GCC_TM_P_H'			> ${.TARGET}
	echo '#define GCC_TM_P_H'			>> ${.TARGET}
	echo '#ifdef IN_GCC'				>> ${.TARGET}
	echo '# include "config/${GCC_CPU}/${GCC_CPU}-protos.h"'	>> ${.TARGET}
	echo '# include "tm-preds.h"'			>> ${.TARGET}
	echo '#endif'					>> ${.TARGET}
	echo '#endif /* GCC_TM_P_H */'			>> ${.TARGET}

optionlist: ${optionsfiles} Makefile
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-gather.awk \
		${optionsfiles} > optionlist

options.c: optionlist
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-functions.awk \
		     -f ${GCCDIR}/gcc/opt-read.awk \
		     -f ${GCCDIR}/gcc/optc-gen.awk \
		     -v header_name="config.h system.h coretypes.h tm.h" \
		< optionlist > ${.TARGET}

options-save.c: optionlist
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-functions.awk \
		     -f ${GCCDIR}/gcc/opt-read.awk \
		     -f ${GCCDIR}/gcc/optc-save-gen.awk \
		     -v header_name="config.h system.h coretypes.h tm.h" \
		< optionlist > ${.TARGET}

options.h: optionlist
	/usr/bin/awk -f ${GCCDIR}/gcc/opt-functions.awk \
		     -f ${GCCDIR}/gcc/opt-read.awk \
		     -f ${GCCDIR}/gcc/opth-gen.awk \
		< optionlist > ${.TARGET}

i386-builtin-types.inc:
	/usr/bin/awk -f ${GCCDIR}/gcc/config/i386/i386-builtin-types.awk \
		        ${GCCDIR}/gcc/config/i386/i386-builtin-types.def \
		> ${.TARGET}

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
	echo '#include "c-family/c-common.def"' >> ${.TARGET}
.for F in ${lang_tree_files}
	echo '#include "$F"' >> ${.TARGET}
.endfor

CLEANFILES+=	version.c configargs.h bconfig.h tm.h tm_p.h
CLEANFILES+=	options.c options-save.c options.h optionlist
CLEANFILES+=	specs.h all-tree.def bversion.h plugin-version.h

# keep this order!
afterdepend: version.c configargs.h bconfig.h tm.h tm_p.h options.h options.c
afterdepend: options-save.c specs.h all-tree.def bversion.h plugin-version.h

.if ${TARGET_ARCH} == "i386" || ${TARGET_ARCH} == "x86_64"
CLEANFILES+=	i386-builtin-types.inc
afterdepend:	i386-builtin-types.inc
.endif

.include <bsd.prog.mk>
