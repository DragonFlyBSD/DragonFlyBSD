# Makefile for xtend (Stark) 10/30/93
# $FreeBSD: src/libexec/xtend/Makefile,v 1.7.2.1 2001/04/25 10:41:03 ru Exp $
# $DragonFly: src/libexec/xtend/Makefile,v 1.2 2003/06/17 04:27:08 dillon Exp $

BINMODE=	555

PROG=	xtend
SRCS=	xtend.c status.c packet.c user.c
CFLAGS+= -DXTENUNAME=\"xten\" -DXTENGNAME=\"xten\"
DPADD=	${LIBUTIL}
LDADD=	-lutil

MAN=	xtend.8

.include <bsd.prog.mk>
