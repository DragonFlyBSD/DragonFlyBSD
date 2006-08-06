#
# $DragonFly: doc/share/mk/doc.project.mk,v 1.3 2006/08/06 20:58:06 justin Exp $
# Matches with: 
# $FreeBSD: doc/share/mk/doc.project.mk,v 1.26 2005/02/20 17:14:25 hrs Exp $
# This file includes the other makefiles, which contain enough
# knowledge to perform their duties without the system make files.
#

# ------------------------------------------------------------------------
#
# Document-specific variables:
#
#	DOC		This _must_ be set if there is a document to
#			build.  It should be without prefix.
#
#	DOCFORMAT	Format of the document.  Defaults to docbook.
#			docbook is also the only option currently.
#
# 	MAINTAINER	This denotes who is responsible for maintaining
# 			this section of the project.  If unset, set to
# 			doc-team@DragonFlyBSD.org
#

# ------------------------------------------------------------------------
#
# User-modifiable variables:
#
#	PREFIX		Standard path to document-building applications
#			installed to serve the documentation build
#			process, usually by installing the docproj port
#			or package.  Default is ${LOCALBASE} or /usr/local
#
#	NOINCLUDEMK	Whether to include the standard BSD make files,
#			or just to emulate them poorly.  Set this if you
#			aren't on DragonFlyBSD, or a compatible sibling.  By
#			default is not set.
#

# ------------------------------------------------------------------------
#
# Make files included:
#
#	doc.install.mk	Installation specific information, including
#			ownership and permissions.
#
#	doc.subdir.mk	Subdirectory related configuration, including
#			handling "obj" builds.
#
# 	doc.common.mk	targets and variables commonly used in doc/ and
#			www/ tree.
#
# DOCFORMAT-specific make files, like:
#
#	doc.docbook.mk	Building and installing docbook documentation.
#			Currently the only method.
#

# Document-specific defaults
DOCFORMAT?=	docbook
MAINTAINER?=	doc-team@DragonFlyBSD.org

# Master list of known target formats.  The doc.<format>.mk files implement 
# the code to convert from their source format to one or more of these target
# formats
ALL_FORMATS=	html html.tar html-split html-split.tar txt rtf ps pdf tex dvi tar pdb

# User-modifiable
LOCALBASE?=	/usr/pkg
PREFIX?=	${LOCALBASE}
PRI_LANG?=	en

CP?=		/bin/cp
CAT?=		/bin/cat
ECHO_CMD?=	echo
LN?=		/bin/ln
MKDIR?=		/bin/mkdir
RM?=		/bin/rm
MV?=		/bin/mv
HTML2TXT?=	${PREFIX}/bin/links
HTML2TXTOPTS?=	-dump -width 72 ${HTML2TXTFLAGS}
ISPELL?=	ispell
ISPELLOPTS?=	-l -p /usr/share/dict/freebsd ${ISPELLFLAGS}

.if exists(/usr/bin/perl)
PERL?=          /usr/bin/perl
.elif exists({$PREFIX}/bin/perl)
PERL?=          {$PREFIX}/bin/perl
.else
PERL?=          perl
.endif
REALPATH?=      /bin/realpath
SETENV?=        /usr/bin/env
XSLTPROC?=      ${PREFIX}/bin/xsltproc
TIDY?=          ${PREFIX}/bin/tidy
#
# In teTeX 3.0 and later, pdfetex(1) is used as the default TeX
# engine for JadeTeX and tex(1) cannot be used as ${TEX_CMD} anymore
# due to incompatibility of the format file.  Since the teTeX 3.0
# distribution has "${PREFIX}/share/texmf-dist/LICENSE.texmf,"
# it is checked here to determine which TeX engine should be used.
.if exists(${PREFIX}/share/texmf-dist/LICENSE.texmf)
TEX_CMD?=       ${PREFIX}/bin/etex
PDFTEX_CMD?=    ${PREFIX}/bin/pdfetex
.else
TEX_CMD?=       ${PREFIX}/bin/tex
PDFTEX_CMD?=    ${PREFIX}/bin/pdftex
.endif
LATEX_CMD?=     ${PREFIX}/bin/latex
JADETEX_CMD?=   ${TEX_CMD} "&jadetex"
PDFJADETEX_CMD?=${PDFTEX_CMD} "&pdfjadetex"
FOP_CMD?=       ${PREFIX}/share/fop/fop.sh
XEP_CMD?=       sh ${HOME}/XEP/xep.sh
JAVA_CMD?=      ${PREFIX}/bin/javavm
SAXON_CMD?=     ${JAVA_CMD} -jar ${PREFIX}/share/java/classes/saxon.jar


# Image processing (contains code used by the doc.<format>.mk files, so must
# be listed first).
.include "doc.images.mk"

# targets and variables commonly used in doc/ and www/ tree.
.include "doc.common.mk"

DOC_LOCAL_MK=   ${DOC_PREFIX}/${LANGCODE}/share/mk/doc.local.mk

.if exists(${DOC_LOCAL_MK})
.include "${DOC_LOCAL_MK}"
.endif

# Ownership information.
.include "doc.install.mk"

# Format-specific configuration
.if defined(DOC)
.if ${DOCFORMAT} == "docbook"
.include "doc.docbook.mk"
.endif
.if ${DOCFORMAT} == "html"
.include "doc.html.mk"
.endif
.endif

# Subdirectory glue.
.include "doc.subdir.mk"
