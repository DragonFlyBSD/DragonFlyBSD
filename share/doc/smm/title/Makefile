# $FreeBSD: src/share/doc/smm/title/Makefile,v 1.5.2.2 2002/02/18 12:29:45 ru Exp $
# $DragonFly: src/share/doc/smm/title/Makefile,v 1.2 2003/06/17 04:36:57 dillon Exp $

VOLUME=	smm
SRCS=	Title
DOC=	Title

# unroff(1) requires a macro package as an argument
.if defined(PRINTERDEVICE) && ${PRINTERDEVICE} == "html"
MACROS=	-ms
.endif

.include <bsd.doc.mk>
