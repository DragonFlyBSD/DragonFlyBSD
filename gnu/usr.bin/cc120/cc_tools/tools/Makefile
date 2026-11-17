.include "../Makefile.inc"
.include "../../Makefile.langs"

NXCXXFLAGS+=		-DGENERATOR_FILE -I${.OBJDIR}
NXCXXFLAGS+=		-I${GCCDIR}/libcpp/include
BUILD_LIBS=		../libiberty/libiberty.na
BUILD_LIBS_genmatch=	../libcpp/libcpp.na
NXLD_genautomata=	-lm

.if ${NXCXXFLAGS:M-flto}
NXCXXFLAGS+=	-fno-lto
.endif

MIC=		sh ${GCCDIR}/move-if-change

# derived from gcc/Makefile.in
BUILD_RTL=	rtl.no \
		read-rtl.no \
		ggc-none.no \
		vec.no \
		min-insn-modes.no \
		gensupport.no \
		print-rtl.no \
		hash-table.no \
		sort.no
BUILD_MD=	read-md.no
BUILD_ERRORS=	errors.no

# From gcc/Makefile.in
# Note: swapped 'condmd' with 'match' to correct the dependency.
genprogrtl=	attr attr-common attrtab automata codes conditions config emit \
		extract flags mddump opinit output peep preds recog target-def
genprogmd=	$(genprogrtl) mddeps constants enums
genprogerr=	$(genprogmd) genrtl modes gtype hooks cfn-macros match
genprog=	$(genprogerr) check checksum condmd

NO_RTL=		${genprogrtl:S/^/gen/g:S/$/.no/g}
NO_MD=		${genprogmd:S/^/gen/g:S/$/.no/g}
NO_PROGERR=	${genprogerr:S/^/gen/g:S/$/.no/g}
NO_PROG=	${genprog:S/^/gen/g:S/$/.no/g}

# All these programs use the RTL reader ($(BUILD_RTL)).
${NO_RTL}: ${BUILD_RTL}
.for f in ${genprogrtl}
gen${f}.nx: ${BUILD_RTL}
.endfor

# All these programs use the MD reader ($(BUILD_MD)).
${NO_MD}: ${BUILD_MD}
.for f in ${genprogmd}
gen${f}.nx: ${BUILD_MD}
.endfor

# All these programs need to report errors.
${NO_PROGERR}: ${BUILD_ERRORS}
.for f in ${genprogerr}
gen${f}.nx: ${BUILD_ERRORS}
.endfor

# Filtered headers to be generated.
CORETYPES_H=	insn-modes.h insn-modes-inline.h
GTM_H=		insn-constants.h

# Program objects depend on ${CORETYPES_H}
genprog_coretypes=	${genprog:Nchecksum:Ngenrtl:Ngtype:Nmodes}
NO_CORETYPES=		${genprog_coretypes:S/^/gen/g:S/$/.no/g}
${NO_CORETYPES}: ${CORETYPES_H}

# Program objects except gen{checksum,constants,enums,genrtl,gtype,mddeps,modes}.no
# depend on ${GTM_H}
genprog_gtm=	${genprog:Nchecksum:Nconstants:Nenums:Ngenrtl:Ngtype:Nmddeps:Nmodes}
NO_GTM=		${genprog_gtm:S/^/gen/g:S/$/.no/g}
${NO_GTM}: ${GTM_H}

# Make these RTL objects depend on some common headers for simplicity.
# (roughly derived from gcc/Makefile.in)
${BUILD_RTL}: ${CORETYPES_H} ${GTM_H} genrtl.h gtype-desc.h

# Extra dependencies manually derived from gcc/Makefile.in
read-md.no inchash.no: ${CORETYPES_H}
genmatch.no: gtype-desc.h case-cfn-macros.h
gencfn-macros.no: gtype-desc.h

