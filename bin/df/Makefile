#	@(#)Makefile	8.3 (Berkeley) 5/8/95
# $FreeBSD: src/bin/df/Makefile,v 1.9.2.3 2002/04/05 00:19:21 iedowse Exp $
# $DragonFly: src/bin/df/Makefile,v 1.3 2004/09/26 15:38:44 asmodai Exp $

PROG=	df
SRCS=	df.c vfslist.c

WARNS?=	6

MOUNT=	${.CURDIR}/../../sbin/mount
CFLAGS+= -I${MOUNT}
.PATH:	${MOUNT}

DPADD=	${LIBM}
LDADD=	-lm

.include <bsd.prog.mk>
