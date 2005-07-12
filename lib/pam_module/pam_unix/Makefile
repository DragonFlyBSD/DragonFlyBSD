# $DragonFly: src/lib/pam_module/pam_unix/Makefile,v 1.2 2005/07/12 23:30:24 joerg Exp $

LIB=	pam_unix
SRCS=	pam_unix.c
WARNS?=	2
WARNS_NO_UNUSED_PARAMETERS=

LDADD=	-lcrypt

.include <bsd.lib.mk>

.PATH: ${OPENPAM_DIR}/modules/pam_unix
