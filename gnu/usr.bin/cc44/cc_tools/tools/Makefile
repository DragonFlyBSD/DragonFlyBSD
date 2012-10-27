.include "../Makefile.inc"
.include "../../Makefile.langs"

NXCFLAGS+=	-DGENERATOR_FILE -I${.OBJDIR}
NXLDLIBS+=	-lm

MIC= sh ${GCCDIR}/move-if-change

BUILD_LIBS=	../libiberty/libiberty.na
BUILD_RTL=	rtl.no read-rtl.no ggc-none.no vec.no \
		min-insn-modes.no gensupport.no print-rtl.no
BUILD_ERRORS=	errors.no


${BUILD_RTL}: tm.h insn-modes.h gtype-desc.h


genprogmd = attr attrtab automata codes conditions config constants emit \
	    extract flags mddeps opinit output peep preds recog
.for f in ${genprogmd}
gen$f.nx: ${BUILD_RTL} ${BUILD_ERRORS}
GENTOOLS+=	gen$f.nx
.endfor
gengenrtl.nx: ${BUILD_ERRORS}
genmodes.nx: ${BUILD_ERRORS}
gengtype.nx: gengtype-lex.no gengtype-parse.no ${BUILD_ERRORS}

NEED_RTL=	support attr attrtab automata codes conditions config \
		constants emit extract flags mddeps opinit output peep \
		preds recog
${NEED_RTL:S/^/gen/g:S/$/.no/g} print-rtl.no read-rtl.no: insn-modes.h

genattrtab.no ggc-none.no vec.no: gtype-desc.h


GENTOOLS+=	gengenrtl.nx genmodes.nx gengtype.nx gencheck.nx
GENTOOLS+=	gencondmd.nx gcov-iov.nx genchecksum.nx
.for f in ${GENTOOLS}
$f: ${f:.nx=.no} ${BUILD_LIBS}
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} ${.ALLSRC:M*.n[oa]} ${NXLDLIBS} -o ${.TARGET}
.endfor

gencondmd.no: tree-check.h insn-constants.h

CLEANFILES+=	${GENTOOLS} ${GENTOOLS:.nx=.no}
CLEANFILES+=	gengtype-lex.c gengtype-lex.no gengtype-parse.no
CLEANFILES+=	${BUILD_RTL} ${BUILD_ERRORS}


MD_DEPS=	${md_file}

_MIC: .USE
	${MIC} ${.TARGET}.tmp ${.TARGET}
_PL: .USE
	${.OBJDIR}/${.ALLSRC:M*.nx} > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}
_MD: .USE
	${.OBJDIR}/${.ALLSRC:M*.nx} ${md_file} > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

simple_generated_h = insn-attr.h insn-codes.h insn-config.h insn-flags.h

simple_generated_c = insn-attrtab.c insn-automata.c insn-emit.c \
		     insn-extract.c insn-opinit.c insn-output.c \
		     insn-peep.c insn-recog.c

.for f in ${simple_generated_h} ${simple_generated_c}
.for p in gen${f:R:S/^insn-//}.nx
$f: $p ${MD_DEPS} insn-conditions.md _MIC
	${.OBJDIR}/$p ${md_file} insn-conditions.md > ${.TARGET}.tmp
.endfor
.endfor

insn-constants.h: genconstants.nx ${MD_DEPS} _MD
tree-check.h: gencheck.nx _PL
gencondmd.c: genconditions.nx ${MD_DEPS} tm-preds.h tm-constrs.h _MD
insn-conditions.md: gencondmd.nx _PL
genrtl.c: gengenrtl.nx _PL
genrtl.h: gengenrtl.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h > ${.TARGET}.tmp
insn-modes.c: genmodes.nx _PL
insn-modes.h: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h > ${.TARGET}.tmp
min-insn-modes.c: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -m > ${.TARGET}.tmp
insn-preds.c: genpreds.nx _MD
tm-preds.h: genpreds.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h ${md_file} > ${.TARGET}.tmp
tm-constrs.h: genpreds.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -c ${md_file} > ${.TARGET}.tmp
gtype-desc.c gtype-desc.h: gengtype.nx gtyp-input.list ${GTFILES:N[*]}
	${.OBJDIR}/${.ALLSRC:M*.nx} ${GCCDIR}/gcc ${.ALLSRC:M*.list}
gcov-iov.h: gcov-iov.nx BASE-VER _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "$$(cat ${.ALLSRC:M*BASE-VER})" "" > ${.TARGET}.tmp

gtyp-input.list: _MIC
	rm -f ${.TARGET}.tmp
	for f in ${GTFILES}; \
	do \
		echo "$$f" >> ${.TARGET}.tmp; \
	done


GENFILES= \
	${simple_generated_h} ${simple_generated_c} \
	insn-constants.h tree-check.h gencondmd.c insn-conditions.md \
	genrtl.c genrtl.h insn-modes.c insn-modes.h min-insn-modes.c \
	insn-preds.c tm-preds.h tm-constrs.h gtype-desc.c gtype-desc.h \
	gcov-iov.h

CLEANFILES+=	${GENFILES} gtyp-input.list
CLEANFILES+=	gt-* gtype-*.h

genfiles: ${GENTOOLS} ${GENFILES}
depend all: genfiles

.include <bsd.obj.mk>
.include <bsd.dep.mk>
