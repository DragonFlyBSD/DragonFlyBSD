# $DragonFly: src/lib/pam_module/pam_cleartext_pass_ok/Makefile,v 1.1 2005/07/08 15:42:01 joerg Exp $

LIB=	pam_cleartext_pass_ok
SRCS=	pam_cleartext_pass_ok.c
WARNS?=	6
WARNS_NO_UNUSED_PARAMETERS=
NOMAN=

DPADD=	${LIBSKEY}
LDADD=	-lskey

.include <bsd.lib.mk>
