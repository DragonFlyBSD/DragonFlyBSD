# $FreeBSD: src/usr.sbin/mrouted/mrinfo/Makefile,v 1.11.2.1 2001/04/25 12:10:11 ru Exp $
# $DragonFly: src/usr.sbin/mrouted/mrinfo/Makefile,v 1.2 2003/06/17 04:29:57 dillon Exp $

PROG=	mrinfo

S=	${.CURDIR}/..
.PATH:	$S
CFLAGS+=	-I$S
DPADD=	${LIBMROUTED}
LDADD=	${LIBMROUTED}

MAN=	mrinfo.8
BINMODE=4555

.include <bsd.prog.mk>
