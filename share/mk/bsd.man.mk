# $FreeBSD: src/share/mk/bsd.man.mk,v 1.31.2.11 2002/12/19 13:48:33 ru Exp $
#
# The include file <bsd.man.mk> handles installing manual pages and
# their links.
#
#
# +++ variables +++
#
# DESTDIR	Change the tree where the man pages gets installed. [not set]
#
# MANDIR	Base path for manual installation. [${SHAREDIR}/man/man]
#
# MANOWN	Manual owner. [${SHAREOWN}]
#
# MANGRP	Manual group. [${SHAREGRP}]
#
# MANMODE	Manual mode. [${NOBINMODE}]
#
# MANSUBDIR	Subdirectory under the manual page section, i.e. "/i386"
#		or "/tahoe" for machine specific manual pages.
#
# MAN		The manual pages to be installed. For sections see
#		variable ${SECTIONS}
#
# MCOMPRESS_CMD	Program to compress man pages. Output is to
#		stdout. [${COMPRESS_CMD}]
#
# MLINKS	List of manual page links (using a suffix). The
#		linked-to file must come first, the linked file
#		second, and there may be multiple pairs. The files
#		are hard-linked.
#
# NOMANCOMPRESS	If you do not want unformatted manual pages to be
#		compressed when they are installed. [not set]
#
# NOMLINKS	If you do not want install manual page links. [not set]
#
# MANFILTER	command to pipe the raw man page through before compressing
#		or installing.  Can be used to do sed substitution.
#
# MANBUILDCAT	create preformatted manual pages in addition to normal
#		pages. [not set]
#
# MANDOC_CMD	command and flags to create preformatted pages
# MROFF_CMD	groff command for manlint and mandiff
#
# +++ targets +++
#
#	maninstall:
#		Install the manual pages and their links.
#

.if !target(__<bsd.init.mk>__)
.error bsd.man.mk cannot be included directly.
.endif

_MANINSTALLFLAGS:= ${MANINSTALLFLAGS}

MINSTALL?=	${INSTALL} -o ${MANOWN} -g ${MANGRP} -m ${MANMODE} \
		${_MANINSTALLFLAGS}

CATDIR=		${MANDIR:H:S/$/\/cat/}
CATEXT=		.cat
MANDOC_CMD?=	mandoc -Tascii
MROFF_CMD?=	groff -Tascii -mtty-char -man -t

MCOMPRESS_CMD?=	${COMPRESS_CMD}
MCOMPRESS_EXT?=	${COMPRESS_EXT}

SECTIONS=	1 2 3 4 5 6 7 8 9
.SUFFIXES:	${SECTIONS:S/^/./g}

# Backwards compatibility.
.if !defined(MAN)
.for sect in ${SECTIONS}
.if defined(MAN${sect}) && !empty(MAN${sect})
MAN+=	${MAN${sect}}
.endif
.endfor
.endif

_manpages:
all-man: _manpages

.if defined(NOMANCOMPRESS)

# Make special arrangements to filter to a temporary file at build time
# for NOMANCOMPRESS.
.if defined(MANFILTER)
FILTEXTENSION=		.filt
.else
FILTEXTENSION=
.endif

ZEXT=

.if defined(MANFILTER)
.if defined(MAN) && !empty(MAN)
CLEANFILES+=	${MAN:T:S/$/${FILTEXTENSION}/g}
CLEANFILES+=	${MAN:T:S/$/${CATEXT}${FILTEXTENSION}/g}
.for page in ${MAN}
.for target in ${page:T:S/$/${FILTEXTENSION}/g}
_manpages: ${target}
${target}: ${page}
	${MANFILTER} < ${.ALLSRC} > ${.TARGET}
.endfor
.if defined(MANBUILDCAT) && !empty(MANBUILDCAT)
.for target in ${page:T:S/$/${CATEXT}${FILTEXTENSION}/g}
_manpages: ${target}
${target}: ${page}
	${MANFILTER} < ${.ALLSRC} | ${MANDOC_CMD} > ${.TARGET}
.endfor
.endif
.endfor
.endif
.else
.if defined(MAN) && !empty(MAN)
CLEANFILES+=	${MAN:T:S/$/${CATEXT}/g}
.if defined(MANBUILDCAT) && !empty(MANBUILDCAT)
.for page in ${MAN}
.for target in ${page:T:S/$/${CATEXT}/g}
_manpages: ${target}
${target}: ${page}
	${MANDOC_CMD} ${.ALLSRC} > ${.TARGET}
.endfor
.endfor
.else
_manpages: ${MAN}
.endif
.endif
.endif

.else

ZEXT=		${MCOMPRESS_EXT}

.if defined(MAN) && !empty(MAN)
CLEANFILES+=	${MAN:T:S/$/${MCOMPRESS_EXT}/g}
CLEANFILES+=	${MAN:T:S/$/${CATEXT}${MCOMPRESS_EXT}/g}
.for page in ${MAN}
.for target in ${page:T:S/$/${MCOMPRESS_EXT}/}
_manpages: ${target}
${target}: ${page}
.if defined(MANFILTER)
	${MANFILTER} < ${.ALLSRC} | ${MCOMPRESS_CMD} > ${.TARGET}
