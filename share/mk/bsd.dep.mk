# $FreeBSD: src/share/mk/bsd.dep.mk,v 1.27.2.3 2002/12/23 16:33:37 ru Exp $
#
# The include file <bsd.dep.mk> handles Makefile dependencies.
#
#
# +++ variables +++
#
# CTAGS		A tags file generation program [gtags]
#
# CTAGSFLAGS	Options for ctags(1) [not set]
#
# DEPENDFILE	dependencies file [.depend]
#
# GTAGSFLAGS	Options for gtags(1) [-o]
#
# HTAGSFLAGS	Options for htags(1) [not set]
#
# MKDEP		Options for ${MKDEPCMD} [not set]
#
# MKDEPCMD	Makefile dependency list program [mkdep]
#
# MKDEPINTDEPS	Extra internal dependencies for intermediates [not set]
#
# SRCS          List of source files (c, c++, assembler)
#
#
# +++ targets +++
#
#	cleandepend:
#		Remove depend and tags file
#
#	depend:
#		Make the dependencies for the source files, and store
#		them in the file ${DEPENDFILE}.
#
#	tags:
#		In "ctags" mode, create a tags file for the source files.
#		In "gtags" mode, create a (GLOBAL) gtags file for the
#		source files.  If HTML is defined, htags(1) is also run
#		after gtags(1).

.if !target(__<bsd.init.mk>__)
.error bsd.dep.mk cannot be included directly.
.endif

CTAGS?=		gtags
CTAGSFLAGS?=
GTAGSFLAGS?=	-o
HTAGSFLAGS?=

MKDEPCMD?=	mkdep
DEPENDFILE?=	.depend

# Keep `tags' here, before SRCS are mangled below for `depend'.
.if !target(tags) && defined(SRCS) && !defined(NOTAGS)
tags: ${SRCS}
.if ${CTAGS:T} == "ctags"
	@${CTAGS} ${CTAGSFLAGS} -f /dev/stdout \
	    ${.ALLSRC:N*.h} | sed "s;${.CURDIR}/;;" > ${.TARGET}
.elif ${CTAGS:T} == "gtags"
	@cd ${.CURDIR} && ${CTAGS} ${GTAGSFLAGS} ${.OBJDIR}
.if defined(HTML)
	@cd ${.CURDIR} && htags ${HTAGSFLAGS} -d ${.OBJDIR} ${.OBJDIR}
.endif
.endif
.endif

.if defined(SRCS)
CLEANFILES?=

.for _LSRC in ${SRCS:M*.l:N*/*}
.for _LC in ${_LSRC:R}.c
${_LC}: ${_LSRC}
	${LEX} -t ${LFLAGS} ${.ALLSRC} > ${.TARGET}
SRCS:=	${SRCS:S/${_LSRC}/${_LC}/}
CLEANFILES+= ${_LC}
.endfor
.endfor

.for _YSRC in ${SRCS:M*.y:N*/*}
.for _YC in ${_YSRC:R}.c
SRCS:=	${SRCS:S/${_YSRC}/${_YC}/}
CLEANFILES+= ${_YC}
.if !empty(YFLAGS:M-d) && !empty(SRCS:My.tab.h)
.ORDER: ${_YC} y.tab.h
${_YC} y.tab.h: ${_YSRC}
	${YACC} ${YFLAGS} ${.ALLSRC}
	cp y.tab.c ${_YC}
CLEANFILES+= y.tab.c y.tab.h
.elif !empty(YFLAGS:M-d)
.for _YH in ${_YC:S/.c/.h/}
${_YH}: ${_YC}
${_YC}: ${_YSRC}
	${YACC} ${YFLAGS} -o ${_YC} ${.ALLSRC}
SRCS+= ${_YH}
CLEANFILES+= ${_YH}
.endfor
.else
${_YC}: ${_YSRC}
	${YACC} ${YFLAGS} -o ${_YC} ${.ALLSRC}
.endif
.endfor
.endfor
.endif

.if !target(depend)
.if defined(SRCS)
depend: beforedepend _dependincs ${DEPENDFILE} afterdepend

# Tell bmake not to look for generated files via .PATH
.NOPATH: ${DEPENDFILE}

# Different types of sources are compiled with slightly different flags.
# Split up the sources, and filter out headers and non-applicable flags.
# Separate flag groups out of the sources and treat them differently.
# The special "_" group is for all files not in any group.
__FLAGS=
__FLAGS_FILES=	${SRCS}
.for _FG in ${FLAGS_GROUPS}
.for _FFILE in ${${_FG}_FLAGS_FILES}
__FLAGS_FILES:=	${__FLAGS_FILES:N${_FFILE}}
.endfor
.endfor

_DEPENDFILES=	${FLAGS_GROUPS:S/^/.depend_/g}

${DEPENDFILE}: ${_DEPENDFILES}

