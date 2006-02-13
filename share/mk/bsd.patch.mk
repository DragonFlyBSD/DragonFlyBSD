# $DragonFly: src/share/mk/bsd.patch.mk,v 1.1 2006/02/13 13:27:20 corecode Exp $
#
# The include file <bsd.patch.mk> handles patching of files and sources.
#
# It is advisable to include this file before a rule which deduces ${OBJS}
# from ${SRCS}.
#
# +++ variables +++
#
# SRCS		List of source files, especially patches (*.patch)
#
# CONTRIBDIR	Location of source files to which the patch files are relative
#
# Patch files are added to ${SRCS} and follow the following patterns:
#   *.no_obj.patch	The patched source file won't be added to ${SRCS}.
#   *.h.patch		The patched source file is a header and will be
#			added to ${SRCS}.
#   *.patch		The patched source file will be compiled to an object
#			and will be added to ${SRCS}.
#
# All commas contained in the patch filename will be replaced to slashes to
# form a path in a subdir.
#
# Example:
#
# CONTRIBDIR=	${.CURDIR}/../../contrib/foo-1.4/src
# SRCS+=	foomain.c.patch include,fooconf.h.patch
#
# This will patch ${CONTRIBDIR}/foomain.c with foomain.c.patch and add
# the patched foomain.c to ${SRCS}.
# The file ${CONTRIBDIR}/include/fooconf.h will be patched with
# include,fooconf.h.patch;  the patched file will be created in
# ${.OBJDIR}/include/fooconf.h and will be added to ${SRCS}.
#

.if !target(__<bsd.init.mk>__)
.error bsd.patch.mk cannot be included directly.
.endif

.if defined(SRCS)
CLEANFILES?=

.for _PSRC in ${SRCS:M*.no_obj.patch}
.for _PC in ${_PSRC:T:C/(\.no_obj)?\.patch$//:S|,|/|g}

${_PC}: ${CONTRIBDIR}/${_PC} ${_PSRC}
	mkdir -p ${.TARGET:H}
	patch -o ${.TARGET} -i ${.ALLSRC:M*.patch} ${CONTRIBDIR}/${.TARGET}

beforedepend: ${PC_}

CLEANFILES:=	${CLEANFILES} ${_PC}
.endfor
.endfor

.for _PSRC in ${SRCS:N*.no_obj.patch:M*.patch}
.for _PC in ${_PSRC:T:C/(\.no_obj)?\.patch$//:S|,|/|g}

${_PC}: ${CONTRIBDIR}/${_PC} ${_PSRC}
	mkdir -p ${.TARGET:H}
	patch -o ${.TARGET} -i ${.ALLSRC:M*.patch} ${CONTRIBDIR}/${.TARGET}

SRCS:=	${SRCS:N${_PC}:S|${_PSRC}|${_PC}|}
CLEANFILES:=	${CLEANFILES} ${_PC}
.endfor
.endfor
.endif
