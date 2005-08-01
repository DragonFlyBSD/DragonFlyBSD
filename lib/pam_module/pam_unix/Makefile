# $DragonFly: src/lib/pam_module/pam_unix/Makefile,v 1.4 2005/08/01 16:15:19 joerg Exp $

LIB=	pam_unix
SRCS=	pam_unix.c
WARNS?=	6
MAN=	pam_unix.8

DPADD=	${LIBCRYPT}
LDADD=	-lcrypt

.include <bsd.lib.mk>

.PATH: ${OPENPAM_DIR}/modules/pam_unix
