#
# Generate crunched binaries using crunchgen(1).
#
# Make variables used to generate the crunchgen(1) config file:
#
# CRUNCH_SRCDIRS	Directories to search for included programs
# CRUNCH_PATH_${D}	Path to the source directory ${D}
# CRUNCH_PROGS_${D}	Programs to be included inside directory ${D}
# CRUNCH_LIBS		Libraries to be statically linked with
# CRUNCH_SHLIBS		Libraries to be dynamically linked with
# CRUNCH_BUILDOPTS	Build options to be added for every program
# CRUNCH_BUILDTOOLS	Programs that need build tools built in the
#			local architecture.
#
# Special options can be specified for individual programs:
#
# CRUNCH_SRCDIR_${P}	Base source directory for program ${P}
# CRUNCH_BUILDOPTS_${P}	Additional build options for ${P}
# CRUNCH_ALIAS_${P}	Additional names to be used for ${P}
#
# By default, any name appearing in CRUNCH_PROGS or CRUNCH_ALIAS_${P}
# will be used to generate a hard/soft link to the resulting binary.
# Specific links can be suppressed by setting
# CRUNCH_SUPPRESS_LINK_${NAME} to 1.
#
# If CRUNCH_GENERATE_LINKS is set to 'no', then no links will be generated.
# If CRUNCH_USE_SYMLINKS is defined, then soft links will be used instead
# of hard links.
#

# $FreeBSD: head/share/mk/bsd.crunchgen.mk 305257 2016-09-01 23:52:20Z bdrewery $


CONF=	${PROG}.conf
OUTMK=	${PROG}.mk
OUTC=	${PROG}.c
OUTPUTS=${OUTMK} ${OUTC} ${PROG}.cache
CRUNCHOBJS= ${.OBJDIR}
CRUNCH_GENERATE_LINKS?= yes
CRUNCH_LINKTYPE?= hard
.if defined(CRUNCH_USE_SYMLINKS)
CRUNCH_LINKTYPE= soft
.endif

CLEANFILES+= ${CONF} *.o *.lo *.c *.mk *.cache *.a *.h

# Set a default SRCDIR for each for simpler handling below.
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
CRUNCH_SRCDIR_${P}?=	${CRUNCH_PATH_${D}}/${D}/${P}
.endfor
.endfor

# Program names and their aliases that contribute links to crunched
# executable, except for the suppressed ones.
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
${OUTPUTS}: ${CRUNCH_SRCDIR_${P}}/Makefile
.if ${CRUNCH_GENERATE_LINKS} == "yes"
.ifndef CRUNCH_SUPPRESS_LINK_${P}
.if ${CRUNCH_LINKTYPE} == "soft"
SYMLINKS+= ${PROG} ${BINDIR}/${P}
.else
LINKS+= ${BINDIR}/${PROG} ${BINDIR}/${P}
.endif
.endif   # !CRUNCH_SUPPRESS_LINK_${P}
.for A in ${CRUNCH_ALIAS_${P}}
.ifndef CRUNCH_SUPPRESS_LINK_${A}
.if ${CRUNCH_LINKTYPE} == "soft"
SYMLINKS+= ${PROG} ${BINDIR}/${A}
.else
LINKS+= ${BINDIR}/${PROG} ${BINDIR}/${A}
.endif
.endif   # !CRUNCH_SUPPRESS_LINK_${A}
.endfor  # CRUNCH_ALIAS_${P}
.endif   # CRUNCH_GENERATE_LINKS
.endfor  # CRUNCH_PROGS_${D}
.endfor  # CRUNCH_SRCDIRS

.if !defined(_SKIP_BUILD)
all: ${PROG}
.endif
exe: ${PROG}

${CONF}: Makefile
	echo "# Auto-generated, do not edit" >${.TARGET}
.ifdef CRUNCH_BUILDOPTS
	echo "buildopts ${CRUNCH_BUILDOPTS}" >>${.TARGET}
.endif
.ifdef CRUNCH_LIBS
	echo "libs ${CRUNCH_LIBS}" >>${.TARGET}
.endif
.ifdef CRUNCH_SHLIBS
	echo "libs_so ${CRUNCH_SHLIBS}" >>${.TARGET}
.endif
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
	echo "progs ${P}" >>${.TARGET}
	echo "special ${P} srcdir ${CRUNCH_SRCDIR_${P}}" >>${.TARGET}
.ifdef CRUNCH_BUILDOPTS_${P}
	echo "special ${P} buildopts DIRPRFX=${DIRPRFX}${P}/ ${CRUNCH_BUILDOPTS_${P}}" >>${.TARGET}
.else
	echo "special ${P} buildopts DIRPRFX=${DIRPRFX}${P}/" >>${.TARGET}
.endif
.ifdef CRUNCH_KEEP_${P}
	echo "special ${P} keep ${CRUNCH_KEEP_${P}}" >>${.TARGET}
.endif
.for A in ${CRUNCH_ALIAS_${P}}
	echo "ln ${P} ${A}" >>${.TARGET}
.endfor
.endfor
.endfor

CRUNCHGEN?= crunchgen
CRUNCHENV?=	# empty
.ORDER: ${OUTPUTS} objs
${OUTPUTS:[1]}: .META
${OUTPUTS:[2..-1]}: .NOMETA
${OUTPUTS}: ${CONF}
	MAKE="${MAKE}" ${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} \
	    ${CRUNCHGEN} -fq -m ${OUTMK} -c ${OUTC} ${CONF}
	# Avoid redundantly calling 'make objs' which we've done by our
	# own dependencies.
	sed -i '' -e "s/^\(${PROG}:.*\) \$$(SUBMAKE_TARGETS)/\1/" ${OUTMK}

# These 2 targets cannot use .MAKE since they depend on the generated
# ${OUTMK} above.
${PROG}: ${OUTPUTS} objs .NOMETA .PHONY
	${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} \
	    ${MAKE} .MAKE.MODE="${.MAKE.MODE} curdirOk=yes" \
	    .MAKE.META.IGNORE_PATHS="${.MAKE.META.IGNORE_PATHS}" \
	    -f ${OUTMK} exe

objs: ${OUTMK} .META
	${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} \
	    ${MAKE} -f ${OUTMK} objs

# <sigh> Someone should replace the bin/csh build-tools with
# shell scripts so we can remove this nonsense.
.for _tool in ${CRUNCH_BUILDTOOLS}
build-tools-${_tool}:
	${_+_}cd ${.CURDIR}/../../../${_tool}; \
	    ${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} ${MAKE} obj; \
	    ${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} ${MAKE} build-tools
build-tools: build-tools-${_tool}
.endfor

# Use a separate build tree to hold files compiled for this crunchgen binary
# Yes, this does seem to partly duplicate bsd.subdir.mk, but I can't
# get that to cooperate with bsd.prog.mk.  Besides, many of the standard
# targets should NOT be propagated into the components.
.for __target in clean cleandepend cleandir obj objlink
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
${__target}_crunchdir_${P}: .PHONY .MAKE
	${_+_}cd ${CRUNCH_SRCDIR_${P}} && \
	    ${CRUNCHENV} MAKEOBJDIRPREFIX=${CANONICALOBJDIR} ${MAKE} \
	    DIRPRFX=${DIRPRFX}${P}/ ${CRUNCH_BUILDOPTS} ${__target}
${__target}: ${__target}_crunchdir_${P}
.endfor
.endfor
.endfor

clean:
	rm -f ${CLEANFILES}
	${_+_}if [ -e ${.OBJDIR}/${OUTMK} ]; then		\
		${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS}	\
		    ${MAKE} -f ${OUTMK} clean;			\
	fi
