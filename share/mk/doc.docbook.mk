#
# $DragonFly: doc/share/mk/doc.docbook.mk,v 1.1.1.1 2004/04/02 09:36:34 hmp Exp $
# $DragonFly: doc/share/mk/doc.docbook.mk,v 1.1.1.1 2004/04/02 09:36:34 hmp Exp $
#
# This include file <doc.docbook.mk> handles building and installing of
# DocBook documentation in the DragonFlyBSD Documentation Project.
#
# Documentation using DOCFORMAT=docbook is expected to be marked up
# according to the DocBook DTD
#

# ------------------------------------------------------------------------
#
# Document-specific variables
#
#	DOC		This should be set to the name of the DocBook
#			marked-up file, without the .sgml or .docb suffix.
#			
#			It also determins the name of the output files -
#			${DOC}.html.
#
#	DOCBOOKSUFFIX	The suffix of your document, defaulting to .sgml
#
#	SRCS		The names of all the files that are needed to
#			build this document - This is useful if any of
#			them need to be generated.  Changing any file in
#			SRCS causes the documents to be rebuilt.
#
#       HAS_INDEX       This document has index terms and so an index
#                       can be created if specified with GEN_INDEX.
#

# ------------------------------------------------------------------------
#
# Variables used by both users and documents:
#
#	SGMLFLAGS	Additional options to pass to various SGML
#			processors (e.g., jade, nsgmls).  Typically
#			used to define "IGNORE" entities to "INCLUDE"
#			 with "-i<entity-name>"
#
#	JADEFLAGS	Additional options to pass to Jade.  Typically
#			used to set additional variables, such as
#			"%generate-article-toc%".
#
#	TIDYFLAGS	Additional flags to pass to Tidy.  Typically
#			used to set "-raw" flag to handle 8bit characters.
#
#	EXTRA_CATALOGS	Additional catalog files that should be used by
#			any SGML processing applications.
#
#	NO_TIDY		If you do not want to use tidy, set this to "YES".
#
#       GEN_INDEX       If this document has an index (HAS_INDEX) and this
#                       variable is defined, then index.sgml will be added 
#                       to the list of dependencies for source files, and 
#                       collateindex.pl will be run to generate index.sgml.
#
#	CSS_SHEET	Full path to a CSS stylesheet suitable for DocBook.
#			Default is ${DOC_PREFIX}/share/misc/docbook.css
#
# Print-output options :
#
#       NICE_HEADERS    If defined, customized chapter headers will be created
#			that you may find more aesthetically pleasing.	Note
#			that this option only effects print output formats for
#			Enlish language books.
#
#       MIN_SECT_LABELS If defined, do not display the section number for 4th
#                       and 5th level section titles.  This would change 
#                       "N.N.N.N Section title" into "Section Title" while
#                       higher level sections are still printed with numbers.
#
#       TRACE={1,2}     Trace TeX's memory usage.  Set this to 1 for minimal
#                       tracing or 2 for maximum tracing.  TeX memory 
#                       statistics will be written out to <filename>.log.
#                       For more information see the TeXbook, p301.
#
#       TWO_SIDE        If defined, two sided output will be created.  This 
#                       means that new chapters will only start on odd 
#                       numbered (aka right side, aka recto) pages and the 
#                       headers and footers will be aligned appropriately 
#                       for double sided paper.  Blank pages may be added as
#                       needed.
#
#       JUSTIFY         If defined, text will be right justified so that the
#                       right edge is smooth.  Words may be hyphenated using
#                       the defalt TeX hyphenation rules for this purpose.
#
#       BOOK_OUTPUT     A collection of options are set suitable for printing
#                       a book.  This option may be an order of magnitude more
#                       CPU intensive than the default build.
#
#       RLE             Use Run-Length Encoding for EPS files, this will
#                       result in signficiantly smaller PostScript files, 
#                       but may take longer for a printer to process.
#
#       GREYSCALE_IMAGES Convert the screenshots to greyscale before
#                        embedding them into the PostScript output.
#

#
# Documents should use the += format to access these.
#

DOCBOOKSUFFIX?= sgml

MASTERDOC?=	${.CURDIR}/${DOC}.${DOCBOOKSUFFIX}

# Which stylesheet type to use.  'dsssl' or 'xsl'
STYLESHEET_TYPE?=	dsssl

.if ${MACHINE_ARCH} != "i386"
OPENJADE=	yes
.endif

.if defined(OPENJADE)
JADE?=		${PREFIX}/bin/openjade
JADECATALOG?=	${PREFIX}/share/sgml/openjade/catalog
NSGMLS?=	${PREFIX}/bin/onsgmls
JADEFLAGS+=	-V openjade
SX?=		${PREFIX}/bin/osx
.else
JADE?=		${PREFIX}/bin/jade
JADECATALOG?=	${PREFIX}/share/sgml/jade/catalog
NSGMLS?=	${PREFIX}/bin/nsgmls
NSGMLSWARNINGS=	-wempty -wunclosed
SX?=		${PREFIX}/bin/sx
.endif

DSLHTML?=	${DOC_PREFIX}/share/sgml/default.dsl
DSLPRINT?=	${DOC_PREFIX}/share/sgml/default.dsl
DSLPGP?=	${DOC_PREFIX}/share/sgml/pgp.dsl
DRAGONFLYBSDCATALOG=	${DOC_PREFIX}/share/sgml/catalog
LANGUAGECATALOG=${DOC_PREFIX}/${LANGCODE}/share/sgml/catalog

