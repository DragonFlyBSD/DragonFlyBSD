# $FreeBSD: src/kerberos5/libexec/hprop/Makefile,v 1.15 2004/02/05 18:51:50 ru Exp $
# $DragonFly: src/kerberos5/libexec/hprop/Makefile,v 1.4 2005/01/16 14:25:46 eirikn Exp $

PROG=	hprop
MAN=	hprop.8
SRCS=	hprop.c mit_dump.c v4_dump.c
CFLAGS+=-I${KRB5DIR}/lib/roken -I${KRB5DIR}/lib/krb5 -I${KRB5DIR}/lib/asn1
DPADD=	${LIBHDB} ${LIBKRB5} ${LIBROKEN} ${LIBVERS} \
	${LIBASN1} ${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR} ${LDAPDPADD}
LDADD=	-lhdb -lkrb5 -lroken ${LIBVERS} \
	-lasn1 -lcrypto -lcrypt -lcom_err ${LDAPLDADD}
LDFLAGS=${LDAPLDFLAGS}

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kdc
