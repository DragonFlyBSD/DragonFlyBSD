#	From: @(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/lpr/lpd/Makefile,v 1.10.2.3 2002/06/30 03:59:15 gad Exp $
# $DragonFly: src/usr.sbin/lpr/lpd/Makefile,v 1.2 2003/06/17 04:29:56 dillon Exp $

PROG=	lpd
MAN=	lpd.8
SRCS=	lpd.c printjob.c recvjob.c lpdchar.c modes.c

CFLAGS+= -I${.CURDIR}/../common_source

DPADD=	${LIBLPR}
LDADD=	${LIBLPR}

.include <bsd.prog.mk>
