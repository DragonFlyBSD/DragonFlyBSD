# $Id: rst2htm.mk,v 1.9 2014/02/22 01:52:41 sjg Exp $
#
#	@(#) Copyright (c) 2009, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

# convert reStructuredText to HTML, using rst2html.py from
# docutils - http://docutils.sourceforge.net/

.if empty(TXTSRCS)
TXTSRCS != 'ls' -1t ${.CURDIR}/*.txt ${.CURDIR}/*.rst 2>/dev/null; echo
.endif
RSTSRCS ?= ${TXTSRCS}
HTMFILES ?= ${RSTSRCS:R:T:O:u:%=%.htm}
RST2HTML ?= rst2html.py
RST2PDF ?= rst2pdf
RST2S5 ?= rst2s5.py
# the following will run RST2S5 if the target name contains the word 'slides'
# otherwise it uses RST2HTML
RST2HTM = ${"${.TARGET:T:M*slides*}":?${RST2S5} ${RST2S5_FLAGS}:${RST2HTML} ${RST2HTML_FLAGS}}

RST_SUFFIXES ?= .rst .txt

CLEANFILES += ${HTMFILES}

html:	${HTMFILES}

.SUFFIXES: ${RST_SUFFIXES} .htm .pdf

${RST_SUFFIXES:@s@$s.htm@}:
	${RST2HTM} ${.IMPSRC} ${.TARGET}

${RST_SUFFIXES:@s@$s.pdf@}:
	${RST2PDF} ${.IMPSRC} ${.TARGET}

.for s in ${RSTSRCS:O:u}
${s:R:T}.htm: $s
${s:R:T}.pdf: $s
.endfor