.else
	${MCOMPRESS_CMD} ${.ALLSRC} > ${.TARGET}
.endif
.endfor
.if defined(MANBUILDCAT) && !empty(MANBUILDCAT)
.for target in ${page:T:S/$/${CATEXT}${MCOMPRESS_EXT}/}
_manpages: ${target}
${target}: ${page}
.if defined(MANFILTER)
	${MANFILTER} < ${.ALLSRC} | ${MANDOC_CMD} | ${MCOMPRESS_CMD} > ${.TARGET}
.else
	${MANDOC_CMD} ${.ALLSRC} | ${MCOMPRESS_CMD} > ${.TARGET}
.endif
.endfor
.endif
.endfor
.endif

.endif

.if !defined(NOMLINKS) && defined(MLINKS) && !empty(MLINKS)
.for _oname _osect _dname _dsect in ${MLINKS:C/\.([^.]*)$/.\1 \1/}
_MANLINKS+=	${MANDIR}${_osect}${MANSUBDIR}/${_oname} \
		${MANDIR}${_dsect}${MANSUBDIR}/${_dname}
.if defined(MANBUILDCAT) && !empty(MANBUILDCAT)
_MANLINKS+=	${CATDIR}${_osect}${MANSUBDIR}/${_oname} \
		${CATDIR}${_dsect}${MANSUBDIR}/${_dname}
.endif
.endfor
.endif

maninstall: _maninstall
_maninstall:
.if defined(MAN) && !empty(MAN)
_maninstall: ${MAN}
.if defined(NOMANCOMPRESS)
.if defined(MANFILTER)
.for page in ${MAN}
	${MINSTALL} ${page:T:S/$/${FILTEXTENSION}/g} \
		${DESTDIR}${MANDIR}${page:E}${MANSUBDIR}/${page}
.if defined(MANBUILDCAT) && !empty(MANBUILDCAT)
	${MINSTALL} ${page:T:S/$/${CATEXT}${FILTEXTENSION}/g} \
		${DESTDIR}${CATDIR}${page:E}${MANSUBDIR}/${page}
.endif
.endfor
.else	# !defined(MANFILTER)
	@set ${.ALLSRC:C/\.([^.]*)$/.\1 \1/}; \
	while : ; do \
		case $$# in \
			0) break;; \
			1) echo "warn: missing extension: $$1"; break;; \
		esac; \
		page=$$1; shift; sect=$$1; shift; \
		d=${DESTDIR}${MANDIR}$${sect}${MANSUBDIR}; \
		${ECHO} ${MINSTALL} $${page} $${d}; \
		${MINSTALL} $${page} $${d}; \
	done
.if defined(MANBUILDCAT) && !empty(MANBUILDCAT)
.for page in ${MAN}
	${MINSTALL} ${page:T:S/$/${CATEXT}/} \
		${DESTDIR}${CATDIR}${page:E}${MANSUBDIR}/${page:T}
.endfor
.endif
.endif	# defined(MANFILTER)
.else	# !defined(NOMANCOMPRESS)
.for page in ${MAN}
	${MINSTALL} ${page:T:S/$/${MCOMPRESS_EXT}/g} \
		${DESTDIR}${MANDIR}${page:E}${MANSUBDIR}
.if defined(MANBUILDCAT) && !empty(MANBUILDCAT)
	${MINSTALL} ${page:T:S/$/${CATEXT}${MCOMPRESS_EXT}/g} \
		${DESTDIR}${CATDIR}${page:E}${MANSUBDIR}/${page:T:S/$/${MCOMPRESS_EXT}/}
.endif
.endfor
.endif	# defined(NOMANCOMPRESS)
.endif
.for l t in ${_MANLINKS}
	@rm -f ${DESTDIR}${t} ${DESTDIR}${t}${MCOMPRESS_EXT}
	${LN} ${DESTDIR}${l}${ZEXT} ${DESTDIR}${t}${ZEXT}
.endfor

manlint:
#mandiff:
.if defined(MAN) && !empty(MAN)
.for page in ${MAN}
manlint: ${page}lint
#mandiff: ${page}diff
${page}lint: ${page}
.if defined(MANFILTER)
#	@${MANFILTER} < ${.ALLSRC} | ${MROFF_CMD} -ww -z
	@-${MANFILTER} < ${.ALLSRC} | ${MANDOC_CMD} -Tlint
.else
#	@${MROFF_CMD} -ww -z ${.ALLSRC}
	@-${MANDOC_CMD} -Tlint ${.ALLSRC}
.endif
#${page}.out.groff: ${page}
#	@-${MROFF_CMD} ${.ALLSRC} 2>&1 > ${.TARGET}
#${page}.out.mandoc: ${page}
#	@-${MANDOC_CMD} -Werror ${.ALLSRC} 2>&1 > ${.TARGET}
#${page}diff: ${page}.out.groff ${page}.out.mandoc
#	@-diff -au ${.ALLSRC}
#	@rm ${.ALLSRC}
.endfor
.endif
