# Despite the source files ending in ".c", the c++ compiler needs to build
# everything.  We have to roll our own targets to properly support this.

.include "../Makefile.inc"
.include "../../Makefile.langs"

NXCXXFLAGS+=		-DGENERATOR_FILE -I${.OBJDIR}
BUILD_LIBS=		../libiberty/libiberty.na
BUILD_LIBS_genmatch=	../libcpp/libcpp.na
NXLD_genautomata=	-lm

MIC=		sh ${GCCDIR}/move-if-change

BUILD_RTL= 	rtl.no \
		read-rtl.no \
		ggc-none.no \
		vec.no \
		min-insn-modes.no \
		gensupport.no \
		print-rtl.no
BUILD_MD=	read-md.no
BUILD_ERRORS=	errors.no
BUILD_GTYPE=	gengtype-lex.no \
		gengtype-parse.no \
		gengtype-state.no \
		version.no
BUILD_MATCH=	hash-table.no

genprogrtl=	attr attr-common attrtab automata codes conditions config \
		emit extract flags opinit output peep preds recog mddump
genprogmd=	$(genprogrtl) mddeps constants enums
genprogerr=	$(genprogmd) genrtl modes gtype hooks match
genprog=	$(genprogerr) check checksum condmd

NO_RTL=		${genprogrtl:S/^/gen/g:S/$/.no/g}
NO_MD=		${genprogmd:S/^/gen/g:S/$/.no/g}
NO_PROGERR=	${genprogerr:S/^/gen/g:S/$/.no/g}
NO_PROG=	${genprog:S/^/gen/g:S/$/.no/g}
NO_LIST=	${BUILD_RTL} ${BUILD_MD} ${BUILD_ERRORS} \
		${BUILD_GTYPE} ${BUILD_MATCH} ${NO_PROG}

# All these RTL objects needs common headers (tbc)
${BUILD_RTL}: tm.h insn-modes.h gtype-desc.h insn-constants.h
# All these programs use the RTL reader ($(BUILD_RTL)).
${NO_RTL}: ${BUILD_RTL}
# All these programs use the MD reader
${NO_MD}: ${BUILD_MD}
# All these programs need to report errors.
${NO_PROGERR}: ${BUILD_ERRORS}

.for f in ${genprogrtl}
gen${f}.nx: ${BUILD_RTL}
.endfor

.for f in ${genprogmd}
gen${f}.nx: ${BUILD_MD}
.endfor

.for f in ${genprogerr}
gen${f}.nx: ${BUILD_ERRORS}
.endfor

.for f in ${genprog}
GENTOOLS+= gen${f}.nx
.endfor

# For some reason, gcov-iov is an oddball
GENTOOLS+=	gcov-iov.nx
NO_LIST+=	gcov-iov.no

gengtype.nx: ${BUILD_GTYPE}
genmatch.nx: ${BUILD_MATCH} vec.no

.for f in ${GENTOOLS}
$f: ${f:.nx=.no} ${BUILD_LIBS_${f:R}} ${BUILD_LIBS}
	${NXCXX} ${NXCXXFLAGS} ${NXLDFLAGS} ${.ALLSRC:M*.n[oa]} \
		${NXLD_${f:R}} -o ${.TARGET}
.endfor

.for nofile in ${NO_LIST}
${nofile}: ${nofile:.no=.c}
	${NXCXX} ${NXCXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

gencheck.no: tm.h insn-constants.h
gencondmd.no: insn-constants.h

MD_DEPS=	${GCCDIR}/gcc/common.md ${md_file}
MD_DEPS_PLUS=	${MD_DEPS} insn-conditions.md

_MIC: .USE
	${MIC} ${.TARGET}.tmp ${.TARGET}
_PL: .USE
	${.OBJDIR}/${.ALLSRC:M*.nx} > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}
_MD: .USE
	${.OBJDIR}/${.ALLSRC:M*.nx} ${MD_DEPS} > ${.TARGET}.tmp
	${MIC} ${.TARGET}.tmp ${.TARGET}

simple_rtl_generated_h=	insn-attr.h insn-attr-common.h insn-codes.h \
			insn-config.h insn-flags.h
simple_rtl_generated_c=	insn-automata.c insn-emit.c insn-extract.c \
			insn-output.c insn-peep.c insn-recog.c
simple_generated_h=	$(simple_rtl_generated_h) insn-constants.h
simple_generated_c=	$(simple_rtl_generated_c) insn-enums.c insn-preds.c

