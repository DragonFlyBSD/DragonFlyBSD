# $FreeBSD: src/kerberosIV/lib/libsl/Makefile,v 1.3.2.1 2002/07/19 18:46:25 ru Exp $

LIB=		sl
INTERNALLIB=	YES
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/sl \
		-I${KRB4DIR}/lib/roken \
		-I${INCLUDEOBJDIR}
SRCS=		sl.c ss.c strtok_r.c

.include <bsd.lib.mk>

.PATH:		${KRB4DIR}/lib/sl
.PATH:		${KRB4DIR}/lib/roken
