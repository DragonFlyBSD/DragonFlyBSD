# $FreeBSD: src/kerberos5/usr.bin/kpasswd/Makefile,v 1.14 2004/02/05 18:51:52 ru Exp $
# $DragonFly: src/kerberos5/usr.bin/kpasswd/Makefile,v 1.1 2005/01/16 14:25:47 eirikn Exp $

PROG=	kpasswd
CFLAGS+=-I${KRB5DIR}/lib/roken
DPADD=	${LIBKRB5} ${LIBROKEN} ${LIBVERS} \
	${LIBASN1} ${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR}
LDADD=	-lkrb5 -lroken ${LIBVERS} \
	-lasn1 -lcrypto -lcrypt -lcom_err

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kpasswd
