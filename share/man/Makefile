#	@(#)Makefile	8.2 (Berkeley) 4/16/94
# $FreeBSD: src/share/man/Makefile,v 1.11.2.1 2003/04/16 11:28:45 ru Exp $
# $DragonFly: src/share/man/Makefile,v 1.3 2004/11/05 17:36:05 dillon Exp $

# XXX MISSING:	man3f
SUBDIR=	man1 man3 man4 man5 man6 man7 man8 man9

MAKEWHATIS=	/usr/sbin/makewhatis	
	
makedb:
	${MAKEWHATIS}	${DESTDIR}${BINDIR}/man
	${MAKEWHATIS}	${DESTDIR}${BINDIR}/perl/man
.if !defined(NOCRYPT) && !defined(NO_OPENSSL)
	${MAKEWHATIS}	${DESTDIR}${BINDIR}/openssl/man
.endif
	
.include "${.CURDIR}/../Makefile.inc"
.include <bsd.subdir.mk>
