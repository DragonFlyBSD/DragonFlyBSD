# $FreeBSD: src/gnu/usr.bin/cvs/doc/Makefile,v 1.12.2.3 2002/12/19 21:18:01 peter Exp $
# $DragonFly: src/gnu/usr.bin/cvs/doc/Makefile,v 1.6 2006/09/13 19:34:24 corecode Exp $

.include "${.CURDIR}/../Makefile.inc"

SRCDIR= ${CVSDIR}/doc
CONTRIBDIR= ${SRCDIR}

INFO=	cvs cvsclient
INFOSECTION= "Programming & development tools."
INFOENTRY_cvs= "* CVS: (cvs).			CVS Reference Manual."
INFOENTRY_cvsclient= "* CVS-CLIENT: (cvsclient).	CVS client/server Reference Manual."

CLEANFILES+=	getdate-cvs.texi cvs.texinfo

cvs.texinfo: _PATCHFILE ${CONTRIBDIR}/cvs.texinfo cvs.texinfo.patch

cvs.info: getdate-cvs.texi

getdate-cvs.texi: getdate.texi
	sed -e "s/^@chapter /@appendixsec /" \
	    -e "s/^@section /@appendixsubsec /" \
	    < ${.ALLSRC} > ${.TARGET}

.include <bsd.patch.mk>
.include <bsd.info.mk>
