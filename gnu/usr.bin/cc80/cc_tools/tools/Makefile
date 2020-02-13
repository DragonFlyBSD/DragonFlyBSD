# Despite the source files ending in ".c", the c++ compiler needs to build
# everything.  We have to roll our own targets to properly support this.

.include "../Makefile.inc"
.include "../../Makefile.langs"

NXCXXFLAGS+=		-DGENERATOR_FILE -I${.OBJDIR}
BUILD_LIBS=		../libiberty/libiberty.na
BUILD_LIBS_genmatch=	../libcpp/libcpp.na
LEX_HEAD=		../../cc_prep/gengtype-lex.head
NXLD_genautomata=	-lm

MIC=		sh ${GCCDIR}/move-if-change

BUILD_RTL=	rtl.no \
		read-rtl.no \
		ggc-none.no \
		vec.no \
		min-insn-modes.no \
		gensupport.no \
		print-rtl.no \
		hash-table.no
BUILD_MD=	read-md.no
BUILD_ERRORS=	errors.no
BUILD_GTYPE=	gengtype-lex.no \
		gengtype-parse.no \
		gengtype-state.no \
		version.no

genprogrtl=	attr attr-common attrtab automata codes conditions config emit \
		extract flags opinit output peep preds recog mddump target-def
genprogmd=	$(genprogrtl) mddeps constants enums
genprogerr=	$(genprogmd) genrtl modes gtype hooks cfn-macros match
genprog=	$(genprogerr) check checksum condmd

NO_RTL=		${genprogrtl:S/^/gen/g:S/$/.no/g}
NO_MD=		${genprogmd:S/^/gen/g:S/$/.no/g}
NO_PROGERR=	${genprogerr:S/^/gen/g:S/$/.no/g}
NO_PROG=	${genprog:S/^/gen/g:S/$/.no/g}
NO_LIST=	${BUILD_RTL} ${BUILD_MD} ${BUILD_ERRORS} \
		${BUILD_GTYPE} ${NO_PROG}

# All these RTL objects needs common headers (tbc)
${BUILD_RTL}: tm.h insn-modes.h insn-modes-inline.h gtype-desc.h insn-constants.h
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
genmatch.nx: hash-table.no vec.no
gencfn-macros.nx: hash-table.no vec.no ggc-none.no

.for f in ${GENTOOLS}
$f: ${f:.nx=.no} ${BUILD_LIBS_${f:R}} ${BUILD_LIBS}
	${NXCXX} ${NXCXXFLAGS:N-flto} ${NXLDFLAGS} ${.ALLSRC:M*.n[oa]} \
		${NXLD_${f:R}} -o ${.TARGET}
.endfor

.for nofile in ${NO_LIST}
${nofile}: ${nofile:.no=.c}
	${NXCXX} ${NXCXXFLAGS:N-flto} -c ${.IMPSRC} -o ${.TARGET}
.endfor

gencheck.no: tm.h insn-constants.h
gencondmd.no: insn-constants.h
read-md.no: insn-modes.h insn-modes-inline.h
gencfn-macros.no: insn-modes.h gtype-desc.h

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
			insn-config.h insn-flags.h insn-target-def.h
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
insn-modes-inline.h: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -i > ${.TARGET}.tmp
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
case-cfn-macros.h: gencfn-macros.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -c > ${.TARGET}.tmp
cfn-operators.pd: gencfn-macros.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -o > ${.TARGET}.tmp
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
gimple-match.c: genmatch.nx match.pd cfn-operators.pd gimple-match-head.c _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} --gimple ${GCCDIR}/gcc/match.pd \
	> ${.TARGET}.tmp
generic-match.c: genmatch.nx match.pd cfn-operators.pd generic-match-head.c _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} --generic ${GCCDIR}/gcc/match.pd \
	> ${.TARGET}.tmp
genmatch.c hash-table.c: gtype-desc.h

pass-instances.def: ${GCCDIR}/gcc/passes.def $(PASSES_EXTRA)
	/usr/bin/awk -f ${GCCDIR}/gcc/gen-pass-instances.awk \
		${.ALLSRC} > ${.TARGET}

# XXX plain cpp(1) safe? also /usr/bin/awk?
# XXX^2 cpp(1) as clang-cpp in traditional-cpp mode does not expand "FOO (blah)"
NXCCCPP?=	${NXCC} -E -P
params.list: ${GCCDIR}/gcc/params-list.h ${GCCDIR}/gcc/params.def
	${NXCCCPP} ${GCCDIR}/gcc/params-list.h | sed 's/^#.*//;/^$$/d' \
	> tmp-params.list
	${MIC} tmp-params.list params.list

params.options: ${GCCDIR}/gcc/params-options.h ${GCCDIR}/gcc/params.def
	${NXCCCPP} ${GCCDIR}/gcc/params-options.h | sed 's/^#.*//;/^$$/d' \
	> tmp-params.options
	${MIC} tmp-params.options params.options

# This is gold, don't ask.
gengtype-lex.c: gengtype-lex.l ${LEX_HEAD}
	${LEX} ${LFLAGS} -o${.TARGET}.tmp ${.IMPSRC}
	cat ${LEX_HEAD} ${.TARGET}.tmp > ${.TARGET}

GENFILES=	${simple_generated_h} ${simple_generated_c} \
		tree-check.h genrtl.h insn-modes.h insn-modes-inline.h \
		tm-preds.h tm-constrs.h \
		gtype-desc.c gtype-desc.h gcov-iov.h target-hooks-def.h \
		common/common-target-hooks-def.h pass-instances.def \
		c-family/c-target-hooks-def.h min-insn-modes.c \
		insn-modes.c insn-constants.h insn-conditions.md \
		gencondmd.c gimple-match.c generic-match.c \
		case-cfn-macros.h cfn-operators.pd \
		params.list params.options \
		stamp-opinit stamp-tabs

CLEANFILES+=	${GENTOOLS} ${NO_LIST}
CLEANFILES+=	gengtype-lex.c gengtype-lex.c.tmp
CLEANFILES+=	${GENFILES} gtyp-input.list gtype.state
CLEANFILES+=	gt-* gtype-*.h pass-instances.def
CLEANFILES+=	insn-opinit.[ch] insn-*tab.c
CLEANFILES+=	params.list params.options tmp-params.list tmp-params.options
CLEANDIRS+=	common c-family

dossier:
	mkdir -p common c-family

.if defined(LIBGCC_ONLY)
GENLIBGCC=	${GENTOOLS:Mgenconstants*} ${GENTOOLS:Mgenmodes*} \
		${GENTOOLS:Mgcov-iov*} \
		${GENFILES:Minsn-constants.h} ${GENFILES:Minsn-modes.h} \
		${GENFILES:Mgcov-iov.h}
genfiles: dossier ${GENLIBGCC}
.else
genfiles: dossier ${GENTOOLS} ${GENFILES}
.endif
depend all: genfiles

.include <bsd.obj.mk>
.include <bsd.dep.mk>
.include <bsd.sys.mk>
