# $FreeBSD: src/usr.bin/bzip2/doc/Makefile,v 1.1.2.2 2002/02/18 09:13:07 sobomax Exp $
# $DragonFly: src/usr.bin/bzip2/doc/Attic/Makefile,v 1.2 2003/06/17 04:29:25 dillon Exp $

.PATH: ${.CURDIR}/../../../contrib/bzip2

INFO=	bzip2
INFOSECTION=	"Programming & development tools."
MAKEINFOFLAGS=	--no-validate

CLEANFILES+=	bzip2.texi bzip2.texi.orig

bzip2.texi:	manual.texi manual.texi.diff
	patch -s -b .orig -o ${.TARGET} < ${.ALLSRC:M*.diff} ${.ALLSRC:M*.texi}

.include <bsd.info.mk>