ISO8879CATALOG=	${PREFIX}/share/sgml/iso8879/catalog

.if ${STYLESHEET_TYPE} == "dsssl"
DOCBOOKCATALOG=	${PREFIX}/share/sgml/docbook/catalog
.elif ${STYLESHEET_TYPE} == "xsl"
DOCBOOKCATALOG= ${PREFIX}/share/xml/docbook/catalog
.endif

CATALOG_PORTS_SGML=	${PREFIX}/share/sgml/catalog.ports

DSSSLCATALOG=	${PREFIX}/share/sgml/docbook/dsssl/modular/catalog
COLLATEINDEX=	${PREFIX}/share/sgml/docbook/dsssl/modular/bin/collateindex.pl

XSLTPROC?=	${PREFIX}/bin/xsltproc
XSLHTML?=	${DOC_PREFIX}/share/xsl/dragonfly-html.xsl
XSLHTMLCHUNK?=	${DOC_PREFIX}/share/xsl/dragonfly-html-chunk.xsl
XSLFO?=		${DOC_PREFIX}/share/xsl/dragonfly-fo.xsl
INDEXREPORTSCRIPT= ${DOC_PREFIX}/share/misc/indexreport.pl

IMAGES_LIB?=

.for c in ${LANGUAGECATALOG} ${DRAGONFLYBSDCATALOG} ${DSSSLCATALOG} ${ISO8879CATALOG} ${DOCBOOKCATALOG} ${JADECATALOG} ${EXTRA_CATALOGS} ${CATALOG_PORTS_SGML}
.if exists(${c})
CATALOGS+=	-c ${c}
.endif
.endfor
SGMLFLAGS+=	-D ${CANONICALOBJDIR}
JADEOPTS=	${JADEFLAGS} ${SGMLFLAGS} ${CATALOGS}
XSLTPROCOPTS=	${XSLTPROCFLAGS}

KNOWN_FORMATS=	html html.tar html-split html-split.tar \
		txt rtf ps pdf tex dvi tar pdb

CSS_SHEET?=	${DOC_PREFIX}/share/misc/docbook.css
PDFTEX_DEF?=	${DOC_PREFIX}/share/web2c/pdftex.def
PDF_GENINFO?=	${DOC_PREFIX}/share/web2c/pdf_geninfo.sh

HTMLOPTS?=	-ioutput.html -d ${DSLHTML} ${HTMLFLAGS}

PRINTOPTS?=	-ioutput.print -d ${DSLPRINT} ${PRINTFLAGS}

.if defined(BOOK_OUTPUT)
NICE_HEADERS=1
MIN_SECT_LABELS=1
TWO_SIDE=1
JUSTIFY=1
#WITH_FOOTNOTES=1
#GEN_INDEX=1
.endif
.if defined(JUSTIFY)
TEXCMDS+=	\RequirePackage{url}
PRINTOPTS+=	-ioutput.print.justify
.endif
.if defined(TWO_SIDE)
PRINTOPTS+=	-V %two-side% -ioutput.print.twoside
TEXCMDS+=	\def\PageTwoSide{1}
.endif
.if defined(NICE_HEADERS)
PRINTOPTS+=    -ioutput.print.niceheaders
.endif
.if defined(MIN_SECT_LABELS)
PRINTOPTS+=    -V minimal-section-labels
.endif
.if defined(TRACE)
TEXCMDS+=	\tracingstats=${TRACE}
.endif
.if defined(RLE)
PNMTOPSFLAGS+=	-rle
.endif

.if !defined(WITH_INLINE_LEGALNOTICE) || empty(WITH_INLINE_LEGALNOTICE)
HTMLFLAGS+=	-V %generate-legalnotice-link%
.endif
.if defined(WITH_ARTICLE_TOC) && !empty(WITH_ARTICLE_TOC)
HTMLFLAGS+=	-V %generate-article-toc%
#PRINTFLAGS+=	-V %generate-article-toc%
.endif
.if defined(WITH_BIBLIOXREF_TITLE) && !empty(WITH_BIBLIOXREF_TITLE)
HTMLFLAGS+=	-V biblio-xref-title
PRINTFLAGS+=	-V biblio-xref-title
.endif
.if defined(WITH_DOCFORMAT_NAVI_LINK) && !empty(WITH_DOCFORMAT_NAVI_LINK)
HTMLFLAGS+=	-V %generate-docformat-navi-link%
.elif (${FORMATS:Mhtml} == "html") && (${FORMATS:Mhtml-split} == "html-split")
HTMLFLAGS+=	-V %generate-docformat-navi-link%
.endif
.if defined(WITH_ALL_TRADEMARK_SYMBOLS) && !empty(WITH_ALL_TRADEMARK_SYMBOLS)
HTMLFLAGS+=	-V %show-all-trademark-symbols%
PRINTFLAGS+=	-V %show-all-trademark-symbols%
.endif

PERL?=		/usr/bin/perl
PKG_CREATE?=	/usr/sbin/pkg_create
SORT?=		/usr/bin/sort
TAR?=		/usr/bin/tar
TOUCH?=		/usr/bin/touch
XARGS?=		/usr/bin/xargs

