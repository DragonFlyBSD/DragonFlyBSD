# $DragonFly: src/lib/pam_module/pam_unix/Makefile,v 1.1 2005/07/08 15:42:01 joerg Exp $

LIB=	pam_unix
SRCS=	pam_unix.c
WARNS?=	2
WARNS_NO_UNUSED_PARAMETERS=

LDADD=	-lcrypt
DPADD=	${LIBCRYPT}

.include <bsd.lib.mk>

.PATH: ${OPENPAM_DIR}/modules/pam_unix
