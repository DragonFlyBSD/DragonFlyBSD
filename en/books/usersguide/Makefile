# 
# $DragonFly: doc/en/books/usersguide/Makefile,v 1.1 2004/05/05 18:38:57 justin Exp $
#
# Build the DragonFly Guidebook.
#

MAINTAINER=docs@dragonflybsd.org

DOC?= book

FORMATS?= html-split

#HAS_INDEX= true

INSTALL_COMPRESSED?= gz
INSTALL_ONLY_COMPRESSED?=

# 
# SRCS lists the individual SGML files that make up the document. Changes
# to any of these files will force a rebuild
#

# SGML content
SRCS=  book.sgml

# Entities

DOC_PREFIX?= ${.CURDIR}/../../..
.include "${DOC_PREFIX}/share/mk/doc.project.mk"