TEX?=		${PREFIX}/bin/tex
LATEX?=		${PREFIX}/bin/latex
PDFTEX?=	${PREFIX}/bin/pdflatex
JADETEX?=	${TEX} "&jadetex"
PDFJADETEX?=	${PDFTEX} "&pdfjadetex"
GROFF?=		groff
TIDY?=		${PREFIX}/bin/tidy
TIDYOPTS?=	-wrap 90 -m -raw -preserve -f /dev/null -asxml ${TIDYFLAGS}
HTML2TXT?=	${PREFIX}/bin/links
HTML2TXTOPTS?=	-dump ${HTML2TXTFLAGS}
HTML2PDB?=	${PREFIX}/bin/iSiloBSD
HTML2PDBOPTS?=	-y -d0 -Idef ${HTML2PDBFLAGS}
DVIPS?=		${PREFIX}/bin/dvips
.if defined(PAPERSIZE)
DVIPSOPTS?=	-t ${PAPERSIZE:L} ${DVIPSFLAGS}
.endif

GZIP?=	-9
GZIP_CMD?=	gzip -qf ${GZIP}
BZIP2?=	-9
BZIP2_CMD?=	bzip2 -qf ${BZIP2}
ZIP?=	-9
ZIP_CMD?=	${PREFIX}/bin/zip -j ${ZIP}

# ------------------------------------------------------------------------
#
# Look at ${FORMATS} and work out which documents need to be generated.
# It is assumed that the HTML transformation will always create a file
# called index.html, and that for every other transformation the name
# of the generated file is ${DOC}.format.
#
# ${_docs} will be set to a list of all documents that must be made
# up to date.
#
# ${CLEANFILES} is a list of files that should be removed by the "clean"
# target. ${COMPRESS_EXT:S/^/${DOC}.${_cf}.&/ takes the COMPRESS_EXT
# var, and prepends the filename to each listed extension, building a
# second list of files with the compressed extensions added.
#

# Note: ".for _curformat in ${KNOWN_FORMATS}" is used several times in
# this file. I know they could have been rolled together in to one, much
# larger, loop. However, that would have made things more complicated
# for a newcomer to this file to unravel and understand, and a syntax
# error in the loop would have affected the entire
# build/compress/install process, instead of just one of them, making it
# more difficult to debug.
#

# Note: It is the aim of this file that *all* the targets be available,
# not just those appropriate to the current ${FORMATS} and
# ${INSTALL_COMPRESSED} values.
#
# For example, if FORMATS=html and INSTALL_COMPRESSED=gz you could still
# type
#
#     make book.rtf.bz2
#
# and it will do the right thing. Or
#
#     make install-rtf.bz2
#
# for that matter. But don't expect "make clean" to work if the FORMATS
# and INSTALL_COMPRESSED variables are wrong.
#

.if ${.OBJDIR} != ${.CURDIR}
LOCAL_CSS_SHEET= ${.OBJDIR}/${CSS_SHEET:T}
.else
LOCAL_CSS_SHEET= ${CSS_SHEET:T}
.endif

.for _curformat in ${FORMATS}
_cf=${_curformat}

.if ${_cf} == "html-split"
_docs+= index.html HTML.manifest ln*.html
CLEANFILES+= $$([ -f HTML.manifest ] && ${XARGS} < HTML.manifest) \
		HTML.manifest ln*.html
CLEANFILES+= PLIST.${_curformat}

.else
_docs+= ${DOC}.${_curformat}
CLEANFILES+= ${DOC}.${_curformat}
CLEANFILES+= PLIST.${_curformat}

.if ${_cf} == "html-split.tar"
CLEANFILES+= $$([ -f HTML.manifest ] && ${XARGS} < HTML.manifest) \
		HTML.manifest ln*.html

.elif ${_cf} == "html.tar"
CLEANFILES+= ${DOC}.html

.elif ${_cf} == "txt"
CLEANFILES+= ${DOC}.html-text

.elif ${_cf} == "dvi"
CLEANFILES+= ${DOC}.aux ${DOC}.log ${DOC}.tex

.elif ${_cf} == "tex"
CLEANFILES+= ${DOC}.aux ${DOC}.log

.elif ${_cf} == "ps"
CLEANFILES+= ${DOC}.aux ${DOC}.dvi ${DOC}.log ${DOC}.tex-ps ${DOC}.tex
.for _curimage in ${LOCAL_IMAGES_EPS:M*share*}
CLEANFILES+= ${_curimage:T} ${_curimage:H:T}/${_curimage:T}
.endfor

.elif ${_cf} == "pdf"
CLEANFILES+= ${DOC}.aux ${DOC}.dvi ${DOC}.log ${DOC}.out ${DOC}.tex-pdf \
		${DOC}.tex
.for _curimage in ${IMAGES_PDF:M*share*}
CLEANFILES+= ${_curimage:T} ${_curimage:H:T}/${_curimage:T}
.endfor
.for _curimage in ${LOCAL_IMAGES_EPS:M*share*}
CLEANFILES+= ${_curimage:T} ${_curimage:H:T}/${_curimage:T}
.endfor

.elif ${_cf} == "pdb"
_docs+= ${.CURDIR:T}.pdb
CLEANFILES+= ${.CURDIR:T}.pdb

.endif
.endif

.if (${STYLESHEET_TYPE} == "xsl")
CLEANFILES+= .sxerr
.endif

.if (${LOCAL_CSS_SHEET} != ${CSS_SHEET}) && \
    (${_cf} == "html-split" || ${_cf} == "html-split.tar" || \
     ${_cf} == "html" || ${_cf} == "html.tar" || ${_cf} == "txt")
