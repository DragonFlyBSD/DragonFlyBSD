
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