genrecog.nx: hash-table.no inchash.no
gencfn-macros.nx: hash-table.no vec.no ggc-none.no sort.no
genmatch.nx: hash-table.no vec.no sort.no
gengtype.nx: gengtype-lex.no gengtype-parse.no gengtype-state.no

NO_LIST=	inchash.no \
		gengtype-lex.no gengtype-parse.no gengtype-state.no
NO_LIST+=	${BUILD_RTL} ${BUILD_MD} ${BUILD_ERRORS}
NO_LIST+=	${NO_PROG}
.for nofile in ${NO_LIST}
${nofile}: ${nofile:.no=.cc}
.endfor

GENTOOLS=	${genprog:S/^/gen/g:S/$/.nx/g}
.for f in ${GENTOOLS}
$f: ${f:.nx=.no} ${BUILD_LIBS_${f:R}} ${BUILD_LIBS}
	${NXCXX} ${NXCXXFLAGS} ${NXLDFLAGS} ${.ALLSRC:M*.n[oa]} \
		${NXLD_${f:R}} -o ${.TARGET}
.endfor

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

# from gcc/Makefile.in
simple_rtl_generated_h=	insn-attr.h insn-attr-common.h insn-codes.h \
			insn-config.h insn-flags.h insn-target-def.h
simple_rtl_generated_c=	insn-automata.cc insn-emit.cc \
			insn-extract.cc insn-output.cc \
			insn-peep.cc insn-recog.cc
simple_generated_h=	$(simple_rtl_generated_h) insn-constants.h
simple_generated_c=	$(simple_rtl_generated_c) insn-enums.cc

${simple_generated_h} ${simple_generated_c}: ${MD_DEPS}
${simple_rtl_generated_h} ${simple_rtl_generated_c}: ${MD_DEPS_PLUS}

.for f in ${simple_generated_h} ${simple_generated_c}
. for generator in gen${f:R:S/^insn-//}.nx
$f: ${generator} _MIC
	${.OBJDIR}/${generator} ${.ALLSRC:M*.md} > ${.TARGET}.tmp
. endfor
.endfor

stamp-opinit: genopinit.nx ${MD_DEPS_PLUS}
	${.OBJDIR}/${.ALLSRC:M*.nx} ${.ALLSRC:M*.md} \
		-hinsn-opinit.h.tmp -cinsn-opinit.cc.tmp
	${MIC} insn-opinit.h.tmp insn-opinit.h
	${MIC} insn-opinit.cc.tmp insn-opinit.cc
	touch ${.TARGET}
stamp-attrtab: genattrtab.nx ${MD_DEPS_PLUS}
	${.OBJDIR}/${.ALLSRC:M*.nx} ${.ALLSRC:M*.md} \
		-Ainsn-attrtab.cc.tmp -Dinsn-dfatab.cc.tmp \
		-Linsn-latencytab.cc.tmp
	${MIC} insn-attrtab.cc.tmp insn-attrtab.cc
	${MIC} insn-dfatab.cc.tmp insn-dfatab.cc
	${MIC} insn-latencytab.cc.tmp insn-latencytab.cc
	touch ${.TARGET}

gencondmd.cc:		genconditions.nx ${MD_DEPS} tm-preds.h tm-constrs.h _MD
insn-conditions.md:	gencondmd.nx _PL
tree-check.h:		gencheck.nx _PL

genrtl.h: gengenrtl.nx _PL
insn-modes.cc: genmodes.nx _PL
insn-modes.h: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h > ${.TARGET}.tmp
insn-modes-inline.h: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -i > ${.TARGET}.tmp
min-insn-modes.cc: genmodes.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -m > ${.TARGET}.tmp

insn-preds.cc: genpreds.nx ${MD_DEPS} _MD
tm-preds.h: genpreds.nx ${MD_DEPS} _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -h ${MD_DEPS} > ${.TARGET}.tmp
tm-constrs.h: genpreds.nx ${MD_DEPS} _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -c ${MD_DEPS} > ${.TARGET}.tmp

case-cfn-macros.h: gencfn-macros.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -c > ${.TARGET}.tmp
cfn-operators.pd: gencfn-macros.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -o > ${.TARGET}.tmp

target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "Target Hook" > ${.TARGET}.tmp
common/common-target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "Common Target Hook" > ${.TARGET}.tmp
c-family/c-target-hooks-def.h: genhooks.nx _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} "C Target Hook" > ${.TARGET}.tmp

