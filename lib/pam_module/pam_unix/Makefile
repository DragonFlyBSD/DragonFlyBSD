# $DragonFly: src/lib/pam_module/pam_unix/Makefile,v 1.3 2005/07/28 19:25:41 joerg Exp $

LIB=	pam_unix
SRCS=	pam_unix.c
WARNS?=	2
WARNS_NO_UNUSED_PARAMETERS=

DPADD=	${LIBCRYPT}
LDADD=	-lcrypt

.include <bsd.lib.mk>

.PATH: ${OPENPAM_DIR}/modules/pam_unix
