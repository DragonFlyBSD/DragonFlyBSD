# $DragonFly: src/lib/pam_module/pam_krb5/Makefile,v 1.2 2005/07/14 12:30:14 joerg Exp $

LIB=	pam_krb5
SRCS=	pam_krb5.c
WARNS?=	6
MAN=	pam_krb5.8

LDADD=		-lkrb5 -lgssapi -lasn1 -lcrypto -lcrypt -lcom_err \
		-lroken 

.include <bsd.lib.mk>
