# $DragonFly: doc/en/articles/contributors/Makefile,v 1.1.1.1 2004/04/02 09:35:45 hmp Exp $
#
DOC?= article

FORMATS?= html-split html pdf

INSTALL_COMPRESSED?=gz
INSTALL_ONLY_COMPRESSED?=

DOC_TITLE=		DragonFlyBSD Contributors
DOC_CREATOR=	Hiten Pandya, Justin C. Sherrill

WITH_ARTICLE_TOC?=YES

SRCS= article.sgml
SRCS+=contrib.ent
SRCS+=contrib.additional.sgml
SRCS+=contrib.team.sgml
SRCS+=contrib.docteam.sgml

DOC_PREFIX?= ${.CURDIR}/../../..

.include "${DOC_PREFIX}/share/mk/doc.project.mk"
