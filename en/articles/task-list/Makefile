# $DragonFly: doc/en/articles/task-list/Makefile,v 1.1.1.1 2004/04/02 09:35:46 hmp Exp $
#
# This Makefile represents how one can added a customized XML
# document that does not use DocBook XML.
DOC= task-list

FORMATS?= html
CUSTOMIZED_XML?= yes
XSLHTML?= task-list.xsl
CSS_SHEET?= task-list.css
STYLESHEET_TYPE?= xsl

SRCS= task-list.xml

DOC_PREFIX?= ${.CURDIR}/../../..

.include "${DOC_PREFIX}/share/mk/doc.project.mk"
