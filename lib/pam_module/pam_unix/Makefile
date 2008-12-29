# $DragonFly: src/lib/pam_module/pam_unix/Makefile,v 1.5 2006/02/18 19:40:10 joerg Exp $

LIB=	pam_unix
SRCS=	pam_unix.c
MAN=	pam_unix.8

DPADD=	${LIBCRYPT} ${LIBUTIL}
LDADD=	-lcrypt -lutil

.if !defined(NO_NIS)
CFLAGS+= -DYP
DPADD+=	${LIBYPCLNT}
LDADD+=	-lypclnt
.endif

.include <bsd.lib.mk>