CLEANFILES+= ${LOCAL_CSS_SHEET}
.endif

.if !defined(WITH_INLINE_LEGALNOTICE) || empty(WITH_INLINE_LEGALNOTICE) && \
    (${_cf} == "html-split" || ${_cf} == "html-split.tar" || \
     ${_cf} == "html" || ${_cf} == "html.tar" || ${_cf} == "txt")
CLEANFILES+= LEGALNOTICE.html TRADEMARKS.html
.endif

.endfor		# _curformat in ${FORMATS} #


#
# Build a list of install-${format}.${compress_format} targets to be
# by "make install". Also, add ${DOC}.${format}.${compress_format} to
# ${_docs} and ${CLEANFILES} so they get built/cleaned by "all" and
# "clean".
#

.if defined(INSTALL_COMPRESSED) && !empty(INSTALL_COMPRESSED)
.for _curformat in ${FORMATS}
_cf=${_curformat}
.for _curcomp in ${INSTALL_COMPRESSED}

.if ${_cf} != "html-split" && ${_cf} != "html"
_curinst+= install-${_curformat}.${_curcomp}
_docs+= ${DOC}.${_curformat}.${_curcomp}
CLEANFILES+= ${DOC}.${_curformat}.${_curcomp}

.if  ${_cf} == "pdb"
_docs+= ${.CURDIR:T}.${_curformat}.${_curcomp}
CLEANFILES+= ${.CURDIR:T}.${_curformat}.${_curcomp}

.endif
.endif
.endfor
.endfor
.endif

#
# Index generation
#
CLEANFILES+= 		${INDEX_SGML}

.if defined(GEN_INDEX) && defined(HAS_INDEX)
JADEFLAGS+=		-i chap.index
HTML_SPLIT_INDEX?=	html-split.index
HTML_INDEX?=		html.index
PRINT_INDEX?=		print.index
INDEX_SGML?=		index.sgml

CLEANFILES+= 		${HTML_SPLIT_INDEX} ${HTML_INDEX} ${PRINT_INDEX}
.endif

.MAIN: all

all: ${_docs}

# XML --------------------------------------------------------------------

# sx generates a lot of (spurious) errors of the form "reference to
# internal SDATA entity ...".  So dump the errors to separate file, and
# grep for any other errors to show them to the user.
#
# Better approaches to handling this would be most welcome

.if !defined(CUSTOMIZED_XML)
${DOC}.xml: ${SRCS}
	echo '<!DOCTYPE book SYSTEM "/usr/local/share/xml/docbook/4.2/docbookx.dtd">' > ${DOC}.xml
	${SX} -xlower -xndata ${MASTERDOC} 2> .sxerr | tail -n +2 >> ${DOC}.xml 
	@-grep -v 'reference to internal SDATA entity' .sxerr
.endif

# HTML-SPLIT -------------------------------------------------------------

.if !defined(CUSTOMIZED_XML)
XSLTPROCFLAGS?=	--nonet --param dragonfly.output.html.images "'1'"
.else
XSLTPROCFLAGS?=
.endif

.if ${STYLESHEET_TYPE} == "dsssl"
index.html HTML.manifest: ${SRCS} ${LOCAL_IMAGES_LIB} ${LOCAL_IMAGES_PNG} \
			  ${LOCAL_IMAGES_TXT} ${INDEX_SGML} ${HTML_SPLIT_INDEX} ${LOCAL_CSS_SHEET}
	${JADE} -V html-manifest ${HTMLOPTS} -ioutput.html.images \
		${JADEOPTS} -t sgml ${MASTERDOC}
.elif ${STYLESHEET_TYPE} == "xsl"
index.html: ${DOC}.xml ${LOCAL_IMAGES_LIB} ${LOCAL_IMAGES_PNG} \
	#${INDEX_SGML} ${HTML_SPLIT_INDEX} ${LOCAL_CSS_SHEET}
	${XSLTPROC} ${XSLTPROCOPTS} ${XSLTPROCFLAGS} ${XSLHTMLCHUNK} \
		${DOC}.xml
.endif
.if !defined(NO_TIDY)
	-${TIDY} ${TIDYOPTS} $$(${XARGS} < HTML.manifest)
.endif

# HTML -------------------------------------------------------------------

.if ${STYLESHEET_TYPE} == "dsssl"
${DOC}.html: ${SRCS} ${LOCAL_IMAGES_LIB} ${LOCAL_IMAGES_PNG} \
	     ${LOCAL_IMAGES_TXT} ${INDEX_SGML} ${HTML_INDEX} ${LOCAL_CSS_SHEET}
	${JADE} -V nochunks ${HTMLOPTS} -ioutput.html.images \
		${JADEOPTS} -t sgml ${MASTERDOC} > ${.TARGET} || \
		(${RM} -f ${.TARGET} && false)
.elif ${STYLESHEET_TYPE} == "xsl"
${DOC}.html: ${DOC}.xml ${LOCAL_IMAGES_LIB} ${LOCAL_IMAGES_PNG} \
	${INDEX_SGML} ${LOCAL_CSS_SHEET}     
	${XSLTPROC} ${XSLTPROCOPTS} ${XSLTPROCFLAGS} ${XSLHTML} \
		${DOC}.xml > ${.TARGET}
