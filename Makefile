# $DragonFly: doc/Makefile,v 1.1.1.1 2004/04/02 09:35:45 hmp Exp $
#
# The user can override the default list of languages to build and install
# with the DOC_LANG variable.
# 
#.if defined(DOC_LANG) && !empty(DOC_LANG)
#SUBDIR = 	${DOC_LANG}
#.else
#SUBDIR =	en
#.endif

SUBDIR= 	en

DOC_PREFIX?=   ${.CURDIR}

#SUP?=		${PREFIX}/bin/cvsup
#SUPFLAGS?=	-g -L 2 -P -
#.if defined(SUPHOST)
#SUPFLAGS+=	-h ${SUPHOST}
#.endif
#
#CVS?=		/usr/bin/cvs
#CVSFLAGS?=	-R -q

.include "${DOC_PREFIX}/share/mk/doc.project.mk"
