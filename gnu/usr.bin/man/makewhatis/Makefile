# $FreeBSD: src/gnu/usr.bin/man/makewhatis/Makefile,v 1.16.2.2 2001/04/25 14:04:13 ru Exp $
# $DragonFly: src/gnu/usr.bin/man/makewhatis/Attic/Makefile,v 1.2 2003/06/17 04:25:46 dillon Exp $
MAINTAINER= wosch

SCRIPTS=makewhatis.perl makewhatis.local.sh
MAN=	makewhatis.1 makewhatis.local.8

libexecdir=/usr/libexec
LINKS=${libexecdir}/makewhatis.local ${libexecdir}/catman.local 
SCRIPTSDIR_makewhatis.local.sh=	${libexecdir}
MLINKS=	makewhatis.local.8 catman.local.8

.include <bsd.prog.mk>
