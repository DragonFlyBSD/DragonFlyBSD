# $DragonFly: src/lib/pam_module/pam_unix/Makefile,v 1.4.2.1 2006/02/18 19:41:10 joerg Exp $

LIB=	pam_unix
SRCS=	pam_unix.c
WARNS?=	6
MAN=	pam_unix.8

DPADD=	${LIBCRYPT} ${LIBUTIL}
LDADD=	-lcrypt -lutil

.include <bsd.lib.mk>

.PATH: ${OPENPAM_DIR}/modules/pam_unix