.endif
.if !defined(NO_TIDY)
	-${TIDY} ${TIDYOPTS} ${.TARGET}
.endif

# HTML-TEXT --------------------------------------------------------------

# Special target to produce HTML with no images in it.
.if ${STYLESHEET_TYPE} == "dsssl"
${DOC}.html-text: ${SRCS} ${INDEX_SGML} ${HTML_INDEX} ${LOCAL_IMAGES_TXT}
	${JADE} -V nochunks ${HTMLOPTS} \
		${JADEOPTS} -t sgml ${MASTERDOC} > ${.TARGET} || \
		(${RM} -f ${.TARGET} && false)
.elif ${STYLESHEET_TYPE} == "xsl"
${DOC}.html-text: ${DOC}.xml ${INDEX_SGML} ${HTML_INDEX}
	${XSLTPROC} ${XSLTPROCOPTS} --param dragonfly.output.html.images "'0'" ${XSLHTML} \
		${DOC}.xml > ${.TARGET}
.endif

${DOC}.html-split.tar: HTML.manifest ${LOCAL_IMAGES_LIB} \
		       ${LOCAL_IMAGES_PNG} ${LOCAL_CSS_SHEET}
	${TAR} cf ${.TARGET} $$(${XARGS} < HTML.manifest) \
		${LOCAL_IMAGES_LIB} ${IMAGES_PNG:N*share*} ${CSS_SHEET:T}
.for _curimage in ${IMAGES_PNG:M*share*}
	${TAR} rf ${.TARGET} -C ${IMAGES_EN_DIR}/${DOC}s/${.CURDIR:T} ${_curimage:S|${IMAGES_EN_DIR}/${DOC}s/${.CURDIR:T}/||}
.endfor

${DOC}.html.tar: ${DOC}.html ${LOCAL_IMAGES_LIB} \
		 ${LOCAL_IMAGES_PNG} ${LOCAL_CSS_SHEET}
	${TAR} cf ${.TARGET} ${DOC}.html \
		${LOCAL_IMAGES_LIB} ${IMAGES_PNG:N*share*} ${CSS_SHEET:T}
.for _curimage in ${IMAGES_PNG:M*share*}
	${TAR} rf ${.TARGET} -C ${IMAGES_EN_DIR}/${DOC}s/${.CURDIR:T} ${_curimage:S|${IMAGES_EN_DIR}/${DOC}s/${.CURDIR:T}/||}
.endfor

# TXT --------------------------------------------------------------------

${DOC}.txt: ${DOC}.html-text
	${HTML2TXT} ${HTML2TXTOPTS} ${.ALLSRC} > ${.TARGET}

# PDB --------------------------------------------------------------------

${DOC}.pdb: ${DOC}.html ${LOCAL_IMAGES_LIB} ${LOCAL_IMAGES_PNG}
	${HTML2PDB} ${HTML2PDBOPTS} ${DOC}.html ${.TARGET}

${.CURDIR:T}.pdb: ${DOC}.pdb
	${LN} -f ${.ALLSRC} ${.TARGET}

.if defined(INSTALL_COMPRESSED) && !empty(INSTALL_COMPRESSED)
.for _curcomp in ${INSTALL_COMPRESSED}
${.CURDIR:T}.pdb.${_curcomp}: ${DOC}.pdb.${_curcomp}
	${LN} -f ${.ALLSRC} ${.TARGET}
.endfor
.endif

# RTF --------------------------------------------------------------------

${DOC}.rtf: ${SRCS} ${LOCAL_IMAGES_EPS} ${LOCAL_IMAGES_TXT}
	${JADE} -V rtf-backend ${PRINTOPTS} \
		${JADEOPTS} -t rtf -o ${.TARGET} ${MASTERDOC}

#
# This sucks, but there's no way round it.  The PS and PDF formats need
# to use different image formats, which are chosen at the .tex stage.  So,
# we need to create a different .tex file depending on our eventual output
# format, which will then lead on to a different .dvi file as well.
#

${DOC}.tex: ${SRCS} ${LOCAL_IMAGES_EPS} ${INDEX_SGML} ${PRINT_INDEX} \
		${LOCAL_IMAGES_TXT} ${LOCAL_IMAGES_EN}
	${JADE} -V tex-backend ${PRINTOPTS} \
		${JADEOPTS} -t tex -o ${.TARGET} ${MASTERDOC}

${DOC}.tex-ps: ${DOC}.tex
	${LN} -f ${.ALLSRC} ${.TARGET}

.if !target(${DOC}.tex-pdf)
${DOC}.tex-pdf: ${SRCS} ${IMAGES_PDF} ${INDEX_SGML} ${PRINT_INDEX} \
		${LOCAL_IMAGES_TXT}
	${RM} -f ${.TARGET}
	${CAT} ${PDFTEX_DEF} > ${.TARGET}
	/bin/sh ${PDF_GENINFO} >> ${.TARGET}
	${JADE} -V tex-backend ${PRINTOPTS} -ioutput.print.pdf \
		${JADEOPTS} -t tex -o /dev/stdout ${MASTERDOC} >> ${.TARGET}
.endif

${DOC}.dvi: ${DOC}.tex ${LOCAL_IMAGES_EPS}
.for _curimage in ${LOCAL_IMAGES_EPS:M*share*}
	${CP} -p ${_curimage} ${.CURDIR:H:H}/${_curimage:H:S|${IMAGES_EN_DIR}/||:S|${.CURDIR}||}
