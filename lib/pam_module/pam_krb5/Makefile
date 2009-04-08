# $DragonFly: src/lib/pam_module/pam_krb5/Makefile,v 1.3 2005/07/28 19:25:41 joerg Exp $

LIB=	pam_krb5
SRCS=	pam_krb5.c
MAN=	pam_krb5.8

DPADD=	${LIBKRB5} ${LIBGSSAPI} ${LIBASN1} ${LIBCRYPTO} ${LIBCRYPT} \
	${LIBCOM_ERR} ${LIBROKEN}
LDADD=	-lkrb5 -lgssapi -lasn1 -lcrypto -lcrypt -lcom_err -lroken 

.include <bsd.lib.mk>