#
# __FLAG_FILES is built from SRCS.  That means it will contain
# also .h files and other files that are not direct sources, but which
# might be required to even run mkdep.  This is important if those are
# generated as well, like some forwarding headers.
#
# We'll have to pass these "sources" on to the other .depend_ file targets,
# since otherwise they might be run before the generated sources are
# generated.  _ALL_DEPENDS captures all files in SRCS that are not handled
# by the mkdep calls, i.e. all sources that are not being used directly
# for the .depend* file.
#
_ALL_DEPENDS=${__FLAGS_FILES:N*.[sS]:N*.c:N*.cc:N*.C:N*.cpp:N*.cpp:N*.cxx:N*.m}

.for _FG in _ ${FLAGS_GROUPS}
.depend${_FG:S/^/_/:N__}: ${${_FG}_FLAGS_FILES} ${_ALL_DEPENDS}
	-rm -f ${.TARGET}
	-> ${.TARGET}
.if ${${_FG}_FLAGS_FILES:M*.[csS]} != ""
	${MKDEPCMD} -f ${.TARGET} -a ${MKDEP} \
	    ${${_FG}_FLAGS} \
	    ${CFLAGS:M-nostdinc*} ${CFLAGS:M-[BID]*} \
	    ${CFLAGS:M-std=*} \
	    ${.ALLSRC:M*.[csS]}
.endif
.if ${${_FG}_FLAGS_FILES:M*.cc} != "" || \
    ${${_FG}_FLAGS_FILES:M*.C} != "" || \
    ${${_FG}_FLAGS_FILES:M*.cpp} != "" || \
    ${${_FG}_FLAGS_FILES:M*.cxx} != ""
	${MKDEPCMD} -f ${.TARGET} -a ${MKDEP} \
	    ${${_FG}_FLAGS} \
	    ${CXXFLAGS:M-nostdinc*} ${CXXFLAGS:M-[BID]*} \
	    ${CXXFLAGS:M-std=*} \
	    ${.ALLSRC:M*.cc} ${.ALLSRC:M*.C} ${.ALLSRC:M*.cpp} ${.ALLSRC:M*.cxx}
.endif
.if ${${_FG}_FLAGS_FILES:M*.m} != ""
	${MKDEPCMD} -f ${.TARGET} -a ${MKDEP} \
	    ${${_FG}_FLAGS} \
	    ${OBJCFLAGS:M-nostdinc*} ${OBJCFLAGS:M-[BID]*} \
	    ${OBJCFLAGS:M-Wno-import*} \
	    ${.ALLSRC:M*.m}
.endif
.if !empty(${_FG:M_}) && !empty(_DEPENDFILES)
	cat ${_DEPENDFILES} >> ${.TARGET}
.endif
.if defined(MKDEPINTDEPS) && !empty(MKDEPINTDEPS)
. for _ITD in ${MKDEPINTDEPS:O:u}
	TMP=_depend$$$$; \
	sed -e "s,${_ITD:C/^(.*:)(.*)/\1,\2 \1/},g" < ${.TARGET} > $$TMP; \
	mv $$TMP ${.TARGET}
. endfor
.endif
.endfor

.if target(_EXTRADEPEND)
_EXTRADEPEND: .USE
${DEPENDFILE}: _EXTRADEPEND
.endif

.else
depend: beforedepend _dependincs afterdepend
.endif
.if !target(beforedepend)
beforedepend:
.endif
.if !target(afterdepend)
afterdepend:
.endif
.endif

.if !target(cleandepend)
cleandepend:
.if defined(SRCS)
.if ${CTAGS:T} == "ctags"
	rm -f ${DEPENDFILE} ${_DEPENDFILES} tags
.elif ${CTAGS:T} == "gtags"
	rm -f ${DEPENDFILE} ${_DEPENDFILES} GPATH GRTAGS GSYMS GTAGS
.if defined(HTML)
	rm -rf HTML
.endif
.endif
.endif
.endif

.if !target(checkdpadd) && (defined(DPADD) || defined(LDADD))
checkdpadd:
.if ${OBJFORMAT} != aout
	@ldadd=`echo \`for lib in ${DPADD} ; do \
		echo $$lib | sed 's;^/usr/lib/lib\(.*\)\.a;-l\1;' ; \
	done \`` ; \
	ldadd1=`echo ${LDADD}` ; \
	if [ "$$ldadd" != "$$ldadd1" ] ; then \
		echo ${.CURDIR} ; \
		echo "DPADD -> $$ldadd" ; \
		echo "LDADD -> $$ldadd1" ; \
	fi
.else
	@dpadd=`echo \`ld -Bstatic -f ${LDADD}\`` ; \
	if [ "$$dpadd" != "${DPADD}" ] ; then \
		echo ${.CURDIR} ; \
		echo "LDADD -> $$dpadd" ; \
		echo "DPADD =  ${DPADD}" ; \
	fi
.endif
.endif

.if defined(INCS) && make(depend)
_dependincs: buildincludes .WAIT installincludes
.else
_dependincs:
.endif

.ORDER: beforedepend _dependincs ${DEPENDFILE} afterdepend
