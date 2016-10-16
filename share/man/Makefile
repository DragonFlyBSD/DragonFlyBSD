#	@(#)Makefile	8.2 (Berkeley) 4/16/94
# $FreeBSD: src/share/man/Makefile,v 1.11.2.1 2003/04/16 11:28:45 ru Exp $

SUBDIR=	man1 man3 man4 man5 man6 man7 man8 man9

MAKEWHATIS=	/usr/sbin/makewhatis	
	
makedb:
	${MAKEWHATIS}	${DESTDIR}${BINDIR}/man
	
.include "${.CURDIR}/../Makefile.inc"
.include <bsd.subdir.mk>