.endfor
	@${ECHO} "==> TeX pass 1/3"
	-${JADETEX} '${TEXCMDS} \nonstopmode\input{${DOC}.tex}'
	@${ECHO} "==> TeX pass 2/3"
	-${JADETEX} '${TEXCMDS} \nonstopmode\input{${DOC}.tex}'
	@${ECHO} "==> TeX pass 3/3"
	-${JADETEX} '${TEXCMDS} \nonstopmode\input{${DOC}.tex}'

.if !target(${DOC}.pdf)
${DOC}.pdf: ${DOC}.tex-pdf ${IMAGES_PDF}
.for _curimage in ${IMAGES_PDF:M*share*}
	${CP} -p ${_curimage} ${.CURDIR:H:H}/${_curimage:H:S|${IMAGES_EN_DIR}/||:S|${.CURDIR}||}
.endfor
	@${ECHO} "==> PDFTeX pass 1/3"
	-${PDFJADETEX} '${TEXCMDS} \nonstopmode\input{${DOC}.tex-pdf}'
	@${ECHO} "==> PDFTeX pass 2/3"
	-${PDFJADETEX} '${TEXCMDS} \nonstopmode\input{${DOC}.tex-pdf}'
	@${ECHO} "==> PDFTeX pass 3/3"
	${PDFJADETEX} '${TEXCMDS} \nonstopmode\input{${DOC}.tex-pdf}'
.endif

${DOC}.ps: ${DOC}.dvi
	${DVIPS} ${DVIPSOPTS} -o ${.TARGET} ${.ALLSRC}

${DOC}.tar: ${SRCS} ${LOCAL_IMAGES} ${LOCAL_CSS_SHEET}
	${TAR} cf ${.TARGET} -C ${.CURDIR} ${SRCS} \
		-C ${.OBJDIR} ${IMAGES} ${CSS_SHEET:T}

#
# Build targets for any formats we've missed that we don't handle.
#
.for _curformat in ${ALL_FORMATS}
.if !target(${DOC}.${_curformat})
${DOC}.${_curformat}:
	@${ECHO_CMD} \"${_curformat}\" is not a valid output format for this document.
.endif
.endfor


# ------------------------------------------------------------------------
#
# Validation targets
#

#
# Lets you quickly check that the document conforms to the DTD without
# having to convert it to any other formats
#

lint validate:
	${NSGMLS} ${NSGMLSWARNINGS} -s ${SGMLFLAGS} ${CATALOGS} ${MASTERDOC}


# ------------------------------------------------------------------------
#
# Index targets
#

#
# Generate a different .index file based on the format name
#
# If we're not generating an index (the default) then we need to create
# an empty index.sgml file so that we can reference index.sgml in book.sgml
#

${INDEX_SGML}:
	${PERL} ${COLLATEINDEX} -N -o ${.TARGET}

${HTML_INDEX}:
	${JADE} -V html-index -V nochunks ${HTMLOPTS} -ioutput.html.images \
		${JADEOPTS} -t sgml ${MASTERDOC} > /dev/null
	${PERL} ${COLLATEINDEX} -g -o ${INDEX_SGML} ${.TARGET}

${HTML_SPLIT_INDEX}:
	${JADE} -V html-index ${HTMLOPTS} -ioutput.html.images \
		${JADEOPTS} -t sgml ${MASTERDOC} > /dev/null
	${PERL} ${COLLATEINDEX} -g -o ${INDEX_SGML} ${.TARGET}

${PRINT_INDEX}: ${HTML_INDEX}
	${CP} -p ${HTML_INDEX} ${.TARGET}


# ------------------------------------------------------------------------
#
# Compress targets
#

#
# The list of compression extensions this Makefile knows about. If you
# add new compression schemes, add to this list (which is a list of
# extensions, hence bz2, *not* bzip2) and extend the _PROG_COMPRESS_*
# targets.
#

KNOWN_COMPRESS=	gz bz2 zip

#
# You can't build suffix rules to do compression, since you can't
# wildcard the source suffix. So these are defined .USE, to be tacked on
# as dependencies of the compress-* targets.
#

_PROG_COMPRESS_gz: .USE
	${GZIP_CMD} < ${.ALLSRC} > ${.TARGET}

_PROG_COMPRESS_bz2: .USE
	${BZIP2_CMD} < ${.ALLSRC} > ${.TARGET}

_PROG_COMPRESS_zip: .USE
	${ZIP_CMD} ${.TARGET} ${.ALLSRC}

#
# Build a list of targets for each compression scheme and output format.
# Don't compress the html-split or html output format (because they need
# to be rolled in to tar files first).
#
.for _curformat in ${KNOWN_FORMATS}
_cf=${_curformat}
.for _curcompress in ${KNOWN_COMPRESS}
.if ${_cf} == "html-split" || ${_cf} == "html"
${DOC}.${_cf}.tar.${_curcompress}: ${DOC}.${_cf}.tar \
				   _PROG_COMPRESS_${_curcompress}
.else
${DOC}.${_cf}.${_curcompress}: ${DOC}.${_cf} _PROG_COMPRESS_${_curcompress}
.endif
.endfor
.endfor

#
# Build targets for any formats we've missed that we don't handle.
#
.for _curformat in ${ALL_FORMATS}
.for _curcompress in ${KNOWN_COMPRESS}
.if !target(${DOC}.${_curformat}.${_curcompress})
${DOC}.${_curformat}.${_curcompress}:
	@${ECHO_CMD} \"${_curformat}.${_curcompress}\" is not a valid output format for this document.