gimple-match.cc: genmatch.nx match.pd cfn-operators.pd gimple-match-head.cc _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} --gimple ${GCCDIR}/gcc/match.pd \
		> ${.TARGET}.tmp
generic-match.cc: genmatch.nx match.pd cfn-operators.pd generic-match-head.cc _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} --generic ${GCCDIR}/gcc/match.pd \
		> ${.TARGET}.tmp

gtyp-input.list: _MIC
	rm -f ${.TARGET}.tmp
	for f in ${GTFILES}; do \
		echo "$$f" >> ${.TARGET}.tmp; \
	done
gtype.state: gengtype.nx gtyp-input.list ${GTFILES:N[*]} _MIC
	${.OBJDIR}/${.ALLSRC:M*.nx} -S ${GCCDIR}/gcc -I ${.ALLSRC:M*.list} \
		-w ${.TARGET}.tmp
gtype-desc.cc gtype-desc.h: gengtype.nx gtype.state
	${.OBJDIR}/${.ALLSRC:M*.nx} -r gtype.state

# This rule originated from the GCC 5.0 work which used a development branch
# where the 'gengtype-lex.cc' file was unavailable.  Although a release tarball
# provides it, generating it is easy and saves ~100KB disk space.
gengtype-lex.cc: gengtype-lex.l gengtype-lex.head
	${LEX} ${LFLAGS} -o${.TARGET}.tmp ${GCCDIR}/gcc/gengtype-lex.l
	cat gengtype-lex.head ${.TARGET}.tmp > ${.TARGET}
gengtype-lex.head:
	echo '#ifdef HOST_GENERATOR_FILE'	>  ${.TARGET}
	echo '#include "config.h"'		>> ${.TARGET}
	echo '#else'				>> ${.TARGET}
	echo '#include "bconfig.h"'		>> ${.TARGET}
	echo '#endif'				>> ${.TARGET}

GENFILES=	${simple_generated_h} ${simple_generated_c} \
		stamp-opinit stamp-attrtab \
		gencondmd.cc insn-conditions.md tree-check.h \
		genrtl.h insn-modes.cc insn-modes.h \
		insn-modes-inline.h min-insn-modes.cc \
		insn-preds.cc tm-preds.h tm-constrs.h \
		case-cfn-macros.h cfn-operators.pd \
		target-hooks-def.h \
		common/common-target-hooks-def.h \
		c-family/c-target-hooks-def.h \
		gimple-match.cc generic-match.cc \
		gtype-desc.cc gtype-desc.h

CLEANFILES+=	${NO_LIST} ${GENTOOLS} ${GENFILES}
CLEANFILES+=	insn-opinit.* insn-*tab.cc
CLEANFILES+=	gtyp-input.list gtype.state gengtype-lex.*
CLEANFILES+=	gt-*.h gtype-*.h *.tmp
CLEANDIRS+=	common c-family

dossier:
	mkdir -p common c-family

.if defined(LIBGCC_ONLY)
# required by libgcc via tm.h
GENLIBGCC=	genconstants.nx genmodes.nx \
		insn-constants.h insn-modes.h
genfiles: dossier ${GENLIBGCC}
.else
genfiles: dossier ${GENTOOLS} ${GENFILES}
.endif
depend all: genfiles

.include <bsd.obj.mk>
.include <bsd.dep.mk>
.include <bsd.sys.mk>
