# $FreeBSD: src/gnu/usr.bin/cvs/doc/Makefile,v 1.12.2.3 2002/12/19 21:18:01 peter Exp $
# $DragonFly: src/gnu/usr.bin/cvs/doc/Makefile,v 1.7 2007/09/04 16:53:56 corecode Exp $

.include "${.CURDIR}/../Makefile.inc"

SRCDIR= ${CVSDIR}/doc

INFO=	cvs cvsclient
INFOSECTION= "Programming & development tools."
INFOENTRY_cvs= "* CVS: (cvs).			CVS Reference Manual."
INFOENTRY_cvsclient= "* CVS-CLIENT: (cvsclient).	CVS client/server Reference Manual."

CLEANFILES+=	getdate-cvs.texi cvs.texinfo

cvs.info: getdate-cvs.texi

getdate-cvs.texi: getdate.texi
	sed -e "s/^@chapter /@appendixsec /" \
	    -e "s/^@section /@appendixsubsec /" \
	    < ${.ALLSRC} > ${.TARGET}

.include <bsd.info.mk>