.endif
.endfor
.endfor


# ------------------------------------------------------------------------
#
# Install targets
#
# Build install-* targets, one per allowed value in FORMATS. Need to
# build two specific targets;
#
#    install-html-split - Handles multiple .html files being generated
#                         from one source. Uses the HTML.manifest file
#                         created by the stylesheets, which should list
#                         each .html file that's been created.
#
#    install-*          - Every other format. The wildcard expands to
#                         the other allowed formats, all of which should
#                         generate just one file.
#
# "beforeinstall" and "afterinstall" are hooks in to this process.
# Redefine them to do things before and after the files are installed,
# respectively.

populate_html_docs:
.if exists(HTML.manifest)
_html_docs!=${CAT} HTML.manifest
.endif

spellcheck-html-split: populate_html_docs
.for _html_file in ${_html_docs}
	@echo "Spellcheck ${_html_file}"
	@${HTML2TXT} ${HTML2TXTOPTS} ${.CURDIR}/${_html_file} | ${ISPELL} ${ISPELLOPTS}
.endfor
spellcheck-html:
.for _entry in ${_docs}
	@echo "Spellcheck ${_entry}"
	@${HTML2TXT} ${HTML2TXTOPTS} ${.CURDIR}/${_entry} | ${ISPELL} ${ISPELLOPTS}
.endfor
spellcheck-txt:
.for _entry in ${_docs:M*.txt}
	@echo "Spellcheck ${_entry}"
	@ < ${.CURDIR}/${_entry} ${ISPELL} ${ISPELLOPTS}
.endfor
.for _curformat in ${FORMATS}
.if !target(spellcheck-${_curformat})
spellcheck-${_curformat}:
	@echo "Spellcheck is not currently supported for the ${_curformat} format."
.endif
.endfor

spellcheck: ${FORMATS:C/^/spellcheck-/}

indexreport:
.for _entry in ${SRCS:M*.sgml}
	@echo "indexreport ${_entry}"
	@${PERL} ${INDEXREPORTSCRIPT} ${.CURDIR}/${_entry}
.endfor

#
# Build a list of install-format targets to be installed. These will be
# dependencies for the "realinstall" target.
#

.if !defined(INSTALL_ONLY_COMPRESSED) || empty(INSTALL_ONLY_COMPRESSED)
_curinst+= ${FORMATS:S/^/install-/g}
.endif

realinstall: ${_curinst}

.for _curformat in ${KNOWN_FORMATS}
_cf=${_curformat}
.if !target(install-${_cf})
.if ${_cf} == "html-split"
install-${_curformat}: index.html
.else
install-${_curformat}: ${DOC}.${_curformat}
.endif
	@[ -d ${DESTDIR} ] || ${MKDIR} -p ${DESTDIR}
.if ${_cf} == "html-split"
	${INSTALL_DOCS} $$(${XARGS} < HTML.manifest) ${DESTDIR}
.else
	${INSTALL_DOCS} ${.ALLSRC} ${DESTDIR}
.endif
.if (${_cf} == "html-split" || ${_cf} == "html") && !empty(LOCAL_CSS_SHEET)
	${INSTALL_DOCS} ${LOCAL_CSS_SHEET} ${DESTDIR}
.if ${_cf} == "html-split"
	@if [ -f ln*.html ]; then \
		${INSTALL_DOCS} ln*.html ${DESTDIR}; \
	fi
	@if [ -f LEGALNOTICE.html ]; then \
		${INSTALL_DOCS} LEGALNOTICE.html ${DESTDIR}; \
	fi
	@if [ -f TRADEMARKS.html ]; then \
		${INSTALL_DOCS} TRADEMARKS.html ${DESTDIR}; \
	fi
	@if [ -f ${.OBJDIR}/${DOC}.ln ]; then \
		cd ${DESTDIR}; sh ${.OBJDIR}/${DOC}.ln; \
	fi
.endif
.for _curimage in ${IMAGES_LIB}
	@[ -d ${DESTDIR}/${LOCAL_IMAGES_LIB_DIR}/${_curimage:H} ] || \
		${MKDIR} -p ${DESTDIR}/${LOCAL_IMAGES_LIB_DIR}/${_curimage:H}
	${INSTALL_DOCS} ${LOCAL_IMAGES_LIB_DIR}/${_curimage} \
			${DESTDIR}/${LOCAL_IMAGES_LIB_DIR}/${_curimage:H}
.endfor
# Install the images.  First, loop over all the image names that contain a
# directory separator, make the subdirectories, and install.  Then loop over
# the ones that don't contain a directory separator, and install them in the
# top level.
# Install at first images from /usr/share/images then localized ones
# cause of a different origin path.
.for _curimage in ${IMAGES_PNG:M*/*:M*share*}
	${MKDIR} -p ${DESTDIR:H:H}/${_curimage:H:S|${IMAGES_EN_DIR}/||:S|${.CURDIR}||}
	${INSTALL_DOCS} ${_curimage} ${DESTDIR:H:H}/${_curimage:H:S|${IMAGES_EN_DIR}/||:S|${.CURDIR}||}
.endfor
.for _curimage in ${IMAGES_PNG:M*/*:N*share*}
	${MKDIR} -p ${DESTDIR}/${_curimage:H}
	${INSTALL_DOCS} ${_curimage} ${DESTDIR}/${_curimage:H}
