# $FreeBSD: src/kerberos5/lib/libsl/Makefile,v 1.3.2.1 2002/07/19 18:46:24 ru Exp $
# $DragonFly: src/kerberos5/lib/libsl/Makefile,v 1.2 2003/06/17 04:26:17 dillon Exp $

LIB=	sl
CFLAGS+=-I${KRB5DIR}/lib/sl \
	-I${KRB5DIR}/lib/roken \
	-I${KRB5DIR}/include \
	-I${.OBJDIR}
SRCS=	sl.c roken.h

INTERNALLIB= yes

.include <bsd.lib.mk>

.PATH:	${KRB5DIR}/lib/sl

build-tools:	make-roken
