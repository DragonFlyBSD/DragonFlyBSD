.include "../Makefile.inc"
.include "../../Makefile.langs"

NXCFLAGS+=	-DGENERATOR_FILE -I${.OBJDIR}
NXLDLIBS+=	-lm

MIC= sh ${GCCDIR}/move-if-change

BUILD_LIBS=	../libiberty/libiberty.na
BUILD_RTL= 	rtl.no \
		read-rtl.no \
		ggc-none.no \
		vec.no \
		min-insn-modes.no \
		gensupport.no \
		print-rtl.no
BUILD_MD=	read-md.no
BUILD_ERRORS=	errors.no


${BUILD_RTL}: tm.h insn-modes.h gtype-desc.h insn-constants.h

genprogrtl = attr attr-common attrtab automata codes conditions config emit \
	     extract flags opinit output peep preds recog
genprogmd  = $(genprogrtl) mddeps constants enums
genprogerr = $(genprogmd) genrtl modes gtype hooks

# All these programs use the RTL reader ($(BUILD_RTL)).
${genprogrtl:S/^/gen/g:S/$/.no/g}: ${BUILD_RTL}
# All these programs use the MD reader
${genprogmd:S/^/gen/g:S/$/.no/g}: ${BUILD_MD}
# All these programs need to report errors.
${genprogerr:S/^/gen/g:S/$/.no/g}: ${BUILD_ERRORS}

.for f in ${genprogrtl}
gen${f}.nx: ${BUILD_RTL}
.endfor

.for f in ${genprogmd}
gen${f}.nx: ${BUILD_MD}
.endfor

.for f in ${genprogerr}
gen${f}.nx: ${BUILD_ERRORS}
GENTOOLS+= gen${f}.nx
.endfor

gengtype.nx: gengtype-lex.no gengtype-parse.no gengtype-state.no version.no

genattrtab.o ggc-none.no vec.no: gtype-desc.h

GENTOOLS+= gencheck.nx gencondmd.nx gcov-iov.nx genchecksum.nx

.for f in ${GENTOOLS}
$f: ${f:.nx=.no} ${BUILD_LIBS}
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} ${.ALLSRC:M*.n[oa]} ${NXLDLIBS} -o ${.TARGET}
.endfor

gencheck.no: tm.h insn-constants.h
gencondmd.no: tree-check.h insn-constants.h

CLEANFILES+=	${GENTOOLS} ${GENTOOLS:.nx=.no}
CLEANFILES+=	gengtype-lex.c gengtype-lex.no gengtype-parse.no
CLEANFILES+=	gengtype-state.no version.no
CLEANFILES+=	${BUILD_RTL} ${BUILD_MD} ${BUILD_ERRORS}


MD_DEPS=	${md_file}

_MIC: .USE
	${MIC} ${.TARGET}.tmp ${.TARGET}
_PL: .USE
	${.OBJDIR}/${.ALLSRC:M*.nx} > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}
_MD: .USE
	${.OBJDIR}/${.ALLSRC:M*.nx} ${md_file} > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

simple_generated_h = insn-attr.h insn-codes.h insn-config.h insn-flags.h \
		     insn-attr-common.h

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
insn-enums.c: genenums.nx _MD
tm-preds.h: genpreds.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h ${md_file} > ${.TARGET}.tmp
tm-constrs.h: genpreds.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -c ${md_file} > ${.TARGET}.tmp
gtype.state: gengtype.nx gtyp-input.list ${GTFILES:N[*]} _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -S ${GCCDIR}/gcc -I ${.ALLSRC:M*.list} \
	  -w gtype.state.tmp
gtype-desc.c gtype-desc.h: gengtype.nx gtype.state
	${.OBJDIR}/${.ALLSRC:M*.nx} -r gtype.state
gcov-iov.h: gcov-iov.nx BASE-VER _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "$$(cat ${.ALLSRC:M*BASE-VER})" "" > ${.TARGET}.tmp
target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "Target Hook" > ${.TARGET}.tmp
c-family/c-target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "C Target Hook" > ${.TARGET}.tmp
common/common-target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "Common Target Hook" > ${.TARGET}.tmp

gtyp-input.list: _MIC
	rm -f ${.TARGET}.tmp
	for f in ${GTFILES}; do \
		echo "$$f" >> ${.TARGET}.tmp; \
	done

GENFILES= \
	${simple_generated_h} ${simple_generated_c} \
	insn-constants.h tree-check.h gencondmd.c insn-conditions.md \
	genrtl.c genrtl.h insn-modes.c insn-modes.h min-insn-modes.c \
	insn-preds.c tm-preds.h tm-constrs.h gtype-desc.c gtype-desc.h \
	gcov-iov.h insn-enums.c target-hooks-def.h \
	c-family/c-target-hooks-def.h \
	common/common-target-hooks-def.h

CLEANFILES+=	${GENFILES} gtyp-input.list gtype.state
CLEANFILES+=	gt-* gtype-*.h
CLEANDIRS+=	common c-family

dossier:
	mkdir -p common c-family

.if defined(LIBGCC_ONLY)
genfiles: ${GENTOOLS:Mgcov-iov*} ${GENFILES:Mgcov-io*}
.else
genfiles: ${GENTOOLS} ${GENFILES}
.endif
depend all: dossier genfiles

.include <bsd.obj.mk>
.include <bsd.dep.mk>
.include <bsd.sys.mk>