.endfor
.for _curimage in ${IMAGES_PNG:N*/*}
	${INSTALL_DOCS} ${_curimage} ${DESTDIR}/${_curimage}
.endfor
.elif ${_cf} == "tex" || ${_cf} == "dvi"
.for _curimage in ${IMAGES_EPS:M*/*}
	${MKDIR} -p ${DESTDIR}/${_curimage:H:S|${IMAGES_EN_DIR}/||:S|${.CURDIR:T}/||}
	${INSTALL_DOCS} ${_curimage} ${DESTDIR}/${_curimage:H:S|${IMAGES_EN_DIR}/||:S|${.CURDIR:T}/||}
.endfor
.for _curimage in ${IMAGES_EPS:N*/*}
	${INSTALL_DOCS} ${_curimage} ${DESTDIR}
.endfor
.elif ${_cf} == "pdb"
	${LN} -f ${DESTDIR}/${.ALLSRC} ${DESTDIR}/${.CURDIR:T}.${_curformat}
.endif

.if ${_cf} == "html-split"
.for _compressext in ${KNOWN_COMPRESS}
install-${_curformat}.tar.${_compressext}: ${DOC}.${_curformat}.tar.${_compressext}
	@[ -d ${DESTDIR} ] || ${MKDIR} -p ${DESTDIR}
	${INSTALL_DOCS} ${.ALLSRC} ${DESTDIR}
.endfor
.else
.for _compressext in ${KNOWN_COMPRESS}
.if !target(install-${_curformat}.${_compressext})
install-${_curformat}.${_compressext}: ${DOC}.${_curformat}.${_compressext}
	@[ -d ${DESTDIR} ] || ${MKDIR} -p ${DESTDIR}
	${INSTALL_DOCS} ${.ALLSRC} ${DESTDIR}
.if ${_cf} == "pdb"
	${LN} -f ${DESTDIR}/${.ALLSRC} \
		 ${DESTDIR}/${.CURDIR:T}.${_curformat}.${_compressext}
.endif
.endif
.endfor
.endif
.endif
.endfor

#
# Build install- targets for any formats we've missed that we don't handle.
#

.for _curformat in ${ALL_FORMATS}
.if !target(install-${_curformat})
install-${_curformat}:
	@${ECHO_CMD} \"${_curformat}\" is not a valid output format for this document.

.for _compressext in ${KNOWN_COMPRESS}
install-${_curformat}.${_compressext}:
	@${ECHO_CMD} \"${_curformat}.${_compressext}\" is not a valid output format for this document.
.endfor
.endif
.endfor


# ------------------------------------------------------------------------
#
# Package building
#

#
# realpackage is what is called in each subdirectory when a package
# target is called, or, rather, package calls realpackage in each
# subdirectory as it goes.
#
# packagelist returns the list of targets that would be called during
# package building.
#

realpackage: ${FORMATS:S/^/package-/}
packagelist:
	@${ECHO_CMD} ${FORMATS:S/^/package-/}

#
# Build a list of package targets for each output target.  Each package
# target depends on the corresponding install target running.
#

.for _curformat in ${KNOWN_FORMATS}
_cf=${_curformat}
.if ${_cf} == "html-split"
PLIST.${_curformat}: index.html
	@${SORT} HTML.manifest > PLIST.${_curformat}
.else
PLIST.${_curformat}: ${DOC}.${_curformat}
	@${ECHO_CMD} ${DOC}.${_curformat} > PLIST.${_curformat}
.endif
.if (${_cf} == "html-split" || ${_cf} == "html") && \
    (!empty(LOCAL_IMAGES_LIB) || !empty(IMAGES_PNG) || !empty(CSS_SHEET))
	@${ECHO_CMD} ${LOCAL_IMAGES_LIB} ${IMAGES_PNG} ${LOCAL_CSS_SHEET} | \
		${XARGS} -n1 >> PLIST.${_curformat}
.elif (${_cf} == "tex" || ${_cf} == "dvi") && !empty(IMAGES_EPS)
	@${ECHO_CMD} ${IMAGES_EPS} | ${XARGS} -n1 >> PLIST.${_curformat}
.elif ${_cf} == "pdb"
	@${ECHO_CMD} ${.CURDIR:T}.${_curformat} >> PLIST.${_curformat}
.endif

${PACKAGES}/${.CURDIR:T}.${LANGCODE}.${_curformat}.tgz: PLIST.${_cf}
	@${PKG_CREATE} -v -f ${.ALLSRC} -p ${DESTDIR} -s ${.OBJDIR} \
		-c -"FDP ${.CURDIR:T} ${_curformat} package" \
		-d -"FDP ${.CURDIR:T} ${_curformat} package" ${.TARGET}

package-${_curformat}: ${PACKAGES}/${.CURDIR:T}.${LANGCODE}.${_curformat}.tgz
.endfor

.if ${LOCAL_CSS_SHEET} != ${CSS_SHEET}
${LOCAL_CSS_SHEET}: ${CSS_SHEET}
	${RM} -f ${.TARGET}
	${CAT} ${.ALLSRC} > ${.TARGET}
.if defined(CSS_SHEET_ADDITIONS)
	${CAT} ${.CURDIR}/${CSS_SHEET_ADDITIONS} >> ${.TARGET}
.endif
.endif