.for f in ${simple_rtl_generated_h} ${simple_rtl_generated_c}
. for generator in gen${f:R:S/^insn-//}.nx
$f: ${generator} ${MD_DEPS_PLUS} _MIC
	${.OBJDIR}/${generator} ${MD_DEPS_PLUS} > ${.TARGET}.tmp
. endfor
.endfor

insn-modes.h: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h > ${.TARGET}.tmp
gtyp-input.list: _MIC
	rm -f ${.TARGET}.tmp
	for f in ${GTFILES}; do \
		echo "$$f" >> ${.TARGET}.tmp; \
	done
gtype.state: gengtype.nx gtyp-input.list ${GTFILES:N[*]} _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -S ${GCCDIR}/gcc -I ${.ALLSRC:M*.list} \
	  -w gtype.state.tmp
gtype-desc.c gtype-desc.h: gengtype.nx gtype.state
	${.OBJDIR}/${.ALLSRC:M*.nx} -r gtype.state
genrtl.h: gengenrtl.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} > ${.TARGET}.tmp
min-insn-modes.c: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -m > ${.TARGET}.tmp
tm-preds.h: genpreds.nx ${MD_DEPS} _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h ${MD_DEPS} > ${.TARGET}.tmp
tm-constrs.h: genpreds.nx ${MD_DEPS} _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -c ${MD_DEPS} > ${.TARGET}.tmp
insn-constants.h:   genconstants.nx ${MD_DEPS} _MD
insn-enums.c:       genenums.nx ${MD_DEPS} _MD
insn-preds.c:       genpreds.nx ${MD_DEPS} _MD
gencondmd.c:        genconditions.nx ${MD_DEPS} tm-preds.h tm-constrs.h _MD
insn-conditions.md: gencondmd.nx _PL
insn-modes.c:       genmodes.nx _PL
tree-check.h:       gencheck.nx _PL
gcov-iov.h: gcov-iov.nx BASE-VER _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} '${GCCCOMPLETEVER}' '' > ${.TARGET}.tmp
target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "Target Hook" > ${.TARGET}.tmp
common/common-target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "Common Target Hook" > ${.TARGET}.tmp
c-family/c-target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "C Target Hook" > ${.TARGET}.tmp
stamp-opinit: genopinit.nx ${MD_DEPS_PLUS}
	${.OBJDIR}/${.ALLSRC:M*.nx} ${.ALLSRC:M*.md} \
		-hinsn-opinit.h.tmp -cinsn-opinit.c.tmp
	${MIC} insn-opinit.h.tmp insn-opinit.h
	${MIC} insn-opinit.c.tmp insn-opinit.c
	touch stamp-opinit
stamp-tabs: genattrtab.nx ${MD_DEPS_PLUS}
	${.OBJDIR}/${.ALLSRC:M*.nx} ${.ALLSRC:M*.md} \
		-Ainsn-attrtab.c.tmp -Dinsn-dfatab.c.tmp \
		-Linsn-latencytab.c.tmp
	${MIC} insn-attrtab.c.tmp insn-attrtab.c
	${MIC} insn-dfatab.c.tmp insn-dfatab.c
	${MIC} insn-latencytab.c.tmp insn-latencytab.c
	touch stamp-tabs
gimple-match.c: genmatch.nx match.pd _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} --gimple ${GCCDIR}/gcc/match.pd \
	> ${.TARGET}.tmp
generic-match.c: genmatch.nx match.pd _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} --generic ${GCCDIR}/gcc/match.pd \
	> ${.TARGET}.tmp
genmatch.c hash-table.c: gtype-desc.h

pass-instances.def: ${GCCDIR}/gcc/passes.def
	/usr/bin/awk -f ${GCCDIR}/gcc/gen-pass-instances.awk \
		${.ALLSRC} > ${.TARGET}

GENFILES=	${simple_generated_h} ${simple_generated_c} \
		tree-check.h genrtl.h insn-modes.h tm-preds.h tm-constrs.h \
		gtype-desc.c gtype-desc.h gcov-iov.h target-hooks-def.h \
		common/common-target-hooks-def.h pass-instances.def \
		c-family/c-target-hooks-def.h min-insn-modes.c \
		insn-modes.c insn-constants.h insn-conditions.md \
		gencondmd.c gimple-match.c generic-match.c \
		stamp-opinit stamp-tabs

CLEANFILES+=	${GENTOOLS} ${NO_LIST} gengtype-lex.c
CLEANFILES+=	${GENFILES} gtyp-input.list gtype.state
CLEANFILES+=	gt-* gtype-*.h pass-instances.def
CLEANFILES+=	insn-opinit.[ch] insn-*tab.c
CLEANDIRS+=	common c-family

dossier:
	mkdir -p common c-family

genfiles: dossier ${GENTOOLS} ${GENFILES}
depend all: genfiles

.include <bsd.obj.mk>
.include <bsd.dep.mk>
