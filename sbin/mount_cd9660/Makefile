#	@(#)Makefile	8.3 (Berkeley) 3/27/94
# $FreeBSD: src/sbin/mount_cd9660/Makefile,v 1.4.6.1 2001/04/25 10:58:34 ru Exp $

NOSHARED=YES

PROG=	mount_cd9660
MAN=	mount_cd9660.8
LDADD=  -lutil -lkiconv
DPADD=	${LIBUTIL} ${LIBKICONV}

.include <bsd.prog.mk>
